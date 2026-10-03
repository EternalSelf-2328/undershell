// SPDX-License-Identifier: GPL-3.0-or-later
// Translated from ryoku/shell/.../modules/desktop/music/{MusicWidget,
// MusicLyrics, MusicViz, MusicSeek, MusicTransport, MusicPulse}.qml (GPL-3.0).
#include "nowplaying.hpp"
#include "i18n.hpp"

#include "m3shapes.hpp"

#include "media.hpp"
#include "motion.hpp"

#include <cmath>
#include <linux/input-event-codes.h>
#include <numbers>
#include <unistd.h>

namespace undershell {

namespace {
constexpr float PAD = 16, GAP = 14, COVER = 168, INFO_H = 42, SEEK_H = 38;
const char* FONT = "Space Grotesk";
const char* MONO = "JetBrains Mono";
const char* DISPLAY = "Fraunces 144pt";
const char* POSTER = "Google Sans Flex";

Color withAlpha(Color c, float a) {
  c.a *= a;
  return c;
}

bool spanish() { return spanishUi(); }

std::string stamp(double sec) {
  const long s = static_cast<long>(std::max(0.0, sec));
  if (s >= 3600) return std::format("{}:{:02d}:{:02d}", s / 3600, (s % 3600) / 60, s % 60);
  return std::format("{}:{:02d}", s / 60, s % 60);
}

void spawnDetached(const std::string& cmd) {
  if (cmd.empty()) return;
  if (fork() == 0) {
    setsid();
    if (fork() == 0) {
      execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
      _exit(127);
    }
    _exit(0);
  }
}

// a music note glyph drawn with primitives (Ryoku's GlyphIcon "music")
void noteGlyph(Canvas& c, float cx, float cy, float size, Color col) {
  const float s = size / 14;
  c.circle(cx - 3.2F * s, cy + 3.6F * s, 2.4F * s, col);
  c.circle(cx + 3.8F * s, cy + 2.4F * s, 2.4F * s, col);
  c.segment(cx - 1.0F * s, cy + 3.6F * s, cx - 1.0F * s, cy - 5.0F * s, 1.4F * s, col, false);
  c.segment(cx + 6.0F * s, cy + 2.4F * s, cx + 6.0F * s, cy - 6.2F * s, 1.4F * s, col, false);
  c.segment(cx - 1.0F * s, cy - 5.0F * s, cx + 6.0F * s, cy - 6.2F * s, 2.2F * s, col, false);
}
}  // namespace

NowPlayingConfig NowPlayingConfig::fromTable(const toml::table& t) {
  NowPlayingConfig c;
  c.plate = t["plate"].value_or(c.plate);
  c.showLyrics = t["show_lyrics"].value_or(c.showLyrics);
  c.viz = t["viz"].value_or(c.viz);
  c.musicApp = t["music_app"].value_or(c.musicApp);
  c.accentSource = t["accent_source"].value_or(c.accentSource);
  c.ink = t["ink"].value_or(c.ink);
  c.opacity = std::clamp(t["opacity"].value_or(c.opacity), 0.0, 1.0);
  c.fps = static_cast<int>(std::clamp<int64_t>(t["fps"].value_or(int64_t{30}), 5, 120));
  c.coverShape = t["cover_shape"].value_or(c.coverShape);
  c.lyricsStyle = t["lyrics_style"].value_or(c.lyricsStyle);
  return c;
}

void NowPlayingWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  configure(NowPlayingConfig::fromTable(cfg.options), noct);
}

void NowPlayingWidget::configure(const NowPlayingConfig& cfg, const NoctaliaState& noct) {
  m_cfg = cfg;
  m_noct = noct;
  m_ink = noct.color(cfg.ink, Color::fromHex("#f4f1ea"));
  m_theme = noct.color("primary");
  m_accent = m_theme;
  m_measures.clear();
  if (m_viz.empty()) m_viz.assign(40, 0.0F);
  if (cfg.coverShape != "rounded" && cfg.coverShape != "cycle") setCoverShape(cfg.coverShape, !m_shapeNow.empty());
  m_shapeTrack.clear();  // "cycle" picks again
}

