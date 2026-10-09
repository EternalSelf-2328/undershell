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
// Color's own default is opaque white, which is never what an outline wants
constexpr Color kClear{0, 0, 0, 0};
constexpr float kLead = 5;    // between the title's ink and the artist's
constexpr float kRailH = 18;  // the row the seek rail lives on

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
  c.layout = t["layout"].value_or(c.layout);
  c.plate = t["plate"].value_or(c.plate);
  c.showLyrics = t["show_lyrics"].value_or(c.showLyrics);
  c.showCover = t["show_cover"].value_or(c.showCover);
  c.showArtist = t["show_artist"].value_or(c.showArtist);
  c.showTime = t["show_time"].value_or(c.showTime);
  c.showRail = t["show_rail"].value_or(c.showRail);
  c.showTransport = t["show_transport"].value_or(c.showTransport);
  c.showOpen = t["show_open"].value_or(c.showOpen);
  c.showPulse = t["show_pulse"].value_or(c.showPulse);
  c.showViz = t["show_viz"].value_or(c.showViz);
  c.viz = t["viz"].value_or(c.viz);
  c.musicApp = t["music_app"].value_or(c.musicApp);
  c.accentSource = t["accent_source"].value_or(c.accentSource);
  c.ink = t["ink"].value_or(c.ink);
  c.opacity = std::clamp(t["opacity"].value_or(c.opacity), 0.0, 1.0);
  c.fps = static_cast<int>(std::clamp<int64_t>(t["fps"].value_or(int64_t{30}), 5, 120));
  c.coverShape = t["cover_shape"].value_or(c.coverShape);
  c.railStyle = t["rail_style"].value_or(c.railStyle);
  c.titleSize = std::clamp(t["title_size"].value_or(c.titleSize), 0.4, 2.5);
  c.artistSize = std::clamp(t["artist_size"].value_or(c.artistSize), 0.4, 2.5);
  c.timeSize = std::clamp(t["time_size"].value_or(c.timeSize), 0.4, 2.5);
  c.titleFont = t["title_font"].value_or(c.titleFont);
  c.artistFont = t["artist_font"].value_or(c.artistFont);
  c.timeFont = t["time_font"].value_or(c.timeFont);
  c.lyricsStyle = t["lyrics_style"].value_or(c.lyricsStyle);
  if (c.lyricsStyle == "poster") c.lyricsStyle = "focus";  // what replaced it
  c.textForm = t["text_form"].value_or(c.textForm);
  c.textAlign = t["text_align"].value_or(c.textAlign);
  c.textValign = t["text_valign"].value_or(c.textValign);
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
  // the record turns while the song plays
  if (m_media && m_media->state().playing) m_spin += dt * 1.1;
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
  if (m_cfg.layout == "text") return false;  // nothing on it moves; a new song redraws it
  const MediaService* m = ctx.media;
  const bool playing = m && m->state().present && m->state().playing;
  return playing || std::abs(m_glide) > 0.5F || m_morph < 1 || std::abs(m_morphVel) > 1e-4;
}

// ── where each piece of the card goes ─────────────────────────────────────
// A layout is drawn at its own design size and says where every piece
// belongs. The drawing and the hit targets both read it, so they cannot
// drift apart, and a new layout is a new block here plus nothing else.
std::pair<float, float> NowPlayingWidget::designSize() const {
  const Places p = places();
  return {p.w, p.h};
}

static void designSizeOf(const NowPlayingConfig& cfg, float& w, float& h) {
  NowPlayingWidget np;
  np.configure(cfg, NoctaliaState{});
  const auto p = np.designSize();
  w = p.first;
  h = p.second;
}

void nowPlayingDesignSize(const std::string& layout, float& w, float& h) {
  NowPlayingConfig cfg;
  cfg.layout = layout;
  designSizeOf(cfg, w, h);
}

void nowPlayingDesignSize(const toml::table& options, float& w, float& h) {
  designSizeOf(NowPlayingConfig::fromTable(options), w, h);
}

// How tall the track's own band comes out, from what its faces really paint:
// the title, and the artist under it when it is shown. It is measured on a
// stand-in, not on this song's words, so the card does not resize per track.
float NowPlayingWidget::textBandHeight() const {
  const float th = TextRenderer::measureInk("Ag", titleStyle()).h;
  return m_cfg.showArtist ? th + kLead + TextRenderer::measureInk("Ag", artistStyle()).h : th;
}

TextStyle NowPlayingWidget::titleStyle() const {
  return {.family = m_cfg.titleFont.empty() ? DISPLAY : m_cfg.titleFont,
          .size = 20 * static_cast<float>(m_cfg.titleSize),
          .weight = 600};
}
TextStyle NowPlayingWidget::artistStyle() const {
  return {.family = m_cfg.artistFont.empty() ? FONT : m_cfg.artistFont,
          .size = 12.5F * static_cast<float>(m_cfg.artistSize),
          .weight = 500};
}
TextStyle NowPlayingWidget::clockStyle() const {
  return {.family = m_cfg.timeFont.empty() ? MONO : m_cfg.timeFont,
          .size = 11 * static_cast<float>(m_cfg.timeSize),
          .weight = 600};
}

