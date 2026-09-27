// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's inspector (the selected widget's options, edited live) and the
// gallery (add a widget). Everything writes through the config file, which
// stays the single source of truth; the in-memory options are updated at the
// same time so a change shows on the very next frame.
#include "app.hpp"
#include "schema.hpp"

#include <climits>
#include <cmath>
#include <filesystem>
#include <regex>
#include <xkbcommon/xkbcommon.h>

namespace undershell {

bool spanishUi() {
  for (const char* v : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
    const char* s = std::getenv(v);
    if (s && *s) return std::string_view(s).starts_with("es");
  }
  return false;
}

namespace {
constexpr float kPanelW = 300, kRowH = 30, kColorRowH = 54, kHeadH = 46, kFootH = 46, kLabelW = 118;
constexpr double kCoalesceProp = 1.0;

Color withAlphaC(Color c, float a) {
  c.a *= a;
  return c;
}

// the current value of an option as text, falling back to the schema default
std::string valueText(const toml::table& opts, const PropSpec& p) {
  const toml::node* n = opts.get(p.key);
  switch (p.kind) {
    case PropSpec::Enum:
    case PropSpec::Color: return n && n->is_string() ? n->value_or(p.defText) : p.defText;
    case PropSpec::Bool: return (n && n->is_boolean() ? n->value_or(p.def != 0) : p.def != 0) ? "true" : "false";
    case PropSpec::Number: {
      double v = p.def;
      if (n) v = n->value_or(p.def);
      if (p.integer) return std::to_string(static_cast<long>(std::lround(v)));
  return p.step < 0.095 ? std::format("{:.2f}", v) : std::format("{:.1f}", v);  // as many decimals as a step has
    }
  }
  return {};
}

double numberOf(const toml::table& opts, const PropSpec& p) {
  const toml::node* n = opts.get(p.key);
  return n ? n->value_or(p.def) : p.def;
}

// stores a TOML-formatted value into an options table
void storeOption(toml::table& t, const std::string& key, const std::string& tomlValue) {
  if (tomlValue.size() >= 2 && tomlValue.front() == '"') t.insert_or_assign(key, tomlValue.substr(1, tomlValue.size() - 2));
  else if (tomlValue == "true" || tomlValue == "false") t.insert_or_assign(key, tomlValue == "true");
  else if (tomlValue.find_first_of(".eE") != std::string::npos) t.insert_or_assign(key, std::stod(tomlValue));
  else if (!tomlValue.empty()) t.insert_or_assign(key, static_cast<int64_t>(std::stoll(tomlValue)));
}

std::string formatNumber(const PropSpec& p, double v) {
  v = std::clamp(std::round((v - p.min) / p.step) * p.step + p.min, p.min, p.max);
  if (p.integer) return std::to_string(static_cast<long>(std::lround(v)));
  return p.step < 0.095 ? std::format("{:.2f}", v) : std::format("{:.1f}", v);  // as many decimals as a step has
}

const char* typeName(const std::string& type, bool es) {
  if (type == "visualizer") return es ? "Visualizador" : "Visualizer";
  if (type == "clock") return es ? "Reloj" : "Clock";
  if (type == "now_playing") return es ? "Música" : "Now playing";
  return "Widget";
}
}  // namespace

// ── widget lifecycle (gallery, duplicate, delete) ───────────────────────────

std::string App::addWidget(const std::string& type, const std::string& look, int x, int y, bool record) {
  const Output* o = m_outputs.empty() ? nullptr : m_outputs.front().get();
  if (m_pointerEdit) o = m_pointerEdit->output;
  const int ow = o ? static_cast<int>(o->logicalW()) : 1920, oh = o ? static_cast<int>(o->logicalH()) : 1080;
  std::string id = type;
  for (int n = 2;; ++n) {
    bool taken = false;
    for (auto& c : m_config.widgets) taken = taken || c.id == id;
    if (!taken) break;
    id = type + std::to_string(n);
  }
  const int w = type == "visualizer" ? 1000 : 560;
  const int h = type == "clock" ? 240 : (type == "now_playing" ? 302 : 280);
  if (x == INT_MIN) x = (ow - w) / 2;
  if (y == INT_MIN) y = (oh - h) / 2;
  const std::string block = Config::defaultBlock(type, id, o ? o->name : "", x, y, w, h, look);
  if (!Config::appendBlock(m_configPath, block)) return {};
  WidgetConfig placeholder;  // keeps ids unique before the reload lands
  placeholder.id = id;
  m_config.widgets.push_back(placeholder);
  if (record) {
    EditOp op;
    op.kind = "add";
    op.id = id;
    op.block = block;
    m_undo.push_back(op);
    m_redo.clear();
  }
  m_pendingSelect = id;
  m_reloadConfigAt = nowSeconds() + 0.05;
  return id;
}

void App::removeWidget(Widget& w) {
  std::string block;
  if (!Config::removeBlock(m_configPath, w.cfg.id, &block)) return;
  EditOp op;
  op.kind = "remove";
  op.id = w.cfg.id;
  op.block = block;
  m_undo.push_back(op);
  m_redo.clear();
  if (m_selected == &w) m_selected = nullptr;
  if (m_pointerWidget == &w) m_pointerWidget = nullptr;
  m_reloadConfigAt = nowSeconds() + 0.02;
  markEditDirty();
}

void App::duplicateWidget(Widget& w) {
  std::string block = Config::blockText(m_configPath, w.cfg.id);
  if (block.empty()) return;
  // "visualizer-2" copies to "visualizer-3", not "visualizer-2-2"
  const std::string base = std::regex_replace(w.cfg.id, std::regex(R"(-\d+$)"), "");
  std::string id = base;
  for (int n = 2;; ++n) {
    bool taken = false;
    for (auto& c : m_config.widgets) taken = taken || c.id == id;
    if (!taken) break;
    id = base + "-" + std::to_string(n);
  }
  // the copy: new id, offset 32 px so it is visibly separate
  block = Config::retargetBlock(block, id, w.cfg.x + 32, w.cfg.y + 32);
  if (!Config::appendBlock(m_configPath, block)) return;
  WidgetConfig placeholder;
  placeholder.id = id;
  m_config.widgets.push_back(placeholder);
  EditOp op;
  op.kind = "add";
  op.id = id;
  op.block = block;
  m_undo.push_back(op);
  m_redo.clear();
  m_pendingSelect = id;
  m_reloadConfigAt = nowSeconds() + 0.05;
}

void App::setProp(Widget& w, const std::string& key, const std::string& tomlValue) {
  const toml::node* before = w.cfg.options.get(key);
  EditOp op;
  op.kind = "prop";
  op.id = w.cfg.id;
  op.key = key;
  op.value = before ? Config::tomlText(*before) : std::string();
  // a slider burst on one key is one undo step
  const double now = nowSeconds();
  const bool coalesce = m_lastOpKind == "prop:" + key && m_lastOpId == w.cfg.id && now - m_lastOpAt < kCoalesceProp;
  if (!coalesce) {
    m_undo.push_back(op);
    m_redo.clear();
  }
  m_lastOpKind = "prop:" + key;
  m_lastOpId = w.cfg.id;
  m_lastOpAt = now;
  applyProp(w, key, tomlValue);
}

// writes a value without recording undo (setProp and undo/redo use it)
void App::applyProp(Widget& w, const std::string& key, const std::string& tomlValue) {
  std::string v = tomlValue;
  if (v.empty())  // "absent before": restore the schema default
    for (const auto& p : schemaFor(w.cfg.type))
      if (key == p.key)
        v = p.kind == PropSpec::Number ? formatNumber(p, p.def)
            : p.kind == PropSpec::Bool ? (p.def != 0 ? "true" : "false")
                                       : "\"" + p.defText + "\"";
  if (v.empty()) return;
  storeOption(w.cfg.options, key, v);
  if (key == "depth") w.cfg.depth = v == "true";
  if (key == "depth_level") {
    try {
      w.cfg.depthLevel = std::clamp(std::stod(v), 0.0, 100.0);
    } catch (const std::exception&) {
    }
  }
  if (key == "rotation") {
    try {
      setRotation(w, std::stod(v));
    } catch (const std::exception&) {
    }
  }
  if (w.impl) w.impl->configure(w.cfg, m_noctalia.state());
  w.needsRender = true;
  w.drewEmpty = false;
  Config::setKey(m_configPath, w.cfg.id, key, v);
  markEditDirty();
}

// ── layout ──────────────────────────────────────────────────────────────────
// Everything the editor draws on top: the floating toolbar (add, undo, redo,
// magnet, grid, widget chips, help, done), its tooltip, the gallery under the
// add button, the shortcuts card and the inspector of the selected widget.

namespace {
constexpr float kBarY = 50, kBarH = 46, kBtn = 34, kGap = 6, kSep = 14, kChipH = 30;
// the Profiles (saved layouts) panel
constexpr float kSavesW = 540, kSavesHead = 92, kSaveRowH = 70, kSavePitch = 76;
constexpr int kSaveRows = 5;
// the depth brush panel
constexpr float kPaintW = 300, kPaintH = 478;
const char* kToolKeys[5][3] = {{"front", "Al frente", "To front"},
                               {"back", "Al fondo", "To back"},
                               {"match", "Igualar", "Match"},
                               {"erase", "Borrar", "Erase"},
                               {"smooth", "Suavizar", "Smooth"}};
const char* kSelectKeys[4][3] = {{"brush", "Pincel", "Brush"}, {"wand", "Varita", "Wand"}, {"lasso", "Lazo", "Lasso"}, {"hand", "Mano", "Hand"}};

bool isSaveControl(App::UiControl::Type t) {
  using T = App::UiControl::Type;
  return t == T::SaveNew || t == T::SaveLoad || t == T::SaveOverwrite || t == T::SaveRename || t == T::SaveDelete ||
         t == T::SaveRow;
}

const char* chipIcon(const std::string& type) {
  return type == "clock" ? "clock" : (type == "now_playing" ? "music" : "bars");
}

std::string tipFor(App::UiControl::Type t, bool es) {
  using T = App::UiControl::Type;
  switch (t) {
    case T::Plus: return es ? "Agregar widget" : "Add widget";
    case T::Undo: return es ? "Deshacer  (Ctrl+Z)" : "Undo  (Ctrl+Z)";
    case T::Redo: return es ? "Rehacer  (Ctrl+Shift+Z)" : "Redo  (Ctrl+Shift+Z)";
    case T::Magnet: return es ? "Imán: alinear con bordes, centro y otros widgets" : "Magnet: snap to edges, centre and widgets";
    case T::Grid: return es ? "Cuadrícula" : "Grid";
    case T::Help: return es ? "Atajos de teclado" : "Keyboard shortcuts";
    case T::Saves: return es ? "Perfiles guardados" : "Saved profiles";
    case T::Paint: return es ? "Pincel de profundidad: corrige qué queda delante" : "Depth brush: fix what stands in front";
    case T::PaintZoomIn: return es ? "Acercar (rueda o +)" : "Zoom in (wheel or +)";
    case T::PaintZoomOut: return es ? "Alejar (rueda o −)" : "Zoom out (wheel or −)";
    case T::PaintZoomReset: return es ? "Tamaño real (0)" : "Actual size (0)";
    case T::SaveOverwrite: return es ? "Sobrescribir con lo que hay en pantalla" : "Overwrite with what is on screen";
    case T::SaveRename: return es ? "Renombrar" : "Rename";
    case T::SaveDelete: return es ? "Eliminar (clic otra vez para confirmar)" : "Delete (click again to confirm)";
    case T::Done: return es ? "Terminar  (Esc)" : "Done  (Esc)";
    default: return {};
  }
}
}  // namespace

void App::layoutUi(const EditSurface& e) {
  m_ui.clear();
  m_uiOutput = e.output;
  const float W = static_cast<float>(e.w), H = static_cast<float>(e.h);
  const TextStyle chipText{.family = "Space Grotesk", .size = 13, .weight = 600};

  // ── toolbar: measure, then centre ──
  struct Item {
    UiControl::Type type;
    float w;
    std::string value;
  };
  std::vector<Item> items = {{UiControl::Plus, kBtn, ""},   {UiControl::Panel, kSep, "sep"},
                             {UiControl::Undo, kBtn, ""},   {UiControl::Redo, kBtn, ""},
                             {UiControl::Panel, kSep, "sep"}, {UiControl::Magnet, kBtn, ""},
                             {UiControl::Grid, kBtn, ""},   {UiControl::Panel, kSep, "sep"},
                             {UiControl::Saves, kBtn, ""},  {UiControl::Paint, kBtn, ""},
                             {UiControl::Panel, kSep, "sep"}};
  for (auto& w : m_widgets) {
    if (!w->impl || w->output != e.output) continue;
    float tw = 0, th = 0, b = 0;
    TextRenderer::measure(w->cfg.id, chipText, tw, th, b);
    items.push_back({UiControl::Chip, tw + 44, w->cfg.id});
  }
  items.push_back({UiControl::Panel, kSep, "sep"});
  items.push_back({UiControl::Help, kBtn, ""});
  items.push_back({UiControl::Done, kBtn + 44, ""});
  float total = 12;
  for (auto& it : items) total += it.w + kGap;
  total += 12 - kGap;
  const float bx = std::round((W - total) / 2);
  m_ui.push_back({UiControl::Panel, {bx, kBarY, total, kBarH}, -1, "toolbar"});
  float x = bx + 12;
  Rect addRect, savesRect;
  for (auto& it : items) {
    const float h = it.type == UiControl::Chip ? kChipH : kBtn;
    Rect r{x, kBarY + (kBarH - h) / 2, it.w, h};
    if (it.value != "sep") m_ui.push_back({it.type, r, -1, it.value});
    if (it.type == UiControl::Plus) addRect = r;
    if (it.type == UiControl::Saves) savesRect = r;
    x += it.w + kGap;
  }

  // ── inspector of the selected widget ──
  Widget* w = m_selected;
  if (w && w->output == e.output && w->impl) {
    const auto& schema = schemaFor(w->cfg.type);
    float contentH = kHeadH + kFootH;
    for (const auto& p : schema) contentH += p.kind == PropSpec::Color ? kColorRowH : kRowH;
    const float top = kBarY + kBarH + 14;
    const float maxH = H - top - 16;
    const float panelH = std::min(contentH, maxH);
    m_inspScroll = std::clamp(m_inspScroll, 0.0F, std::max(0.0F, contentH - panelH));
    float px, py;
    if (w->impl->fullscreen()) {  // a frame owns the screen: dock at the right
      px = W - kPanelW - 24;
      py = top;
    } else {
      px = static_cast<float>(w->cfg.x + w->cfg.width) + 16;
      if (px + kPanelW > W - 12) px = static_cast<float>(w->cfg.x) - 16 - kPanelW;
      if (px < 12) px = W - kPanelW - 16;
      py = std::clamp(static_cast<float>(w->cfg.y), top, std::max(top, H - panelH - 16));
    }
    m_ui.push_back({UiControl::Panel, {px, py, kPanelW, panelH}, -1, "inspector"});
    float y = py + kHeadH - m_inspScroll + 6;  // same origin as drawUi
    auto visible = [&](const Rect& r) { return r.y >= py + kHeadH - 2 && r.y + r.h <= py + panelH - kFootH + 2; };
    const float cx = px + kLabelW + 14, cw = kPanelW - kLabelW - 28;
    for (size_t i = 0; i < schema.size(); ++i) {
      const auto& p = schema[i];
      const int idx = static_cast<int>(i);
      if (p.kind == PropSpec::Enum) {
        Rect prev{cx, y + 4, 24, 22}, next{cx + cw - 24, y + 4, 24, 22};
        if (visible(prev)) {
          m_ui.push_back({UiControl::Prev, prev, idx});
          m_ui.push_back({UiControl::Next, {cx + 24, y + 4, cw - 48, 22}, idx});
          m_ui.push_back({UiControl::Next, next, idx});
        }
        y += kRowH;
      } else if (p.kind == PropSpec::Bool) {
        Rect t{cx + cw - 38, y + 5, 38, 20};
        if (visible(t)) m_ui.push_back({UiControl::Toggle, t, idx});
        y += kRowH;
      } else if (p.kind == PropSpec::Number) {
        Rect sr{cx, y + 5, cw - 46, 20};
        if (visible(sr)) m_ui.push_back({UiControl::Slider, sr, idx});
        y += kRowH;
      } else {
        const auto& sw = colorSwatches();
        const float size = 20, gap = (kPanelW - 28 - sw.size() * size) / (sw.size() - 1);
        for (size_t k = 0; k < sw.size(); ++k) {
          Rect r{px + 14 + k * (size + gap), y + 26, size, size};
          if (visible(r)) m_ui.push_back({UiControl::Swatch, r, idx, sw[k]});
        }
        y += kColorRowH;
      }
    }
    const float fy = py + panelH - kFootH + 8;
    m_ui.push_back({UiControl::Duplicate, {px + 14, fy, (kPanelW - 38) / 2, 30}});
    m_ui.push_back({UiControl::Delete, {px + 24 + (kPanelW - 38) / 2, fy, (kPanelW - 38) / 2, 30}});
  }

  // ── depth brush panel (docked left) ──
  if (m_paintMode) {
    const float px = 24, py = kBarY + kBarH + 14;
    m_ui.push_back({UiControl::Panel, {px, py, kPaintW, kPaintH}, -1, "paint"});
    const float sw4 = (kPaintW - 28 - 18) / 4;
    for (int t = 0; t < 4; ++t)
      m_ui.push_back({UiControl::PaintSelect, {px + 14 + t * (sw4 + 6), py + 84, sw4, 48}, -1, std::to_string(t)});
    const float aw = (kPaintW - 28 - 12) / 3;
    for (int t = 0; t < 5; ++t)
      m_ui.push_back({UiControl::PaintTool, {px + 14 + (t % 3) * (aw + 6), py + 164 + (t / 3) * 40, aw, 34}, -1, std::to_string(t)});
    if (m_selectTool <= 1) m_ui.push_back({UiControl::PaintSize, {px + 14, py + 272, kPaintW - 28 - 60, 20}});
    if (m_selectTool == 0 || m_selectTool == 2) m_ui.push_back({UiControl::PaintSmart, {px + kPaintW - 14 - 38, py + 306, 38, 20}});
    m_ui.push_back({UiControl::PaintZoomOut, {px + 14, py + 340, 34, 30}});
    m_ui.push_back({UiControl::PaintZoomIn, {px + 14 + 34 + 70, py + 340, 34, 30}});
    m_ui.push_back({UiControl::PaintZoomReset, {px + kPaintW - 14 - 56, py + 340, 56, 30}});
    const float hb = (kPaintW - 28 - 8) / 2;
    m_ui.push_back({UiControl::PaintUndo, {px + 14, py + 382, hb, 32}});
    m_ui.push_back({UiControl::PaintClear, {px + 22 + hb, py + 382, hb, 32}});
  }

  // ── popovers last: drawn on top, hit first ──
  if (m_galleryOpen) {
    const float gx = std::clamp(addRect.x - 10, 12.0F, W - 282), gy = kBarY + kBarH + 10;
    m_ui.push_back({UiControl::Panel, {gx, gy, 270, 3 * 70 + 10}, -1, "gallery"});
    const char* types[] = {"visualizer", "clock", "now_playing"};
    float y = gy + 8;
    for (const char* t : types) {
      m_ui.push_back({UiControl::GalleryItem, {gx + 8, y, 254, 62}, -1, t});
      y += 70;
    }
  }
  if (m_savesOpen) {
    const int n = static_cast<int>(m_saves.size());
    m_saveScroll = std::clamp(m_saveScroll, 0, std::max(0, n - kSaveRows));
    const int shown = std::min(kSaveRows, n - m_saveScroll);
    const float sh = kSavesHead + 48 + (n == 0 ? 56 : shown * kSavePitch) + 10 + (n > kSaveRows ? 18 : 0);
    const float sx = std::clamp(savesRect.x + savesRect.w / 2 - kSavesW / 2, 12.0F, W - kSavesW - 12);
    const float sy = kBarY + kBarH + 10;
    m_ui.push_back({UiControl::Panel, {sx, sy, kSavesW, sh}, -1, "saves"});
    m_ui.push_back({UiControl::SaveNew, {sx + 14, sy + kSavesHead, kSavesW - 28, 38}});
    float ry = sy + kSavesHead + 48;
    for (int k = 0; k < shown; ++k) {
      const SavedLayout& sv = m_saves[static_cast<size_t>(m_saveScroll + k)];
      const Rect row{sx + 14, ry, kSavesW - 28, kSaveRowH};
      m_ui.push_back({UiControl::SaveRow, row, -1, sv.id});
      float bx = row.x + row.w - 10 - 28;
      const float by = row.y + (row.h - 28) / 2;
      m_ui.push_back({UiControl::SaveDelete, {bx, by, 28, 28}, -1, sv.id});
      bx -= 32;
      m_ui.push_back({UiControl::SaveRename, {bx, by, 28, 28}, -1, sv.id});
      bx -= 32;
      m_ui.push_back({UiControl::SaveOverwrite, {bx, by, 28, 28}, -1, sv.id});
      bx -= 74;
      m_ui.push_back({UiControl::SaveLoad, {bx, by, 68, 28}, -1, sv.id});
      ry += kSavePitch;
    }
  }
  if (m_helpOpen) {
    const float hw = 580, hh = 334;
    m_ui.push_back({UiControl::Panel, {std::round((W - hw) / 2), kBarY + kBarH + 10, hw, hh}, -1, "help"});
  }
}

void App::layoutGallery(float, float) {}

int App::uiHit(double x, double y) const {
  // last matching control wins (controls are listed after their plate)
  int hit = -1;
  for (size_t i = 0; i < m_ui.size(); ++i)
    if (m_ui[i].r.contains(static_cast<float>(x), static_cast<float>(y))) hit = static_cast<int>(i);
  return hit;
}

// ── interaction ─────────────────────────────────────────────────────────────

bool App::uiPress(int index, double x) {
  if (index < 0 || index >= static_cast<int>(m_ui.size())) return false;
  const UiControl c = m_ui[static_cast<size_t>(index)];
  Widget* w = m_selected;
  // a click anywhere but the name being typed keeps that name; a delete stays
  // armed only for the very next click
  if (!(c.type == UiControl::SaveRow && c.value == m_renaming)) commitRename();
  if (c.type != UiControl::SaveDelete) m_confirmDelete.clear();
  if (c.type != UiControl::PaintClear) m_confirmClear = false;
  switch (c.type) {
    case UiControl::Paint:
      setPaintMode(!m_paintMode);
      return true;
    case UiControl::PaintTool:
      m_paintTool = std::clamp(std::atoi(c.value.c_str()), 0, 4);
      markEditDirty();
      return true;
    case UiControl::PaintSelect:
      m_selectTool = std::clamp(std::atoi(c.value.c_str()), 0, 3);
      m_lasso.clear();
      markEditDirty();
      return true;
    case UiControl::PaintZoomIn:
    case UiControl::PaintZoomOut:
      if (m_uiOutput) paintZoom(c.type == UiControl::PaintZoomIn ? 1.5 : 1 / 1.5, m_uiOutput->logicalW() / 2, m_uiOutput->logicalH() / 2);
      return true;
    case UiControl::PaintZoomReset:
      m_zoom = 1;
      markEditDirty();
      return true;
    case UiControl::PaintSize:
      m_drag = Drag::Slider;
      m_sliderControl = index;
      uiDrag(x);
      return true;
    case UiControl::PaintSmart:
      m_smartBrush = !m_smartBrush;
      markEditDirty();
      return true;
    case UiControl::PaintUndo:
      paintUndo();
      return true;
    case UiControl::PaintClear:
      if (m_confirmClear) {
        paintClear();
        m_confirmClear = false;
      } else {
        m_confirmClear = true;
      }
      markEditDirty();
      return true;
    case UiControl::Saves:
      m_savesOpen = !m_savesOpen;
      m_galleryOpen = m_helpOpen = false;
      if (m_savesOpen) refreshSaves();
      markEditDirty();
      return true;
    case UiControl::SaveNew: {
      const std::string id = saveLayout("");
      if (!id.empty()) {
        m_renaming = id;  // type a name right away (Enter keeps "Profile N")
        m_renameText.clear();
        m_saveScroll = 0;
      }
      markEditDirty();
      return true;
    }
    case UiControl::SaveLoad:
      loadSave(c.value);
      return true;
    case UiControl::SaveOverwrite:
      overwriteSave(c.value);
      return true;
    case UiControl::SaveRename:
      for (const auto& sv : m_saves)
        if (sv.id == c.value) {
          m_renaming = sv.id;
          m_renameText = sv.name;
        }
      markEditDirty();
      return true;
    case UiControl::SaveDelete:
      if (m_confirmDelete == c.value) {
        Config::deleteSave(savesDir(), c.value);
        m_confirmDelete.clear();
        refreshSaves();
      } else {
        m_confirmDelete = c.value;
      }
      markEditDirty();
      return true;
    case UiControl::SaveRow:
      markEditDirty();
      return true;
    case UiControl::Plus:
      m_galleryOpen = !m_galleryOpen;
      m_helpOpen = m_savesOpen = false;
      markEditDirty();
      return true;
    case UiControl::GalleryItem:
      addWidget(c.value, "", INT_MIN, INT_MIN, true);
      m_galleryOpen = false;
      markEditDirty();
      return true;
    case UiControl::Undo: undo(); return true;
    case UiControl::Redo: redo(); return true;
    case UiControl::Magnet:
      m_snapOn = !m_snapOn;
      markEditDirty();
      return true;
    case UiControl::Grid:
      m_gridOn = !m_gridOn;
      markEditDirty();
      return true;
    case UiControl::Help:
      m_helpOpen = !m_helpOpen;
      m_galleryOpen = m_savesOpen = false;
      markEditDirty();
      return true;
    case UiControl::Done: setEditMode(false); return true;
    case UiControl::Chip:
      for (auto& ww : m_widgets)
        if (ww->cfg.id == c.value) {
          if (m_selected != ww.get()) m_inspScroll = 0;
          m_selected = ww.get();
        }
      markEditDirty();
      return true;
    case UiControl::Panel:
      if (c.value == "help") {  // a click on the card closes it
        m_helpOpen = false;
        markEditDirty();
      }
      return true;  // plates swallow clicks
    case UiControl::Duplicate:
      if (w) duplicateWidget(*w);
      return true;
    case UiControl::Delete:
      if (w) removeWidget(*w);
      return true;
    default: break;
  }
  if (!w || c.prop < 0) return true;
  const auto& schema = schemaFor(w->cfg.type);
  if (c.prop >= static_cast<int>(schema.size())) return true;
  const PropSpec& p = schema[static_cast<size_t>(c.prop)];
  if (c.type == UiControl::Prev || c.type == UiControl::Next) {
    const std::string cur = valueText(w->cfg.options, p);
    int i = 0;
    for (size_t k = 0; k < p.options.size(); ++k)
      if (p.options[k] == cur) i = static_cast<int>(k);
    const int n = static_cast<int>(p.options.size());
    i = ((i + (c.type == UiControl::Next ? 1 : -1)) % n + n) % n;
    setProp(*w, p.key, "\"" + p.options[static_cast<size_t>(i)] + "\"");
  } else if (c.type == UiControl::Toggle) {
    setProp(*w, p.key, valueText(w->cfg.options, p) == "true" ? "false" : "true");
  } else if (c.type == UiControl::Swatch) {
    setProp(*w, p.key, "\"" + c.value + "\"");
  } else if (c.type == UiControl::Slider) {
    m_drag = Drag::Slider;
    m_sliderControl = index;
    // Shift: nudge from the current value, one step per 4 px, instead of
    // jumping to where the pointer is
    m_sliderFine = modActive(XKB_MOD_NAME_SHIFT);
    m_sliderX = x;
    m_sliderStart = numberOf(w->cfg.options, p);
    if (!m_sliderFine) uiDrag(x);
  }
  return true;
}

void App::uiDrag(double x) {
  if (m_sliderControl < 0 || m_sliderControl >= static_cast<int>(m_ui.size())) return;
  const UiControl& c = m_ui[static_cast<size_t>(m_sliderControl)];
  if (c.type == UiControl::PaintSize) {
    const double t = std::clamp((x - c.r.x) / c.r.w, 0.0, 1.0);
    if (m_selectTool == 1) m_wandTolerance = static_cast<float>(std::max(0.02, t));
    else m_brush = 6 + t * t * (320 - 6);  // finer control at small sizes
    markEditDirty();
    return;
  }
  if (!m_selected) return;
  const auto& schema = schemaFor(m_selected->cfg.type);
  if (c.prop < 0 || c.prop >= static_cast<int>(schema.size())) return;
  const PropSpec& p = schema[static_cast<size_t>(c.prop)];
  const double t = std::clamp((x - c.r.x) / c.r.w, 0.0, 1.0);
  const std::string v = formatNumber(p, m_sliderFine ? m_sliderStart + std::round((x - m_sliderX) / 4.0) * p.step
                                                     : p.min + t * (p.max - p.min));
  if (v != valueText(m_selected->cfg.options, p)) setProp(*m_selected, p.key, v);
}

bool App::uiScroll(double x, double y, int step) {
  const int hit = uiHit(x, y);
  if (hit < 0) return false;
  const UiControl c = m_ui[static_cast<size_t>(hit)];
  if (c.value == "saves" || isSaveControl(c.type)) {
    const int before = m_saveScroll;
    m_saveScroll = std::clamp(m_saveScroll + step, 0, std::max(0, static_cast<int>(m_saves.size()) - kSaveRows));
    if (m_saveScroll != before) markEditDirty();
    return true;
  }
  Widget* w = m_selected;
  if (w && c.prop >= 0) {
    const auto& schema = schemaFor(w->cfg.type);
    const PropSpec& p = schema[static_cast<size_t>(c.prop)];
    if (p.kind == PropSpec::Number) {
      setProp(*w, p.key, formatNumber(p, numberOf(w->cfg.options, p) + (step > 0 ? -p.step : p.step)));
      return true;
    }
    if (p.kind == PropSpec::Enum) {
      UiControl copy = c;
      copy.type = step > 0 ? UiControl::Next : UiControl::Prev;
      m_ui.push_back(copy);
      uiPress(static_cast<int>(m_ui.size()) - 1, x);
      return true;
    }
  }
  if (c.value == "inspector" || c.prop >= 0) {
    m_inspScroll += step * 36.0F;  // scroll a tall inspector
    markEditDirty();
  }
  return true;  // the wheel over any editor chrome never reaches widgets
}

// ── drawing ─────────────────────────────────────────────────────────────────

namespace {
// small line icons (1.6 px strokes), centred on (cx, cy)
void icon(Canvas& c, const std::string& name, float cx, float cy, Color col) {
  const float w = 1.7F;
  if (name == "plus") {
    c.segment(cx - 7, cy, cx + 7, cy, 2.2F, col);
    c.segment(cx, cy - 7, cx, cy + 7, 2.2F, col);
  } else if (name == "undo" || name == "redo") {
    const float s = name == "undo" ? 1.0F : -1.0F;
    c.arc(cx, cy + 1, 6, w, name == "undo" ? 0.0F : 3.1416F, name == "undo" ? 3.1416F : 6.2832F, col, true);
    c.triangle(cx - s * 6.0F - 3.5F, cy - 5, cx - s * 6.0F + 3.5F, cy - 5, cx - s * 6.0F, cy + 1, col);
  } else if (name == "magnet") {
    c.arc(cx, cy, 6, 3.2F, 1.5708F, 4.7124F, col, false);
    c.segment(cx - 6, cy, cx - 6, cy - 6, 3.2F, col, false);
    c.segment(cx + 6, cy, cx + 6, cy - 6, 3.2F, col, false);
    c.roundRect(cx - 7.6F, cy - 8.5F, 3.2F, 2.6F, 0, Color{1, 1, 1, col.a});
    c.roundRect(cx + 4.4F, cy - 8.5F, 3.2F, 2.6F, 0, Color{1, 1, 1, col.a});
  } else if (name == "grid") {
    for (int i = -1; i <= 1; ++i) {
      c.segment(cx - 7, cy + i * 4.5F, cx + 7, cy + i * 4.5F, 1.3F, col, false);
      c.segment(cx + i * 4.5F, cy - 7, cx + i * 4.5F, cy + 7, 1.3F, col, false);
    }
  } else if (name == "help") {
    // a question mark: the hook, the stem and the dot
    c.arc(cx, cy - 2.5F, 4.2F, 2.1F, -1.3F + 6.2832F, 6.2832F + 1.9F, col, true);
    c.arc(cx, cy - 2.5F, 4.2F, 2.1F, 0.0F, 1.9F, col, true);
    c.segment(cx + 2.0F, cy + 1.2F, cx, cy + 2.6F, 2.1F, col);
    c.circle(cx, cy + 6.6F, 1.4F, col);
  } else if (name == "check") {
    c.segment(cx - 6, cy, cx - 2, cy + 4.5F, 2.2F, col);
    c.segment(cx - 2, cy + 4.5F, cx + 6.5F, cy - 5, 2.2F, col);
  } else if (name == "bars") {
    const float h[] = {6, 11, 8};
    for (int i = 0; i < 3; ++i) c.roundRect(cx - 5 + i * 4, cy + 5 - h[i], 2.4F, h[i], 1.2F, col);
  } else if (name == "clock") {
    c.circle(cx, cy, 6, Color{0, 0, 0, 0}, 1.6F, col);
    c.segment(cx, cy, cx, cy - 3.5F, 1.5F, col);
    c.segment(cx, cy, cx + 2.8F, cy + 1.2F, 1.5F, col);
  } else if (name == "music") {
    c.circle(cx - 2.5F, cy + 3.5F, 2.4F, col);
    c.segment(cx - 0.6F, cy + 3.5F, cx - 0.6F, cy - 5.5F, 1.5F, col, false);
    c.segment(cx - 0.6F, cy - 5.5F, cx + 4.5F, cy - 3.5F, 1.8F, col, false);
  } else if (name == "copy") {
    c.roundRect(cx - 5, cy - 3, 8, 9, 2, Color{0, 0, 0, 0}, 1.4F, col);
    c.roundRect(cx - 2, cy - 6, 8, 9, 2, Color{0, 0, 0, 0}, 1.4F, col);
  } else if (name == "save") {
    // a floppy disk
    c.roundRect(cx - 6.5F, cy - 6.5F, 13, 13, 2.2F, Color{0, 0, 0, 0}, 1.5F, col);
    c.roundRect(cx - 3.5F, cy - 6.5F, 6.5F, 4.2F, 0.8F, Color{0, 0, 0, 0}, 1.3F, col);
    c.roundRect(cx - 3.8F, cy + 1.2F, 7.6F, 4.6F, 1, col);
  } else if (name == "brush") {
    c.segment(cx + 5.5F, cy - 6.5F, cx - 0.5F, cy + 0.5F, 2.2F, col);
    c.circle(cx - 3.0F, cy + 3.5F, 3.4F, col);
  } else if (name == "wand") {
    c.segment(cx - 6, cy + 6, cx + 3, cy - 3, 2.2F, col);
    c.segment(cx + 5, cy - 8, cx + 5, cy - 4, 1.4F, col);
    c.segment(cx + 3, cy - 6, cx + 7, cy - 6, 1.4F, col);
    c.circle(cx + 7.5F, cy + 1.5F, 1.1F, col);
    c.circle(cx - 1.5F, cy - 7.0F, 1.1F, col);
  } else if (name == "lasso") {
    c.circle(cx, cy - 1.5F, 6.5F, Color{0, 0, 0, 0}, 1.5F, col);
    c.segment(cx - 3.5F, cy + 4, cx - 5.5F, cy + 8, 1.5F, col);
  } else if (name == "hand") {
    c.roundRect(cx - 5, cy - 1, 10, 8, 3, Color{0, 0, 0, 0}, 1.5F, col);
    for (int k = 0; k < 3; ++k) c.segment(cx - 3.5F + k * 3.5F, cy - 1, cx - 3.5F + k * 3.5F, cy - 7 + (k == 1 ? -1.0F : 0.0F), 1.5F, col);
    c.segment(cx - 5, cy + 2, cx - 8, cy - 1, 1.5F, col);
  } else if (name == "smooth") {
    c.circle(cx, cy, 7, withAlphaC(col, 0.25F));
    c.circle(cx, cy, 4.5F, withAlphaC(col, 0.5F));
    c.circle(cx, cy, 2.2F, col);
  } else if (name == "minus") {
    c.segment(cx - 6, cy, cx + 6, cy, 2.2F, col);
  } else if (name == "front" || name == "back") {
    // two stacked cards, the highlighted one in front or behind
    const bool front = name == "front";
    c.roundRect(cx - 7, cy - 6, 9, 9, 2, front ? Color{0, 0, 0, 0} : col, 1.4F, col);
    c.roundRect(cx - 2, cy - 2, 9, 9, 2, front ? col : Color{0.075F, 0.075F, 0.085F, 1}, 1.4F, col);
  } else if (name == "match") {
    c.circle(cx - 3.5F, cy, 3.6F, Color{0, 0, 0, 0}, 1.5F, col);
    c.circle(cx + 3.5F, cy, 3.6F, col);
    c.segment(cx - 7.5F, cy + 6.5F, cx + 7.5F, cy + 6.5F, 1.4F, col);
  } else if (name == "erase") {
    c.roundRect(cx - 6, cy - 3, 12, 7, 2, Color{0, 0, 0, 0}, 1.5F, col);
    c.segment(cx - 1, cy - 3, cx - 1, cy + 4, 1.4F, col);
    c.segment(cx - 7, cy + 6.5F, cx + 7, cy + 6.5F, 1.4F, col);
  } else if (name == "pencil") {
    c.segment(cx - 4.5F, cy + 4.5F, cx + 4.2F, cy - 4.2F, 2.6F, col);
    c.triangle(cx - 6.5F, cy + 6.5F, cx - 6.1F, cy + 3.0F, cx - 3.0F, cy + 6.1F, col);
  } else if (name == "trash") {
    c.segment(cx - 6, cy - 4.5F, cx + 6, cy - 4.5F, 1.5F, col);
    c.segment(cx - 2, cy - 6.5F, cx + 2, cy - 6.5F, 1.5F, col);
    c.roundRect(cx - 4.5F, cy - 3.5F, 9, 10, 2, Color{0, 0, 0, 0}, 1.4F, col);
  }
}
}  // namespace

void App::drawUi(EditSurface& e) {
  layoutUi(e);
  const float W = static_cast<float>(e.w), H = static_cast<float>(e.h);
  const bool es = spanishUi();
  const Color plate{0.075F, 0.075F, 0.085F, 0.94F}, raised{1, 1, 1, 0.06F}, line{1, 1, 1, 0.09F};
  const Color ink{0.96F, 0.95F, 0.93F, 1}, dim{0.96F, 0.95F, 0.93F, 0.55F};
  const Color accent = m_selected && m_selected->impl ? m_selected->impl->accent() : m_noctalia.state().color("primary");
  const Color onAccent{0.06F, 0.06F, 0.07F, 1};
  Canvas& cv = m_editCanvas;
  cv.begin(W, H, e.scale, &m_text);
  cv.setTransform(1, 0, 0);
  const TextStyle label{.family = "Space Grotesk", .size = 13, .weight = 500};
  const TextStyle strong{.family = "Space Grotesk", .size = 13, .weight = 600};
  const TextStyle mono{.family = "JetBrains Mono", .size = 12, .weight = 600};
  const TextStyle head{.family = "Space Grotesk", .size = 15, .weight = 700};
  const TextStyle keyS{.family = "JetBrains Mono", .size = 11, .weight = 700};
  auto measure = [&](const std::string& t, const TextStyle& st) {
    float w = 0, h = 0, b = 0;
    TextRenderer::measure(t, st, w, h, b);
    return std::pair<float, float>{w, h};
  };
  auto hovered = [&](const UiControl& c) {
    return m_uiHover >= 0 && m_uiHover < static_cast<int>(m_ui.size()) && &m_ui[static_cast<size_t>(m_uiHover)] == &c;
  };
  auto plateAt = [&](const Rect& r, float radius) {
    cv.roundRect(r.x, r.y + 3, r.w, r.h, radius, Color{0, 0, 0, 0.28F});  // soft drop shadow
    cv.roundRect(r.x, r.y, r.w, r.h, radius, plate, 1, line);
  };

  // ── toolbar ──
  for (const UiControl& c : m_ui)
    if (c.type == UiControl::Panel && c.value == "toolbar") {
      plateAt(c.r, c.r.h / 2);
      // separators between groups
      float prevRight = -1;
      for (const UiControl& d : m_ui) {
        if (d.r.y < c.r.y || d.r.y > c.r.y + c.r.h || d.type == UiControl::Panel) continue;
        if (prevRight > 0 && d.r.x - prevRight > kGap + 4)
          cv.segment((prevRight + d.r.x) / 2, c.r.y + 12, (prevRight + d.r.x) / 2, c.r.y + c.r.h - 12, 1, line, false);
        prevRight = d.r.x + d.r.w;
      }
    }
  const UiControl* tipCtl = nullptr;
  for (const UiControl& c : m_ui) {
    const bool hot = hovered(c);
    const float cx = c.r.x + c.r.w / 2, cy = c.r.y + c.r.h / 2;
    switch (c.type) {
      case UiControl::Plus:
        cv.circle(cx, cy, c.r.w / 2, hot || m_galleryOpen ? accent : withAlphaC(accent, 0.85F));
        icon(cv, "plus", cx, cy, onAccent);
        break;
      case UiControl::Undo:
      case UiControl::Redo: {
        const bool can = c.type == UiControl::Undo ? !m_undo.empty() : !m_redo.empty();
        if (hot && can) cv.circle(cx, cy, c.r.w / 2, raised);
        icon(cv, c.type == UiControl::Undo ? "undo" : "redo", cx, cy, can ? ink : withAlphaC(ink, 0.3F));
        break;
      }
      case UiControl::Magnet:
      case UiControl::Grid:
      case UiControl::Help:
      case UiControl::Saves:
      case UiControl::Paint: {
        const bool on = c.type == UiControl::Magnet ? m_snapOn
                        : c.type == UiControl::Grid ? m_gridOn
                        : c.type == UiControl::Help ? m_helpOpen
                        : c.type == UiControl::Saves ? m_savesOpen
                                                     : m_paintMode;
        if (on) cv.circle(cx, cy, c.r.w / 2, withAlphaC(accent, 0.22F));
        else if (hot) cv.circle(cx, cy, c.r.w / 2, raised);
        icon(cv,
             c.type == UiControl::Magnet ? "magnet"
             : c.type == UiControl::Grid ? "grid"
             : c.type == UiControl::Help ? "help"
             : c.type == UiControl::Saves ? "save"
                                          : "brush",
             cx, cy, on ? accent : ink);
        break;
      }
      case UiControl::Done: {
        cv.roundRect(c.r.x, c.r.y, c.r.w, c.r.h, c.r.h / 2, hot ? withAlphaC(ink, 0.16F) : raised);
        icon(cv, "check", c.r.x + 17, cy, ink);
        cv.text(es ? "Listo" : "Done", strong, c.r.x + 30, cy - 9, ink);
        break;
      }
      case UiControl::Chip: {
        Widget* cw = nullptr;
        for (auto& ww : m_widgets)
          if (ww->cfg.id == c.value) cw = ww.get();
        const bool sel = cw && cw == m_selected;
        const Color ca = cw && cw->impl ? cw->impl->accent() : accent;
        cv.roundRect(c.r.x, c.r.y, c.r.w, c.r.h, c.r.h / 2, sel ? withAlphaC(ca, 0.25F) : (hot ? raised : Color{0, 0, 0, 0}),
                     1, sel ? withAlphaC(ca, 0.7F) : line);
        icon(cv, cw ? chipIcon(cw->cfg.type) : "bars", c.r.x + 17, cy, sel ? ca : dim);
        cv.text(c.value, strong, c.r.x + 30, cy - 9, sel ? ink : dim);
        break;
      }
      default: break;
    }
    if (hot && !tipFor(c.type, es).empty()) tipCtl = &c;
  }

  // ── inspector ──
  Widget* w = m_selected;
  const UiControl* panel = nullptr;
  for (const UiControl& c : m_ui)
    if (c.type == UiControl::Panel && c.value == "inspector") panel = &c;
  if (w && w->impl && panel) {
    const Rect P = panel->r;
    plateAt(P, 16);
    cv.roundRect(P.x + 14, P.y + 16, 22, 22, 7, withAlphaC(w->impl->accent(), 0.22F));
    icon(cv, chipIcon(w->cfg.type), P.x + 25, P.y + 27, w->impl->accent());
    cv.text(typeName(w->cfg.type, es), head, P.x + 44, P.y + 13, ink);
    cv.text(w->cfg.id, mono, P.x + 44, P.y + 32, dim);
    cv.segment(P.x + 14, P.y + kHeadH + 4, P.x + P.w - 14, P.y + kHeadH + 4, 1, line, false);

    const auto& schema = schemaFor(w->cfg.type);
    float y = P.y + kHeadH - m_inspScroll + 6;
    const float cx = P.x + kLabelW + 14, cw = kPanelW - kLabelW - 28;
    const float top = P.y + kHeadH, bottom = P.y + P.h - kFootH + 2;
    cv.clip(P.x, top + 2, P.w, bottom - top - 4);
    for (const auto& p : schema) {
      const float rowH = p.kind == PropSpec::Color ? kColorRowH : kRowH;
      if (y + rowH >= top && y <= bottom) {
        cv.text(es ? p.labelEs : p.labelEn, label, P.x + 16, y + 6, dim);
        const std::string v = valueText(w->cfg.options, p);
        if (p.kind == PropSpec::Enum) {
          cv.roundRect(cx, y + 3, cw, 24, 12, raised, 1, line);
          cv.triangle(cx + 12, y + 15, cx + 17, y + 11, cx + 17, y + 19, dim);
          cv.triangle(cx + cw - 12, y + 15, cx + cw - 17, y + 11, cx + cw - 17, y + 19, dim);
          auto [tw, th] = measure(v, mono);
          cv.text(v, mono, cx + (cw - tw) / 2, y + 15 - th / 2, accent);
        } else if (p.kind == PropSpec::Bool) {
          const bool on = v == "true";
          const float tx = cx + cw - 38;
          cv.roundRect(tx, y + 5, 38, 20, 10, on ? accent : withAlphaC(ink, 0.14F));
          cv.circle(on ? tx + 28 : tx + 10, y + 15, 7.5F, on ? onAccent : ink);
        } else if (p.kind == PropSpec::Number) {
          const float sw = cw - 46;
          const double t = std::clamp((numberOf(w->cfg.options, p) - p.min) / (p.max - p.min), 0.0, 1.0);
          cv.roundRect(cx, y + 13, sw, 4, 2, withAlphaC(ink, 0.14F));
          cv.roundRect(cx, y + 13, static_cast<float>(sw * t), 4, 2, accent);
          cv.circle(cx + static_cast<float>(sw * t), y + 15, 7, ink, 2, accent);
          cv.text(v, mono, cx + sw + 10, y + 7, ink);
        } else {
          const auto& sws = colorSwatches();
          const float size = 20, gap = (kPanelW - 28 - sws.size() * size) / (sws.size() - 1);
          bool listed = false;
          for (size_t k = 0; k < sws.size(); ++k) {
            const float sx = P.x + 14 + k * (size + gap), sy = y + 26;
            listed = listed || sws[k] == v;
            if (sws[k] == v) cv.circle(sx + size / 2, sy + size / 2, size / 2 + 3, Color{0, 0, 0, 0}, 1.8F, ink);
            cv.circle(sx + size / 2, sy + size / 2, size / 2, m_noctalia.state().color(sws[k]), 1, line);
          }
          if (!listed) cv.text(v, mono, cx + cw - 70, y + 6, ink);
        }
      }
      y += rowH;
    }
    cv.clip();
    // scroll hint when the list continues
    if (m_inspScroll > 1) cv.roundRect(P.x + P.w / 2 - 16, top + 3, 32, 3, 1.5F, withAlphaC(ink, 0.25F));
    cv.segment(P.x + 14, P.y + P.h - kFootH + 2, P.x + P.w - 14, P.y + P.h - kFootH + 2, 1, line, false);
    for (const UiControl& c : m_ui) {
      if (c.type != UiControl::Duplicate && c.type != UiControl::Delete) continue;
      const bool del = c.type == UiControl::Delete, hot = hovered(c);
      const Color bg = del ? Color{0.9F, 0.25F, 0.2F, hot ? 0.32F : 0.18F} : (hot ? withAlphaC(ink, 0.14F) : raised);
      cv.roundRect(c.r.x, c.r.y, c.r.w, c.r.h, 10, bg);
      const Color fg = del ? Color{1, 0.62F, 0.57F, 1} : ink;
      const std::string s = del ? (es ? "Eliminar" : "Delete") : (es ? "Duplicar" : "Duplicate");
      auto [tw, th] = measure(s, strong);
      const float gx = c.r.x + (c.r.w - tw - 22) / 2;
      icon(cv, del ? "trash" : "copy", gx + 7, c.r.y + c.r.h / 2, fg);
      cv.text(s, strong, gx + 22, c.r.y + (c.r.h - th) / 2, fg);
    }
  }

  // ── gallery ──
  for (const UiControl& c : m_ui)
    if (c.type == UiControl::Panel && c.value == "gallery") plateAt(c.r, 16);
  for (const UiControl& c : m_ui) {
    if (c.type != UiControl::GalleryItem) continue;
    const bool hot = hovered(c);
    cv.roundRect(c.r.x, c.r.y, c.r.w, c.r.h, 12, hot ? withAlphaC(accent, 0.16F) : raised);
    cv.roundRect(c.r.x + 12, c.r.y + 15, 32, 32, 10, withAlphaC(accent, 0.22F));
    icon(cv, chipIcon(c.value), c.r.x + 28, c.r.y + 31, accent);
    const char* desc = c.value == "visualizer" ? (es ? "Espectro de audio · 12 estilos" : "Audio spectrum · 12 looks")
                       : c.value == "clock"   ? (es ? "12 caras · fecha · clima" : "12 faces · date · weather")
                                              : (es ? "Portada · letra · controles" : "Cover · lyrics · controls");
    cv.text(typeName(c.value, es), head, c.r.x + 56, c.r.y + 11, ink);
    cv.text(desc, label, c.r.x + 56, c.r.y + 34, dim);
  }

  // ── depth brush ──
  for (const UiControl& c : m_ui) {
    if (c.type != UiControl::Panel || c.value != "paint") continue;
    const Rect P = c.r;
    plateAt(P, 16);
    cv.roundRect(P.x + 16, P.y + 16, 24, 24, 8, withAlphaC(accent, 0.22F));
    icon(cv, "brush", P.x + 28, P.y + 28, accent);
    cv.text(es ? "Pincel de profundidad" : "Depth brush", head, P.x + 50, P.y + 12, ink);
    cv.text(es ? "Corrige qué queda delante" : "Fix what stands in front", label, P.x + 50, P.y + 32, dim);
    cv.text(es ? "Herramienta" : "Tool", label, P.x + 14, P.y + 62, dim);
    cv.text(es ? "Acción" : "Action", label, P.x + 14, P.y + 142, dim);
    if (m_selectTool <= 1)
      cv.text(m_selectTool == 1 ? (es ? "Tolerancia" : "Tolerance") : (es ? "Tamaño" : "Size"), label, P.x + 14, P.y + 250, dim);
    if (m_selectTool == 0 || m_selectTool == 2)
      cv.text(es ? "Ajustar a bordes" : "Keep to edges", label, P.x + 14, P.y + 306, dim);
    const std::string zoomText = std::format("{:.0f} %", m_zoom * 100);
    auto [zw, zh] = measure(zoomText, mono);
    cv.text(zoomText, mono, P.x + 14 + 34 + 35 - zw / 2, P.y + 348, ink);
    const DepthMask* dm = m_uiOutput ? m_depth.paintable(m_uiOutput->name) : nullptr;
    const std::string who = m_selected ? m_selected->cfg.id : (es ? "los widgets" : "the widgets");
    const int plane = static_cast<int>(std::lround(previewPlane() * 100));
    static const char* hintsEs[4] = {"Arrastra para pintar · Shift+rueda: tamaño", "Clic en un objeto: lo toma entero",
                                     "Clics alrededor · 1.er punto o Enter: cerrar", "Arrastra para mover la vista"};
    static const char* hintsEn[4] = {"Drag to paint · Shift+wheel: size", "Click an object: takes all of it",
                                     "Click around · first point or Enter: close", "Drag to move the view"};
    if (dm) {
      cv.text(es ? hintsEs[m_selectTool] : hintsEn[m_selectTool], label, P.x + 14, P.y + 426, ink);
      cv.text(std::format("{} {} {} · {} {} · {}", es ? "Teñido: tapa a" : "Tint: covers", who, "", es ? "plano" : "plane", plane,
                          es ? "rueda: zoom" : "wheel: zoom"),
              label, P.x + 14, P.y + 446, dim);
    } else {
      cv.text(es ? "Este fondo aún no tiene mapa de profundidad." : "No depth map for this wallpaper yet.", label, P.x + 14,
              P.y + 426, Color{1, 0.62F, 0.57F, 1});
      cv.text(es ? "Genéralo en Wallpaper Depth." : "Generate it in Wallpaper Depth.", label, P.x + 14, P.y + 446, dim);
    }
  }
  for (const UiControl& c : m_ui) {
    const bool hot = hovered(c);
    if (c.type == UiControl::PaintSelect || c.type == UiControl::PaintTool) {
      const bool sel = c.type == UiControl::PaintSelect;
      const int t = std::clamp(std::atoi(c.value.c_str()), 0, sel ? 3 : 4);
      const bool on = t == (sel ? m_selectTool : m_paintTool);
      cv.roundRect(c.r.x, c.r.y, c.r.w, c.r.h, 10, on ? withAlphaC(accent, 0.28F) : (hot ? withAlphaC(ink, 0.12F) : raised), 1,
                   on ? withAlphaC(accent, 0.75F) : line);
      const char* const* k = sel ? kSelectKeys[t] : kToolKeys[t];
      const std::string name = es ? k[1] : k[2];
      auto [tw, th] = measure(name, label);
      if (sel) {  // icon above the name
        icon(cv, k[0], c.r.x + c.r.w / 2, c.r.y + 16, on ? accent : ink);
        cv.text(name, label, c.r.x + (c.r.w - tw) / 2, c.r.y + 28, on ? ink : dim);
      } else {
        icon(cv, k[0], c.r.x + 15, c.r.y + c.r.h / 2, on ? accent : ink);
        cv.text(name, label, c.r.x + 28, c.r.y + (c.r.h - th) / 2, on ? ink : dim);
      }
    } else if (c.type == UiControl::PaintSize) {
      const bool tol = m_selectTool == 1;
      const double t = tol ? m_wandTolerance : std::sqrt(std::clamp((m_brush - 6) / (320 - 6), 0.0, 1.0));
      cv.roundRect(c.r.x, c.r.y + 8, c.r.w, 4, 2, withAlphaC(ink, 0.14F));
      cv.roundRect(c.r.x, c.r.y + 8, static_cast<float>(c.r.w * t), 4, 2, accent);
      cv.circle(c.r.x + static_cast<float>(c.r.w * t), c.r.y + 10, 7, ink, 2, accent);
      cv.text(tol ? std::format("{:.0f} %", m_wandTolerance * 100) : std::format("{:.0f} px", m_brush), mono, c.r.x + c.r.w + 10,
              c.r.y + 2, ink);
    } else if (c.type == UiControl::PaintSmart) {
      cv.roundRect(c.r.x, c.r.y, 38, 20, 10, m_smartBrush ? accent : withAlphaC(ink, 0.14F));
      cv.circle(m_smartBrush ? c.r.x + 28 : c.r.x + 10, c.r.y + 10, 7.5F, m_smartBrush ? onAccent : ink);
    } else if (c.type == UiControl::PaintZoomIn || c.type == UiControl::PaintZoomOut || c.type == UiControl::PaintZoomReset) {
      cv.roundRect(c.r.x, c.r.y, c.r.w, c.r.h, 9, hot ? withAlphaC(ink, 0.14F) : raised, 1, line);
      if (c.type == UiControl::PaintZoomReset) {
        auto [tw, th] = measure("1:1", mono);
        cv.text("1:1", mono, c.r.x + (c.r.w - tw) / 2, c.r.y + (c.r.h - th) / 2, ink);
      } else {
        icon(cv, c.type == UiControl::PaintZoomIn ? "plus" : "minus", c.r.x + c.r.w / 2, c.r.y + c.r.h / 2, ink);
      }
      if (hot) tipCtl = &c;
    } else if (c.type == UiControl::PaintUndo || c.type == UiControl::PaintClear) {
      const bool clr = c.type == UiControl::PaintClear;
      const bool armed = clr && m_confirmClear;
      const bool can = clr ? true : !m_editsUndo.empty();
      const Color red{0.9F, 0.25F, 0.2F, 1};
      cv.roundRect(c.r.x, c.r.y, c.r.w, c.r.h, 10, armed ? red : clr ? withAlphaC(red, hot ? 0.32F : 0.18F) : (hot && can ? withAlphaC(ink, 0.14F) : raised));
      const std::string t = clr ? (armed ? (es ? "¿Seguro?" : "Sure?") : (es ? "Borrar todo" : "Clear all")) : (es ? "Deshacer" : "Undo");
      auto [tw, th] = measure(t, strong);
      cv.text(t, strong, c.r.x + (c.r.w - tw) / 2, c.r.y + (c.r.h - th) / 2,
              armed ? ink : clr ? Color{1, 0.62F, 0.57F, 1} : (can ? ink : withAlphaC(ink, 0.35F)));
    }
  }
  if (m_paintMode && m_pointerEdit && m_pointerEdit->output == e.output) {
    const float px = static_cast<float>(m_px), py = static_cast<float>(m_py);
    // the lasso so far, and a band to the pointer
    if (!m_lasso.empty()) {
      std::vector<std::pair<float, float>> pts;
      for (const auto& [wx, wy] : m_lasso) {
        double sx = 0, sy = 0;
        paintScreen(e.output, wx, wy, sx, sy);
        pts.emplace_back(static_cast<float>(sx), static_cast<float>(sy));
      }
      for (size_t k = 0; k + 1 < pts.size(); ++k) {
        cv.segment(pts[k].first, pts[k].second, pts[k + 1].first, pts[k + 1].second, 4, Color{0, 0, 0, 0.5F});
        cv.segment(pts[k].first, pts[k].second, pts[k + 1].first, pts[k + 1].second, 2, accent);
      }
      if (m_uiHover < 0) cv.segment(pts.back().first, pts.back().second, px, py, 1.5F, withAlphaC(ink, 0.7F));
      const bool closing = pts.size() >= 3 && std::hypot(px - pts.front().first, py - pts.front().second) < 14;
      for (size_t k = 0; k < pts.size(); ++k)
        cv.circle(pts[k].first, pts[k].second, k == 0 ? (closing ? 7.0F : 5.0F) : 3.5F, k == 0 && closing ? accent : ink, 1.5F,
                  Color{0, 0, 0, 0.6F});
    }
    if (m_uiHover < 0) {
      if (m_selectTool == 0) {  // the brush outline under the pointer
        cv.circle(px, py, static_cast<float>(m_brush), Color{0, 0, 0, 0}, 2.5F, Color{0, 0, 0, 0.45F});
        cv.circle(px, py, static_cast<float>(m_brush), Color{0, 0, 0, 0}, 1.3F, ink);
        cv.circle(px, py, 1.6F, ink);
      } else if (m_selectTool == 1) {
        icon(cv, "wand", px + 12, py - 12, ink);
      }
    }
  }

  // ── saved profiles ──
  for (const UiControl& c : m_ui) {
    if (c.type != UiControl::Panel || c.value != "saves") continue;
    const Rect P = c.r;
    plateAt(P, 16);
    cv.roundRect(P.x + 16, P.y + 16, 24, 24, 8, withAlphaC(accent, 0.22F));
    icon(cv, "save", P.x + 28, P.y + 28, accent);
    cv.text(es ? "Perfiles guardados" : "Saved profiles", head, P.x + 50, P.y + 18, ink);
    const std::string wallPath = profileWallpaper();
    const std::string wall = wallPath.empty() ? "-" : std::filesystem::path(wallPath).filename().string();
    cv.text(es ? "Cada fondo ya recuerda su disposición por sí solo (este: " + wall + ")."
               : "Each wallpaper already remembers its own layout (this one: " + wall + ").",
            label, P.x + 18, P.y + 50, dim);
    cv.text(es ? "Aquí guardas copias con nombre, para volver a ellas cuando quieras."
               : "Here you keep named copies to come back to whenever you like.",
            label, P.x + 18, P.y + 68, dim);
    const int n = static_cast<int>(m_saves.size());
    if (n == 0)
      cv.text(es ? "Aún no hay perfiles. Guarda el primero con el botón de arriba."
                 : "No profiles yet. Save the first one with the button above.",
              label, P.x + 18, P.y + kSavesHead + 64, dim);
    if (n > kSaveRows) {
      const std::string more = std::format("{}–{} / {}  ·  {}", m_saveScroll + 1, m_saveScroll + std::min(kSaveRows, n - m_saveScroll), n,
                                           es ? "rueda para ver más" : "scroll for more");
      auto [mw, mh] = measure(more, label);
      cv.text(more, label, P.x + (P.w - mw) / 2, P.y + P.h - 24, dim);
    }
  }
  for (const UiControl& c : m_ui) {
    const bool hot = hovered(c);
    if (c.type == UiControl::SaveNew) {
      cv.roundRect(c.r.x, c.r.y, c.r.w, c.r.h, 12, hot ? withAlphaC(accent, 0.32F) : withAlphaC(accent, 0.2F), 1,
                   withAlphaC(accent, 0.5F));
      const std::string t = es ? "Guardar lo que hay en pantalla" : "Save what is on screen";
      auto [tw, th] = measure(t, strong);
      const float gx = c.r.x + (c.r.w - tw - 24) / 2;
      icon(cv, "plus", gx + 7, c.r.y + c.r.h / 2, accent);
      cv.text(t, strong, gx + 24, c.r.y + (c.r.h - th) / 2, ink);
    } else if (c.type == UiControl::SaveRow) {
      const SavedLayout* sv = nullptr;
      for (const auto& s2 : m_saves)
        if (s2.id == c.value) sv = &s2;
      if (!sv) continue;
      const bool cur = sv->id == m_saveCurrent;
      cv.roundRect(c.r.x, c.r.y, c.r.w, c.r.h, 12, hot ? withAlphaC(ink, 0.09F) : raised, 1,
                   cur ? withAlphaC(accent, 0.6F) : line);
      // a thumbnail of the layout on a little screen
      const float tw = 96, th = 54, tx = c.r.x + 8, ty = c.r.y + (c.r.h - th) / 2;
      cv.roundRect(tx, ty, tw, th, 6, Color{0, 0, 0, 0.45F}, 1, line);
      const float sc = std::min(tw / W, th / H);
      cv.clip(tx, ty, tw, th);
      for (const auto& wc : sv->widgets) {
        const Box b = visualBox(wc);
        const Color col = wc.type == "clock" ? withAlphaC(ink, 0.8F) : wc.type == "now_playing" ? withAlphaC(accent, 0.55F) : accent;
        cv.roundRect(tx + b.x * sc, ty + b.y * sc, std::max(2.0F, b.w * sc), std::max(2.0F, b.h * sc), 1.5F, withAlphaC(col, 0.85F));
      }
      cv.clip();
      const float textX = tx + tw + 12, textW = c.r.x + c.r.w - 10 - 28 - 32 - 32 - 74 - 8 - textX;
      if (sv->id == m_renaming) {
        cv.roundRect(textX - 4, c.r.y + 9, textW + 4, 26, 7, Color{0, 0, 0, 0.35F}, 1.4F, accent);
        cv.clip(textX, c.r.y + 9, textW - 4, 26);
        const bool empty = m_renameText.empty();
        const std::string shown = empty ? sv->name : m_renameText;
        auto [nw, nh] = measure(shown, strong);
        const float off = std::max(0.0F, nw - (textW - 14));
        cv.text(shown, strong, textX + 4 - off, c.r.y + 22 - nh / 2, empty ? dim : ink);
        cv.segment(textX + 5 + (empty ? 0 : nw - off), c.r.y + 14, textX + 5 + (empty ? 0 : nw - off), c.r.y + 30, 1.6F, accent, false);
        cv.clip();
      } else {
        cv.clip(textX, c.r.y, textW, c.r.h);
        cv.text(sv->name, strong, textX, c.r.y + 12, ink);
        cv.clip();
      }
      std::string meta = std::format("{}  ·  {}  ·  {} widget{}", sv->created.size() > 5 ? sv->created.substr(5) : sv->created,
                                     sv->wallpaper.empty() ? "-" : sv->wallpaper, sv->widgets.size(), sv->widgets.size() == 1 ? "" : "s");
      if (cur) meta = (es ? "en pantalla  ·  " : "on screen  ·  ") + meta;
      cv.clip(textX, c.r.y, textW, c.r.h);
      cv.text(meta, label, textX, c.r.y + 40, cur ? withAlphaC(accent, 0.9F) : dim);
      cv.clip();
    } else if (c.type == UiControl::SaveLoad) {
      cv.roundRect(c.r.x, c.r.y, c.r.w, c.r.h, 9, hot ? accent : withAlphaC(accent, 0.85F));
      const std::string t = es ? "Cargar" : "Load";
      auto [tw, th] = measure(t, strong);
      cv.text(t, strong, c.r.x + (c.r.w - tw) / 2, c.r.y + (c.r.h - th) / 2, onAccent);
    } else if (c.type == UiControl::SaveOverwrite || c.type == UiControl::SaveRename || c.type == UiControl::SaveDelete) {
      const bool armed = c.type == UiControl::SaveDelete && c.value == m_confirmDelete;
      const Color red{0.9F, 0.25F, 0.2F, 1};
      if (armed) cv.roundRect(c.r.x, c.r.y, c.r.w, c.r.h, 9, red);
      else if (hot) cv.roundRect(c.r.x, c.r.y, c.r.w, c.r.h, 9, c.type == UiControl::SaveDelete ? withAlphaC(red, 0.3F) : withAlphaC(ink, 0.14F));
      const Color fg = armed ? ink : c.type == UiControl::SaveDelete ? Color{1, 0.62F, 0.57F, 1} : ink;
      icon(cv, c.type == UiControl::SaveOverwrite ? "save" : c.type == UiControl::SaveRename ? "pencil" : "trash",
           c.r.x + c.r.w / 2, c.r.y + c.r.h / 2, fg);
    }
    if (hot && !tipFor(c.type, es).empty() && isSaveControl(c.type)) tipCtl = &c;
  }

  // ── shortcuts card ──
  for (const UiControl& c : m_ui) {
    if (c.type != UiControl::Panel || c.value != "help") continue;
    plateAt(c.r, 16);
    cv.text(es ? "Atajos del editor" : "Editor shortcuts", head, c.r.x + 20, c.r.y + 16, ink);
    struct K {
      const char *keys, *en, *es;
    };
    static const K rows[] = {
        {"Arrastrar", "move", "mover"},
        {"Esquina", "resize", "cambiar tamaño"},
        {"Asa superior", "tilt (Shift: fine)", "inclinar (Shift: fino)"},
        {"Rueda", "next look / value", "estilo o valor siguiente"},
        {"Shift", "hold: no magnet", "mantener: sin imán"},
        {"← → ↑ ↓", "nudge 1 px", "ajustar 1 px"},
        {"Shift+←", "nudge 16 px", "ajustar 16 px"},
        {"Alt+←", "resize", "tamaño"},
        {"Ctrl+← →", "tilt 0.1° (Shift 1°)", "inclinar 0.1° (Shift 1°)"},
        {"Tab", "next widget", "siguiente widget"},
        {"Ctrl+Z", "undo", "deshacer"},
        {"Ctrl+Shift+Z", "redo", "rehacer"},
        {"Ctrl+D", "duplicate", "duplicar"},
        {"Supr", "delete", "eliminar"},
        {"Esc", "done", "terminar"},
        {"Clic der.", "done", "terminar"},
    };
    const int n = static_cast<int>(std::size(rows)), half = (n + 1) / 2;
    for (int i = 0; i < n; ++i) {
      const float colX = c.r.x + 20 + (i >= half ? c.r.w / 2 : 0), rowY = c.r.y + 54 + (i % half) * 34;
      const std::string k = rows[i].keys;
      auto [kw, kh] = measure(k, keyS);
      cv.roundRect(colX, rowY, kw + 14, 22, 6, raised, 1, line);
      cv.text(k, keyS, colX + 7, rowY + 11 - kh / 2, ink);
      cv.text(es ? rows[i].es : rows[i].en, label, colX + kw + 24, rowY + 3, dim);
    }
  }

  // ── tooltip under the hovered toolbar button ──
  if (tipCtl) {
    const std::string t = tipFor(tipCtl->type, es);
    auto [tw, th] = measure(t, label);
    const float tx = std::clamp(tipCtl->r.x + tipCtl->r.w / 2 - tw / 2 - 10, 8.0F, W - tw - 28);
    const bool inPanel = isSaveControl(tipCtl->type) || tipCtl->type == UiControl::PaintZoomIn ||
                         tipCtl->type == UiControl::PaintZoomOut || tipCtl->type == UiControl::PaintZoomReset;
    const float ty = inPanel ? tipCtl->r.y + tipCtl->r.h + 6 : kBarY + kBarH + 8;
    if (inPanel || (!m_galleryOpen && !m_helpOpen && !m_savesOpen)) {
      cv.roundRect(tx, ty, tw + 20, th + 12, 8, Color{0.02F, 0.02F, 0.025F, 0.94F});
      cv.text(t, label, tx + 10, ty + 6, ink);
    }
  }
  (void)H;
}

}  // namespace undershell
