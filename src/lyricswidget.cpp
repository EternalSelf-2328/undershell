// SPDX-License-Identifier: GPL-3.0-or-later
// The lyrics widget (see lyricswidget.hpp).
#include "lyricswidget.hpp"

#include "i18n.hpp"

#include <algorithm>
#include <cmath>

namespace undershell {

namespace {
template <typename T>
T get(const toml::table& t, std::string_view key, T def) {
  if (auto v = t[key].value<T>()) return *v;
  return def;
}

constexpr double kChange = 0.42;      // seconds a change of line takes
constexpr double kSampleStep = 2.6;   // the sample lines' pace
constexpr double kLastLine = 4.0;     // how long the last line counts as sung (karaoke)

double ease(double t) {
  t = std::clamp(t, 0.0, 1.0);
  return 1 - std::pow(1 - t, 3);
}

bool movesLetters(const TextConfig& c) {
  return c.animation == "wave" || c.animation == "bounce" || c.animation == "glitch" || c.animation == "flicker" ||
         c.animation == "shake" || c.react == "letters";
}
}  // namespace

LyricsConfig LyricsConfig::fromTable(const toml::table& t) {
  LyricsConfig c;
  c.look = TextConfig::fromTable(t);
  c.layout = get<std::string>(t, "layout", c.layout);
  c.transition = get<std::string>(t, "transition", c.transition);
  c.fallback = get<std::string>(t, "fallback", c.fallback);
  if (auto d = t["offset"].value<double>()) c.offset = std::clamp(*d, -5.0, 5.0);
  return c;
}

void LyricsWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) { configure(LyricsConfig::fromTable(cfg.options), noct); }

void LyricsWidget::configure(const LyricsConfig& cfg, const NoctaliaState& noct) {
  m_cfg = cfg;
  m_style = TextWidget::styleOf(cfg.look, noct, &m_accent);
  for (auto& [i, fx] : m_fx) fx->setStyle(m_style);
}

std::string LyricsWidget::lineText(int i) const {
  if (m_mode == Mode::Single) return i == 0 ? m_single : std::string();
  if (i < 0 || i >= static_cast<int>(m_lines.size())) return {};
  const std::string& t = m_lines[static_cast<size_t>(i)].text;
  return t.find_first_not_of(" \t") == std::string::npos ? std::string("♪") : t;  // an instrumental break
}

void LyricsWidget::tick(const TickContext& ctx) {
  const double dt = ctx.dt;
  m_now = ctx.now;
  // what there is to show
  Mode mode = Mode::None;
  if (!ctx.media) {
    mode = Mode::Sample;  // drawn without a desktop (snapshots, tests)
  } else {
    const MediaState& st = ctx.media->state();
    m_playing = st.present && st.playing;
    if (st.present && st.lyrics == MediaState::Lyrics::Synced && !st.lines.empty()) {
      mode = Mode::Synced;
      if (st.trackKey != m_track || m_lyricsState != st.lyrics || m_lines.size() != st.lines.size()) {
        m_lines = st.lines;
        m_track = st.trackKey;
        m_fx.clear();
        m_idx = m_prev = -1;
      }
      m_pos = st.positionSec(ctx.now) - m_cfg.offset;
    } else if (st.present) {
      if (m_cfg.fallback == "title") {
        mode = Mode::Single;
        const std::string single = st.artist.empty() ? st.title : st.title + " — " + st.artist;
        if (single != m_single) m_fx.erase(0);
        m_single = single;
      } else if (m_cfg.fallback == "note") {
        mode = Mode::Single;
        if (m_single != "♪") m_fx.erase(0);
        m_single = "♪";
      }
    } else if (!ctx.audio.onsets && !ctx.audio.silent) {
      mode = Mode::Sample;  // the editor's demo: something to style
    }
    m_lyricsState = st.lyrics;
  }
  if (mode == Mode::Sample) {
    if (m_mode != Mode::Sample) {
      const bool es = spanishUi();
      const char* lines[] = {es ? "Esta es la letra" : "These are the lyrics", es ? "de la canción que suena" : "of the song that plays",
                             es ? "línea a línea, a tiempo" : "line by line, in time", "♪"};
      m_lines.clear();
      for (int k = 0; k < 4; ++k) m_lines.push_back({static_cast<int>(k * kSampleStep * 1000), lines[k]});
      m_fx.clear();
      m_idx = m_prev = -1;
      m_track.clear();
    }
    m_pos = std::fmod(ctx.now, kSampleStep * 4);
    m_playing = true;
  }
  if (mode != m_mode) {
    m_mode = mode;
    m_fx.clear();
    m_idx = m_prev = -1;
  }
  // the line being sung
  int idx = -1;
  if (m_mode == Mode::Single) idx = 0;
  if (m_mode == Mode::Synced || m_mode == Mode::Sample) {
    const auto ms = static_cast<int>(std::floor(m_pos * 1000));
    auto it = std::upper_bound(m_lines.begin(), m_lines.end(), ms, [](int v, const LyricLine& l) { return v < l.ms; });
    idx = static_cast<int>(it - m_lines.begin()) - 1;
  }
  if (idx != m_idx) {
    m_prev = m_idx;
    m_idx = idx;
    m_changedAt = ctx.now;
    // keep only the lines round this one
    for (auto it = m_fx.begin(); it != m_fx.end();)
      it = std::abs(it->first - idx) > 3 ? m_fx.erase(it) : std::next(it);
  }
  // the music
  m_beat *= std::exp(-dt * 7);
  if (m_cfg.look.react != "none") {
    if (ctx.audio.kicks != m_kicks) {
      m_kicks = ctx.audio.kicks;
      m_beat = std::max(m_beat, std::clamp(0.55 + ctx.audio.kickStrength * 0.45, 0.55, 1.0));
    } else if (!ctx.audio.onsets && !ctx.audio.silent) {
      m_beat = std::max(m_beat, std::clamp(ctx.audio.energy * 1.4 - 0.3, 0.0, 1.0));
    }
    if (ctx.audio.bands) m_bands = *ctx.audio.bands;
    if (ctx.audio.silent) std::fill(m_bands.begin(), m_bands.end(), 0.0F);
  }
}