// Starts a morph from the shape on screen to `name` (or jumps there).
void NowPlayingWidget::setCoverShape(const std::string& name, bool animate) {
  if (name == m_shapeName && !m_shapeNow.empty()) return;
  m_shapeName = name;
  m_shapeTo = m3ShapeRadii(name, 128);
  m_shapeFrom = m_shapeNow.empty() || !animate ? m_shapeTo : m_shapeNow;
  m_morph = animate ? 0 : 1;
  m_morphVel = 0;
  if (m_shapeNow.empty()) m_shapeNow = m_shapeTo;
}

Canvas::Size NowPlayingWidget::measure(const std::string& text, const TextStyle& st) {
  const std::string key = std::format("{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}", text, st.family, st.size, st.weight, st.maxWidth,
                                      st.maxLines, st.variations);
  auto it = m_measures.find(key);
  if (it != m_measures.end()) return it->second;
  if (m_measures.size() > 1024) m_measures.clear();
  return m_measures[key] = Canvas::measure(text, st);
}

void NowPlayingWidget::tick(const TickContext& ctx) {
  const double dt = std::clamp(ctx.dt, 0.0, 0.1);
  m_now = ctx.now;
  m_media = ctx.media;
  // the no-lyrics spectrum: 40 bands of the shared analyser, eased (~90 ms)
  static std::vector<float> src;
  if (!ctx.audio.silent && ctx.audio.bands && !ctx.audio.bands->empty()) Motion::resample(*ctx.audio.bands, 40, src);
  else src.assign(40, 0.0F);
  const float k = static_cast<float>(1 - std::exp(-dt / 0.06));
  for (size_t i = 0; i < m_viz.size(); ++i) m_viz[i] += (src[i] - m_viz[i]) * k;
  // lyric glide toward the sung line (Theme.slow)
  const float kg = static_cast<float>(1 - std::exp(-dt / 0.12));
  m_glide += (0 - m_glide) * kg;
  // the poster line eases in (critically damped: type never overshoots)
  if (m_posterP < 1) m_posterP = std::min(1.0, m_posterP + (1 - m_posterP) * (1 - std::exp(-dt / 0.14)) + dt * 0.02);
  // "cycle": each song gets its own shape (the same song, the same shape)
  if (m_cfg.coverShape == "cycle" && m_media && m_media->state().present && m_media->state().trackKey != m_shapeTrack) {
    m_shapeTrack = m_media->state().trackKey;
    static const char* kCycle[] = {"cookie12Sided", "flower", "clover8Leaf", "softBurst", "cookie9Sided", "sunny",
                                   "puffyDiamond", "cookie6Sided", "clover4Leaf", "cookie7Sided", "pentagon", "verySunny"};
    const size_t pick = std::hash<std::string>{}(m_shapeTrack) % std::size(kCycle);
    setCoverShape(kCycle[pick], !m_shapeNow.empty());
  }
  // the morph: Material's expressive spatial spring (it overshoots a little)
  if (!m_shapeTo.empty() && (m_morph < 1 || std::abs(m_morphVel) > 1e-4)) {
    const double omega = 11.0, zeta = 0.62;
    m_morphVel += (omega * omega * (1 - m_morph) - 2 * zeta * omega * m_morphVel) * dt;
    m_morph += m_morphVel * dt;
    if (std::abs(1 - m_morph) < 1e-3 && std::abs(m_morphVel) < 1e-3) m_morph = 1, m_morphVel = 0;
    m_shapeNow.resize(m_shapeTo.size());
    for (size_t i = 0; i < m_shapeTo.size(); ++i)
      m_shapeNow[i] = std::clamp(m_shapeFrom[i] + (m_shapeTo[i] - m_shapeFrom[i]) * static_cast<float>(m_morph), 0.05F, 1.15F);
  }
}

bool NowPlayingWidget::animating(const TickContext& ctx) const {
  const MediaService* m = ctx.media;
  const bool playing = m && m->state().present && m->state().playing;
  return playing || std::abs(m_glide) > 0.5F || m_morph < 1 || std::abs(m_morphVel) > 1e-4 || m_posterP < 0.999;
}

