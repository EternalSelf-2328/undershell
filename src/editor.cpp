// SPDX-License-Identifier: GPL-3.0-or-later
// The desktop editor's interaction: selection, dragging and resizing with
// smart guides, keyboard nudging with key repeat, undo/redo and on-canvas
// labels. Pointer positions come from the fullscreen editor surface, so they
// are output coordinates and never depend on the widget being moved.
#include "app.hpp"
#include "i18n.hpp"
#include "clock.hpp"
#include "shape.hpp"
#include "textwidget.hpp"
#include "snap.hpp"

// the generated header names a parameter `namespace`
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace

#include <cmath>
#include <filesystem>
#include <format>
#include <numbers>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

namespace undershell {

namespace {
constexpr double kSnapPx = 8.0;
constexpr double kCoalesceSec = 0.8;
const char* kLooks[] = {"bars", "split", "dots", "segments", "wave", "ribbon",
                        "curtain", "line", "frame", "radial", "orb", "spiral", "halo", "vortex", "fire", "muzzle", "electric", "sfx", "speedlines", "rain", "ripples", "neon", "fireworks", "terminal", "tv"};

// The resize grip under the pointer: 0 top-left, 1 top-right, 2 bottom-left,
// 3 bottom-right, -1 none (the grips turn with the widget)
int gripAt(const Widget& w, double x, double y) {
  if (handPlaced(w.cfg)) return -1;  // its corners are pins or mesh points, not resize grips
  double lx = 0, ly = 0;
  toLocal(w.cfg, x, y, lx, ly);
  const double gs = std::min({26.0, w.cfg.width / 2.0, w.cfg.height / 2.0});
  if (lx < 0 || ly < 0 || lx >= w.cfg.width || ly >= w.cfg.height) return -1;
  const bool left = lx < gs, right = lx > w.cfg.width - gs, top = ly < gs, bottom = ly > w.cfg.height - gs;
  if (top && left) return 0;
  if (top && right) return 1;
  if (bottom && left) return 2;
  if (bottom && right) return 3;
  return -1;
}
bool inGrip(const Widget& w, double x, double y) { return gripAt(w, x, y) >= 0; }
const char* gripCursor(int corner) {
  static const char* names[] = {"nw-resize", "ne-resize", "sw-resize", "se-resize"};
  return corner >= 0 && corner < 4 ? names[corner] : "grab";
}

// pointer angle about the widget's centre, degrees
double angleAt(const Widget& w, double x, double y) {
  return std::atan2(y - (w.cfg.y + w.cfg.height / 2.0), x - (w.cfg.x + w.cfg.width / 2.0)) * 180.0 / std::numbers::pi;
}

std::string degreesText(double d) {
  const double r = std::round(d * 10) / 10;
  return r == std::floor(r) ? std::format("{:.0f}", r) : std::format("{:.1f}", r);
}

bool spanish() { return spanishUi(); }

// the key that selects a widget's look, and its current value
const char* lookKey(const Widget& w) {
  if (w.cfg.type == "clock") return "face";
  if (w.cfg.type == "shape") return "shape";
  if (w.cfg.type == "now_playing") return "layout";
  if (w.cfg.type == "image") return "fit";
  if (w.cfg.type == "calendar") return "layout";
  if (w.cfg.type == "notes") return "plate";
  return "style";
}
std::string lookOf(const Widget& w) {
  if (w.cfg.type == "clock") return w.cfg.options["face"].value_or(std::string("digital"));
  if (w.cfg.type == "shape") return w.cfg.options["shape"].value_or(std::string("rect"));
  if (w.cfg.type == "now_playing") return w.cfg.options["layout"].value_or(std::string("sheet"));
  if (w.cfg.type == "image") return w.cfg.options["fit"].value_or(std::string("fit"));
  if (w.cfg.type == "text" || w.cfg.type == "lyrics") return w.cfg.options["style"].value_or(std::string("plain"));
  if (w.cfg.type == "calendar") return w.cfg.options["layout"].value_or(std::string("month"));
  if (w.cfg.type == "notes") return w.cfg.options["plate"].value_or(std::string("paper"));
  return w.cfg.options["style"].value_or(std::string("bars"));
}
}  // namespace

// ── hit testing / geometry ──────────────────────────────────────────────────

Widget* App::widgetAt(const Output* o, double x, double y) {
  // the selected widget wins overlaps, then the topmost (last listed)
  if (m_selected && m_selected->output == o && m_selected->impl && !m_selected->impl->fullscreen() &&
      insideWidget(m_selected->cfg, x, y))
    return m_selected;
  for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it) {
    Widget& w = **it;
    if (w.output != o || !w.impl || w.impl->fullscreen() || !w.surface || w.cfg.locked) continue;  // locked: the panel picks it
    if (insideWidget(w.cfg, x, y)) return &w;
  }
  return nullptr;
}

// the knob above the selected widget's (turned) top edge
int App::pinAt(const Widget& w, double x, double y) const {
  if (!handPlaced(w.cfg) || &w != m_selected || (!m_multi.empty() && m_multiFor == w.cfg.id) || w.cfg.locked) return -1;
  const double* p = handlePoints(w.cfg);
  int best = -1;
  double bestD = w.cfg.meshed ? 11 : 14;  // mesh points sit closer together
  for (int i = 0; i < handleCount(w.cfg); ++i) {
    const double d = std::hypot(x - p[2 * i], y - p[2 * i + 1]);
    if (d < bestD) bestD = d, best = i;
  }
  return best;
}

// The points go to the config as one undo step; the box (the widget's own
// resolution) follows the shape's size so it renders crisp, at its real aspect.
void App::commitPin(Widget& w) {
  m_lastOpKind.clear();
  int nw = 0, nh = 0;
  if (w.cfg.meshed) {
    setProp(w, "mesh", pointsText(w.cfg.mesh, 2 * w.cfg.meshN * w.cfg.meshN));
    // moved by hand: no longer the preset it started from
    setQuietly(w, "mesh_preset", "\"custom\"");
    meshFitSize(w.cfg, nw, nh);
  } else {
    setProp(w, "pin", pinText(w.cfg.pin));
    const double* q = w.cfg.pin;
    auto len = [&](int a, int b) { return std::hypot(q[2 * a] - q[2 * b], q[2 * a + 1] - q[2 * b + 1]); };
    nw = std::max(48, static_cast<int>(std::lround((len(0, 1) + len(3, 2)) / 2)));
    nh = std::max(32, static_cast<int>(std::lround((len(0, 3) + len(1, 2)) / 2)));
  }
  if (nw != w.cfg.width || nh != w.cfg.height) {
    w.cfg.width = nw;
    w.cfg.height = nh;
    placeLayer(w);
    persist(w);
  }
}

bool App::inRotateHandle(const Widget& w, double x, double y) const {
  if (&w != m_selected || !w.impl || w.impl->fullscreen() || handPlaced(w.cfg) || w.cfg.locked) return false;
  if (!m_multi.empty() && m_multiFor == w.cfg.id) return false;  // several picked: the group's knob
  double kx = 0, ky = 0;
  toOutput(w.cfg, w.cfg.width / 2.0, -OverlayPass::kHandleGap, kx, ky);
  return std::hypot(x - kx, y - ky) <= OverlayPass::kHandleR + 6;
}

// Widgets may hang off the screen edge for composition, but a quarter of
// them (at least 48 px) always stays on the output so none can be lost.
void App::clampToOutput(WidgetConfig& c, const Output* o) const {
  if (!o) return;
  clampBox(c.x, c.y, c.width, c.height, static_cast<int>(o->logicalW()), static_cast<int>(o->logicalH()));
}

