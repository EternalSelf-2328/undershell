// SPDX-License-Identifier: GPL-3.0-or-later
// The desktop editor's interaction: selection, dragging and resizing with
// smart guides, keyboard nudging with key repeat, undo/redo and on-canvas
// labels. Pointer positions come from the fullscreen editor surface, so they
// are output coordinates and never depend on the widget being moved.
#include "app.hpp"
#include "clock.hpp"
#include "snap.hpp"

// the generated header names a parameter `namespace`
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace

#include <cmath>
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
                        "curtain", "line", "frame", "radial", "orb", "spiral"};

bool inGrip(const Widget& w, double x, double y) {
  return x > w.cfg.x + w.cfg.width - 26 && y > w.cfg.y + w.cfg.height - 26 && x < w.cfg.x + w.cfg.width &&
         y < w.cfg.y + w.cfg.height;
}

bool spanish() {
  for (const char* v : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
    const char* s = std::getenv(v);
    if (s && *s) return std::string_view(s).starts_with("es");
  }
  return false;
}

// the key that selects a widget's look, and its current value
const char* lookKey(const Widget& w) { return w.cfg.type == "clock" ? "face" : "style"; }
std::string lookOf(const Widget& w) {
  if (w.cfg.type == "clock") return w.cfg.options["face"].value_or(std::string("digital"));
  return w.cfg.options["style"].value_or(std::string("bars"));
}
}  // namespace

// ── hit testing / geometry ──────────────────────────────────────────────────

Widget* App::widgetAt(const Output* o, double x, double y) {
  // the selected widget wins overlaps, then the topmost (last listed)
  if (m_selected && m_selected->output == o && x >= m_selected->cfg.x && y >= m_selected->cfg.y &&
      x < m_selected->cfg.x + m_selected->cfg.width && y < m_selected->cfg.y + m_selected->cfg.height)
    return m_selected;
  for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it) {
    Widget& w = **it;
    if (w.output != o || !w.impl || w.impl->fullscreen() || !w.surface) continue;
    if (x >= w.cfg.x && y >= w.cfg.y && x < w.cfg.x + w.cfg.width && y < w.cfg.y + w.cfg.height) return &w;
  }
  return nullptr;
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
  w.appliedX = c.x;
  w.appliedY = c.y;
  if (w.layer) {
    zwlr_layer_surface_v1_set_margin(w.layer, c.y, 0, 0, c.x);
    wl_surface_commit(w.surface);
  }
  w.needsRender = true;
  w.drewEmpty = false;
}