// The sung line as a poster (after Sung's PosterLine, MIT): broken where the
// plain lyric wraps, every word takes its own weight, roundness and slant in
// Google Sans Flex, and each row is set on the width axis until it fills the
// column from edge to edge (a short row is set larger instead, up to half as
// large again). At p = 0 it is the plain line exactly; p eases it into the
// poster as the line arrives.
NowPlayingWidget::Poster NowPlayingWidget::posterFor(const std::string& text, float width, float base, int plainWeight,
                                                     float p) {
  Poster out;
  // the plain line's own breaks: greedy, at the plain style
  const TextStyle plain{.family = POSTER, .size = base, .weight = plainWeight, .variations = std::format("wght={}", plainWeight)};
  std::vector<std::string> words;
  for (size_t i = 0; i < text.size();) {
    const size_t j = text.find(' ', i);
    const std::string w = text.substr(i, j == std::string::npos ? std::string::npos : j - i);
    if (!w.empty()) words.push_back(w);
    if (j == std::string::npos) break;
    i = j + 1;
  }
  if (words.empty()) return out;
  const float space = measure(" ", plain).w;
  std::vector<std::vector<std::string>> rows(1);
  float lineW = 0;
  for (const auto& w : words) {
    const float ww = measure(w, plain).w;
    if (!rows.back().empty() && lineW + space + ww > width) {
      rows.emplace_back();
      lineW = 0;
    }
    lineW += (rows.back().empty() ? 0 : space) + ww;
    rows.back().push_back(w);
  }
  // four word styles, two of them fully rounded (Material pairs the font's
  // roundness with its rounded shapes); a word keeps its style by its text
  struct WordStyle {
    int wght;
    float rond, slnt;
  };
  static const WordStyle kStyles[] = {{820, 100, 0}, {340, 0, -10}, {620, 0, 0}, {920, 100, -6}};
  const auto mix = [p](float a, float b) { return a + (b - a) * p; };
  auto styleOf = [&](const std::string& w, float wdth, float size) {
    const WordStyle& ws = kStyles[std::hash<std::string>{}(w) % std::size(kStyles)];
    TextStyle t{.family = POSTER, .size = size, .weight = static_cast<int>(mix(plainWeight, ws.wght))};
    t.variations = std::format("wght={},wdth={:.0f},ROND={:.0f},slnt={:.1f}", t.weight, mix(100, wdth), mix(0, ws.rond), mix(0, ws.slnt));
    return t;
  };
  float y = 0;
  for (const auto& row : rows) {
    // the width (25..151) that makes the row fill the column, by bisection
    auto rowWidth = [&](float wdth, float size) {
      float total = 0;
      for (size_t k = 0; k < row.size(); ++k) total += measure(row[k], styleOf(row[k], wdth, size)).w + (k ? space : 0);
      return total;
    };
    float lo = 25, hi = 151, wdth = 100, size = base;
    if (rowWidth(hi, base) < width) {
      wdth = 151;  // still short at the widest: set it larger
      float slo = base, shi = base * 1.5F;
      for (int it = 0; it < 8; ++it) {
        const float mid = (slo + shi) / 2;
        (rowWidth(151, mid) < width ? slo : shi) = mid;
      }
      size = slo;
    } else {
      for (int it = 0; it < 8; ++it) {
        const float mid = (lo + hi) / 2;
        (rowWidth(mid, base) < width ? lo : hi) = mid;
      }
      wdth = lo;
    }
    const float sz = mix(base, size);
    float used = 0, rowH = 0;
    std::vector<float> ws;
    for (const auto& w : row) {
      const auto z = measure(w, styleOf(w, wdth, sz));
      ws.push_back(z.w);
      used += z.w;
      rowH = std::max(rowH, z.h);
    }
    // the gaps take up the last pixels, so the row meets the measure exactly
    const float gap = row.size() > 1 ? mix(space, std::max(space * 0.4F, (width - used) / (row.size() - 1))) : 0;
    float x = 0;
    for (size_t k = 0; k < row.size(); ++k) {
      out.words.push_back({row[k], styleOf(row[k], wdth, sz), x, y});
      x += ws[k] + gap;
    }
    y += rowH * 0.92F;
  }
  out.h = y;
  return out;
}