void App::moveWidget(Widget& w, int x, int y) {
  WidgetConfig c = w.cfg;
  c.x = x;
  c.y = y;
  clampToOutput(c, w.output);
  if (c.x == w.cfg.x && c.y == w.cfg.y) return;
  w.cfg.x = c.x;
  w.cfg.y = c.y;
  placeLayer(w);
  w.needsRender = true;
  w.drewEmpty = false;
}

void App::resizeWidget(Widget& w, int width, int height) {
  if (width == w.cfg.width && height == w.cfg.height) return;
  w.cfg.width = width;
  w.cfg.height = height;
  placeLayer(w);
  w.needsRender = true;
  w.drewEmpty = false;
}

void App::setRotation(Widget& w, double degrees) {
  degrees = normalizeDegrees(degrees);
  if (std::abs(degrees - w.cfg.rotation) < 1e-6) return;
  w.cfg.rotation = degrees;
  placeLayer(w);
  markEditDirty();
}

void App::onMoveApplied(Widget*, int, int) {}

void App::persist(Widget& w) {
  const WidgetConfig s = toStored(w);  // the file keeps the block's own space
  for (auto& c : m_config.widgets)
    if (c.id == w.cfg.id) {
      c.x = s.x;
      c.y = s.y;
      c.width = s.width;
      c.height = s.height;
    }
  if (!Config::saveGeometry(m_configPath, s)) US_WARN("could not save the new geometry of {}", w.cfg.id);
}

// Snaps a box being moved (all edges + centre) or resized (right/bottom edge)
// to the output's edges and centre and to other widgets on the same output.
// Records the guide lines to draw. Shift disables snapping.
void App::snapBox(const Widget& w, int& x, int& y, int& width, int& height, bool moving, int corner) {
  m_guidesV.clear();
  m_guidesH.clear();
  if (m_snapOn == modActive(XKB_MOD_NAME_SHIFT) || !w.output) return;  // magnet off, or Shift held
  const double ow = w.output->logicalW(), oh = w.output->logicalH();
  std::vector<double> tx = {0, ow / 2, ow}, ty = {0, oh / 2, oh};
  for (auto& o : m_widgets) {
    if (o.get() == &w || o->output != w.output || !o->impl || o->impl->fullscreen() || !o->surface) continue;
    const Box b = visualBox(o->cfg);  // what you see, even when turned
    tx.insert(tx.end(), {double(b.x), b.x + b.w / 2.0, double(b.x + b.w)});
    ty.insert(ty.end(), {double(b.y), b.y + b.h / 2.0, double(b.y + b.h)});
  }
  auto best = [](const std::vector<double>& edges, const std::vector<double>& targets, double& delta, double& guide) {
    return snapAxis(edges, targets, kSnapPx, delta, guide);
  };
  double d = 0, g = 0;
  if (moving) {
    if (best({double(x), x + width / 2.0, double(x + width)}, tx, d, g)) {
      x += static_cast<int>(std::lround(d));
      m_guidesV.push_back(static_cast<float>(g));
    }
    if (best({double(y), y + height / 2.0, double(y + height)}, ty, d, g)) {
      y += static_cast<int>(std::lround(d));
      m_guidesH.push_back(static_cast<float>(g));
    }
  } else {
    // resizing: only the edges of the grabbed corner move
    const bool right = corner & 1, bottom = corner & 2;
    if (best({double(right ? x + width : x)}, tx, d, g)) {
      const int dd = static_cast<int>(std::lround(d));
      if (right) width += dd;
      else x += dd, width -= dd;
      m_guidesV.push_back(static_cast<float>(g));
    }
    if (best({double(bottom ? y + height : y)}, ty, d, g)) {
      const int dd = static_cast<int>(std::lround(d));
      if (bottom) height += dd;
      else y += dd, height -= dd;
      m_guidesH.push_back(static_cast<float>(g));
    }
  }
}

// ── undo / redo ─────────────────────────────────────────────────────────────

App::EditOp App::snapshot(const Widget& w) const {
  return {w.cfg.id, w.cfg.x, w.cfg.y, w.cfg.width, w.cfg.height, lookOf(w)};
}

void App::pushUndo(const Widget& w, const char* kind) {
  const double now = nowSeconds();
  // a burst of arrow presses on one widget is one step
  const bool coalesce = std::string_view(kind) == "nudge" && m_lastOpKind == kind && m_lastOpId == w.cfg.id &&
                        now - m_lastOpAt < kCoalesceSec;
  m_lastOpKind = kind;
  m_lastOpId = w.cfg.id;
  m_lastOpAt = now;
  if (coalesce) return;
  m_undo.push_back(snapshot(w));
  if (m_undo.size() > 200) m_undo.erase(m_undo.begin());
  m_redo.clear();
}

void App::applyOp(const EditOp& op) {
  for (auto& w : m_widgets) {
    if (w->cfg.id != op.id) continue;
    WidgetConfig c = w->cfg;
    c.width = op.w;
    c.height = op.h;
    clampToOutput(c, w->output);
    resizeWidget(*w, c.width, c.height);
    moveWidget(*w, op.x, op.y);
    persist(*w);
    if (op.style != lookOf(*w)) setStyle(*w, op.style);
    m_selected = w.get();
    markEditDirty();
    return;
  }
}

// the inverse of `op` against the current state, for the other stack
App::EditOp App::inverseOf(const EditOp& op) const {
  const auto& widgets = m_widgets;
  EditOp inv = op;
  if (op.kind == "add") inv.kind = "remove";
  else if (op.kind == "remove") inv.kind = "add";
  else if (op.kind == "layout") inv.block = Config::widgetBlocks(readFile(m_configPath));
  else if (op.kind == "prop") {
    for (auto& w : widgets)
      if (w->cfg.id == op.id) {
        const toml::node* n = w->cfg.options.get(op.key);
        inv.value = n ? Config::tomlText(*n) : std::string();
      }
  }
  return inv;
}

void App::undo() {
  if (m_undo.empty()) return;
  EditOp op = m_undo.back();
  m_undo.pop_back();
  m_lastOpKind.clear();
  if (op.kind == "geom") {
    for (auto& w : m_widgets)
      if (w->cfg.id == op.id) m_redo.push_back(snapshot(*w));
    applyOp(op);
    return;
  }
  m_redo.push_back(inverseOf(op));
  applyEditOp(op, true);
}

void App::redo() {
  if (m_redo.empty()) return;
  EditOp op = m_redo.back();
  m_redo.pop_back();
  m_lastOpKind.clear();
  if (op.kind == "geom") {
    for (auto& w : m_widgets)
      if (w->cfg.id == op.id) m_undo.push_back(snapshot(*w));
    applyOp(op);
    return;
  }
  m_undo.push_back(inverseOf(op));
  applyEditOp(op, false);
}

// undo of "add" removes the block, undo of "remove" puts it back, "prop"
// restores the recorded value
void App::applyEditOp(const EditOp& op, bool undoing) {
  (void)undoing;
  if (op.kind == "add") {
    Config::removeBlock(m_configPath, op.id);
    if (m_selected && m_selected->cfg.id == op.id) m_selected = nullptr;
    m_reloadConfigAt = nowSeconds() + 0.02;
  } else if (op.kind == "remove") {
    Config::appendBlock(m_configPath, op.block);
    WidgetConfig placeholder;
    placeholder.id = op.id;
    m_config.widgets.push_back(placeholder);
    m_pendingSelect = op.id;
    m_reloadConfigAt = nowSeconds() + 0.02;
  } else if (op.kind == "layout") {
    replaceLayout(op.block, false);
  } else if (op.kind == "prop") {
    for (auto& w : m_widgets)
      if (w->cfg.id == op.id) {
        applyProp(*w, op.key, op.value);
        m_selected = w.get();
      }
  }
  markEditDirty();
}

