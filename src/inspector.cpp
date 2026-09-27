// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's inspector (the selected widget's options, edited live) and the
// gallery (add a widget). Everything writes through the config file, which
// stays the single source of truth; the in-memory options are updated at the
// same time so a change shows on the very next frame.
#include "app.hpp"
#include "schema.hpp"

#include <climits>
#include <cmath>
#include <regex>
#include <xkbcommon/xkbcommon.h>

namespace undershell {

namespace {
constexpr float kPanelW = 300, kRowH = 30, kColorRowH = 54, kHeadH = 46, kFootH = 46, kLabelW = 118;
constexpr double kCoalesceProp = 1.0;

Color withAlphaC(Color c, float a) {
  c.a *= a;
  return c;
}

bool spanishUi() {
  for (const char* v : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
    const char* s = std::getenv(v);
    if (s && *s) return std::string_view(s).starts_with("es");
  }
  return false;
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
                             {UiControl::Grid, kBtn, ""},   {UiControl::Panel, kSep, "sep"}};
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
  Rect addRect;
  for (auto& it : items) {
    const float h = it.type == UiControl::Chip ? kChipH : kBtn;
    Rect r{x, kBarY + (kBarH - h) / 2, it.w, h};
    if (it.value != "sep") m_ui.push_back({it.type, r, -1, it.value});
    if (it.type == UiControl::Plus) addRect = r;
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
  switch (c.type) {
    case UiControl::Plus:
      m_galleryOpen = !m_galleryOpen;
      m_helpOpen = false;
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
      m_galleryOpen = false;
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
  if (m_sliderControl < 0 || m_sliderControl >= static_cast<int>(m_ui.size()) || !m_selected) return;
  const UiControl& c = m_ui[static_cast<size_t>(m_sliderControl)];
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
      case UiControl::Help: {
        const bool on = c.type == UiControl::Magnet ? m_snapOn : (c.type == UiControl::Grid ? m_gridOn : m_helpOpen);
        if (on) cv.circle(cx, cy, c.r.w / 2, withAlphaC(accent, 0.22F));
        else if (hot) cv.circle(cx, cy, c.r.w / 2, raised);
        icon(cv, c.type == UiControl::Magnet ? "magnet" : (c.type == UiControl::Grid ? "grid" : "help"), cx, cy,
             on ? accent : ink);
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
    const float ty = kBarY + kBarH + 8;
    if (!m_galleryOpen && !m_helpOpen) {
      cv.roundRect(tx, ty, tw + 20, th + 12, 8, Color{0.02F, 0.02F, 0.025F, 0.94F});
      cv.text(t, label, tx + 10, ty + 6, ink);
    }
  }
  (void)H;
}

}  // namespace undershell