void NowPlayingWidget::layoutTargets(bool seekable) {
  m_targets.clear();
  const float cy = PAD + COVER + GAP + INFO_H + 8 + SEEK_H / 2;
  const float nextX = kW - PAD - 15, playX = nextX - 15 - 6 - 19, prevX = playX - 19 - 6 - 15;
  m_targets.push_back({"next", {nextX - 15, cy - 15, 30, 30}});
  m_targets.push_back({"play", {playX - 19, cy - 19, 38, 38}});
  m_targets.push_back({"prev", {prevX - 15, cy - 15, 30, 30}});
  if (seekable) m_targets.push_back({"rail", {PAD, cy - 10, prevX - 15 - 14 - PAD, 20}});
  m_targets.push_back({"open", {kW - PAD - 26, PAD, 26, 26}});
}

std::vector<Rect> NowPlayingWidget::inputRects() const {
  std::vector<Rect> out;
  for (const auto& t : m_targets) out.push_back({m_ox + t.r.x * m_k, m_oy + t.r.y * m_k, t.r.w * m_k, t.r.h * m_k});
  return out;
}

bool NowPlayingWidget::onPointer(const PointerEvent& ev) {
  const float x = (ev.x - m_ox) / m_k, y = (ev.y - m_oy) / m_k;
  std::string hit;
  Rect hitRect;
  for (const auto& t : m_targets)
    if (t.r.contains(x, y)) {
      hit = t.id;
      hitRect = t.r;
    }
  bool redraw = false;
  if (ev.type == PointerEvent::Leave) hit.clear();
  if (hit != m_hover) {
    m_hover = hit;
    redraw = true;
  }
  if (ev.type == PointerEvent::Press && ev.button == BTN_LEFT && m_media) {
    const MediaState& s = m_media->state();
    if (hit == "play" && s.canToggle) m_media->toggle();
    else if (hit == "next" && s.canNext) m_media->next();
    else if (hit == "prev" && s.canPrev) m_media->previous();
    else if (hit == "rail" && s.canSeek && s.lengthUs > 0)
      m_media->seek(std::clamp((x - hitRect.x) / hitRect.w, 0.0F, 1.0F) * s.lengthUs / 1e6);
    else if (hit == "open") spawnDetached(m_cfg.musicApp);
    redraw = true;
  }
  return redraw;
}