void App::setStyle(Widget& w, const std::string& look) {
  // write through the config: the file watcher reloads and reconfigures the
  // widget in place, so the file stays the single source of truth
  w.cfg.options.insert_or_assign(lookKey(w), look);
  Config::setKey(m_configPath, w.cfg.id, lookKey(w), "\"" + look + "\"");
  markEditDirty();
}

// ── selection / keyboard actions ────────────────────────────────────────────

Widget* App::target() {
  if (m_selected) return m_selected;
  return m_pointerWidget;
}

void App::cycleSelection(int step) {
  m_pinCorner = -1;
  m_fontPickFor.clear();
  closeColorPick();
  m_elExpanded.clear();
  std::vector<Widget*> list;
  for (auto& w : m_widgets)
    if (w->impl && w->surface) list.push_back(w.get());  // fullscreen ones too (inspector only)
  if (list.empty()) return;
  auto it = std::find(list.begin(), list.end(), m_selected);
  int idx = it == list.end() ? 0 : static_cast<int>(it - list.begin()) + step;
  idx = (idx % static_cast<int>(list.size()) + static_cast<int>(list.size())) % static_cast<int>(list.size());
  m_selected = list[static_cast<size_t>(idx)];
  markEditDirty();
}

void App::nudge(int dx, int dy, bool resize) {
  if (multiSelected()) {  // the picked widgets together (a burst of presses is one undo step)
    if (resize) return;
    const double now = nowSeconds();
    if (!(m_lastOpKind == "nudge-group" && now - m_lastOpAt < kCoalesceSec)) {
      EditOp op;
      op.kind = "layout";
      op.block = Config::widgetBlocks(readFile(m_configPath));
      m_undo.push_back(op);
      m_redo.clear();
    }
    m_lastOpKind = "nudge-group";
    m_lastOpAt = now;
    std::vector<Widget*> moved;
    for (Widget* w : selection()) {
      if (w->cfg.locked) continue;
      w->cfg.x += dx, w->cfg.y += dy;
      double* p = handlePoints(w->cfg);
      for (int i = 0; i < handleCount(w->cfg); ++i) p[2 * i] += dx, p[2 * i + 1] += dy;
      placeLayer(*w);
      w->needsRender = true;
      moved.push_back(w);
    }
    writeGeometry(moved);
    markEditDirty();
    return;
  }
  Widget* w = target();
  if (!w || w->cfg.locked) return;
  if (handPlaced(w->cfg)) {
    // the last point moved, or all of them: one pixel at a time for precision
    double* p = handlePoints(w->cfg);
    for (int i = 0; i < handleCount(w->cfg); ++i)
      if (m_pinCorner < 0 || m_pinCorner == i) {
        p[2 * i] += dx;
        p[2 * i + 1] += dy;
      }
    placeLayer(*w);
    commitPin(*w);
    m_lastOpKind = w->cfg.meshed ? "prop:mesh" : "prop:pin";  // a burst of arrows is one undo step
    m_lastOpId = w->cfg.id;
    markEditDirty();
    return;
  }
  pushUndo(*w, "nudge");
  if (resize) {
    WidgetConfig c = w->cfg;
    c.width += dx;
    c.height += dy;
    clampToOutput(c, w->output);
    resizeWidget(*w, c.width, c.height);
  } else {
    moveWidget(*w, w->cfg.x + dx, w->cfg.y + dy);
  }
  persist(*w);
  markEditDirty();
}

bool App::modActive(const char* name) const {
  return m_xkbState && xkb_state_mod_name_is_active(m_xkbState, name, XKB_STATE_MODS_EFFECTIVE) > 0;
}

// Returns true for keys that auto-repeat while held.
bool App::keyAction(uint32_t key) {
  if (!m_renaming.empty()) return textKey(key);
  if (!m_layerRenaming.empty()) return layerKey(key);
  if (!m_fieldKey.empty()) return fieldKey(key);
  if (!m_noteFor.empty()) return noteKey(key);
  if (!m_colorPickFor.empty()) return colorKey(key);
  if (!m_fontPickFor.empty()) return fontKey(key);
  if (m_paintMode) {
    const bool ctrlHeld = modActive(XKB_MOD_NAME_CTRL);
    const Output* o = m_pointerEdit ? m_pointerEdit->output : m_uiOutput;
    const double cx = o ? o->logicalW() / 2 : 0, cy = o ? o->logicalH() / 2 : 0;
    const double pan = 90 / m_zoom;
    switch (key) {
      case KEY_ESC:
        if (!m_lasso.empty()) m_lasso.clear();
        else setPaintMode(false);
        markEditDirty();
        return false;
      case KEY_ENTER:
      case KEY_KPENTER: lassoClose(); return false;
      case KEY_BACKSPACE:
        if (!m_lasso.empty()) m_lasso.pop_back();
        markEditDirty();
        return true;
      case KEY_LEFT: m_viewX -= pan; clampPaintView(o); markEditDirty(); return true;
      case KEY_RIGHT: m_viewX += pan; clampPaintView(o); markEditDirty(); return true;
      case KEY_UP: m_viewY -= pan; clampPaintView(o); markEditDirty(); return true;
      case KEY_DOWN: m_viewY += pan; clampPaintView(o); markEditDirty(); return true;
      case KEY_EQUAL:
      case KEY_KPPLUS: paintZoom(1.25, cx, cy); return true;
      case KEY_MINUS:
      case KEY_KPMINUS: paintZoom(1 / 1.25, cx, cy); return true;
      case KEY_0:
      case KEY_KP0:
        m_zoom = 1;
        markEditDirty();
        return false;
      case KEY_Z:
        if (ctrlHeld) {
          paintUndo();
          return false;
        }
        break;
      default: break;
    }
  }
  const bool shift = modActive(XKB_MOD_NAME_SHIFT);
  const bool ctrl = modActive(XKB_MOD_NAME_CTRL);
  const bool alt = modActive(XKB_MOD_NAME_ALT);
  const int step = shift ? std::max(1, m_config.gridSize) : 1;
  xkb_keysym_t sym = m_xkbState ? xkb_state_key_get_one_sym(m_xkbState, key + 8) : XKB_KEY_NoSymbol;
  switch (key) {
    case KEY_ESC:
      if (m_paintMode) {
        setPaintMode(false);
        return false;
      }
      if (m_helpOpen || m_galleryOpen || m_savesOpen) {
        m_helpOpen = m_galleryOpen = m_savesOpen = false;
        m_confirmDelete.clear();
        markEditDirty();
        return false;
      }
      setEditMode(false);
      return false;
    case KEY_ENTER:
    case KEY_KPENTER:
      setEditMode(false);
      return false;
    case KEY_LEFT:
    case KEY_RIGHT:
      if (ctrl) {  // tilt: 0.1°, Shift 1° (held keys repeat)
        Widget* w = target();
        if (w && w->impl && !w->impl->fullscreen() && !w->cfg.locked && !multiSelected()) {
          const double d = (shift ? 1.0 : 0.1) * (key == KEY_LEFT ? -1 : 1);
          setProp(*w, "rotation", degreesText(normalizeDegrees(w->cfg.rotation + d)));
        }
        return true;
      }
      nudge(key == KEY_LEFT ? -step : step, 0, alt);
      return true;
    case KEY_UP: nudge(0, -step, alt); return true;
    case KEY_DOWN: nudge(0, step, alt); return true;
    case KEY_TAB: cycleSelection(shift ? -1 : 1); return false;
    case KEY_DELETE:
      deleteSelection();
      return false;
    case KEY_PAGEUP:  // in the stack: a step, Shift to the front
    case KEY_PAGEDOWN:
      raiseSelection((key == KEY_PAGEUP ? 1 : -1) * (shift ? 2 : 1));
      return false;
    default: break;
  }
  const bool isZ = sym == XKB_KEY_z || sym == XKB_KEY_Z || (sym == XKB_KEY_NoSymbol && key == KEY_Z);
  const bool isY = sym == XKB_KEY_y || sym == XKB_KEY_Y || (sym == XKB_KEY_NoSymbol && key == KEY_Y);
  if (ctrl && isZ && m_paintMode) {  // depth mode: undo the last stroke
    paintUndo();
    return false;
  }
  if (ctrl && isZ) {
    shift ? redo() : undo();
    return true;
  }
  if (ctrl && isY) {
    redo();
    return true;
  }
  const bool isD = sym == XKB_KEY_d || sym == XKB_KEY_D || (sym == XKB_KEY_NoSymbol && key == KEY_D);
  if (ctrl && isD && m_selected) duplicateSelection();
  const bool isG = sym == XKB_KEY_g || sym == XKB_KEY_G || (sym == XKB_KEY_NoSymbol && key == KEY_G);
  if (ctrl && isG) {
    if (shift) ungroupSelection();
    else if (!groupSelection().empty()) m_layersOpen = true;  // the group shows in the panel
    markEditDirty();
  }
  return false;
}

