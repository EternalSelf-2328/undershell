// SPDX-License-Identifier: GPL-3.0-or-later
// The text widget (see textwidget.hpp): its styles, its placeholders and how
// its letters move.
#include "textwidget.hpp"

#include "i18n.hpp"
#include "media.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

namespace undershell {

namespace {
template <typename T>
T get(const toml::table& t, std::string_view key, T def) {
  if (auto v = t[key].value<T>()) return *v;
  return def;
}

// the motion's reach, a share of the size: room kept round the text for it
float slackOf(const TextConfig& c) {
  float s = 0;
  if (c.animation == "wave") s = 0.12F;
  else if (c.animation == "bounce") s = 0.26F;
  else if (c.animation == "glitch") s = 0.08F;
  else if (c.animation == "shake") s = 0.04F;
  else if (c.animation == "typewriter") s = 0.06F;
  if (c.react == "letters") s = std::max(s, 0.34F);
  if (c.react == "pulse") s = std::max(s, 0.08F);
  return s;
}

bool perLetter(const TextConfig& c) {
  return c.animation == "wave" || c.animation == "bounce" || c.animation == "typewriter" || c.animation == "glitch" ||
         c.animation == "flicker" || c.animation == "shake" || c.react == "letters";
}

// typewriter timing: letters a second, how long it holds once written
constexpr double kTypeRate = 11, kTypeHold = 2.6, kTypeFade = 0.45, kTypeRest = 0.6;
}  // namespace

const std::vector<std::string>& TextWidget::styles() {
  static const std::vector<std::string> all = {"plain", "neon", "retro", "comic", "outline", "sticker", "chrome", "glitch", "rainbow",
                                               "typewriter"};
  return all;
}

const std::map<std::string, std::string>& TextWidget::preset(const std::string& style) {
  // every style names every key it owns, so picking one starts afresh
  static const std::map<std::string, std::string> plain = {
      {"font", "\"Inter Display\""}, {"weight", "800"},        {"italic", "false"},          {"spacing", "0.0"},
      {"fill", "\"solid\""},         {"color", "\"on_surface\""}, {"color2", "\"primary\""},   {"angle", "90"},
      {"outline", "0.0"},            {"outline_color", "\"#111111\""}, {"shadow", "0.3"},      {"shadow_color", "\"#000000\""},
      {"glow", "0.0"},               {"glow_color", "\"primary\""}, {"extrude", "0.0"},        {"extrude_color", "\"#1a1020\""},
      {"extrude_angle", "135"},      {"animation", "\"none\""},
  };
  auto with = [&](std::initializer_list<std::pair<const std::string, std::string>> over) {
    auto m = plain;
    for (const auto& [k, v] : over) m[k] = v;
    return m;
  };
  static const std::map<std::string, std::map<std::string, std::string>> all = {
      {"plain", plain},
      {"neon", with({{"font", "\"Space Grotesk\""}, {"weight", "500"}, {"color", "\"#fff4fa\""}, {"shadow", "0.0"}, {"glow", "0.85"},
                     {"outline", "0.12"}, {"outline_color", "\"primary\""}, {"animation", "\"flicker\""}})},
      {"retro", with({{"weight", "900"}, {"italic", "true"}, {"fill", "\"gradient\""}, {"color", "\"tertiary\""}, {"color2", "\"primary\""},
                      {"outline", "0.16"}, {"outline_color", "\"#fff4e0\""}, {"shadow", "0.0"}, {"extrude", "0.55"}})},
      {"comic", with({{"font", "\"Bangers\""}, {"weight", "400"}, {"spacing", "0.03"}, {"color", "\"#ffd83d\""}, {"outline", "0.4"},
                      {"shadow", "0.0"}, {"extrude", "0.3"}, {"extrude_color", "\"#111111\""}, {"extrude_angle", "120"},
                      {"animation", "\"bounce\""}})},
      {"outline", with({{"weight", "900"}, {"fill", "\"none\""}, {"outline", "0.3"}, {"outline_color", "\"primary\""}, {"shadow", "0.0"}})},
      {"sticker", with({{"weight", "900"}, {"color", "\"primary\""}, {"outline", "0.7"}, {"outline_color", "\"#ffffff\""}, {"shadow", "0.45"}})},
      {"chrome", with({{"weight", "900"}, {"fill", "\"gradient\""}, {"color", "\"#ffffff\""}, {"color2", "\"#5d6470\""}, {"outline", "0.12"},
                       {"outline_color", "\"#1b1d22\""}, {"extrude", "0.25"}, {"extrude_color", "\"#3a3f48\""}})},
      {"glitch", with({{"font", "\"JetBrains Mono\""}, {"weight", "700"}, {"shadow", "0.0"}, {"animation", "\"glitch\""}})},
      {"rainbow", with({{"weight", "900"}, {"fill", "\"rainbow\""}, {"shadow", "0.35"}, {"animation", "\"wave\""}})},
      {"typewriter", with({{"font", "\"JetBrains Mono\""}, {"weight", "600"}, {"shadow", "0.2"}, {"animation", "\"typewriter\""}})},
  };
  auto it = all.find(style);
  return it != all.end() ? it->second : plain;
}

TextConfig TextConfig::fromTable(const toml::table& given) {
  // the style's keys under the ones the block sets itself
  toml::table t = given;
  const std::string style = given["style"].value_or(std::string("plain"));
  for (const auto& [k, v] : TextWidget::preset(style)) {
    if (t.contains(k)) continue;
    try {
      auto parsed = toml::parse("v = " + v);
      if (auto* n = parsed.get("v")) t.insert(k, *n);
    } catch (const std::exception&) {
    }
  }
  TextConfig c;
  c.style = style;
  c.text = get<std::string>(t, "text", c.text);
  c.font = get<std::string>(t, "font", c.font);
  auto num = [&](const char* key, double def, double lo, double hi) {
    double v = def;
    if (auto d = t[key].value<double>()) v = *d;
    return std::clamp(v, lo, hi);
  };
  c.weight = static_cast<int>(std::lround(num("weight", c.weight, 100, 900)));
  c.italic = get<bool>(t, "italic", c.italic);
  c.spacing = num("spacing", c.spacing, -0.1, 0.6);
  c.align = get<std::string>(t, "align", c.align);
  c.fill = get<std::string>(t, "fill", c.fill);
  c.color = get<std::string>(t, "color", c.color);
  c.color2 = get<std::string>(t, "color2", c.color2);
  c.angle = num("angle", c.angle, 0, 360);
  c.outline = num("outline", c.outline, 0, 1);
  c.outlineColor = get<std::string>(t, "outline_color", c.outlineColor);
  c.shadow = num("shadow", c.shadow, 0, 1);
  c.shadowColor = get<std::string>(t, "shadow_color", c.shadowColor);
  c.glow = num("glow", c.glow, 0, 1);
  c.glowColor = get<std::string>(t, "glow_color", c.glowColor);
  c.extrude = num("extrude", c.extrude, 0, 1);
  c.extrudeColor = get<std::string>(t, "extrude_color", c.extrudeColor);
  c.extrudeAngle = num("extrude_angle", c.extrudeAngle, 0, 360);
  c.animation = get<std::string>(t, "animation", c.animation);
  c.speed = num("speed", c.speed, 0.1, 4);
  c.react = get<std::string>(t, "react", c.react);
  c.opacity = num("opacity", c.opacity, 0, 1);
  return c;
}

std::string TextWidget::expand(const std::string& text, const MediaState* media, bool* usesTime) {
  if (usesTime) *usesTime = false;
  if (text.find('{') == std::string::npos) return text;
  const bool es = spanishUi();
  const std::time_t tt = std::time(nullptr);
  std::tm tm{};
  localtime_r(&tt, &tm);
  static const char* daysEs[] = {"domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado"};
  static const char* daysEn[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char* monthsEs[] = {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"};
  static const char* monthsEn[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  std::string out;
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '{') {
      const size_t e = text.find('}', i);
      if (e != std::string::npos) {
        const std::string name = text.substr(i + 1, e - i - 1);
        bool known = true;
        if (name == "title") out += media && media->present ? media->title : "";
        else if (name == "artist") out += media && media->present ? media->artist : "";
        else if (name == "album") out += media && media->present ? media->album : "";
        else if (name == "time") {
          out += std::format("{:02}:{:02}", tm.tm_hour, tm.tm_min);
          if (usesTime) *usesTime = true;
        } else if (name == "date") {
          out += es ? std::format("{} {}", tm.tm_mday, monthsEs[tm.tm_mon]) : std::format("{} {}", monthsEn[tm.tm_mon], tm.tm_mday);
          if (usesTime) *usesTime = true;
        } else if (name == "day") {
          out += es ? daysEs[tm.tm_wday] : daysEn[tm.tm_wday];
          if (usesTime) *usesTime = true;
        } else {
          known = false;
        }
        if (known) {
          i = e + 1;
          continue;
        }
      }
    }
    out += text[i++];
  }
  return out;
}

void TextWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) { configure(TextConfig::fromTable(cfg.options), noct); }

TextFxStyle TextWidget::styleOf(const TextConfig& cfg, const NoctaliaState& noct, Color* accent) {
  TextFxStyle s;
  s.font = cfg.font;
  s.weight = cfg.weight;
  s.italic = cfg.italic;
  s.spacing = cfg.spacing;
  s.align = cfg.align == "left" ? 0 : cfg.align == "right" ? 2 : 1;
  s.fill = cfg.fill == "gradient" ? 1 : cfg.fill == "rainbow" ? 2 : cfg.fill == "none" ? 3 : 0;
  s.c1 = noct.color(cfg.color, Color::fromHex("#f4f1ea"));
  s.c2 = noct.color(cfg.color2, Color::fromHex("#e2342a"));
  s.angle = cfg.angle;
  s.outline = cfg.outline;
  s.outlineC = noct.color(cfg.outlineColor, Color::fromHex("#111111"));
  s.shadow = cfg.shadow;
  s.shadowC = noct.color(cfg.shadowColor, Color::fromHex("#000000"));
  s.glow = cfg.glow;
  s.glowC = noct.color(cfg.glowColor, Color::fromHex("#e2342a"));
  s.extrude = cfg.extrude;
  s.extrudeC = noct.color(cfg.extrudeColor, Color::fromHex("#1a1020"));
  s.extrudeAngle = cfg.extrudeAngle;
  if (cfg.react == "glow" && s.glow <= 0) s.glow = 0.001;  // a glow for the music to grow
  if (accent) {
    // a white or grey text would turn the editor grey: then the glow's, the outline's or the theme's colour
    auto vivid = [](Color c) { return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}) > 0.18F; };
    *accent = noct.color("primary", Color::fromHex("#e2342a"));
    for (const auto& [on, c] : {std::pair{s.glow > 0.01, s.glowC}, std::pair{s.outline > 0, s.outlineC}, std::pair{s.fill == 1, s.c2},
                                std::pair{s.fill == 0, s.c1}})
      if (on && vivid(c)) *accent = c;
  }
  return s;
}