void NowPlayingWidget::draw(const DrawContext& ctx) {
  m_media = ctx.media;
  static const MediaState kEmpty;
  const MediaState& s = m_media ? m_media->state() : kEmpty;
  const double now = ctx.now > 0 ? ctx.now : m_now;

  m_accent = (m_cfg.accentSource == "album" && s.hasAccent) ? s.accent : m_theme;
  const Color ink = m_ink, dim = withAlpha(m_ink, 0.7F);
  const Color surface = m_noct.color("surface", Color::fromHex("#0b0b0c"));
  const float op = static_cast<float>(m_cfg.opacity);

  m_k = std::min(ctx.w / kW, ctx.h / kH);
  m_ox = (ctx.w - kW * m_k) / 2;
  m_oy = (ctx.h - kH * m_k) / 2;
  Canvas& c = m_canvas;
  c.begin(ctx.w, ctx.h, ctx.scale, ctx.text);
  c.setTransform(m_k, m_ox, m_oy);
  auto A = [op](Color col, float a = 1) { return withAlpha(col, a * op); };

  // ── the plate ──
  const float R = 22;
  if (m_cfg.plate == "cover") {
    // the sleeve, blurred, under a near-opaque plate tinted a tenth toward it
    if (s.present && s.cover && s.coverW > 0) c.image(s.cover, s.coverW, s.coverH, 0, 0, kW, kH, R, 0.55F * op, 28);
    const Color plate = surface.mix(m_accent, 0.10F);
    c.roundRect(0, 0, kW, kH, R, A(plate, 0.80F), 1, A(ink, m_hover.empty() ? 0.08F : 0.16F));
  } else if (m_cfg.plate == "glass") {
    c.roundRect(0, 0, kW, kH, R, A(surface, 0.35F), 1, A(ink, 0.12F));
  }

  const bool es = spanish();
  const bool seekable = s.present && !s.radio() && s.lengthUs > 0;
  layoutTargets(seekable);

  // corner button: opens the music app
  {
    const float cx = kW - PAD - 13, cy = PAD + 13;
    if (m_hover == "open") c.circle(cx, cy, 13, A(ink, 0.12F));
    noteGlyph(c, cx, cy, 14, A(ink, m_hover.empty() ? 0.5F : 1.0F));
  }

  if (!s.present) {
    noteGlyph(c, kW / 2, kH / 2 - 14, 40, A(dim, 0.7F));
    const TextStyle ts{.family = FONT, .size = 13, .weight = 500};
    const std::string msg = es ? "No suena nada" : "Nothing playing";
    auto z = measure(msg, ts);
    c.text(msg, ts, (kW - z.w) / 2, kH / 2 + 18, A(dim));
    return;
  }

  // ── the sleeve ──
  if (s.cover && s.coverW > 0) {
    const bool shaped = m_cfg.coverShape != "rounded" && m_shapeNow.size() == 128;
    c.image(s.cover, s.coverW, s.coverH, PAD, PAD, COVER, COVER, 10, op, 0, shaped ? m_shapeNow.data() : nullptr);
  } else {
    c.roundRect(PAD, PAD, COVER, COVER, 10, A(m_noct.color("surface_variant", surface)));
    noteGlyph(c, PAD + COVER / 2, PAD + COVER / 2, 40, A(dim));
  }
  // now-playing pulse on the sleeve, only while sound is moving
  if (s.playing) {
    const float barW = 3, gap = 2.5F, maxH = 13, pw = 4 * barW + 3 * gap;
    const float px = PAD + 8, py = PAD + COVER - 8 - 20;
    c.roundRect(px, py, pw + 14, 20, 10, Color{0, 0, 0, 0.42F * op});
    for (int i = 0; i < 4; ++i) {
      const double t = now * 1000 - i * 85;
      const float lv = 0.28F + 0.72F * static_cast<float>(0.5 + 0.5 * std::sin(t / 1120.0 * 2 * std::numbers::pi * 1.7 + i));
      const float h = lv * maxH;
      c.roundRect(px + 7 + i * (barW + gap), py + 10 + maxH / 2 - h, barW, h, barW / 2, Color{1, 1, 1, op});
    }
  }

  // ── the side area: lyrics, or a live spectrum ──
  const float sx = PAD + COVER + GAP, sy = PAD + 26, sw = kW - PAD - sx, sh = COVER - 26;
  const double pos = s.positionSec(now);
  bool drewLyrics = false;
  if (m_cfg.showLyrics && s.lyrics == MediaState::Lyrics::Synced && !s.lines.empty()) {
    int idx = -1;
    const int ms = static_cast<int>(pos * 1000) + 250;
    for (size_t i = 0; i < s.lines.size(); ++i)
      if (s.lines[i].ms <= ms) idx = static_cast<int>(i);
    const float base = 15, lift = 2, spacing = 6;
    auto styleFor = [&](int i) {
      // in poster mode every line is set in the poster's font, so the sung one
      // only changes shape, never typeface
      TextStyle t{.family = m_cfg.lyricsStyle == "poster" ? POSTER : FONT, .size = i == idx ? base + lift : base,
                  .weight = i == idx ? 600 : 500};
      t.maxWidth = sw;
      t.maxLines = 3;
      return t;
    };
    auto textFor = [&](int i) { return s.lines[static_cast<size_t>(i)].text.empty() ? std::string("♪") : s.lines[static_cast<size_t>(i)].text; };
    // glide: when the sung line changes, start from where the column was
    if (idx != m_lyricIndex) {
      if (m_lyricIndex >= 0 && idx >= 0) {
        float delta = 0;
        const int a = std::min(idx, m_lyricIndex), b = std::max(idx, m_lyricIndex);
        for (int i = a; i < b && i < static_cast<int>(s.lines.size()); ++i) delta += measure(textFor(i), styleFor(i)).h + spacing;
        m_glide += idx > m_lyricIndex ? delta : -delta;
      }
      m_lyricIndex = idx;
      m_posterP = 0;  // the new line eases into its poster shape
    }
    c.clip(sx, PAD, sw, COVER);
    const int centre = std::max(idx, 0);
    auto cz = measure(textFor(centre), styleFor(centre));
    // the sung line as a poster, eased in (quantised so the text cache can keep up)
    const bool poster = m_cfg.lyricsStyle == "poster" && idx >= 0;
    Poster pst;
    if (poster) {
      const float q = std::round(static_cast<float>(m_posterP) * 16) / 16;
      pst = posterFor(textFor(idx), sw, base + lift, 600, q);
      cz.h = std::max(cz.h, pst.h);
    }
    float yTop = sy + sh / 2 - cz.h / 2 + m_glide;
    // the sung line and its neighbours, fading with distance
    auto drawLine = [&](int i, float y) {
      const int d = std::abs(i - idx);
      const float o = idx < 0 ? 0.52F : (d == 0 ? 1.0F : (d > 4 ? 0.0F : std::max(0.10F, 0.52F - (d - 1) * 0.14F)));
      if (o <= 0) return;
      if (poster && i == idx) {
        for (const auto& wd : pst.words) c.text(wd.text, wd.style, sx + wd.x, y + wd.y, A(m_accent), o);
        return;
      }
      c.text(textFor(i), styleFor(i), sx, y, i == idx ? A(m_accent) : A(ink), o);
    };
    drawLine(centre, yTop);
    float y = yTop;
    for (int i = centre - 1; i >= std::max(0, centre - 6); --i) {
      y -= measure(textFor(i), styleFor(i)).h + spacing;
      drawLine(i, y);
    }
    y = yTop + cz.h + spacing;
    for (int i = centre + 1; i < std::min(static_cast<int>(s.lines.size()), centre + 7); ++i) {
      drawLine(i, y);
      y += measure(textFor(i), styleFor(i)).h + spacing;
    }
    c.clip();
    drewLyrics = true;
  } else if (m_cfg.showLyrics && s.lyrics == MediaState::Lyrics::Plain) {
    TextStyle t{.family = FONT, .size = 15, .weight = 500};
    t.maxWidth = sw;
    t.maxLines = 7;
    c.clip(sx, PAD, sw, COVER);
    c.text(s.plain, t, sx, sy, A(ink), 0.62F);
    c.clip();
    drewLyrics = true;
  } else if (m_cfg.showLyrics && s.lyrics == MediaState::Lyrics::Searching) {
    TextStyle t{.family = FONT, .size = 15, .weight = 500};
    const std::string msg = es ? "Buscando letra…" : "Looking for lyrics…";
    auto z = measure(msg, t);
    c.text(msg, t, sx + (sw - z.w) / 2, sy + (sh - z.h) / 2, A(dim), 0.8F);
    drewLyrics = true;
  }
  if (!drewLyrics) {
    const int n = static_cast<int>(m_viz.size());
    const float pitch = sw / n, barW = std::max(2.0F, pitch * 0.55F), maxH = sh * 0.8F, mid = sy + sh / 2;
    if (m_cfg.viz == "wave") {
      for (int i = 0; i + 1 < n; ++i) {
        const float x0 = sx + (i + 0.5F) * pitch, x1 = sx + (i + 1.5F) * pitch;
        const float a0 = std::max(barW / 2, m_viz[static_cast<size_t>(i)] * maxH / 2);
        const float a1 = std::max(barW / 2, m_viz[static_cast<size_t>(i + 1)] * maxH / 2);
        c.triangle(x0, mid - a0, x1, mid - a1, x1, mid + a1, A(m_accent, 0.5F));
        c.triangle(x0, mid - a0, x1, mid + a1, x0, mid + a0, A(m_accent, 0.5F));
      }
    } else {
      for (int i = 0; i < n; ++i) {
        const float h = std::max(barW, m_viz[static_cast<size_t>(i)] * maxH);
        c.roundRect(sx + i * pitch + (pitch - barW) / 2, mid - h / 2, barW, h, barW / 2, A(m_accent, 0.85F));
      }
    }
  }

  // ── title, artist, clock ──
  const float iy = PAD + COVER + GAP;
  const std::string st = seekable ? stamp(pos) + " / " + stamp(s.lengthUs / 1e6) : "";
  const TextStyle stS{.family = MONO, .size = 11, .weight = 600};
  const auto stZ = measure(st, stS);
  TextStyle tS{.family = DISPLAY, .size = 20, .weight = 600};
  tS.maxWidth = kW - 2 * PAD - stZ.w - 12;
  TextStyle aS{.family = FONT, .size = 12.5F, .weight = 500};
  aS.maxWidth = tS.maxWidth;
  const auto tZ = measure(s.title, tS);
  const auto aZ = s.artist.empty() ? Canvas::Size{} : measure(s.artist, aS);
  const float colH = tZ.h + (s.artist.empty() ? 0 : 2 + aZ.h);
  float y = iy + (INFO_H - colH) / 2;
  c.text(s.title, tS, PAD, y, A(ink));
  if (!s.artist.empty()) c.text(s.artist, aS, PAD, y + tZ.h + 2, A(dim));
  if (!st.empty()) c.text(st, stS, kW - PAD - stZ.w, iy + INFO_H - 2 - stZ.h, A(dim));

  // ── seek rail + transport ──
  const float cy = iy + INFO_H + 8 + SEEK_H / 2;
  const float nextX = kW - PAD - 15, playX = nextX - 15 - 6 - 19, prevX = playX - 19 - 6 - 15;
  const float railX0 = PAD, railX1 = prevX - 15 - 14;
  if (seekable) {
    const float frac = std::clamp(static_cast<float>(pos / (s.lengthUs / 1e6)), 0.0F, 1.0F);
    const float px = railX0 + (railX1 - railX0) * frac;
    c.segment(railX0, cy, railX1, cy, 2.2F, A(ink, 0.16F));
    // the played part: a travelling wave in the sleeve's colour
    const float phase = s.playing ? static_cast<float>(std::fmod(now, 1.1) / 1.1 * 15.0) : 0.0F;
    c.wave(railX0, px, cy, 3, 15, phase, 2.2F, A(m_accent));
    const float hs = m_hover == "rail" ? 1.35F : 1.0F;
    if (s.canSeek) c.roundRect(px - 1.5F * hs, cy - 6 * hs, 3 * hs, 12 * hs, 1.5F * hs, A(m_accent));
  } else if (s.radio()) {
    const TextStyle lS{.family = MONO, .size = 11, .weight = 600, .letterSpacing = 1.1F};
    c.text(es ? "En vivo" : "Live", lS, railX0, cy - 8, A(dim));
  }
  auto moveBg = [&](const char* id, float x, float r, bool filled, bool enabled) {
    const bool hot = m_hover == id && enabled;
    if (filled) c.circle(x, cy, r, A(m_accent, enabled ? 1.0F : 0.4F));
    else if (hot) c.circle(x, cy, r, A(ink, 0.12F));
  };
  moveBg("prev", prevX, 15, false, s.canPrev);
  moveBg("play", playX, 19, true, s.canToggle);
  moveBg("next", nextX, 15, false, s.canNext);
  const Color onAccent = A(surface), glyph = A(ink);
  // previous: bar + left triangle
  c.roundRect(prevX - 5.5F, cy - 5, 2, 10, 1, withAlpha(glyph, s.canPrev ? 1 : 0.4F));
  c.triangle(prevX + 5, cy - 5, prevX + 5, cy + 5, prevX - 3, cy, withAlpha(glyph, s.canPrev ? 1 : 0.4F));
  // next: right triangle + bar
  c.triangle(nextX - 5, cy - 5, nextX - 5, cy + 5, nextX + 3, cy, withAlpha(glyph, s.canNext ? 1 : 0.4F));
  c.roundRect(nextX + 3.5F, cy - 5, 2, 10, 1, withAlpha(glyph, s.canNext ? 1 : 0.4F));
  // play / pause on the filled tile
  if (s.playing) {
    c.roundRect(playX - 5, cy - 6.5F, 3.5F, 13, 1.2F, onAccent);
    c.roundRect(playX + 1.5F, cy - 6.5F, 3.5F, 13, 1.2F, onAccent);
  } else {
    c.triangle(playX - 4, cy - 7, playX - 4, cy + 7, playX + 7, cy, onAccent);
  }
  m_wasPlaying = s.playing;
}

}  // namespace undershell