void App::onKey(uint32_t key, uint32_t state) {
  if (!m_edit) return;
  if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    const bool repeats = keyAction(key);
    if (repeats && m_repeatRate > 0) {
      m_repeatKey = key;
      m_repeatNext = nowSeconds() + m_repeatDelay / 1000.0;
    }
  } else if (key == m_repeatKey) {
    m_repeatKey = 0;
  }
}

// Typing a saved layout's name: Enter keeps it, Esc drops the edit.
bool App::textKey(uint32_t key) {
  if (key == KEY_ENTER || key == KEY_KPENTER) {
    commitRename();
    return false;
  }
  if (key == KEY_ESC) {
    m_renaming.clear();
    markEditDirty();
    return false;
  }
  if (key == KEY_BACKSPACE) {
    // drop one UTF-8 character
    while (!m_renameText.empty()) {
      const unsigned char c = static_cast<unsigned char>(m_renameText.back());
      m_renameText.pop_back();
      if ((c & 0xC0) != 0x80) break;
    }
    markEditDirty();
    return true;
  }
  if (!m_xkbState || modActive(XKB_MOD_NAME_CTRL)) return false;
  char buf[16] = {};
  const int n = xkb_state_key_get_utf8(m_xkbState, key + 8, buf, sizeof buf);
  if (n > 0 && static_cast<unsigned char>(buf[0]) >= 0x20 && buf[0] != 0x7f && m_renameText.size() < 48) {
    m_renameText += std::string(buf, static_cast<size_t>(n));
    markEditDirty();
    return true;
  }
  return false;
}

// ── typing on a widget (a note) ─────────────────────────────────────────────
// Enter breaks the line, the arrows, Home and End move the caret, Ctrl+S
// keeps what is typed; Esc or a click elsewhere keeps it and stops.

void App::beginNote(Widget& w) {
  if (!w.impl) return;
  endNote();
  m_noteFor = w.cfg.id;
  m_noteText = w.impl->editableText();
  m_noteCaret = m_noteText.size();
  w.impl->setEditing(&m_noteText, m_noteCaret);
  w.needsRender = true;
  markEditDirty();
}

void App::endNote(bool keep) {
  if (m_noteFor.empty()) return;
  for (auto& w : m_widgets)
    if (w->cfg.id == m_noteFor && w->impl) {
      if (keep && m_noteText != w->impl->editableText()) w->impl->saveEditedText(m_noteText);
      w->impl->setEditing(nullptr, 0);
      w->needsRender = true;
      w->drewEmpty = false;
    }
  m_noteFor.clear();
  m_noteText.clear();
  markEditDirty();
}

bool App::noteKey(uint32_t key) {
  std::string& t = m_noteText;
  size_t& c = m_noteCaret;
  c = std::min(c, t.size());
  auto prev = [&](size_t i) {  // the start of the character before i
    if (i == 0) return i;
    do --i;
    while (i > 0 && (static_cast<unsigned char>(t[i]) & 0xC0) == 0x80);
    return i;
  };
  auto next = [&](size_t i) {
    if (i >= t.size()) return t.size();
    do ++i;
    while (i < t.size() && (static_cast<unsigned char>(t[i]) & 0xC0) == 0x80);
    return i;
  };
  auto lineStart = [&](size_t i) { return i == 0 ? 0 : (t.rfind('\n', i - 1) == std::string::npos ? 0 : t.rfind('\n', i - 1) + 1); };
  auto lineEnd = [&](size_t i) { return t.find('\n', i) == std::string::npos ? t.size() : t.find('\n', i); };
  // the same column (in characters) on another line
  auto column = [&](size_t from, size_t to) {
    size_t cols = 0;
    for (size_t i = lineStart(from); i < from; i = next(i)) ++cols;
    size_t i = to;
    const size_t end = lineEnd(to);
    while (cols-- > 0 && i < end) i = next(i);
    return i;
  };
  bool edited = false, moved = true;
  const bool ctrl = modActive(XKB_MOD_NAME_CTRL);
  switch (key) {
    case KEY_ESC: endNote(); return false;
    case KEY_ENTER:
    case KEY_KPENTER: t.insert(c, 1, '\n'), ++c, edited = true; break;
    case KEY_BACKSPACE:
      if (c > 0) {
        const size_t p = prev(c);
        t.erase(p, c - p);
        c = p;
        edited = true;
      }
      break;
    case KEY_DELETE:
      if (c < t.size()) t.erase(c, next(c) - c), edited = true;
      break;
    case KEY_LEFT: c = prev(c); break;
    case KEY_RIGHT: c = next(c); break;
    case KEY_HOME: c = lineStart(c); break;
    case KEY_END: c = lineEnd(c); break;
    case KEY_UP:
      if (lineStart(c) > 0) c = column(c, lineStart(lineStart(c) - 1));
      break;
    case KEY_DOWN:
      if (lineEnd(c) < t.size()) c = column(c, lineEnd(c) + 1);
      break;
    default: moved = false;
  }
  if (!moved && ctrl && key == KEY_S) {  // keep it and go on typing
    for (auto& w : m_widgets)
      if (w->cfg.id == m_noteFor && w->impl) w->impl->saveEditedText(t);
    return false;
  }
  if (!moved && !ctrl && m_xkbState) {
    char buf[16] = {};
    const int n = xkb_state_key_get_utf8(m_xkbState, key + 8, buf, sizeof buf);
    if (n > 0 && static_cast<unsigned char>(buf[0]) >= 0x20 && buf[0] != 0x7f && t.size() < 20000) {
      t.insert(c, buf, static_cast<size_t>(n));
      c += static_cast<size_t>(n);
      edited = moved = true;
    }
  }
  if (!moved) return false;
  (void)edited;
  for (auto& w : m_widgets)
    if (w->cfg.id == m_noteFor && w->impl) {
      w->impl->setEditing(&t, c);
      w->needsRender = true;
    }
  markEditDirty();
  return true;
}

// Typing in an inspector Text row: the widget shows it as it is typed;
// Enter keeps it (one undo step), Shift+Enter starts a new line, Esc drops it.
bool App::fieldKey(uint32_t key) {
  if (key == KEY_ESC) {
    commitField(false);
    return false;
  }
  if (key == KEY_ENTER || key == KEY_KPENTER) {
    if (modActive(XKB_MOD_NAME_SHIFT) && m_fieldText.size() < 400) {
      m_fieldText += '\n';
      previewField();
      return true;
    }
    commitField();
    return false;
  }
  if (key == KEY_BACKSPACE) {
    while (!m_fieldText.empty()) {
      const unsigned char c = static_cast<unsigned char>(m_fieldText.back());
      m_fieldText.pop_back();
      if ((c & 0xC0) != 0x80) break;
    }
    previewField();
    return true;
  }
  if (!m_xkbState || modActive(XKB_MOD_NAME_CTRL)) return false;
  char buf[16] = {};
  const int n = xkb_state_key_get_utf8(m_xkbState, key + 8, buf, sizeof buf);
  if (n > 0 && static_cast<unsigned char>(buf[0]) >= 0x20 && buf[0] != 0x7f && m_fieldText.size() < 400) {
    m_fieldText += std::string(buf, static_cast<size_t>(n));
    previewField();
    return true;
  }
  return false;
}