NowPlayingWidget::Places NowPlayingWidget::places() const {
  Places p;
  const std::string& l = m_cfg.layout;
  const bool moves = m_cfg.showTransport, rail = m_cfg.showRail;
  const bool cover = m_cfg.showCover && l != "poster";
  const float textH = textBandHeight();
  // measured through the ink cache, since this runs for every frame drawn
  const float clockW = m_cfg.showTime ? TextRenderer::measureInk("0:00 / 0:00", clockStyle()).boxW : 0;
  p.clock = m_cfg.showTime;

  // The rows every layout but the sheet stacks, in this order: the track, the
  // rail (with the clock at its end where the band is too narrow to hold it)
  // and the three moves, centred. Only the ones that are shown take any room,
  // which is what makes a card close up around what is left of it.
  auto rowsHeight = [&](float playR, float gap) {
    float h = textH;
    if (rail) h += gap + kRailH;
    if (moves) h += gap + 2 * playR;
    return h;
  };
  auto stack = [&](float x, float y, float w, float gap, float playR, float sideR, bool clockOnRail) {
    p.text = {x, y, w, textH};
    y += textH;
    if (rail) {
      y += gap;
      float rw = w;
      if (clockOnRail && clockW > 0) {
        rw = std::max(40.0F, w - clockW - 12);
        p.time = {x + w - clockW, y, clockW, kRailH};
      }
      p.rail = {x, y, rw, kRailH};
      y += kRailH;
    }
    if (moves) {
      y += gap;
      p.playR = playR;
      p.sideR = sideR;
      p.ctrlY = y + playR;
      p.playX = x + w / 2;
      p.prevX = p.playX - playR - 9 - sideR;
      p.nextX = p.playX + playR + 9 + sideR;
      y += 2 * playR;
    } else {
      p.playR = 0;
    }
  };

  // Only the track: drawn at the widget's own box, which it fills
  if (l == "text") {
    p.w = m_boxW;
    p.h = m_boxH;
    p.plate = {0, 0, p.w, p.h};
    p.radius = std::min(22.0F, p.h / 2);
    p.text = {0, 0, p.w, p.h};
    p.playR = 0;
    p.clock = false;
    return p;
  }
  // A record on its deck: the sleeve is the label, and the disc turns while
  // the song plays. Everything else stands to its right.
  if (l == "vinyl") {
    const float pad = 18, gap = 12;
    p.w = 440;
    const float rows = rowsHeight(17, gap);
    // the record follows what stands beside it, so a card showing little does
    // not end up as one big disc and a line of text
    const float disc = cover ? std::clamp(rows + 24, 92.0F, 124.0F) : 0;
    p.h = std::max(disc, rows) + 2 * pad;
    p.plate = {0, 0, p.w, p.h};
    p.radius = std::min(46.0F, p.h / 2);
    if (cover) {
      p.record = disc / 2;
      p.cover = {pad + disc / 2 - 26, (p.h - 52) / 2, 52, 52};
      p.coverRadius = 26;
    }
    const float x = pad + (cover ? disc + 22 : 0);
    stack(x, (p.h - rows) / 2, p.w - pad - x, gap, 17, 13, true);
    return p;
  }
  // A small card the sleeve steps out of on the left, the track set against
  // the right edge and the three moves centred under it.
  if (l == "tile") {
    const float pad = 14, gap = 10, art = cover ? 80.0F : 0;
    p.w = 320;
    const float rows = rowsHeight(15, gap);
    p.h = std::max(art + 2 * pad, rows + 2 * pad + 8);
    p.plate = {cover ? 40.0F : 0, 6, p.w - (cover ? 40 : 0), p.h - 12};
    p.radius = 26;
    if (cover) {
      p.cover = {6, (p.h - art) / 2, art, art};
      p.coverRadius = 18;
    }
    const float x = (cover ? 100.0F : 20);
    stack(x, (p.h - rows) / 2, p.w - 16 - x, gap, 15, 11, true);
    p.align = 2;
    return p;
  }
  // The sleeve is the card: the track and its moves sit on it, under a veil
  // that keeps them readable whatever the artwork does.
  if (l == "poster") {
    const float pad = 20, gap = 12;
    p.w = 300;
    const float rows = rowsHeight(17, gap);
    p.h = 300 + rows + pad;
    p.plate = {0, 0, p.w, p.h};
    p.radius = 26;
    p.coverPlate = true;
    stack(pad, p.h - pad - rows, p.w - 2 * pad, gap, 17, 13, true);
    return p;
  }
  // One line of it, for a narrow gap: everything side by side, and whatever
  // is left out gives its width back to the track.
  if (l == "strip") {
    const float pad = 10, gap = 14, art = cover ? 56.0F : 0;
    p.w = 560;
    const float playR = 15, sideR = 11;
    const float movesW = moves ? 2 * sideR + 9 + 2 * playR + 9 + 2 * sideR : 0;
    const float railW = rail ? 96.0F : 0;
    p.h = std::max({art, textH, 2 * playR, kRailH}) + 2 * pad + 6;
    p.plate = {0, 0, p.w, p.h};
    p.radius = std::min(22.0F, p.h / 2);
    float x = pad;
    if (cover) {
      p.cover = {x, (p.h - art) / 2, art, art};
      p.coverRadius = 12;
      x += art + 12;
    }
    float right = p.w - pad;
    if (moves) {
      p.playR = playR;
      p.sideR = sideR;
      p.ctrlY = p.h / 2;
      p.nextX = right - sideR;
      p.playX = p.nextX - sideR - 9 - playR;
      p.prevX = p.playX - playR - 9 - sideR;
      right = p.prevX - sideR - 14;
    } else {
      p.playR = 0;
    }
    if (rail) {
      p.rail = {right - railW, p.h / 2 - kRailH / 2, railW, kRailH};
      right -= railW + 14;
    }
    if (clockW > 0) {
      p.time = {right - clockW, p.h / 2 - 10, clockW, 20};
      right -= clockW + 14;
    }
    p.text = {x, (p.h - textH) / 2, std::max(60.0F, right - x), textH};
    return p;
  }
  // Standing up: the sleeve on top and everything else centred under it.
  if (l == "portrait") {
    const float pad = 20, gap = 12, art = cover ? 240.0F : 0;
    p.w = 280;
    const float rows = rowsHeight(17, gap);
    p.h = pad + art + (cover ? 16 : 0) + rows + pad;
    p.plate = {0, 0, p.w, p.h};
    p.radius = 26;
    if (cover) {
      p.cover = {pad, pad, art, art};
      p.coverRadius = 16;
    }
    stack(pad, pad + art + (cover ? 16 : 0), p.w - 2 * pad, gap, 17, 13, true);
    p.align = 1;
    return p;
  }

  // Ryoku's sheet: the sleeve leads, this song's lyrics run beside it, and
  // the track, its clock, the rail and the three moves close it out. Here the
  // rail and the moves share a line, so the rail takes the width the moves
  // leave it -- all of it, when they are not shown.
  const float pad = 16, gap = 14, art = cover ? 168.0F : 0;
  const bool side = m_cfg.showLyrics || m_cfg.showViz;
  p.w = 560;
  if (cover) {
    p.cover = {pad, pad, art, art};
    p.coverRadius = 10;
  }
  const float topH = side || cover ? 168.0F : 0;
  if (side) {
    const float sx = pad + (cover ? art + gap : 0);
    p.side = {sx, pad + 26, p.w - pad - sx, topH - 26};
  }
  float y = pad + topH + (topH > 0 ? gap : 0);
  p.text = {pad, y, p.w - 2 * pad, textH};
  y += textH;
  const float movesH = moves ? 38.0F : 0, rowH = std::max(rail ? 20.0F : 0, movesH);
  p.playR = 0;  // no moves unless the row below has them (it used to keep the default, a button at 0,0)
  if (rowH > 0) {
    y += 8;
    p.ctrlY = y + rowH / 2;
    if (moves) {
      p.playR = 19;
      p.sideR = 15;
      p.nextX = p.w - pad - 15;
      p.playX = p.nextX - 15 - 6 - 19;
      p.prevX = p.playX - 19 - 6 - 15;
    } else {
      p.playR = 0;
    }
    if (rail) {
      const float railRight = moves ? p.prevX - 15 - 14 : p.w - pad;
      p.rail = {pad, p.ctrlY - 10, railRight - pad, 20};
    }
    y += rowH;
  }
  p.h = y + pad;
  if (m_cfg.showOpen) p.open = {p.w - pad - 26, pad, 26, 26};
  p.plate = {0, 0, p.w, p.h};
  p.radius = 22;
  return p;
}