void TextWidget::configure(const TextConfig& cfg, const NoctaliaState& noct) {
  const bool restart = cfg.animation != m_cfg.animation || cfg.text != m_cfg.text;
  m_cfg = cfg;
  m_style = styleOf(cfg, noct, &m_accent);
  m_fx.setStyle(m_style);
  const std::string& t = cfg.text;
  m_media = t.find("{title}") != std::string::npos || t.find("{artist}") != std::string::npos || t.find("{album}") != std::string::npos;
  if (restart) m_start = m_now;
}

void TextWidget::tick(const TickContext& ctx) {
  const double dt = ctx.dt;
  m_now = ctx.now;
  if (m_start == 0) m_start = ctx.now;
  m_shown = expand(m_cfg.text, ctx.media ? &ctx.media->state() : nullptr, &m_time);
  m_beat *= std::exp(-dt * 7);
  m_playing = !ctx.audio.silent;
  if (m_cfg.react != "none") {
    if (ctx.audio.kicks != m_kicks) {
      m_kicks = ctx.audio.kicks;
      m_beat = std::max(m_beat, std::clamp(0.55 + ctx.audio.kickStrength * 0.45, 0.55, 1.0));
    } else if (!ctx.audio.onsets && !ctx.audio.silent) {
      m_beat = std::max(m_beat, std::clamp(ctx.audio.energy * 1.4 - 0.3, 0.0, 1.0));
    }
    m_energy = ctx.audio.energy;
    if (ctx.audio.bands) m_bands = *ctx.audio.bands;
    if (ctx.audio.silent) std::fill(m_bands.begin(), m_bands.end(), 0.0F);
  }
}