static void resizeWidget(Widget& w, int width, int height) {
  if (width == w.cfg.width && height == w.cfg.height) return;
  w.cfg.width = width;
  w.cfg.height = height;
  if (w.layer) {
    zwlr_layer_surface_v1_set_size(w.layer, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    wl_surface_commit(w.surface);
  }
  w.needsRender = true;
  w.drewEmpty = false;
}

void App::onMoveApplied(Widget*, int, int) {}

void App::persist(Widget& w) {
  for (auto& c : m_config.widgets)
    if (c.id == w.cfg.id) {
      c.x = w.cfg.x;
      c.y = w.cfg.y;
      c.width = w.cfg.width;
      c.height = w.cfg.height;
    }
  if (!Config::saveGeometry(m_configPath, w.cfg)) US_WARN("could not save the new geometry of {}", w.cfg.id);
}

// Snaps a box being moved (all edges + centre) or resized (right/bottom edge)
// to the output's edges and centre and to other widgets on the same output.
// Records the guide lines to draw. Shift disables snapping.
void App::snapBox(const Widget& w, int& x, int& y, int& width, int& height, bool moving) {
  m_guidesV.clear();
  m_guidesH.clear();
  if (modActive(XKB_MOD_NAME_SHIFT) || !w.output) return;
  const double ow = w.output->logicalW(), oh = w.output->logicalH();
  std::vector<double> tx = {0, ow / 2, ow}, ty = {0, oh / 2, oh};
  for (auto& o : m_widgets) {
    if (o.get() == &w || o->output != w.output || !o->impl || o->impl->fullscreen()) continue;
    tx.insert(tx.end(), {double(o->cfg.x), o->cfg.x + o->cfg.width / 2.0, double(o->cfg.x + o->cfg.width)});
    ty.insert(ty.end(), {double(o->cfg.y), o->cfg.y + o->cfg.height / 2.0, double(o->cfg.y + o->cfg.height)});
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
    if (best({double(x + width)}, tx, d, g)) {
      width += static_cast<int>(std::lround(d));
      m_guidesV.push_back(static_cast<float>(g));
    }
    if (best({double(y + height)}, ty, d, g)) {
      height += static_cast<int>(std::lround(d));
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

void App::undo() {
  if (m_undo.empty()) return;
  EditOp op = m_undo.back();
  m_undo.pop_back();
  for (auto& w : m_widgets)
    if (w->cfg.id == op.id) m_redo.push_back(snapshot(*w));
  m_lastOpKind.clear();
  applyOp(op);
}

void App::redo() {
  if (m_redo.empty()) return;
  EditOp op = m_redo.back();
  m_redo.pop_back();
  for (auto& w : m_widgets)
    if (w->cfg.id == op.id) m_undo.push_back(snapshot(*w));
  m_lastOpKind.clear();
  applyOp(op);
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
  std::vector<Widget*> list;
  for (auto& w : m_widgets)
    if (w->impl && !w->impl->fullscreen() && w->surface) list.push_back(w.get());
  if (list.empty()) return;
  auto it = std::find(list.begin(), list.end(), m_selected);
  int idx = it == list.end() ? 0 : static_cast<int>(it - list.begin()) + step;
  idx = (idx % static_cast<int>(list.size()) + static_cast<int>(list.size())) % static_cast<int>(list.size());
  m_selected = list[static_cast<size_t>(idx)];
  markEditDirty();
}

void App::nudge(int dx, int dy, bool resize) {
  Widget* w = target();
  if (!w) return;
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
  const bool shift = modActive(XKB_MOD_NAME_SHIFT);
  const bool ctrl = modActive(XKB_MOD_NAME_CTRL);
  const bool alt = modActive(XKB_MOD_NAME_ALT);
  const int step = shift ? std::max(1, m_config.gridSize) : 1;
  xkb_keysym_t sym = m_xkbState ? xkb_state_key_get_one_sym(m_xkbState, key + 8) : XKB_KEY_NoSymbol;
  switch (key) {
    case KEY_ESC:
    case KEY_ENTER:
    case KEY_KPENTER:
      setEditMode(false);
      return false;
    case KEY_LEFT: nudge(-step, 0, alt); return true;
    case KEY_RIGHT: nudge(step, 0, alt); return true;
    case KEY_UP: nudge(0, -step, alt); return true;
    case KEY_DOWN: nudge(0, step, alt); return true;
    case KEY_TAB: cycleSelection(shift ? -1 : 1); return false;
    default: break;
  }
  const bool isZ = sym == XKB_KEY_z || sym == XKB_KEY_Z || (sym == XKB_KEY_NoSymbol && key == KEY_Z);
  const bool isY = sym == XKB_KEY_y || sym == XKB_KEY_Y || (sym == XKB_KEY_NoSymbol && key == KEY_Y);
  if (ctrl && isZ) {
    shift ? redo() : undo();
    return true;
  }
  if (ctrl && isY) {
    redo();
    return true;
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
  if (!m_pointerEdit) return;
  onPointerMotion(x, y);
}

void App::onPointerLeave(wl_surface* s) {
  if (editBySurface(s) != m_pointerEdit) return;
  if (m_drag == Drag::None) {
    m_pointerEdit = nullptr;
    m_pointerWidget = nullptr;
    markEditDirty();
  }
}

void App::onPointerMotion(double x, double y) {
  if (!m_edit || !m_pointerEdit) return;
  m_px = x;
  m_py = y;
  if (m_drag == Drag::None) {
    Widget* hit = widgetAt(m_pointerEdit->output, x, y);
    if (hit != m_pointerWidget) {
      m_pointerWidget = hit;
      markEditDirty();
    }
    setCursor(!hit ? "default" : (inGrip(*hit, x, y) ? "se-resize" : "grab"));
    return;
  }
  Widget* w = m_pointerWidget;
  if (!w) return;
  const int dx = static_cast<int>(std::lround(x - m_pressX));
  const int dy = static_cast<int>(std::lround(y - m_pressY));
  if (m_drag == Drag::Move) {
    int nx = m_startX + dx, ny = m_startY + dy, nw = w->cfg.width, nh = w->cfg.height;
    snapBox(*w, nx, ny, nw, nh, true);
    moveWidget(*w, nx, ny);
  } else {
    int nx = w->cfg.x, ny = w->cfg.y;
    int nw = std::max(48, m_dragW + dx), nh = std::max(32, m_dragH + dy);
    snapBox(*w, nx, ny, nw, nh, false);
    WidgetConfig c = w->cfg;
    c.width = nw;
    c.height = nh;
    clampToOutput(c, w->output);
    resizeWidget(*w, c.width, c.height);
  }
  markEditDirty();
}

void App::onPointerButton(uint32_t serial, uint32_t button, uint32_t state) {
  m_pointerSerial = serial;
  if (!m_edit || !m_pointerEdit) return;
  if (button == BTN_RIGHT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
    setEditMode(false);
    return;
  }
  if (button != BTN_LEFT) return;
  Widget* w = m_pointerWidget;
  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    m_selected = w;  // clicking empty space clears the selection
    markEditDirty();
    if (!w) return;
    pushUndo(*w, "drag");
    m_drag = inGrip(*w, m_px, m_py) ? Drag::Resize : Drag::Move;
    m_pressX = m_px;
    m_pressY = m_py;
    m_startX = w->cfg.x;
    m_startY = w->cfg.y;
    m_dragW = w->cfg.width;
    m_dragH = w->cfg.height;
    setCursor(m_drag == Drag::Resize ? "se-resize" : "grabbing");
  } else if (m_drag != Drag::None && w) {
    // an axis held by a guide keeps it; the others snap to the grid
    const bool free = modActive(XKB_MOD_NAME_SHIFT);
    const bool keepX = !m_guidesV.empty(), keepY = !m_guidesH.empty();
    const int g = m_config.gridSize;
    auto snap = [g](int v) { return snapToGrid(v, g); };
    if (m_drag == Drag::Move) {
      moveWidget(*w, (free || keepX) ? w->cfg.x : snap(w->cfg.x), (free || keepY) ? w->cfg.y : snap(w->cfg.y));
    } else {
      WidgetConfig c = w->cfg;
      if (!free && !keepX) c.width = snap(c.x + c.width) - c.x;
      if (!free && !keepY) c.height = snap(c.y + c.height) - c.y;
      clampToOutput(c, w->output);
      resizeWidget(*w, c.width, c.height);
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
  if (!m_edit || m_drag != Drag::None || !m_pointerWidget) return;
  m_scrollAcc += value;
  if (std::abs(m_scrollAcc) < 10.0) return;  // one wheel notch
  const int step = m_scrollAcc > 0 ? 1 : -1;
  m_scrollAcc = 0;
  Widget& w = *m_pointerWidget;
  std::vector<std::string> looks;
  if (w.cfg.type == "clock") {
    looks = ClockWidget::faces();
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
  pushUndo(w, "style");
  m_selected = &w;
  setStyle(w, looks[static_cast<size_t>(idx)]);
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
  // the help line, centred under the top bar
  const bool es = spanish();
  const std::string help =
      es ? "Arrastra: mover   ·   Esquina: tamaño   ·   Rueda: estilo   ·   Flechas: ajustar (Shift ×16, Alt: tamaño)"
           "   ·   Tab: siguiente   ·   Ctrl+Z / Ctrl+Shift+Z   ·   Shift: sin imán   ·   Esc: salir"
         : "Drag: move   ·   Corner: resize   ·   Wheel: look   ·   Arrows: nudge (Shift ×16, Alt: size)"
           "   ·   Tab: next   ·   Ctrl+Z / Ctrl+Shift+Z   ·   Shift: no snap   ·   Esc: done";
  TextStyle hs{.family = "Space Grotesk", .size = 13, .weight = 500};
  const TextImage& himg = m_text.get(help, hs, e.scale);
  put(help, hs, std::round((W - himg.w) / 2), 62, ink);

  // labels: id · look · size (and position while dragging)
  TextStyle ls{.family = "JetBrains Mono", .size = 12, .weight = 600};
  for (auto& w : m_widgets) {
    if (w->output != e.output || !w->impl || w->impl->fullscreen() || !w->surface) continue;
    const bool sel = w.get() == m_selected;
    std::string label = std::format("{}  ·  {}  ·  {}×{}", w->cfg.id, lookOf(*w), w->cfg.width, w->cfg.height);
    if (m_drag != Drag::None && w.get() == m_pointerWidget) label += std::format("  @ {},{}", w->cfg.x, w->cfg.y);
    float lx = static_cast<float>(std::max(8, w->cfg.x + 10));
    float ly = static_cast<float>(w->cfg.y) - 20;
    if (ly < 90) ly = static_cast<float>(w->cfg.y) + 10;  // keep clear of the help line
    put(label, ls, lx, ly, sel ? w->impl->accent() : ink);
  }
  m_text.collect(20);
}

}  // namespace undershell
