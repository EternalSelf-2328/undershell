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
      return p.integer ? std::to_string(static_cast<long>(std::lround(v))) : std::format("{:.2f}", v);
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
  return p.integer ? std::to_string(static_cast<long>(std::lround(v))) : std::format("{:.2f}", v);
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
  std::string id = w.cfg.id;
  for (int n = 2;; ++n) {
    bool taken = false;
    for (auto& c : m_config.widgets) taken = taken || c.id == id;
    if (!taken) break;
    id = w.cfg.id + "-" + std::to_string(n);
  }
  // the copy: new id, offset 32 px so it is visibly separate
  auto replaceLine = [&](const char* key, const std::string& value) {
    const std::regex re(std::string("(^|\\n)(\\\\s*") + key + R"(\s*=\s*)[^\n#]*)");
    block = std::regex_replace(block, re, "$1$2" + value + " ", std::regex_constants::format_first_only);
  };
  replaceLine("id", "\"" + id + "\"");
  replaceLine("x", std::to_string(w.cfg.x + 32));
  replaceLine("y", std::to_string(w.cfg.y + 32));
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
  if (w.impl) w.impl->configure(w.cfg, m_noctalia.state());
  w.needsRender = true;
  w.drewEmpty = false;
  Config::setKey(m_configPath, w.cfg.id, key, v);
  markEditDirty();
}

// ── layout ──────────────────────────────────────────────────────────────────