bool TextWidget::animating(const TickContext& ctx) const {
  const std::string& a = m_cfg.animation;
  if (a == "wave" || a == "bounce" || a == "shake" || a == "flicker" || m_cfg.fill == "rainbow") return true;
  if (a == "glitch") return std::fmod((m_now - m_start) * m_cfg.speed, kGlitchEvery) < kGlitchBurst + 0.05;
  if (a == "typewriter") {
    // while it fades out; the letters themselves come at nextWakeup
    const size_t n = m_fx.units().size();
    const double cycle = n / kTypeRate + kTypeHold + kTypeFade + kTypeRest;
    const double t = std::fmod((m_now - m_start) * m_cfg.speed, cycle);
    if (t > n / kTypeRate + kTypeHold && t < n / kTypeRate + kTypeHold + kTypeFade) return true;
  }
  if (m_cfg.react != "none" && (!ctx.audio.silent || m_beat > 0.004)) return true;
  return false;
}

int TextWidget::fps() const {
  if (m_cfg.animation == "flicker" && m_cfg.react == "none" && m_cfg.fill != "rainbow") return 12;
  if (m_cfg.animation == "shake" && m_cfg.react == "none") return 30;
  return 60;
}

double TextWidget::nextWakeup(double now) const {
  double next = 1e18;
  if (m_time) {
    const auto wall = std::chrono::system_clock::now().time_since_epoch();
    const double s = std::chrono::duration<double>(wall).count();
    next = now + (60 - std::fmod(s, 60)) + 0.02;
  }
  const double t = (now - m_start) * m_cfg.speed;
  if (m_cfg.animation == "glitch") {
    const double into = std::fmod(t, kGlitchEvery);
    if (into >= kGlitchBurst) next = std::min(next, now + (kGlitchEvery - into) / m_cfg.speed);
  } else if (m_cfg.animation == "typewriter") {
    const size_t n = m_fx.units().size();
    const double cycle = n / kTypeRate + kTypeHold + kTypeFade + kTypeRest;
    const double into = std::fmod(t, cycle);
    double step;
    if (into < n / kTypeRate) step = 1 / kTypeRate - std::fmod(into, 1 / kTypeRate);  // the next letter
    else if (into < n / kTypeRate + kTypeHold) step = std::min(0.5 - std::fmod(into, 0.5), n / kTypeRate + kTypeHold - into);  // the caret blinks
    else step = cycle - into;
    next = std::min(next, now + std::max(0.01, step / m_cfg.speed));
  }
  return next;
}