namespace {
// a TOML basic string, line breaks kept as \n
std::string tomlText(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    if (c == '\n') o += "\\n";
    else if (static_cast<unsigned char>(c) >= 0x20) o += c;
  }
  return o + "\"";
}
}  // namespace

void App::previewField() {
  for (auto& w : m_widgets)
    if (w->cfg.id == m_fieldFor) {
      w->cfg.options.insert_or_assign(m_fieldKey, m_fieldText);
      if (w->impl) w->impl->configure(w->cfg, m_noctalia.state());
      w->needsRender = true;
    }
  markEditDirty();
}

void App::commitField(bool keep) {
  if (m_fieldKey.empty()) return;
  const std::string id = m_fieldFor, key = m_fieldKey, text = m_fieldText, orig = m_fieldOrig;
  m_fieldFor.clear();
  m_fieldKey.clear();
  for (auto& w : m_widgets) {
    if (w->cfg.id != id) continue;
    w->cfg.options.insert_or_assign(key, orig);  // undo starts from what was there
    if (keep && text != orig) {
      setProp(*w, key, tomlText(text));
    } else {
      if (w->impl) w->impl->configure(w->cfg, m_noctalia.state());
      w->needsRender = true;
    }
  }
  markEditDirty();
}

void App::commitRename() {
  if (m_renaming.empty()) return;
  if (!m_renameText.empty()) Config::renameSave(savesDir(), m_renaming, m_renameText);
  m_renaming.clear();
  refreshSaves();
  markEditDirty();
}

void App::onKeymap(int fd, uint32_t size) {
  void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (map == MAP_FAILED) return;
  if (!m_xkb) m_xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  xkb_keymap* km = xkb_keymap_new_from_string(m_xkb, static_cast<const char*>(map), XKB_KEYMAP_FORMAT_TEXT_V1,
                                              XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map, size);
  if (!km) return;
  if (m_xkbState) xkb_state_unref(m_xkbState);
  if (m_keymap) xkb_keymap_unref(m_keymap);
  m_keymap = km;
  m_xkbState = xkb_state_new(km);
}

void App::onModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
  if (m_xkbState) xkb_state_update_mask(m_xkbState, depressed, latched, locked, 0, 0, group);
}

void App::onRepeatInfo(int32_t rate, int32_t delay) {
  m_repeatRate = rate;
  m_repeatDelay = delay;
}

void App::onKeyboardLeave() { m_repeatKey = 0; }

void App::editorTick(double now) {
  int guard = 0;
  while (m_repeatKey && now >= m_repeatNext && guard++ < 8) {
    keyAction(m_repeatKey);
    m_repeatNext += 1.0 / std::max(1, m_repeatRate);
  }
}

void App::validateEditPointers() {
  auto alive = [&](Widget* p) {
    for (auto& w : m_widgets)
      if (w.get() == p) return true;
    return false;
  };
  if (m_selected && !alive(m_selected)) m_selected = nullptr;
  if (m_hoverWidget && !alive(m_hoverWidget)) m_hoverWidget = nullptr;
  if (m_pointerWidget && !alive(m_pointerWidget)) {
    m_pointerWidget = nullptr;
    m_drag = Drag::None;
  }
  markEditDirty();
}

// ── pointer ─────────────────────────────────────────────────────────────────

void App::onPointerEnter(wl_surface* s, uint32_t serial, double x, double y) {
  m_pointerSerial = serial;
  m_pointerEdit = editBySurface(s);
  US_DEBUG("pointer enter {} at {:.0f},{:.0f} (edit={})", m_pointerEdit ? "editor" : (widgetBySurface(s) ? "widget" : "other"), x, y, m_edit);
  if (!m_pointerEdit) {
    widgetPointer(PointerEvent::Enter, s, x, y, 0);
    return;
  }
  onPointerMotion(x, y);
}

void App::onPointerLeave(wl_surface* s) {
  US_DEBUG("pointer leave {} (drag={})", editBySurface(s) ? "editor" : "widget/other", static_cast<int>(m_drag));
  if (!editBySurface(s)) {
    widgetPointer(PointerEvent::Leave, s, 0, 0, 0);
    return;
  }
  if (editBySurface(s) != m_pointerEdit) return;
  if (m_drag == Drag::None) {
    m_pointerEdit = nullptr;
    m_pointerWidget = nullptr;
    markEditDirty();
  }
}

