// SPDX-License-Identifier: GPL-3.0-or-later
// The notes widget (see notes.hpp).
#include "notes.hpp"

#include "i18n.hpp"
#include "text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace undershell {

namespace {
template <typename T>
T get(const toml::table& t, std::string_view key, T def) {
  if (auto v = t[key].value<T>()) return *v;
  return def;
}

Color withA(Color c, float a) {
  c.a *= a;
  return c;
}

std::vector<std::string> splitLines(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == '\n') {
      out.push_back(cur);
      cur.clear();
    } else if (c != '\r') {
      cur += c;
    }
  }
  out.push_back(cur);
  return out;
}

// what Markdown's inline marks leave on screen
std::string plain(std::string s) {
  for (const char* mark : {"**", "__", "`"})
    for (size_t p; (p = s.find(mark)) != std::string::npos;) s.erase(p, std::char_traits<char>::length(mark));
  return s;
}
}  // namespace

NotesConfig NotesConfig::fromTable(const toml::table& t) {
  NotesConfig c;
  c.file = get<std::string>(t, "file", c.file);
  c.plate = get<std::string>(t, "plate", c.plate);
  c.paper = get<std::string>(t, "paper", c.paper);
  c.ink = get<std::string>(t, "ink", c.ink);
  c.accent = get<std::string>(t, "accent", c.accent);
  c.font = get<std::string>(t, "font", c.font);
  if (auto d = t["size"].value<double>()) c.size = std::clamp(*d, 8.0, 72.0);
  if (auto d = t["opacity"].value<double>()) c.opacity = std::clamp(*d, 0.0, 1.0);
  return c;
}

std::string NotesWidget::pathFor(const std::string& id, const std::string& file) {
  if (!file.empty()) return expandHome(file);
  const char* xdg = std::getenv("XDG_DATA_HOME");
  const std::string data = xdg && *xdg ? std::string(xdg) : expandHome("~/.local/share");
  return data + "/undershell/notes/" + (id.empty() ? std::string("notes") : id) + ".md";
}

std::vector<NotesWidget::Line> NotesWidget::parse(const std::string& md) {
  std::vector<Line> out;
  for (const std::string& raw : splitLines(md)) {
    Line l;
    size_t sp = raw.find_first_not_of(' ');
    if (sp == std::string::npos) {
      l.kind = Line::Blank;
      out.push_back(l);
      continue;
    }
    l.indent = static_cast<int>(sp / 2);
    const std::string s = raw.substr(sp);
    auto starts = [&](const char* p) { return s.rfind(p, 0) == 0; };
    if (starts("### ")) l.kind = Line::Heading3, l.text = s.substr(4);
    else if (starts("## ")) l.kind = Line::Heading2, l.text = s.substr(3);
    else if (starts("# ")) l.kind = Line::Heading1, l.text = s.substr(2);
    else if (starts("- [ ] ") || starts("* [ ] ")) l.kind = Line::Task, l.text = s.substr(6);
    else if (starts("- [x] ") || starts("- [X] ") || starts("* [x] ") || starts("* [X] ")) l.kind = Line::Done, l.text = s.substr(6);
    else if (starts("- ") || starts("* ") || starts("+ ")) l.kind = Line::Bullet, l.text = s.substr(2);
    else if (starts("> ")) l.kind = Line::Quote, l.text = s.substr(2);
    else if (s == "---" || s == "***") l.kind = Line::Rule;
    else l.kind = Line::Text, l.text = s;
    l.text = plain(l.text);
    out.push_back(l);
  }
  while (!out.empty() && out.back().kind == Line::Blank) out.pop_back();
  return out;
}

void NotesWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  m_editable = true;
  m_cfg = NotesConfig::fromTable(cfg.options);
  m_paperC = noct.color(m_cfg.paper, Color::fromHex("#ffe58a"));
  m_surfaceC = noct.color("surface", Color::fromHex("#16161a"));
  m_accentC = noct.color(m_cfg.accent, Color::fromHex("#e2342a"));
  if (m_cfg.ink == "auto") {
    const bool paper = m_cfg.plate == "paper";
    const float lum = 0.2126F * m_paperC.r + 0.7152F * m_paperC.g + 0.0722F * m_paperC.b;
    m_inkC = paper ? (lum > 0.5F ? Color{0.13F, 0.12F, 0.10F, 1} : Color{0.97F, 0.96F, 0.93F, 1})
                   : noct.color("on_surface", Color::fromHex("#f4f1ea"));
  } else {
    m_inkC = noct.color(m_cfg.ink, Color::fromHex("#f4f1ea"));
  }
  if (m_cfg.plate == "paper") {
    // a pale accent vanishes on a pale paper: pulled toward the ink until it shows
    auto lum = [](Color c) { return 0.2126F * c.r + 0.7152F * c.g + 0.0722F * c.b; };
    for (int k = 0; k < 4 && std::abs(lum(m_accentC) - lum(m_paperC)) < 0.3F; ++k) m_accentC = m_accentC.mix(m_inkC, 0.35F);
  }
  const std::string path = pathFor(cfg.id, m_cfg.file);
  if (path != m_path) {
    m_path = path;
    load();
  }
}

void NotesWidget::load() {
  std::error_code ec;
  m_text = fs::exists(m_path, ec) ? readFile(m_path) : std::string();
}

void NotesWidget::setEditing(const std::string* text, size_t caret) {
  m_editing = text != nullptr;
  m_draft = text ? *text : std::string();
  m_caret = std::min(caret, m_draft.size());
}

void NotesWidget::saveEditedText(const std::string& text) {
  std::error_code ec;
  fs::create_directories(fs::path(m_path).parent_path(), ec);
  if (writeFileAtomic(m_path, text)) m_text = text;
}

void NotesWidget::toggleTask(int line) {
  auto lines = splitLines(m_text);
  if (line < 0 || line >= static_cast<int>(lines.size())) return;
  std::string& l = lines[static_cast<size_t>(line)];
  if (auto p = l.find("[ ]"); p != std::string::npos) l.replace(p, 3, "[x]");
  else if (auto q = l.find("[x]"); q != std::string::npos) l.replace(q, 3, "[ ]");
  else if (auto r = l.find("[X]"); r != std::string::npos) l.replace(r, 3, "[ ]");
  std::string out;
  for (size_t i = 0; i < lines.size(); ++i) out += lines[i] + (i + 1 < lines.size() ? "\n" : "");
  saveEditedText(out);
}

std::vector<Rect> NotesWidget::inputRects() const {
  std::vector<Rect> r;
  if (m_editing) return r;
  for (const auto& b : m_boxes) r.push_back({b.r.x - 4, b.r.y - 4, b.r.w + 8, b.r.h + 8});
  return r;
}

bool NotesWidget::onPointer(const PointerEvent& ev) {
  int at = -1;
  for (size_t i = 0; i < m_boxes.size(); ++i) {
    const Rect& b = m_boxes[i].r;
    if (ev.x >= b.x - 4 && ev.y >= b.y - 4 && ev.x < b.x + b.w + 4 && ev.y < b.y + b.h + 4) at = static_cast<int>(i);
  }
  if (ev.type == PointerEvent::Leave) at = -1;
  if (ev.type == PointerEvent::Press && at >= 0) {
    toggleTask(m_boxes[static_cast<size_t>(at)].line);
    return true;
  }
  if (at != m_hover) {
    m_hover = at;
    return true;
  }
  return false;
}