void NowPlayingWidget::layoutTargets(const Places& p, bool seekable) {
  m_targets.clear();
  if (p.playR > 0) {
    m_targets.push_back({"next", {p.nextX - p.sideR, p.ctrlY - p.sideR, p.sideR * 2, p.sideR * 2}});
    m_targets.push_back({"play", {p.playX - p.playR, p.ctrlY - p.playR, p.playR * 2, p.playR * 2}});
    m_targets.push_back({"prev", {p.prevX - p.sideR, p.ctrlY - p.sideR, p.sideR * 2, p.sideR * 2}});
  }
  if (seekable && p.rail.w > 0) m_targets.push_back({"rail", p.rail});
  if (p.open.w > 0) m_targets.push_back({"open", p.open});
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

Color NowPlayingWidget::tint(Color col, float a) const { return withAlpha(col, a * m_paint.op); }

// ── the pieces ────────────────────────────────────────────────────────────

// The sleeve as a record: a black disc with its grooves, the artwork for a
// label, and marks that go round while the song plays -- the artwork itself
// cannot turn, so the grooves say that the disc does.
void NowPlayingWidget::drawRecord(Canvas& c, const MediaState& s, const Places& p) {
  const float cx = p.cover.x + p.cover.w / 2, cy = p.cover.y + p.cover.h / 2, R = p.record, label = p.cover.w / 2;
  c.circle(cx, cy, R, tint(Color{0.06F, 0.06F, 0.07F, 1}));
  for (int i = 0; i < 6; ++i) c.circle(cx, cy, label + 7 + i * (R - label - 9) / 5, kClear, 1, tint(m_paint.ink, 0.06F));
  // the light running over the grooves, and the lands between them
  const auto turn = static_cast<float>(m_spin);
  c.arc(cx, cy, (R + label) / 2, R - label - 6, turn, turn + 0.55F, tint(m_paint.ink, 0.07F), false);
  c.arc(cx, cy, (R + label) / 2, R - label - 6, turn + 3.14159F, turn + 3.69F, tint(m_paint.ink, 0.05F), false);
  for (int i = 0; i < 3; ++i) {
    const float a = turn + static_cast<float>(i) * 2.0944F;
    const float s0 = std::sin(a), c0 = std::cos(a);
    c.segment(cx + s0 * (label + 5), cy - c0 * (label + 5), cx + s0 * (R - 4), cy - c0 * (R - 4), 1.2F,
              tint(m_paint.ink, 0.10F));
  }
  if (s.cover && s.coverW > 0) c.image(s.cover, s.coverW, s.coverH, p.cover.x, p.cover.y, p.cover.w, p.cover.h, label, m_paint.op);
  else c.circle(cx, cy, label, tint(m_noct.color("surface_variant", m_paint.surface)));
  c.circle(cx, cy, 4, tint(m_paint.surface, 0.9F));  // the spindle hole
}

void NowPlayingWidget::drawSleeve(Canvas& c, const MediaState& s, const Places& p) {
  if (p.record > 0) {
    drawRecord(c, s, p);
    return;
  }
  if (p.cover.w <= 0) return;
  const Rect& r = p.cover;
  const bool shaped = m_cfg.coverShape != "rounded" && m_shapeNow.size() == 128;
  if (s.cover && s.coverW > 0)
    c.image(s.cover, s.coverW, s.coverH, r.x, r.y, r.w, r.h, p.coverRadius, m_paint.op, 0, shaped ? m_shapeNow.data() : nullptr);
  else {
    c.roundRect(r.x, r.y, r.w, r.h, p.coverRadius, tint(m_noct.color("surface_variant", m_paint.surface)));
    noteGlyph(c, r.x + r.w / 2, r.y + r.h / 2, std::min(40.0F, r.w * 0.24F), tint(m_paint.dim));
  }
  // now-playing pulse on the sleeve, only while sound is moving
  if (!s.playing || !m_cfg.showPulse || r.w < 90) return;
  const float barW = 3, gap = 2.5F, maxH = 13, pw = 4 * barW + 3 * gap;
  const float px = r.x + 8, py = r.y + r.h - 8 - 20;
  c.roundRect(px, py, pw + 14, 20, 10, Color{0, 0, 0, 0.42F * m_paint.op});
  for (int i = 0; i < 4; ++i) {
    const double t = m_paint.now * 1000 - i * 85;
    const float lv = 0.28F + 0.72F * static_cast<float>(0.5 + 0.5 * std::sin(t / 1120.0 * 2 * std::numbers::pi * 1.7 + i));
    const float h = lv * maxH;
    c.roundRect(px + 7 + i * (barW + gap), py + 10 + maxH / 2 - h, barW, h, barW / 2, Color{1, 1, 1, m_paint.op});
  }
}

// the side area: this song's lyrics, or a live spectrum when there are none
void NowPlayingWidget::drawSide(Canvas& c, const MediaState& s, const Places& p) {
  if (p.side.w <= 0) return;
  const float sx = p.side.x, sy = p.side.y, sw = p.side.w, sh = p.side.h;
  const Color ink = m_paint.ink, dim = m_paint.dim;
  const double now = m_paint.now, pos = s.positionSec(now);
  bool drewLyrics = false;
  if (m_cfg.showLyrics && s.lyrics == MediaState::Lyrics::Synced && !s.lines.empty()) {
    int idx = -1;
    const int ms = static_cast<int>(pos * 1000) + 250;
    for (size_t i = 0; i < s.lines.size(); ++i)
      if (s.lines[i].ms <= ms) idx = static_cast<int>(i);
    // `focus` gives the sung line a real jump of size and weight and takes the
    // neighbours further back; `plain` keeps Ryoku's gentler step. One face
    // and two sizes either way, so nothing can come out uneven.
    const bool focus = m_cfg.lyricsStyle == "focus";
    const float base = 15, lift = focus ? 7 : 2, spacing = focus ? 8 : 6;
    auto styleFor = [&](int i) {
      TextStyle t{.family = FONT, .size = i == idx ? base + lift : base, .weight = i == idx ? (focus ? 700 : 600) : 500};
      t.maxWidth = sw;
      t.maxLines = i == idx && focus ? 4 : 3;  // the sung line is set larger: give it a line more
      return t;
    };
    auto textFor = [&](int i) {
      return s.lines[static_cast<size_t>(i)].text.empty() ? std::string("♪") : s.lines[static_cast<size_t>(i)].text;
    };
    // glide: when the sung line changes, start from where the column was
    if (idx != m_lyricIndex) {
      if (m_lyricIndex >= 0 && idx >= 0) {
        float delta = 0;
        const int a = std::min(idx, m_lyricIndex), b = std::max(idx, m_lyricIndex);
        for (int i = a; i < b && i < static_cast<int>(s.lines.size()); ++i) delta += measure(textFor(i), styleFor(i)).h + spacing;
        m_glide += idx > m_lyricIndex ? delta : -delta;
      }
      m_lyricIndex = idx;
    }
    c.clip(sx, sy, sw, sh);  // its own area: a line must not run under the corner button
    const int centre = std::max(idx, 0);
    const auto cz = measure(textFor(centre), styleFor(centre));
    const float yTop = sy + sh / 2 - cz.h / 2 + m_glide;
    // the sung line and its neighbours, fading with distance and, at the edges
    // of the column, with how much of the line is still inside it: a line
    // leaves whole instead of being sliced through the middle. The sung line
    // always leads, even when it wraps to more lines than the column holds.
    auto drawLine = [&](int i, float y) {
      const int d = std::abs(i - idx);
      const float near = focus ? 0.40F : 0.52F;  // the sung line leads further in focus
      float o = idx < 0 ? near : (d == 0 ? 1.0F : (d > 4 ? 0.0F : std::max(0.10F, near - (d - 1) * 0.14F)));
      if (i != idx) {
        // the first few pixels a line loses are the box's own padding; past
        // them it is gone, so the column never shows a line sliced in half
        const float h = measure(textFor(i), styleFor(i)).h;
        const float hidden = h - (std::min(y + h, sy + sh) - std::max(y, sy));
        o *= std::clamp(1 - hidden / 5, 0.0F, 1.0F);
      }
      if (o <= 0) return;
      c.text(textFor(i), styleFor(i), sx, y, i == idx ? tint(m_accent) : tint(ink), o);
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
    c.clip(sx, sy, sw, sh);
    c.text(s.plain, t, sx, sy, tint(ink), 0.62F);
    c.clip();
    drewLyrics = true;
  } else if (m_cfg.showLyrics && s.lyrics == MediaState::Lyrics::Searching) {
    TextStyle t{.family = FONT, .size = 15, .weight = 500};
    const std::string msg = m_paint.es ? "Buscando letra…" : "Looking for lyrics…";
    auto z = measure(msg, t);
    c.text(msg, t, sx + (sw - z.w) / 2, sy + (sh - z.h) / 2, tint(dim), 0.8F);
    drewLyrics = true;
  }
  if (drewLyrics || !m_cfg.showViz) return;
  const int n = static_cast<int>(m_viz.size());
  const float pitch = sw / n, barW = std::max(2.0F, pitch * 0.55F), maxH = sh * 0.8F, mid = sy + sh / 2;
  if (m_cfg.viz == "wave") {
    for (int i = 0; i + 1 < n; ++i) {
      const float x0 = sx + (i + 0.5F) * pitch, x1 = sx + (i + 1.5F) * pitch;
      const float a0 = std::max(barW / 2, m_viz[static_cast<size_t>(i)] * maxH / 2);
      const float a1 = std::max(barW / 2, m_viz[static_cast<size_t>(i + 1)] * maxH / 2);
      c.triangle(x0, mid - a0, x1, mid - a1, x1, mid + a1, tint(m_accent, 0.5F));
      c.triangle(x0, mid - a0, x1, mid + a1, x0, mid + a0, tint(m_accent, 0.5F));
    }
  } else {
    for (int i = 0; i < n; ++i) {
      const float h = std::max(barW, m_viz[static_cast<size_t>(i)] * maxH);
      c.roundRect(sx + i * pitch + (pitch - barW) / 2, mid - h / 2, barW, h, barW / 2, tint(m_accent, 0.85F));
    }
  }
}

// the title, the artist and the clock, laid out on what they really paint
void NowPlayingWidget::drawText(Canvas& c, const MediaState& s, const Places& p) {
  if (p.text.w <= 0) return;
  const bool seekable = s.present && !s.radio() && s.lengthUs > 0;
  const std::string st = seekable && p.clock ? stamp(s.positionSec(m_paint.now)) + " / " + stamp(s.lengthUs / 1e6) : "";
  const TextStyle stS = clockStyle();
  const auto stZ = measure(st, stS);
  // the clock shares the band only where the layout gave it no slot of its own
  const bool clockHere = !st.empty() && p.time.w <= 0;
  TextStyle tS = titleStyle();
  tS.maxWidth = p.text.w - (clockHere ? stZ.w + 12 : 0);
  tS.align = p.align;
  TextStyle aS = artistStyle();
  aS.maxWidth = tS.maxWidth;
  aS.align = p.align;
  // laid out on what the pair really paints, not on its boxes: the display
  // face leaves a third of its box empty above and below, which left the title
  // and the artist adrift from each other and from the band (the clock faces
  // had the same trouble)
  const auto tI = TextRenderer::measureInk(s.title, tS);
  const bool artist = m_cfg.showArtist && !s.artist.empty();
  const auto aI = artist ? TextRenderer::measureInk(s.artist, aS) : TextRenderer::Ink{};
  const float lead = kLead;  // between the title's ink and the artist's
  const float colH = tI.h + (artist ? lead + aI.h : 0);
  const float y = p.text.y + (p.text.h - colH) / 2;  // the top of the ink, not of the box
  // ranged on what each line paints, not on its box: a line set right has to
  // end at the edge by its last letter, not by its box's trailing air
  const float band = tS.maxWidth;  // what is left of the band once the clock has its share
  auto xOf = [&](const TextRenderer::Ink& i) {
    if (p.align == 1) return p.text.x + (band - i.w) / 2 - i.x;
    if (p.align == 2) return p.text.x + band - i.w - i.x;
    return p.text.x - i.x;
  };
  c.text(s.title, tS, xOf(tI), y - tI.y, tint(m_paint.ink));
  if (artist) c.text(s.artist, aS, xOf(aI), y + tI.h + lead - aI.y, tint(m_paint.dim));
  // the clock, in the slot this layout gives it or, with none, on the same
  // line as the artist (or as the title alone)
  if (!st.empty()) {
    const auto sI = TextRenderer::measureInk(st, stS);
    if (p.time.w > 0)
      c.text(st, stS, p.time.x + p.time.w - sI.w - sI.x, p.time.y + (p.time.h - sI.h) / 2 - sI.y, tint(m_paint.dim));
    else
      c.text(st, stS, p.text.x + p.text.w - stZ.w, y + colH - sI.h - sI.y, tint(m_paint.dim));
  }
}

// The text layout: only the track, as large as the box allows. The title and
// the artist keep their sizes' proportion and are scaled together until the
// pair meets the box one way or the other, measured on what they really paint.
// Stacked, a long title takes two lines when that lets it be clearly larger;
// on one line the artist follows the title after a dot, on the same baseline.
void NowPlayingWidget::drawTrackOnly(Canvas& c, const MediaState& s, const Places& p) {
  if (s.title.empty()) return;
  const bool artist = m_cfg.showArtist && !s.artist.empty();
  const float pad = m_cfg.plate == "none" ? 0.0F : std::min(p.w, p.h) * 0.14F;  // off a plate's rounded edge
  const float aw = std::max(1.0F, p.w - 2 * pad), ah = std::max(1.0F, p.h - 2 * pad);
  const int align = m_cfg.textAlign == "center" ? 1 : m_cfg.textAlign == "right" ? 2 : 0;
  constexpr float R = 64;  // the size it is measured at, before it is scaled to the box
  // the artist at half the title by default (the card's 0.625 let a short
  // title and its artist come out nearly alike); artist_size still sets it
  const float ratio = std::clamp(0.8F * artistStyle().size / titleStyle().size, 0.2F, 2.0F);
  TextStyle tS = titleStyle(), aS = artistStyle();
  tS.size = R;
  aS.size = R * ratio;
  tS.align = aS.align = align;
  using Ink = TextRenderer::Ink;
  // where a block of `bw` x `bh` starts in the box, by the alignment
  auto blockX = [&](float bw) { return pad + (align == 1 ? (aw - bw) / 2 : align == 2 ? aw - bw : 0); };
  auto blockY = [&](float bh) {
    return pad + (m_cfg.textValign == "top" ? 0 : m_cfg.textValign == "bottom" ? ah - bh : (ah - bh) / 2);
  };

  if (m_cfg.textForm == "line") {
    const std::string sep = "  ·  ";
    struct Run {
      const std::string* text;
      TextStyle st;
      Color col;
    };
    const std::string* sepText = &sep;
    std::vector<Run> runs = {{&s.title, tS, m_paint.ink}};
    if (artist) {
      runs.push_back({sepText, aS, withAlpha(m_paint.dim, 0.6F)});
      runs.push_back({&s.artist, aS, m_paint.dim});
    }
    // the runs side by side on one baseline: their painted extent around it
    struct Placed {
      float x, baseline;
      Ink ink;
    };
    auto lay = [&](std::vector<Placed>& out, float& left, float& right, float& top, float& bottom) {
      out.clear();
      float x = 0;
      left = top = 1e9F;
      right = bottom = -1e9F;
      for (const Run& r : runs) {
        float w, h, base;
        TextRenderer::measure(*r.text, r.st, w, h, base);
        const Ink ink = TextRenderer::measureInk(*r.text, r.st);
        out.push_back({x, base, ink});
        left = std::min(left, x + ink.x);
        right = std::max(right, x + ink.x + ink.w);
        top = std::min(top, ink.y - base);
        bottom = std::max(bottom, ink.y + ink.h - base);
        x += w;
      }
    };
    std::vector<Placed> placed;
    float l, r, t, b;
    lay(placed, l, r, t, b);
    const float k = 0.98F * std::min(aw / std::max(1.0F, r - l), ah / std::max(1.0F, b - t));
    for (Run& run : runs) run.st.size *= k;
    lay(placed, l, r, t, b);  // again at the size drawn, which does not scale quite linearly
    const float x0 = blockX(r - l) - l, base = blockY(b - t) - t;
    for (size_t i = 0; i < runs.size(); ++i)
      c.text(*runs[i].text, runs[i].st, x0 + placed[i].x, base - placed[i].baseline, tint(runs[i].col));
    return;
  }

  // stacked: the title (on one line or two) over the artist
  const float leadR = R * 0.14F;
  const Ink aR = artist ? TextRenderer::measureInk(s.artist, aS) : Ink{};
  auto scaleFor = [&](const TextStyle& ts) {
    const Ink tI = TextRenderer::measureInk(s.title, ts);
    const float bw = std::max(tI.w, aR.w), bh = tI.h + (artist ? leadR + aR.h : 0);
    return std::min(aw / std::max(1.0F, bw), ah / std::max(1.0F, bh));
  };
  float k = scaleFor(tS);
  if (s.title.find(' ') != std::string::npos) {
    // two lines, broken at the space that leaves the wider of them narrowest
    // (a word is never split, and nothing is left over for a third line)
    float wrap = 1e9F;
    for (size_t at = s.title.find(' '); at != std::string::npos; at = s.title.find(' ', at + 1)) {
      const float a = TextRenderer::measureInk(s.title.substr(0, at), tS).boxW;
      const float b = TextRenderer::measureInk(s.title.substr(at + 1), tS).boxW;
      wrap = std::min(wrap, std::max(a, b) * 1.02F + 1);
    }
    TextStyle two = tS;
    two.maxWidth = wrap;
    two.maxLines = 2;
    const float k2 = scaleFor(two);
    if (k2 > k * 1.15F) {
      tS = two;
      k = k2;
    }
  }
  k *= 0.98F;
  tS.size *= k;
  tS.maxWidth *= k;
  aS.size *= k;
  const float lead = leadR * k;
  const Ink tI = TextRenderer::measureInk(s.title, tS);
  const Ink aI = artist ? TextRenderer::measureInk(s.artist, aS) : Ink{};
  const float bw = std::max(tI.w, aI.w), bh = tI.h + (artist ? lead + aI.h : 0);
  const float bx = blockX(bw), by = blockY(bh);
  // each line ranged in the block by what it paints
  auto lineX = [&](const Ink& i) { return bx + (align == 1 ? (bw - i.w) / 2 : align == 2 ? bw - i.w : 0) - i.x; };
  c.text(s.title, tS, lineX(tI), by - tI.y, tint(m_paint.ink));
  if (artist) c.text(s.artist, aS, lineX(aI), by + tI.h + lead - aI.y, tint(m_paint.dim));
}

void NowPlayingWidget::drawRail(Canvas& c, const MediaState& s, const Places& p) {
  if (p.rail.w <= 0) return;
  const float x0 = p.rail.x, x1 = p.rail.x + p.rail.w, cy = p.rail.y + p.rail.h / 2;
  const bool seekable = s.present && !s.radio() && s.lengthUs > 0;
  if (!seekable) {
    if (!s.radio()) return;
    const TextStyle lS{.family = MONO, .size = 11, .weight = 600, .letterSpacing = 1.1F};
    c.text(m_paint.es ? "En vivo" : "Live", lS, x0, cy - 8, tint(m_paint.dim));
    return;
  }
  const float frac = std::clamp(static_cast<float>(s.positionSec(m_paint.now) / (s.lengthUs / 1e6)), 0.0F, 1.0F);
  const Color spent = tint(m_accent), left = tint(m_paint.ink, 0.16F);
  const std::string& look = m_cfg.railStyle;
  // A ring around the sleeve instead of a rail across the card. It only suits
  // a round sleeve -- the record, or a cover cut to a circle -- since around a
  // square one it would cut the corners off; anywhere else it keeps to the rail.
  const bool roundSleeve = p.record > 0 || (p.cover.w > 0 && (m_cfg.coverShape == "circle" || m_cfg.coverShape == "oval"));
  if (look == "ring" && roundSleeve) {
    const float r = (p.record > 0 ? p.record : p.cover.w / 2) + 8;
    const float ccx = p.cover.x + p.cover.w / 2, ccy = p.cover.y + p.cover.h / 2;
    const float w = m_hover == "rail" ? 4.4F : 3.2F;
    c.arc(ccx, ccy, r, w, 0, 6.2831F, left, false);
    if (frac > 0.002F) c.arc(ccx, ccy, r, w, 0, frac * 6.2831F, spent);
    return;
  }
  const float px = x0 + (x1 - x0) * frac;
  if (look == "dots" || look == "bars") {
    const int n = std::max(6, static_cast<int>((x1 - x0) / 11));
    const float pitch = (x1 - x0) / n;
    for (int i = 0; i < n; ++i) {
      const float cx = x0 + (i + 0.5F) * pitch, done = (i + 0.5F) / n <= frac ? 1.0F : 0.0F;
      const Color col = done > 0 ? spent : left;
      if (look == "dots") c.circle(cx, cy, 1.9F, col);
      else {
        // the played bars stand taller, the way a meter fills
        const float h = done > 0 ? 11 : 5;
        c.roundRect(cx - 1.3F, cy - h / 2, 2.6F, h, 1.3F, col);
      }
    }
  } else if (look == "line") {
    c.segment(x0, cy, x1, cy, 3, left);
    if (frac > 0.002F) c.segment(x0, cy, px, cy, 3, spent);
  } else {
    c.segment(x0, cy, x1, cy, 2.2F, left);
    // the played part: a travelling wave in the sleeve's colour
    const float phase = s.playing ? static_cast<float>(std::fmod(m_paint.now, 1.1) / 1.1 * 15.0) : 0.0F;
    c.wave(x0, px, cy, 3, 15, phase, 2.2F, spent);
  }
  const float hs = m_hover == "rail" ? 1.35F : 1.0F;
  if (s.canSeek) c.roundRect(px - 1.5F * hs, cy - 6 * hs, 3 * hs, 12 * hs, 1.5F * hs, spent);
}

void NowPlayingWidget::drawTransport(Canvas& c, const MediaState& s, const Places& p) {
  if (p.playR <= 0) return;
  const float cy = p.ctrlY, k = p.playR / 19;  // the sheet's size is the measure
  auto moveBg = [&](const char* id, float x, float r, bool filled, bool enabled) {
    const bool hot = m_hover == id && enabled;
    if (filled) c.circle(x, cy, r, tint(m_accent, enabled ? 1.0F : 0.4F));
    else if (hot) c.circle(x, cy, r, tint(m_paint.ink, 0.12F));
  };
  moveBg("prev", p.prevX, p.sideR, false, s.canPrev);
  moveBg("play", p.playX, p.playR, true, s.canToggle);
  moveBg("next", p.nextX, p.sideR, false, s.canNext);
  const Color onAccent = tint(m_paint.surface), glyph = tint(m_paint.ink);
  // previous: bar + left triangle
  c.roundRect(p.prevX - 5.5F * k, cy - 5 * k, 2 * k, 10 * k, k, withAlpha(glyph, s.canPrev ? 1 : 0.4F));
  c.triangle(p.prevX + 5 * k, cy - 5 * k, p.prevX + 5 * k, cy + 5 * k, p.prevX - 3 * k, cy, withAlpha(glyph, s.canPrev ? 1 : 0.4F));
  // next: right triangle + bar
  c.triangle(p.nextX - 5 * k, cy - 5 * k, p.nextX - 5 * k, cy + 5 * k, p.nextX + 3 * k, cy, withAlpha(glyph, s.canNext ? 1 : 0.4F));
  c.roundRect(p.nextX + 3.5F * k, cy - 5 * k, 2 * k, 10 * k, k, withAlpha(glyph, s.canNext ? 1 : 0.4F));
  // play / pause on the filled tile
  if (s.playing) {
    c.roundRect(p.playX - 5 * k, cy - 6.5F * k, 3.5F * k, 13 * k, 1.2F * k, onAccent);
    c.roundRect(p.playX + 1.5F * k, cy - 6.5F * k, 3.5F * k, 13 * k, 1.2F * k, onAccent);
  } else {
    c.triangle(p.playX - 4 * k, cy - 7 * k, p.playX - 4 * k, cy + 7 * k, p.playX + 7 * k, cy, onAccent);
  }
}

void NowPlayingWidget::draw(const DrawContext& ctx) {
  m_media = ctx.media;
  static const MediaState kEmpty;
  const MediaState& s = m_media ? m_media->state() : kEmpty;
  m_boxW = std::max(1.0F, ctx.w);
  m_boxH = std::max(1.0F, ctx.h);
  const Places p = places();

  m_accent = (m_cfg.accentSource == "album" && s.hasAccent) ? s.accent : m_theme;
  m_paint.ink = m_ink;
  m_paint.dim = withAlpha(m_ink, 0.7F);
  m_paint.surface = m_noct.color("surface", Color::fromHex("#0b0b0c"));
  m_paint.op = static_cast<float>(m_cfg.opacity);
  m_paint.now = ctx.now > 0 ? ctx.now : m_now;
  m_paint.es = spanish();

  m_k = std::min(ctx.w / p.w, ctx.h / p.h);
  m_ox = (ctx.w - p.w * m_k) / 2;
  m_oy = (ctx.h - p.h * m_k) / 2;
  Canvas& c = m_canvas;
  c.begin(ctx.w, ctx.h, ctx.scale, ctx.text);
  c.setTransform(m_k, m_ox, m_oy);

  // ── the plate ──
  if (p.coverPlate) {
    // the artwork is the card; a veil at its foot keeps the track readable
    if (s.present && s.cover && s.coverW > 0) {
      c.image(s.cover, s.coverW, s.coverH, p.plate.x, p.plate.y, p.plate.w, p.plate.h, p.radius, m_paint.op);
    } else {
      c.roundRect(p.plate.x, p.plate.y, p.plate.w, p.plate.h, p.radius, tint(m_paint.surface.mix(m_accent, 0.10F), 0.85F));
      // with no artwork the card is all plate, so it says so where the
      // artwork would have been
      const float cy = p.plate.y + (p.text.h > 0 ? (p.text.y - p.plate.y) / 2 : p.plate.h / 2);
      noteGlyph(c, p.plate.x + p.plate.w / 2, cy, 44, tint(m_paint.dim, 0.7F));
    }
    // the veil, built from bands that each reach the foot of the card: the
    // faintest ones lie on top, so where their rounded corners show they are
    // barely there at all. It has to hold against artwork of any brightness,
    // so it closes up hard at the foot where the track sits.
    const float top = p.plate.y + p.plate.h * 0.40F, bottom = p.plate.y + p.plate.h;
    for (int i = 0; i < 18; ++i) {
      const float t = static_cast<float>(i) / 17;
      const float y0 = top + (bottom - top) * static_cast<float>(i) / 18;
      c.roundRect(p.plate.x, y0, p.plate.w, bottom - y0, p.radius, Color{0, 0, 0, (0.012F + 0.125F * t * t) * m_paint.op});
    }
    c.roundRect(p.plate.x, p.plate.y, p.plate.w, p.plate.h, p.radius, kClear, 1,
                tint(m_paint.ink, m_hover.empty() ? 0.08F : 0.16F));
  } else if (m_cfg.plate == "cover") {
    // the sleeve, blurred, under a near-opaque plate tinted a tenth toward it
    if (s.present && s.cover && s.coverW > 0)
      c.image(s.cover, s.coverW, s.coverH, p.plate.x, p.plate.y, p.plate.w, p.plate.h, p.radius, 0.55F * m_paint.op, 28);
    const Color plate = m_paint.surface.mix(m_accent, 0.10F);
    c.roundRect(p.plate.x, p.plate.y, p.plate.w, p.plate.h, p.radius, tint(plate, 0.80F),
                1, tint(m_paint.ink, m_hover.empty() ? 0.08F : 0.16F));
  } else if (m_cfg.plate == "glass") {
    c.roundRect(p.plate.x, p.plate.y, p.plate.w, p.plate.h, p.radius, tint(m_paint.surface, 0.35F), 1, tint(m_paint.ink, 0.12F));
  }

  const bool seekable = s.present && !s.radio() && s.lengthUs > 0;
  layoutTargets(p, seekable);

  // corner button: opens the music app
  if (p.open.w > 0) {
    const float cx = p.open.x + p.open.w / 2, cy = p.open.y + p.open.h / 2;
    if (m_hover == "open") c.circle(cx, cy, p.open.w / 2, tint(m_paint.ink, 0.12F));
    noteGlyph(c, cx, cy, 14, tint(m_paint.ink, m_hover.empty() ? 0.5F : 1.0F));
  }

  if (!s.present) {
    noteGlyph(c, p.w / 2, p.h / 2 - 14, 40, tint(m_paint.dim, 0.7F));
    const TextStyle ts{.family = FONT, .size = 13, .weight = 500};
    const std::string msg = m_paint.es ? "No suena nada" : "Nothing playing";
    auto z = measure(msg, ts);
    c.text(msg, ts, (p.w - z.w) / 2, p.h / 2 + 18, tint(m_paint.dim));
    return;
  }

  if (m_cfg.layout == "text") {
    drawTrackOnly(c, s, p);
    return;
  }
  drawSleeve(c, s, p);
  drawSide(c, s, p);
  drawText(c, s, p);
  drawRail(c, s, p);
  drawTransport(c, s, p);
  m_wasPlaying = s.playing;
}

}  // namespace undershell