void App::onPointerMotion(double x, double y) {
  if (!m_edit || !m_pointerEdit) {
    widgetPointer(PointerEvent::Motion, nullptr, x, y, 0);
    return;
  }
  m_px = x;
  m_py = y;
  uiFor(m_pointerEdit);
  if (m_drag == Drag::Slider) {
    uiDrag(x);
    return;
  }
  if (m_drag == Drag::Scroll) {
    scrollDragTo(y);
    return;
  }
  if (m_drag == Drag::Layer) {  // a row of the Layers panel to another place
    if (std::abs(y - m_layerPressY) > 4) m_layerMoved = true;
    if (m_layerMoved) {
      int slot = -1, last = -1;  // before the first row whose middle is below the pointer
      for (const UiControl& c : m_ui)
        if (c.type == UiControl::LayerRow) {
          if (slot < 0 && y < c.r.y + c.r.h / 2) slot = c.prop;
          last = std::max(last, c.prop + 1);
        }
      m_layerDrop = slot < 0 ? last : slot;
      setCursor("grabbing");
    }
    markEditDirty();
    return;
  }
  if (m_groupMove || m_drag == Drag::GroupResize || m_drag == Drag::GroupRotate) {
    groupDragTo(x, y);
    return;
  }
  if (m_drag == Drag::Panel) {  // moving a panel out of the way
    m_panelPos[m_panelDrag] = {static_cast<float>(x) - m_panelGrabX, static_cast<float>(y) - m_panelGrabY};
    markEditDirty();
    return;
  }
  if (m_paintMode && m_panning) {  // dragging the zoomed view
    m_viewX = m_panViewX - (x - m_panX) / m_zoom;
    m_viewY = m_panViewY - (y - m_panY) / m_zoom;
    clampPaintView(m_pointerEdit->output);
    markEditDirty();
    return;
  }
  const int hover = m_drag == Drag::None ? uiHit(x, y) : -1;
  if (hover != m_uiHover) {
    m_uiHover = hover;
    markEditDirty();
  }
  if (hover >= 0) {
    if (m_pointerWidget) {
      m_pointerWidget = nullptr;
      markEditDirty();
    }
    const auto t = m_ui[static_cast<size_t>(hover)].type;
    setCursor(t == UiControl::PanelGrab ? "grab" : t == UiControl::Panel ? "default" : "pointer");
    return;
  }
  if (m_paintMode) {
    // depth mode: the canvas paints, widgets stay put
    double wx = 0, wy = 0;
    paintWorld(m_pointerEdit->output, x, y, wx, wy);
    if (m_painting) paintMove(wx, wy);
    if (m_pointerWidget) m_pointerWidget = nullptr;
    setCursor(m_selectTool == 3 ? "grab" : "crosshair");
    markEditDirty();  // the brush outline / lasso band follows the pointer
    return;
  }
  if (m_eyedrop && m_drag == Drag::None) {  // the eyedropper's loupe follows the pointer
    markEditDirty();
    if (uiHit(x, y) < 0) {
      setCursor("crosshair");
      return;
    }
  }
  if (m_drag == Drag::None && m_selected && m_selected->output == m_pointerEdit->output && multiSelected()) {
    // the box round the picked widgets: its corners scale them, its knob turns them
    if (const int g = groupGripAt(x, y); g >= 0) {
      setCursor(gripCursor(g));
      return;
    }
    if (inGroupKnob(x, y)) {
      setCursor("grab");
      return;
    }
  }
  if (m_drag == Drag::None) {
    if (m_selected && m_selected->output == m_pointerEdit->output && inRotateHandle(*m_selected, x, y)) {
      if (m_pointerWidget != m_selected) {
        m_pointerWidget = m_selected;
        markEditDirty();
      }
      setCursor("grab");
      return;
    }
    Widget* hit = widgetAt(m_pointerEdit->output, x, y);
    if (hit != m_pointerWidget) {
      m_pointerWidget = hit;
      markEditDirty();
    }
    if (m_selected && m_selected->output == m_pointerEdit->output && pinAt(*m_selected, x, y) >= 0) {
      if (m_pointerWidget != m_selected) {
        m_pointerWidget = m_selected;
        markEditDirty();
      }
      setCursor("crosshair");
      return;
    }
    setCursor(!hit ? "default" : hit->cfg.locked ? "not-allowed" : multiSelected() && isSelected(hit) ? "grab" : gripCursor(gripAt(*hit, x, y)));
    return;
  }
  Widget* w = m_pointerWidget;
  if (!w) return;
  const int dx = static_cast<int>(std::lround(x - m_pressX));
  const int dy = static_cast<int>(std::lround(y - m_pressY));
  if (m_drag == Drag::Rotate) {
    // follow the pointer's angle in small increments: Shift turns ten times
    // slower for fine work; the magnet only catches multiples of 15° within 2°
    const bool fine = modActive(XKB_MOD_NAME_SHIFT);
    const double now = angleAt(*w, x, y);
    m_rotAcc += normalizeDegrees(now - m_rotLast) * (fine ? 0.1 : 1.0);
    m_rotLast = now;
    double a = m_rotStart + m_rotAcc;
    const double near15 = std::round(a / 15.0) * 15.0;
    if (m_snapOn && !fine && std::abs(a - near15) < 2.0) a = near15;
    setRotation(*w, std::round(a * 10.0) / 10.0);
    return;
  }
  if (m_drag == Drag::Pin && handPlaced(w->cfg) && m_pinCorner >= 0) {
    // one point follows the pointer; Shift moves it a quarter as fast
    const double k = modActive(XKB_MOD_NAME_SHIFT) ? 0.25 : 1.0;
    double* p = handlePoints(w->cfg);
    p[2 * m_pinCorner] = m_pinStart[2 * m_pinCorner] + (x - m_pressX) * k;
    p[2 * m_pinCorner + 1] = m_pinStart[2 * m_pinCorner + 1] + (y - m_pressY) * k;
    placeLayer(*w);
    markEditDirty();
    return;
  }
  if (m_drag == Drag::Move && handPlaced(w->cfg)) {  // all the points together
    double* p = handlePoints(w->cfg);
    for (int i = 0; i < handleCount(w->cfg); ++i) {
      p[2 * i] = m_pinStart[2 * i] + (x - m_pressX);
      p[2 * i + 1] = m_pinStart[2 * i + 1] + (y - m_pressY);
    }
    placeLayer(*w);
    markEditDirty();
    return;
  }
  if (m_drag == Drag::Move) {
    // snap what is seen: the turned box's bounds
    int nx = m_startX + dx, ny = m_startY + dy;
    WidgetConfig c = w->cfg;
    c.x = nx;
    c.y = ny;
    const Box vb = visualBox(c);
    int bx = vb.x, by = vb.y, bw = vb.w, bh = vb.h;
    snapBox(*w, bx, by, bw, bh, true);
    moveWidget(*w, nx + bx - vb.x, ny + by - vb.y);
  } else {
    // resize from the grabbed corner, along the widget's own axes; the
    // opposite corner (as seen) stays put, turned or not
    const double sx = (m_gripCorner & 1) ? 1.0 : -1.0, sy = (m_gripCorner & 2) ? 1.0 : -1.0;
    const double r = -w->cfg.rotation * std::numbers::pi / 180.0;
    const double ldx = dx * std::cos(r) - dy * std::sin(r), ldy = dx * std::sin(r) + dy * std::cos(r);
    WidgetConfig c = w->cfg;
    c.width = std::max(48, static_cast<int>(std::lround(m_dragW + sx * ldx)));
    c.height = std::max(32, static_cast<int>(std::lround(m_dragH + sy * ldy)));
    if (!rotated(w->cfg)) {
      // an upright box snaps its moving edges to guides
      int bx = static_cast<int>(std::lround(sx > 0 ? m_anchorX : m_anchorX - c.width));
      int by = static_cast<int>(std::lround(sy > 0 ? m_anchorY : m_anchorY - c.height));
      snapBox(*w, bx, by, c.width, c.height, false, m_gripCorner);
      c.x = bx;
      c.y = by;
    } else {
      const double a = w->cfg.rotation * std::numbers::pi / 180.0;
      const double hx = sx * c.width / 2.0, hy = sy * c.height / 2.0;
      const double cx = m_anchorX + hx * std::cos(a) - hy * std::sin(a);
      const double cy = m_anchorY + hx * std::sin(a) + hy * std::cos(a);
      c.x = static_cast<int>(std::lround(cx - c.width / 2.0));
      c.y = static_cast<int>(std::lround(cy - c.height / 2.0));
    }
    clampToOutput(c, w->output);
    w->cfg.x = c.x;
    w->cfg.y = c.y;
    w->cfg.width = c.width;
    w->cfg.height = c.height;
    placeLayer(*w);
  }
  markEditDirty();
}