void App::layoutUi(const EditSurface& e) {
  m_ui.clear();
  m_uiOutput = e.output;
  const float W = static_cast<float>(e.w), H = static_cast<float>(e.h);

  Widget* w = m_selected;
  if (!w || w->output != e.output || !w->impl || w->impl->fullscreen()) {
    layoutGallery(W, H);
    return;
  }
  const auto& schema = schemaFor(w->cfg.type);
  float contentH = kHeadH + kFootH;
  for (const auto& p : schema) contentH += p.kind == PropSpec::Color ? kColorRowH : kRowH;
  const float maxH = H - 106;
  const float panelH = std::min(contentH, maxH);
  m_inspScroll = std::clamp(m_inspScroll, 0.0F, std::max(0.0F, contentH - panelH));
  float px = static_cast<float>(w->cfg.x + w->cfg.width) + 16;
  if (px + kPanelW > W - 12) px = static_cast<float>(w->cfg.x) - 16 - kPanelW;
  if (px < 12) px = W - kPanelW - 16;
  const float py = std::clamp(static_cast<float>(w->cfg.y), 90.0F, std::max(90.0F, H - panelH - 16));
  const Rect panel{px, py, kPanelW, panelH};
  m_ui.push_back({UiControl::Panel, panel});

  // rows scroll inside the panel; controls outside it are not hit-testable
  float y = py + kHeadH - m_inspScroll;
  auto visible = [&](const Rect& r) { return r.y >= py + kHeadH - 2 && r.y + r.h <= py + panelH - kFootH + 2; };
  const float cx = px + kLabelW + 14, cw = kPanelW - kLabelW - 28;
  for (size_t i = 0; i < schema.size(); ++i) {
    const auto& p = schema[i];
    const int idx = static_cast<int>(i);
    if (p.kind == PropSpec::Enum) {
      Rect prev{cx, y + 4, 24, 22}, next{cx + cw - 24, y + 4, 24, 22};
      if (visible(prev)) {
        m_ui.push_back({UiControl::Prev, prev, idx});
        m_ui.push_back({UiControl::Next, next, idx});
        m_ui.push_back({UiControl::Next, {cx + 24, y + 4, cw - 48, 22}, idx});
      }
      y += kRowH;
    } else if (p.kind == PropSpec::Bool) {
      Rect t{cx + cw - 38, y + 5, 38, 20};
      if (visible(t)) m_ui.push_back({UiControl::Toggle, t, idx});
      y += kRowH;
    } else if (p.kind == PropSpec::Number) {
      Rect s{cx, y + 5, cw - 46, 20};
      if (visible(s)) m_ui.push_back({UiControl::Slider, s, idx});
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
  layoutGallery(W, H);  // last: drawn on top and hit first
}

void App::layoutGallery(float W, float H) {
  // the ＋ button and, when open, the gallery above it
  const float plusR = 26, plusX = W - 60, plusY = H - 110;
  m_ui.push_back({UiControl::Plus, {plusX - plusR, plusY - plusR, 2 * plusR, 2 * plusR}});
  if (m_galleryOpen) {
    const char* types[] = {"visualizer", "clock", "now_playing"};
    float y = plusY - plusR - 12 - 3 * 70;
    m_ui.push_back({UiControl::Panel, {W - 60 - plusR - 270 + 2 * plusR, y - 10, 270, 3 * 70 + 10}});
    for (const char* t : types) {
      m_ui.push_back({UiControl::GalleryItem, {W - 60 - plusR - 270 + 2 * plusR + 8, y, 254, 62}, -1, t});
      y += 70;
    }
  }

}

int App::uiHit(double x, double y) const {
  // last matching control wins (controls are listed after their panel)
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
      markEditDirty();
      return true;
    case UiControl::GalleryItem:
      addWidget(c.value, "", INT_MIN, INT_MIN, true);
      m_galleryOpen = false;
      markEditDirty();
      return true;
    case UiControl::Panel: return true;  // swallow clicks on the plate
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
    uiDrag(x);
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
  const std::string v = formatNumber(p, p.min + t * (p.max - p.min));
  if (v != valueText(m_selected->cfg.options, p)) setProp(*m_selected, p.key, v);
}

bool App::uiScroll(double x, double y, int step) {
  const int hit = uiHit(x, y);
  if (hit < 0) return false;
  const UiControl& c = m_ui[static_cast<size_t>(hit)];
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
  if (c.type == UiControl::Panel || c.prop >= 0) {
    m_inspScroll += step * 36.0F;  // scroll a tall inspector
    markEditDirty();
    return true;
  }
  return false;
}

// ── drawing ─────────────────────────────────────────────────────────────────

void App::drawUi(EditSurface& e) {
  layoutUi(e);
  const float W = static_cast<float>(e.w), H = static_cast<float>(e.h);
  const bool es = spanishUi();
  const Color plate{0.05F, 0.05F, 0.06F, 0.92F}, line{1, 1, 1, 0.10F}, ink{1, 1, 1, 0.92F}, dim{1, 1, 1, 0.55F};
  Color accent = m_selected && m_selected->impl ? m_selected->impl->accent() : m_noctalia.state().color("primary");
  auto text = [&](const std::string& s, const TextStyle& st, float x, float y, Color c) {
    const TextImage& img = m_text.get(s, st, e.scale);
    m_text.draw(img, x, y, c, W, H);
    return img;
  };
  const TextStyle label{.family = "Space Grotesk", .size = 13, .weight = 500};
  const TextStyle mono{.family = "JetBrains Mono", .size = 12, .weight = 600};
  const TextStyle head{.family = "Space Grotesk", .size = 15, .weight = 700};

  for (const UiControl& c : m_ui) {
    if (c.type == UiControl::Plus) {
      m_overlay.drawPill(c.r.x - 3, c.r.y - 3, c.r.w + 6, c.r.h + 6, c.r.w, Color{0, 0, 0, 0.35F}, W, H);
      m_overlay.drawPill(c.r.x, c.r.y, c.r.w, c.r.h, c.r.w / 2, accent, W, H);
      const float mx = c.r.x + c.r.w / 2, my = c.r.y + c.r.h / 2;
      const Color on{0.05F, 0.05F, 0.06F, 1};
      m_overlay.drawPill(mx - 10, my - 1.5F, 20, 3, 1.5F, on, W, H);
      m_overlay.drawPill(mx - 1.5F, my - 10, 3, 20, 1.5F, on, W, H);
    }
  }
  auto drawGallery = [&] {
  // gallery
    if (m_galleryOpen) {
      for (const UiControl& c : m_ui) {
        if (c.type == UiControl::Panel && c.r.w == 270) {
        m_overlay.drawPill(c.r.x - 4, c.r.y - 2, c.r.w + 8, c.r.h + 8, 18, Color{0, 0, 0, 0.35F}, W, H);  // shadow
        m_overlay.drawPill(c.r.x, c.r.y, c.r.w, c.r.h, 14, Color{0.07F, 0.07F, 0.08F, 1.0F}, W, H);
      }
        if (c.type != UiControl::GalleryItem) continue;
        m_overlay.drawPill(c.r.x, c.r.y, c.r.w, c.r.h, 10, Color{1, 1, 1, 0.05F}, W, H);
        const char* desc = c.value == "visualizer" ? (es ? "Espectro de audio · 12 estilos" : "Audio spectrum · 12 looks")
                           : c.value == "clock"   ? (es ? "12 caras · fecha · clima" : "12 faces · date · weather")
                                                  : (es ? "Portada · letra · controles" : "Cover · lyrics · controls");
        text(typeName(c.value, es), head, c.r.x + 14, c.r.y + 10, ink);
        text(desc, label, c.r.x + 14, c.r.y + 34, dim);
      }
    }
  
  };
  // inspector
  Widget* w = m_selected;
  const UiControl* panel = nullptr;
  for (const UiControl& c : m_ui)
    if (c.type == UiControl::Panel && c.r.w == kPanelW) panel = &c;
  if (!w || w->output != e.output || !w->impl || w->impl->fullscreen() || !panel) {
    drawGallery();
    return;
  }
  const Rect P = panel->r;
  m_overlay.drawPill(P.x - 1, P.y - 1, P.w + 2, P.h + 2, 15, withAlphaC(accent, 0.35F), W, H);
  m_overlay.drawPill(P.x, P.y, P.w, P.h, 14, plate, W, H);
  text(std::string(typeName(w->cfg.type, es)) + "  ·  " + w->cfg.id, head, P.x + 14, P.y + 14, ink);

  const auto& schema = schemaFor(w->cfg.type);
  float y = P.y + kHeadH - m_inspScroll;
  const float cx = P.x + kLabelW + 14, cw = kPanelW - kLabelW - 28;
  const float top = P.y + kHeadH - 2, bottom = P.y + P.h - kFootH + 2;
  for (const auto& p : schema) {
    const float rowH = p.kind == PropSpec::Color ? kColorRowH : kRowH;
    if (y >= top && y + rowH <= bottom) {
      text(es ? p.labelEs : p.labelEn, label, P.x + 14, y + 7, dim);
      const std::string v = valueText(w->cfg.options, p);
      if (p.kind == PropSpec::Enum) {
        m_overlay.drawPill(cx, y + 4, cw, 22, 11, Color{1, 1, 1, 0.06F}, W, H);
        text("‹", head, cx + 8, y + 3, ink);
        text("›", head, cx + cw - 16, y + 3, ink);
        float tw = 0, th = 0, b = 0;
        TextRenderer::measure(v, mono, tw, th, b);
        text(v, mono, cx + (cw - tw) / 2, y + 7, accent);
      } else if (p.kind == PropSpec::Bool) {
        const bool on = v == "true";
        const float tx = cx + cw - 38;
        m_overlay.drawPill(tx, y + 5, 38, 20, 10, on ? accent : Color{1, 1, 1, 0.14F}, W, H);
        m_overlay.drawPill(on ? tx + 20 : tx + 2, y + 7, 16, 16, 8, Color{1, 1, 1, 0.95F}, W, H);
      } else if (p.kind == PropSpec::Number) {
        const float sw = cw - 46;
        const double t = std::clamp((numberOf(w->cfg.options, p) - p.min) / (p.max - p.min), 0.0, 1.0);
        m_overlay.drawPill(cx, y + 13, sw, 4, 2, Color{1, 1, 1, 0.14F}, W, H);
        m_overlay.drawPill(cx, y + 13, static_cast<float>(sw * t), 4, 2, accent, W, H);
        m_overlay.drawPill(cx + static_cast<float>(sw * t) - 7, y + 8, 14, 14, 7, Color{1, 1, 1, 0.95F}, W, H);
        text(v, mono, cx + sw + 8, y + 7, ink);
      } else {
        const auto& sws = colorSwatches();
        const float size = 20, gap = (kPanelW - 28 - sws.size() * size) / (sws.size() - 1);
        for (size_t k = 0; k < sws.size(); ++k) {
          const float sx = P.x + 14 + k * (size + gap), sy = y + 26;
          if (sws[k] == v) m_overlay.drawPill(sx - 3, sy - 3, size + 6, size + 6, (size + 6) / 2, ink, W, H);
          m_overlay.drawPill(sx, sy, size, size, size / 2, m_noctalia.state().color(sws[k]), W, H);
        }
        // a custom hex that is not one of the swatches
        bool listed = false;
        for (auto& s : sws) listed = listed || s == v;
        if (!listed) text(v, mono, cx + cw - 70, y + 7, ink);
      }
    }
    y += rowH;
  }
  // footer: duplicate / delete
  for (const UiControl& c : m_ui) {
    if (c.type != UiControl::Duplicate && c.type != UiControl::Delete) continue;
    const bool del = c.type == UiControl::Delete;
    m_overlay.drawPill(c.r.x, c.r.y, c.r.w, c.r.h, 10, del ? Color{0.9F, 0.25F, 0.2F, 0.22F} : Color{1, 1, 1, 0.08F}, W, H);
    const std::string s = del ? (es ? "Eliminar  (Supr)" : "Delete  (Del)") : (es ? "Duplicar  (Ctrl+D)" : "Duplicate  (Ctrl+D)");
    float tw = 0, th = 0, b = 0;
    TextRenderer::measure(s, label, tw, th, b);
    text(s, label, c.r.x + (c.r.w - tw) / 2, c.r.y + (c.r.h - th) / 2, del ? Color{1, 0.6F, 0.55F, 1} : ink);
  }
  drawGallery();
}

}  // namespace undershell