void NotesWidget::draw(const DrawContext& ctx) {
  Canvas& c = m_canvas;
  c.begin(ctx.w, ctx.h, ctx.scale, ctx.text);
  const float W = ctx.w, H = ctx.h, op = static_cast<float>(m_cfg.opacity);
  const auto S = static_cast<float>(m_cfg.size);
  // the plate
  float inset = 0;
  if (m_cfg.plate == "paper") {
    // a sticky note: a soft shadow under it and a strip of tape across its top
    inset = std::max(4.0F, S * 0.3F);
    c.roundRect(inset + 2, inset + 5, W - 2 * inset, H - 2 * inset, 5, Color{0, 0, 0, 0.18F * op});
    c.roundRect(inset + 1, inset + 2.5F, W - 2 * inset, H - 2 * inset, 5, Color{0, 0, 0, 0.14F * op});
    c.roundRect(inset, inset, W - 2 * inset, H - 2 * inset, 5, withA(m_paperC, op));
    const float tw = std::min(W * 0.34F, S * 6);
    c.roundRect(W / 2 - tw / 2, inset - S * 0.45F, tw, S * 1.1F, 2, Color{1, 1, 1, 0.38F * op});
  } else if (m_cfg.plate == "glass") {
    c.roundRect(0, 0, W, H, std::min(W, H) * 0.06F, withA(m_surfaceC, 0.6F * op), 1, withA(m_inkC, 0.1F * op));
  }
  const float pad = inset + std::max(12.0F, S * 0.95F);
  const float x0 = pad, maxW = std::max(20.0F, W - 2 * pad), bottom = H - pad * 0.7F;
  c.clip(inset, inset, W - 2 * inset, H - 2 * inset);
  m_boxes.clear();
  const TextStyle base{.family = m_cfg.font, .size = S, .weight = 500, .maxWidth = maxW, .maxLines = 60};

  if (m_editing) {
    // the Markdown as typed, each line wrapped, with the caret; scrolled so it shows
    const auto lines = splitLines(m_draft);
    size_t at = 0, caretLine = 0, caretCol = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
      if (m_caret >= at && m_caret <= at + lines[i].size()) {
        caretLine = i;
        caretCol = m_caret - at;
        break;
      }
      at += lines[i].size() + 1;
    }
    std::vector<float> ys(lines.size() + 1);
    float y = pad;
    const float lineGap = S * 0.3F;
    for (size_t i = 0; i < lines.size(); ++i) {
      ys[i] = y;
      const float lh = lines[i].empty() ? S * 1.25F : Canvas::measure(lines[i], base).h;
      y += lh + lineGap;
    }
    ys[lines.size()] = y;
    // where the caret is inside its (wrapped) line
    float cx = x0, cy = ys[caretLine], ch = S * 1.25F;
    if (!lines[caretLine].empty()) {
      size_t bytes = 0;
      const auto cl = TextRenderer::clusters(lines[caretLine], base);
      for (const auto& k : cl) {
        if (bytes >= caretCol) {
          cx = x0 + k.x, cy = ys[caretLine] + k.y, ch = k.h;
          break;
        }
        bytes += k.text.size();
        cx = x0 + k.x + k.w, cy = ys[caretLine] + k.y, ch = k.h;
      }
    }
    const float scroll = std::max(0.0F, cy + ch - bottom);
    for (size_t i = 0; i < lines.size(); ++i) {
      const float ly = ys[i] - scroll;
      if (ly > H || ys[i + 1] - scroll < 0) continue;
      if (!lines[i].empty()) c.text(lines[i], base, x0, ly, withA(m_inkC, op));
    }
    c.segment(cx, cy - scroll + ch * 0.12F, cx, cy - scroll + ch * 0.88F, std::max(1.5F, S * 0.09F), withA(m_accentC, op), false);
    c.clip();
    return;
  }

  const auto lines = parse(m_text);
  if (lines.empty()) {
    const std::string hint = spanishUi() ? "Doble clic en el editor para escribir" : "Double-click in the editor to write";
    TextStyle st = base;
    st.italic = true;
    c.text(hint, st, x0, pad, withA(m_inkC, 0.45F * op));
    c.clip();
    return;
  }
  // the source line each parsed one came from (blank lines at the end are dropped)
  float y = pad;
  int src = 0;
  for (const Line& l : lines) {
    const int srcLine = src++;
    if (y > bottom) break;
    const float ind = static_cast<float>(l.indent) * S * 1.2F;
    switch (l.kind) {
      case Line::Blank: y += S * 0.6F; continue;
      case Line::Rule:
        c.segment(x0, y + S * 0.5F, x0 + maxW, y + S * 0.5F, 1.2F, withA(m_inkC, 0.3F * op), false);
        y += S * 1.2F;
        continue;
      case Line::Heading1:
      case Line::Heading2:
      case Line::Heading3: {
        TextStyle st = base;
        st.size = S * (l.kind == Line::Heading1 ? 1.5F : l.kind == Line::Heading2 ? 1.25F : 1.08F);
        st.weight = 700;
        const auto m = Canvas::measure(l.text, st);
        c.text(l.text, st, x0, y, withA(m_inkC, op));
        if (l.kind == Line::Heading1)  // underlined in the accent
          c.roundRect(x0, y + m.h + S * 0.08F, std::min(maxW, S * 2.2F), std::max(2.0F, S * 0.14F), 1, withA(m_accentC, op));
        y += m.h + S * (l.kind == Line::Heading1 ? 0.6F : 0.35F);
        continue;
      }
      default: break;
    }
    TextStyle st = base;
    float tx = x0 + ind;
    Color ink = withA(m_inkC, op);
    if (l.kind == Line::Bullet) {
      c.circle(tx + S * 0.32F, y + S * 0.68F, std::max(1.8F, S * 0.13F), withA(m_accentC, op));
      tx += S * 1.0F;
    } else if (l.kind == Line::Task || l.kind == Line::Done) {
      const float bs = S * 0.82F, by = y + S * 0.26F;
      const Rect box{tx, by, bs, bs};
      const bool hot = m_hover == static_cast<int>(m_boxes.size());
      if (l.kind == Line::Done) {
        c.roundRect(box.x, box.y, bs, bs, bs * 0.22F, withA(m_accentC, op));
        const Color tick = Color{1, 1, 1, op};
        c.segment(box.x + bs * 0.22F, box.y + bs * 0.52F, box.x + bs * 0.42F, box.y + bs * 0.72F, std::max(1.4F, bs * 0.13F), tick);
        c.segment(box.x + bs * 0.42F, box.y + bs * 0.72F, box.x + bs * 0.78F, box.y + bs * 0.3F, std::max(1.4F, bs * 0.13F), tick);
        ink = withA(m_inkC, 0.5F * op);
      } else {
        c.roundRect(box.x, box.y, bs, bs, bs * 0.22F, hot ? withA(m_accentC, 0.25F * op) : Color{0, 0, 0, 0}, std::max(1.3F, bs * 0.1F),
                    withA(hot ? m_accentC : m_inkC, (hot ? 1.0F : 0.7F) * op));
      }
      m_boxes.push_back({box, srcLine});
      tx += S * 1.3F;
    } else if (l.kind == Line::Quote) {
      st.italic = true;
      ink = withA(m_inkC, 0.75F * op);
      tx += S * 0.8F;
    }
    st.maxWidth = std::max(20.0F, x0 + maxW - tx);
    const auto m = Canvas::measure(l.text, st);
    if (l.kind == Line::Quote) c.roundRect(x0 + ind, y + S * 0.1F, std::max(2.0F, S * 0.15F), m.h - S * 0.2F, 1, withA(m_accentC, 0.8F * op));
    c.text(l.text, st, tx, y, ink);
    if (l.kind == Line::Done) {  // struck through, its first line
      const float fw = std::min(Canvas::measure(l.text, TextStyle{.family = m_cfg.font, .size = S, .weight = 500}).w, st.maxWidth);
      c.segment(tx, y + S * 0.68F, tx + fw, y + S * 0.68F, std::max(1.0F, S * 0.07F), withA(m_inkC, 0.5F * op), false);
    }
    y += m.h + S * 0.3F;
  }
  c.clip();
}

}  // namespace undershell