void App::onPointerButton(uint32_t serial, uint32_t button, uint32_t state) {
  m_pointerSerial = serial;
  if (m_edit) uiFor(m_pointerEdit);
  US_DEBUG("button {} {} at {:.0f},{:.0f} edit={} surface={} drag={} ui={} hover={} selected={}", button,
           state == WL_POINTER_BUTTON_STATE_PRESSED ? "down" : "up", m_px, m_py, m_edit, m_pointerEdit ? "editor" : "none",
           static_cast<int>(m_drag), uiHit(m_px, m_py), m_pointerWidget ? m_pointerWidget->cfg.id : "-",
           m_selected ? m_selected->cfg.id : "-");
  if (!m_edit || !m_pointerEdit) {
    widgetPointer(state == WL_POINTER_BUTTON_STATE_PRESSED ? PointerEvent::Press : PointerEvent::Release, nullptr, m_px,
                  m_py, button);
    return;
  }
  if (button == BTN_RIGHT && state == WL_POINTER_BUTTON_STATE_PRESSED && m_eyedrop) {
    m_eyedrop = false;  // the eyedropper first
    setCursor("default");
    markEditDirty();
    return;
  }
  if (button == BTN_RIGHT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
    if (m_paintMode) setPaintMode(false);  // leaves depth mode first
    else setEditMode(false);
    return;
  }
  if (m_paintMode && button == BTN_MIDDLE) {  // the middle button always pans
    m_panning = state == WL_POINTER_BUTTON_STATE_PRESSED && m_zoom > 1.0001;
    m_panX = m_px, m_panY = m_py, m_panViewX = m_viewX, m_panViewY = m_viewY;
    return;
  }
  if (button != BTN_LEFT) return;
  if (m_drag == Drag::Scroll) {
    if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
      m_drag = Drag::None;
      m_scrollDrag.clear();
      markEditDirty();
    }
    return;
  }
  if (m_drag == Drag::Slider) {
    if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
      // a colour dragged in the picker: one entry in the recent colours
      if (m_sliderControl >= 0 && m_sliderControl < static_cast<int>(m_ui.size())) {
        const auto t = m_ui[static_cast<size_t>(m_sliderControl)].type;
        if (t == UiControl::ColorSV || t == UiControl::ColorHue) applyPickedColor(true);
      }
      m_drag = Drag::None;
      m_sliderControl = -1;
      markEditDirty();
    }
    return;
  }
  if (m_drag == Drag::Panel) {
    if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
      m_drag = Drag::None;
      setCursor("grab");
      markEditDirty();
    }
    return;
  }
  if (m_drag == Drag::Layer) {
    if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
      m_drag = Drag::None;
      if (m_layerMoved && m_layerDrop >= 0) dropLayer(m_layerDrop);
      m_layerMoved = false;
      m_layerDrop = -1;
      setCursor("default");
      markEditDirty();
    }
    return;
  }
  if (m_groupMove || m_drag == Drag::GroupResize || m_drag == Drag::GroupRotate) {
    if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
      m_groupMove = false;
      groupDragEnd();
      setCursor("grab");
    }
    return;
  }
  if (m_eyedrop && state == WL_POINTER_BUTTON_STATE_PRESSED && uiHit(m_px, m_py) < 0) {
    // the eyedropper: the wallpaper's colour under the click
    Color c;
    if (eyedropAt(m_pointerEdit->output, m_px, m_py, c)) {
      m_cp = toHsv(c);
      applyPickedColor(true);
    }
    m_eyedrop = false;
    setCursor("default");
    markEditDirty();
    return;
  }
  if (state == WL_POINTER_BUTTON_STATE_PRESSED && uiPress(uiHit(m_px, m_py), m_px)) {
    markEditDirty();
    return;
  }
  if (m_paintMode) {
    double wx = 0, wy = 0;
    paintWorld(m_pointerEdit->output, m_px, m_py, wx, wy);
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
      m_confirmClear = false;
      switch (m_selectTool) {
        case 1: wandAt(wx, wy); break;
        case 2: lassoClick(m_px, m_py); break;
        case 3:
          m_panning = m_zoom > 1.0001;
          m_panX = m_px, m_panY = m_py, m_panViewX = m_viewX, m_panViewY = m_viewY;
          setCursor("grabbing");
          break;
        default: paintBegin(wx, wy); break;
      }
    } else {
      paintEnd();
      if (m_panning) setCursor("grab");
      m_panning = false;
    }
    return;
  }
  Widget* w = m_pointerWidget;
  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    m_galleryOpen = false;
    m_helpOpen = false;
    m_savesOpen = false;
    m_fontPickFor.clear();
    m_confirmDelete.clear();
    commitRename();
    commitLayerRename();
    commitField();
    if (!m_noteFor.empty() && !(w && w->cfg.id == m_noteFor)) endNote();  // a click on the note keeps typing
    markEditDirty();
    if (modActive(XKB_MOD_NAME_SHIFT)) {  // Shift+click: in or out of the selection (empty space keeps it)
      if (w) toggleSelected(w);
      return;
    }
    if (m_selected && m_selected->output == m_pointerEdit->output && multiSelected()) {
      if (const int g = groupGripAt(m_px, m_py); g >= 0) {
        m_gripCorner = g;
        groupDragBegin(Drag::GroupResize);
        setCursor(gripCursor(g));
        return;
      }
      if (inGroupKnob(m_px, m_py)) {
        groupDragBegin(Drag::GroupRotate);
        setCursor("grabbing");
        return;
      }
    }
    // a click picks a widget with its group; a second click on a member of
    // the picked group picks just that member
    const double now = nowSeconds();
    const bool twice = w && m_lastWidgetClick == w->cfg.id && now - m_lastWidgetClickAt < 0.35;
    m_lastWidgetClick = w ? w->cfg.id : std::string();
    m_lastWidgetClickAt = now;
    if (!w) {
      selectOnly(nullptr);  // clicking empty space clears the selection
      return;
    }
    if (twice && !multiSelected() && w->impl && w->impl->editable() && !w->cfg.locked) {  // a note: type on it
      selectOnly(w);
      beginNote(*w);
      return;
    }
    if (!m_noteFor.empty() && w->cfg.id == m_noteFor) return;  // typing: the note stays put
    if (twice && multiSelected() && isSelected(w)) selectOnly(w);
    else if (!isSelected(w)) selectGroup(w);
    if (w->cfg.locked) return;  // picked, never moved
    if (multiSelected()) {
      m_groupMove = true;
      groupDragBegin(Drag::Move);
      setCursor("grabbing");
      return;
    }
    if (handPlaced(w->cfg)) {
      // pins or mesh points: a point moves alone, anywhere else moves them all
      const int corner = pinAt(*w, m_px, m_py);
      std::copy_n(handlePoints(w->cfg), 2 * handleCount(w->cfg), m_pinStart);
      m_pressX = m_px;
      m_pressY = m_py;
      if (corner >= 0) m_pinCorner = corner;
      m_drag = corner >= 0 ? Drag::Pin : Drag::Move;
      setCursor(corner >= 0 ? "crosshair" : "grabbing");
      return;
    }
    if (inRotateHandle(*w, m_px, m_py)) {
      m_drag = Drag::Rotate;
      m_rotStart = w->cfg.rotation;
      m_rotLast = angleAt(*w, m_px, m_py);
      m_rotAcc = 0;
      setCursor("grabbing");
      return;
    }
    pushUndo(*w, "drag");
    m_gripCorner = gripAt(*w, m_px, m_py);
    m_drag = m_gripCorner >= 0 ? Drag::Resize : Drag::Move;
    if (m_gripCorner >= 0) {  // the opposite corner, which stays put (on the box before perspective)
      WidgetConfig flat = w->cfg;
      flat.tiltX = flat.tiltY = flat.skewX = 0;
      toOutput(flat, (m_gripCorner & 1) ? 0.0 : w->cfg.width, (m_gripCorner & 2) ? 0.0 : w->cfg.height, m_anchorX, m_anchorY);
    }
    m_pressX = m_px;
    m_pressY = m_py;
    m_startX = w->cfg.x;
    m_startY = w->cfg.y;
    m_dragW = w->cfg.width;
    m_dragH = w->cfg.height;
    setCursor(m_drag == Drag::Resize ? gripCursor(m_gripCorner) : "grabbing");
  } else if ((m_drag == Drag::Pin || m_drag == Drag::Move) && w && handPlaced(w->cfg)) {
    m_drag = Drag::None;
    bool moved = false;
    const double* p = handlePoints(w->cfg);
    for (int i = 0; i < 2 * handleCount(w->cfg); ++i) moved = moved || std::abs(p[i] - m_pinStart[i]) > 0.05;
    if (moved) commitPin(*w);
    setCursor("grab");
    markEditDirty();
  } else if (m_drag == Drag::Rotate && w) {
    m_drag = Drag::None;
    const double a = w->cfg.rotation;
    // one undo step per turn (never merged with the previous one): setProp
    // records the stored value
    if (std::abs(a - m_rotStart) > 1e-6) {
      m_lastOpKind.clear();
      setProp(*w, "rotation", degreesText(a));
    }
    setCursor("grab");
    markEditDirty();
  } else if (m_drag != Drag::None && w) {
    // an axis held by a guide keeps it; the others snap to the grid (a turned
    // widget's box is not on the grid's axes: it stays where it was dropped)
    const bool free = modActive(XKB_MOD_NAME_SHIFT) || !m_gridOn || rotated(w->cfg);
    const bool keepX = !m_guidesV.empty(), keepY = !m_guidesH.empty();
    const int g = m_config.gridSize;
    auto snap = [g](int v) { return snapToGrid(v, g); };
    if (m_drag == Drag::Move) {
      moveWidget(*w, (free || keepX) ? w->cfg.x : snap(w->cfg.x), (free || keepY) ? w->cfg.y : snap(w->cfg.y));
    } else {
      // the edges that moved snap to the grid; the anchored ones stay
      WidgetConfig c = w->cfg;
      if (!free && !keepX) {
        if (m_gripCorner & 1) {
          c.width = snap(c.x + c.width) - c.x;
        } else {
          const int nx = snap(c.x);
          c.width += c.x - nx;
          c.x = nx;
        }
      }
      if (!free && !keepY) {
        if (m_gripCorner & 2) {
          c.height = snap(c.y + c.height) - c.y;
        } else {
          const int ny = snap(c.y);
          c.height += c.y - ny;
          c.y = ny;
        }
      }
      clampToOutput(c, w->output);
      w->cfg.x = c.x;
      w->cfg.y = c.y;
      w->cfg.width = c.width;
      w->cfg.height = c.height;
      placeLayer(*w);
    }
    // a click without movement is a selection, not an undo step
    if (!m_undo.empty() && m_undo.back().x == w->cfg.x && m_undo.back().y == w->cfg.y &&
        m_undo.back().w == w->cfg.width && m_undo.back().h == w->cfg.height)
      m_undo.pop_back();
    m_drag = Drag::None;
    m_guidesV.clear();
    m_guidesH.clear();
    setCursor("grab");
    persist(*w);
    markEditDirty();
  }
}