bool LyricsWidget::animating(const TickContext& ctx) const {
  if (m_mode == Mode::None) return false;
  if (m_now - m_changedAt < kChange + 0.05) return true;
  if (m_cfg.layout == "karaoke" && m_playing && m_mode != Mode::Single) return true;
  const auto& a = m_cfg.look.animation;
  if (a == "wave" || a == "bounce" || a == "shake" || a == "flicker" || m_cfg.look.fill == "rainbow") return true;
  if (a == "glitch") return std::fmod(m_now * m_cfg.look.speed, kGlitchEvery) < kGlitchBurst + 0.05;
  if (m_cfg.look.react != "none" && (!ctx.audio.silent || m_beat > 0.004)) return true;
  return false;
}

int LyricsWidget::fps() const {
  const auto& a = m_cfg.look.animation;
  const bool letters = a == "wave" || a == "bounce" || m_cfg.look.react != "none" || m_cfg.look.fill == "rainbow";
  if (!letters && m_now - m_changedAt > kChange + 0.05) {
    if (a == "flicker") return 12;
    if (m_cfg.layout == "karaoke") return 30;  // the wipe
  }
  return 60;
}

double LyricsWidget::nextWakeup(double now) const {
  double next = 1e18;
  if ((m_mode == Mode::Synced || m_mode == Mode::Sample) && m_playing) {
    // the next line
    const size_t n = static_cast<size_t>(m_idx + 1);
    if (n < m_lines.size()) next = now + std::max(0.01, m_lines[n].ms / 1000.0 - m_pos);
    else if (m_mode == Mode::Sample) next = now + std::max(0.01, kSampleStep * 4 - m_pos);
  }
  if (m_cfg.look.animation == "glitch") {
    const double into = std::fmod(now * m_cfg.look.speed, kGlitchEvery);
    if (into >= kGlitchBurst) next = std::min(next, now + (kGlitchEvery - into) / m_cfg.look.speed);
  }
  return next;
}

TextFx& LyricsWidget::fxFor(int i) {
  auto& p = m_fx[i];
  if (!p) {
    p = std::make_unique<TextFx>();
    p->setStyle(m_style);
  }
  return *p;
}