// where every unit is this frame
void TextWidget::move(double now, size_t n) {
  m_motion.assign(n, TextFxMotion{});
  const double t = (now - m_start) * m_cfg.speed;
  const auto& units = m_fx.units();
  animateUnits(m_cfg.animation, t, units, m_fx.size(), m_motion);
  if (m_cfg.animation == "typewriter") {
    const double cycle = n / kTypeRate + kTypeHold + kTypeFade + kTypeRest;
    const double into = std::fmod(t, cycle);
    const auto written = static_cast<size_t>(into * kTypeRate);
    const double fadeAt = n / kTypeRate + kTypeHold;
    for (size_t i = 0; i < n; ++i) {
      m_motion[i].shown = i < written;
      if (into > fadeAt) m_motion[i].alpha = static_cast<float>(std::max(0.0, 1 - (into - fadeAt) / kTypeFade));
    }
  }
  reactUnits(m_cfg.react, m_beat, m_bands, m_fx, m_motion);
}

void TextWidget::draw(const DrawContext& ctx) {
  // tick() fills m_shown; the first draw may come before it
  if (m_shown.empty() && !m_cfg.text.empty()) m_shown = expand(m_cfg.text, ctx.media ? &ctx.media->state() : nullptr, &m_time);
  m_fx.layout(m_shown, m_style, ctx.w, ctx.h, ctx.scale, slackOf(m_cfg), perLetter(m_cfg));
  const size_t n = m_fx.units().size();
  move(m_now, n);
  const auto hue = static_cast<float>(std::fmod((m_now - m_start) * 0.12 * m_cfg.speed, 1.0));
  float fade = 1;
  m_fx.draw(ctx, m_motion, hue, static_cast<float>(m_cfg.opacity) * fade);
  if (m_cfg.animation == "typewriter" && n > 0) {
    // the caret after the last letter written, blinking while it waits
    const double t = (m_now - m_start) * m_cfg.speed;
    const double cycle = n / kTypeRate + kTypeHold + kTypeFade + kTypeRest;
    const double into = std::fmod(t, cycle);
    const size_t written = std::min(n, static_cast<size_t>(into * kTypeRate));
    const bool typing = into < n / kTypeRate;
    const bool on = typing || std::fmod(into, 1.0) < 0.5;
    if (on && into < n / kTypeRate + kTypeHold) {
      const auto& u = m_fx.units()[written > 0 ? written - 1 : 0];
      const float S = m_fx.size();
      const float x = written > 0 ? u.x + u.w + S * 0.04F : u.x;
      Color c = m_style.fill == 3 ? m_style.outlineC : m_style.c1;
      c.a = static_cast<float>(m_cfg.opacity);
      m_fx.bar(ctx, x, u.y + u.h * 0.12F, std::max(1.5F, S * 0.07F), u.h * 0.76F, c);
    }
  }
  if (ctx.text) m_fx.collect();
}

}  // namespace undershell