// wheel over a widget in the editor: step through its looks (visualizer
// styles, clock faces)
void App::onScroll(double value) {
  if (!m_edit || m_drag != Drag::None) return;
  m_scrollAcc += value;
  if (std::abs(m_scrollAcc) < 10.0) return;  // one wheel notch
  const int step = m_scrollAcc > 0 ? 1 : -1;
  m_scrollAcc = 0;
  uiFor(m_pointerEdit);
  if (uiScroll(m_px, m_py, step)) return;
  if (m_paintMode) {
    if (modActive(XKB_MOD_NAME_SHIFT)) {  // Shift+wheel: brush size / wand tolerance
      if (m_selectTool == 1) m_wandTolerance = std::clamp(m_wandTolerance + (step > 0 ? -0.05F : 0.05F), 0.02F, 1.0F);
      else m_brush = std::clamp(m_brush * (step > 0 ? 1 / 1.15 : 1.15), 6.0, 320.0);
      markEditDirty();
    } else {
      paintZoom(step > 0 ? 1 / 1.25 : 1.25, m_px, m_py);  // the wheel zooms about the pointer
    }
    return;
  }
  if (!m_pointerWidget) return;
  Widget& w = *m_pointerWidget;
  std::vector<std::string> looks;
  if (w.cfg.type == "clock") {
    looks = ClockWidget::faces();
  } else if (w.cfg.type == "shape") {
    looks = ShapeWidget::shapes();
  } else if (w.cfg.type == "text" || w.cfg.type == "lyrics") {
    looks = TextWidget::styles();
  } else if (w.cfg.type == "calendar") {
    looks = {"month", "week", "day"};
  } else if (w.cfg.type == "notes") {
    looks = {"paper", "glass", "none"};
  } else if (w.cfg.type == "visualizer") {
    for (const char* l : kLooks)
      if (std::string(l) != "frame") looks.push_back(l);  // frame owns the whole screen
  }
  if (looks.empty()) return;
  const std::string cur = lookOf(w);
  int idx = 0;
  for (size_t i = 0; i < looks.size(); ++i)
    if (looks[i] == cur) idx = static_cast<int>(i);
  const int n = static_cast<int>(looks.size());
  idx = ((idx + step) % n + n) % n;
  m_selected = &w;
  if (w.cfg.type == "text" || w.cfg.type == "lyrics") {
    setTextStyle(w, looks[static_cast<size_t>(idx)]);
  } else {
    pushUndo(w, "style");
    setStyle(w, looks[static_cast<size_t>(idx)]);
  }
  US_INFO("{}: {} {}", w.cfg.id, lookKey(w), looks[static_cast<size_t>(idx)]);
}

// ── on-canvas text ──────────────────────────────────────────────────────────

void App::drawEditorText(EditSurface& e) {
  const float W = static_cast<float>(e.w), H = static_cast<float>(e.h);
  const Color ink{1, 1, 1, 0.92F}, shadow{0, 0, 0, 0.55F};
  const Color plate{0.04F, 0.04F, 0.05F, 0.72F};
  auto put = [&](const std::string& text, const TextStyle& st, float x, float y, Color c) {
    const TextImage& img = m_text.get(text, st, e.scale);
    m_overlay.drawPill(x - 10, y - 5, img.w + 20, img.h + 10, 9, plate, W, H);
    m_text.draw(img, x + 1, y + 1, shadow, W, H);
    m_text.draw(img, x, y, c, W, H);
    return img.w;
  };
  // labels: id · look · size (and position while dragging)
  TextStyle ls{.family = "JetBrains Mono", .size = 12, .weight = 600};
  const bool zoomed = m_paintMode && m_zoom > 1.0001;  // the labels would sit over the wrong spot
  for (auto& w : m_widgets) {
    if (zoomed || w->output != e.output || !w->impl || w->impl->fullscreen() || !w->surface) continue;
    const bool sel = isSelected(w.get());
    // what it is: its look, or for a picture the file's name
    std::string what = optionLabel(lookOf(*w), spanish());
    if (w->cfg.type == "image") {
      const std::string file = w->cfg.options["file"].value_or(std::string());
      what = file.empty() ? (spanish() ? "sin imagen" : "no picture") : std::filesystem::path(file).filename().string();
    }
    if (w->cfg.type == "lyrics") what += "  ·  " + optionLabel(w->cfg.options["layout"].value_or(std::string("lines")), spanish());
    if (w->cfg.type == "text") {  // the style and the first words
      std::string words = w->cfg.options["text"].value_or(std::string("undershell"));
      std::replace(words.begin(), words.end(), '\n', ' ');
      if (words.size() > 24) {
        size_t cut = 21;
        while (cut > 0 && (static_cast<unsigned char>(words[cut]) & 0xC0) == 0x80) --cut;
        words = words.substr(0, cut) + "…";
      }
      what += "  ·  \u201c" + words + "\u201d";
    }
    std::string label = std::format("{}  ·  {}  ·  {}×{}", layerName(*w), what, w->cfg.width, w->cfg.height);
    if (turnedOnly(w->cfg) && !handPlaced(w->cfg)) label += std::format("  ·  {}°", degreesText(w->cfg.rotation));
    if (m_drag != Drag::None && m_drag != Drag::Rotate && w.get() == m_pointerWidget)
      label += std::format("  @ {},{}", w->cfg.x, w->cfg.y);
    // above what is seen; a selected turned widget's knob needs the room
    const Box vb = visualBox(w->cfg);
    float lx = static_cast<float>(std::max(8, vb.x + 10));
    float ly = static_cast<float>(vb.y) - 20;
    if (sel && !handPlaced(w->cfg)) ly -= OverlayPass::kHandleGap + OverlayPass::kHandleR;  // clear of the knob
    if (ly < 106) ly = static_cast<float>(vb.y) + 10;  // keep clear of the toolbar
    put(label, ls, lx, ly, sel ? readableAccent(w->impl->accent()) : ink);
  }
  drawUi(e);
  m_text.collect(20);
}

}  // namespace undershell