// One line, `dy` from the middle of the box and grown by `scale` about it;
// `reveal` (0..1) types it out, `wipe` lights it up as it is sung.
void LyricsWidget::drawLine(const DrawContext& ctx, int i, float dy, float scale, float alpha, bool current, float reveal, float wipe) {
  const std::string text = lineText(i);
  if (text.empty() || alpha <= 0.003F) return;
  const TextConfig& look = m_cfg.look;
  const bool letters = current && (movesLetters(look) || reveal < 1);
  const int maxLines = m_cfg.layout == "lines" && !current ? 1 : 2;
  TextFx& fx = fxFor(i);
  // the box's width wraps it; the height is what the layout gives a line
  fx.layout(text, m_style, ctx.w, ctx.h, ctx.scale, 0.04F, letters, m_size, maxLines);
  const size_t n = fx.units().size();
  m_motion.assign(n, TextFxMotion{});
  if (current) {
    const double t = m_now * look.speed;
    animateUnits(look.animation, t, fx.units(), fx.size(), m_motion);
    reactUnits(look.react, m_beat, m_bands, fx, m_motion);
    if (reveal < 1) {
      const auto shown = static_cast<size_t>(std::ceil(reveal * static_cast<double>(n)));
      for (size_t k = 0; k < n; ++k) m_motion[k].shown = k < shown;
    }
  }
  if (scale != 1) scaleUnits(fx, scale, m_motion);
  for (auto& m : m_motion) {
    m.dy += dy;
    m.alpha *= alpha;
  }
  const auto hue = static_cast<float>(std::fmod(m_now * 0.12 * look.speed, 1.0));
  fx.draw(ctx, m_motion, hue, static_cast<float>(look.opacity), wipe);
}

void LyricsWidget::draw(const DrawContext& ctx) {
  if (m_mode == Mode::None || m_idx < 0) {
    // before the first line: in three lines, the first one waits below
    if (m_mode == Mode::None || m_cfg.layout != "lines" || m_lines.empty()) return;
  }
  for (auto& [i, fx] : m_fx) fx->collect();
  const float H = ctx.h;
  const double e = ease((m_now - m_changedAt) / kChange);
  const bool step = m_prev >= 0 && std::abs(m_idx - m_prev) == 1;  // a seek jumps, it does not scroll
  if (m_cfg.layout == "lines" && m_mode != Mode::Single) {
    // three lines that scroll: the one being sung in the middle, the others smaller and faint
    m_size = std::min(H / 5.2F, ctx.w / 9.0F);
    const float D = m_size * 1.62F;
    for (int k = m_idx - 2; k <= m_idx + 2; ++k) {
      double off = k - m_idx;
      if (step && e < 1) off += (1 - e) * (m_idx - m_prev);
      const double d = std::abs(off);
      if (d >= 2) continue;
      const auto scale = static_cast<float>(1 - 0.36 * std::min(d, 1.0));
      const auto alpha = static_cast<float>(d <= 1 ? 1 - 0.58 * d : 0.42 * (2 - d));
      drawLine(ctx, k, static_cast<float>(off) * D, scale, alpha, k == m_idx, 1, -1);
    }
    return;
  }
  // one line at a time
  m_size = std::min(H / 3.0F, ctx.w / 6.5F);  // two wrapped lines and their outline fit
  const bool karaoke = m_cfg.layout == "karaoke" && m_mode != Mode::Single;
  const std::string tr = karaoke ? std::string("fade") : m_cfg.transition;
  const bool changing = e < 1 && m_prev >= 0;
  // the old line is mostly gone before the new one is mostly there, so two
  // wrapped lines never sit on each other
  const double t = std::clamp((m_now - m_changedAt) / kChange, 0.0, 1.0);
  auto smooth = [](double a, double b, double x) {
    const double k = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return k * k * (3 - 2 * k);
  };
  if (changing) {
    // the line leaving
    float dy = 0, scale = 1;
    if (tr == "slide") dy = static_cast<float>(-e) * m_size * 1.3F;
    if (tr == "pop") scale = static_cast<float>(1 + 0.15 * e);
    drawLine(ctx, m_prev, dy, scale, static_cast<float>(1 - smooth(0, 0.55, t)), false, 1, -1);
  }
  float dy = 0, scale = 1, alpha = 1, reveal = 1;
  if (changing || m_prev < 0) {
    const double in = smooth(0.3, 1, t);
    if (tr == "slide") dy = static_cast<float>(1 - ease(in)) * m_size * 0.8F;
    if (tr == "pop") scale = static_cast<float>(0.8 + 0.2 * ease(in));
    if (tr == "typewriter") reveal = static_cast<float>(smooth(0.15, 1, t));
    else alpha = static_cast<float>(in);
  }
  float wipe = -1;
  if (karaoke && m_idx >= 0 && m_idx < static_cast<int>(m_lines.size())) {
    const double from = m_lines[static_cast<size_t>(m_idx)].ms / 1000.0;
    const size_t nx = static_cast<size_t>(m_idx) + 1;
    const double to = nx < m_lines.size() ? m_lines[nx].ms / 1000.0 : from + kLastLine;
    // sung a little ahead of its time, so the light leads the voice
    wipe = static_cast<float>(std::clamp((m_pos - from) / std::max(0.2, (to - from) * 0.9), 0.0, 1.0));
  }
  drawLine(ctx, m_idx, dy, scale, alpha, true, reveal, wipe);
}

}  // namespace undershell
