// SPDX-License-Identifier: GPL-3.0-or-later
// Rain, snow or sakura petals falling through the box: more of them with the
// music, a gust on every kick, and (rain) a far-off lightning bolt on the
// hardest. With depth on, it falls behind the wallpaper's subject.
#include "looks.hpp"

#include "widget.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace undershell {

namespace {

class Rain final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    const std::string kind = t["rain_kind"].value_or(std::string("rain"));
    if (kind != m_kind) m_drops.clear();  // a new sky
    m_kind = kind;
    m_amount = std::clamp(t["rain_amount"].value_or(0.5), 0.0, 1.0);
    m_wind = std::clamp(t["rain_wind"].value_or(0.2), -1.0, 1.0);
    m_speed = std::clamp(t["rain_speed"].value_or(1.0), 0.3, 2.0);
    m_themed = t["rain_colors"].value_or(std::string("classic")) == "theme";
    m_lightning = t["rain_lightning"].value_or(false);
    m_hits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
    m_theme = noct.color("primary");
    m_preview = t["rain_preview"].value_or(-1.0);
    m_warmed = false;
  }

  void tick(const Beat& b) override {
    if (m_preview < 0) step(b);
  }

  void draw(const DrawContext& ctx, float opacity) override {
    m_W = ctx.w;
    m_H = ctx.h;
    if (m_preview >= 0 && !m_warmed) warmUp();
    const float ui = std::clamp(std::min(m_W, m_H) / 300.0F, 0.8F, 2.0F);
    StrokeRenderer::Look look;
    look.opacity = opacity;
    // the far bolt first, behind the rain
    if (m_bolt >= 0) {
      m_strokes.clear();
      const double a = m_bolt;
      const float I = static_cast<float>(0.55 * (std::exp(-a / 0.05) + (a > 0.08 ? 0.7 * std::exp(-(a - 0.08) / 0.05) : 0)));
      if (I > 0.02) {
        BoltShape s;
        s.rough = 0.18F;
        s.branches = 0.6F;
        s.width = 1.2F * ui;
        s.intensity = I;
        s.depth = 5;
        s.boundX0 = 0, s.boundY0 = 0, s.boundX1 = m_W, s.boundY1 = m_H;
        lightning(m_strokes, m_boltX0 * m_W, -2, m_boltX1 * m_W, m_H * 0.75F, s, m_boltSeed);
        look.mode = StrokeRenderer::Mode::Light;
        look.glow = 10 * ui;
        setColours(look, Color{0.95F, 0.96F, 1, 1}, Color{0.55F, 0.62F, 0.85F, 1});
        m_renderer.draw(m_strokes, m_W, m_H, look);
      }
    }
    look.mode = StrokeRenderer::Mode::Flat;
    if (m_kind == "petals") {
      // three pinks, one pass each
      const Color pinks[3] = {{1.0F, 0.72F, 0.80F, 1}, {1.0F, 0.82F, 0.87F, 1}, {0.97F, 0.60F, 0.72F, 1}};
      for (int tone = 0; tone < 3; ++tone) {
        m_strokes.clear();
        for (const Drop& d : m_drops) {
          if (d.tone != tone) continue;
          const float len = d.size * ui * (0.3F + 0.9F * std::abs(std::cos(d.rot * 1.3F))) + 1;  // it tumbles
          const float dx = std::cos(d.rot) * len / 2, dy = std::sin(d.rot) * len / 2;
          m_strokes.push_back({d.x - dx, d.y - dy, d.x + dx, d.y + dy, d.size * 0.8F * ui, d.alpha, d.size * 0.55F * ui});
        }
        const Color c = m_themed ? lighter(m_theme, 0.25F * tone) : pinks[tone];
        setColours(look, c, c);
        m_renderer.draw(m_strokes, m_W, m_H, look);
      }
      return;
    }
    m_strokes.clear();
    if (m_kind == "snow") {
      for (const Drop& d : m_drops) m_strokes.push_back({d.x, d.y, d.x, d.y, d.size * ui, d.alpha});
    } else {
      for (const Drop& d : m_drops)  // a streak: where it fell from in the last 45 ms
        m_strokes.push_back({d.x - d.vx * 0.045F, d.y - d.vy * 0.045F, d.x, d.y, d.size * ui, d.alpha});
      for (const Splash& s : m_splashes) {
        const float k = 1 - s.age / 0.18F, l = 5 * ui * (0.4F + 0.6F * (1 - k));
        m_strokes.push_back({s.x, m_H - 1, s.x - l, m_H - 1 - l * 0.9F, 1.0F * ui, 0.45F * k});
        m_strokes.push_back({s.x, m_H - 1, s.x + l, m_H - 1 - l * 0.9F, 1.0F * ui, 0.45F * k});
      }
    }
    const Color base = m_kind == "snow" ? Color{1, 1, 1, 1} : Color{0.76F, 0.83F, 0.95F, 1};
    const Color c = m_themed ? lighter(m_theme, m_kind == "snow" ? 0.6F : 0.35F) : base;
    setColours(look, c, c);
    m_renderer.draw(m_strokes, m_W, m_H, look);
  }

  [[nodiscard]] bool visible() const override { return !m_drops.empty() || m_bolt >= 0; }
  [[nodiscard]] bool moving() const override { return !m_drops.empty() || m_bolt >= 0 || m_live; }

private:
  struct Drop {
    float x, y, vx, vy, size, alpha, phase, rot, spin;
    int tone;
  };
  struct Splash {
    float x, age;
  };

  float rnd() { return m_rng.next(); }

  Drop spawn(bool anywhere) {
    Drop d{};
    d.x = rnd() * (m_W + 80) - 40;
    d.y = anywhere ? rnd() * m_H : -rnd() * 30 - 4;
    d.phase = rnd() * 6.2831853F;
    d.rot = rnd() * 6.2831853F;
    d.spin = (rnd() < 0.5F ? -1 : 1) * (1.5F + 3 * rnd());
    d.tone = static_cast<int>(rnd() * 3) % 3;
    if (m_kind == "snow") {
      d.size = 1.8F + 3.4F * rnd() * rnd();
      d.alpha = 0.55F + 0.45F * rnd();
      d.vy = m_H * (0.07F + 0.15F * rnd()) * static_cast<float>(m_speed) * (0.6F + 0.15F * d.size);
    } else if (m_kind == "petals") {
      d.size = 4 + 3.5F * rnd();
      d.alpha = 0.85F + 0.15F * rnd();
      d.vy = m_H * (0.06F + 0.1F * rnd()) * static_cast<float>(m_speed);
    } else {
      d.size = 0.9F + 0.7F * rnd();
      d.alpha = 0.22F + 0.4F * rnd();
      d.vy = m_H * (1.7F + 0.9F * rnd()) * static_cast<float>(m_speed);
    }
    return d;
  }

  void step(const Beat& b) {
    const float dt = static_cast<float>(b.dt);
    m_live = b.live;
    m_time += b.dt;
    if (b.kick) m_gust = std::max(m_gust, b.strength * std::clamp(m_hits / 0.7, 0.0, 1.45));
    m_gust *= std::exp(-b.dt / 0.6);
    // how many: the amount, more with the music, and a burst with a gust
    const double cap = m_kind == "snow" ? 220 : m_kind == "petals" ? 90 : 420;
    const size_t want = static_cast<size_t>(cap * m_amount * (b.live ? 0.45 + 0.55 * b.energy : 0.25) + cap * 0.15 * m_gust);
    if (m_drops.empty())
      while (m_drops.size() < want) m_drops.push_back(spawn(true));  // the first fill starts mid-fall
    else
      for (int k = 0; k < 12 && m_drops.size() < want; ++k) m_drops.push_back(spawn(false));
    const float gust = static_cast<float>(m_gust), wind = static_cast<float>(m_wind);
    const float t = static_cast<float>(m_time);
    for (size_t i = 0; i < m_drops.size();) {
      Drop& d = m_drops[i];
      if (m_kind == "snow" || m_kind == "petals") {
        const float sway = m_kind == "petals" ? 0.09F : 0.05F;
        // a gust only nudges them (kicks come every half second or so; a
        // strong push would blow the whole sky out of the box)
        d.vx = m_H * (wind * 0.18F + sway * std::sin(t * (m_kind == "petals" ? 1.6F : 1.1F) + d.phase)) +
               (wind >= 0 ? 1 : -1) * gust * m_H * (m_kind == "petals" ? 0.18F : 0.1F);
        d.rot += d.spin * dt * (1 + 2 * gust);
        d.y += d.vy * (1 + 0.6F * gust) * dt;
      } else {
        d.vx = wind * d.vy * 0.35F * (1 + gust);
        d.y += d.vy * (1 + 0.4F * gust) * dt;
      }
      d.x += d.vx * dt;
      if (m_kind != "rain") {  // flakes and petals drift round the sides
        if (d.x < -40) d.x += m_W + 80;
        if (d.x > m_W + 40) d.x -= m_W + 80;
      }
      if (d.y > m_H + 6 || d.x < -60 || d.x > m_W + 60) {
        if (m_kind == "rain" && d.y > m_H && m_splashes.size() < 80 && rnd() < 0.5F) m_splashes.push_back({d.x, 0});
        if (m_drops.size() > want) {  // fewer now: let this one go
          d = m_drops.back();
          m_drops.pop_back();
          continue;
        }
        d = spawn(false);
      }
      ++i;
    }
    for (size_t i = 0; i < m_splashes.size();) {
      m_splashes[i].age += dt;
      if (m_splashes[i].age > 0.18F) {
        m_splashes[i] = m_splashes.back();
        m_splashes.pop_back();
      } else {
        ++i;
      }
    }
    // a far-off strike on the hardest kicks, not more than every 1.5 s
    if (m_bolt >= 0 && (m_bolt += b.dt) > 0.35) m_bolt = -1;
    if (m_lightning && m_kind == "rain" && b.kick && b.strength > 0.8 && m_time - m_boltAt > 1.5) {
      m_bolt = 0;
      m_boltAt = m_time;
      m_boltSeed = static_cast<uint32_t>(rnd() * 1e7F);
      m_boltX0 = 0.15F + 0.7F * rnd();
      m_boltX1 = std::clamp(m_boltX0 + (rnd() - 0.5F) * 0.4F, 0.05F, 0.95F);
    }
  }

  // previews and tests: three seconds of rain, a kick at the frozen moment
  void warmUp() {
    m_warmed = true;
    m_drops.clear();
    m_splashes.clear();
    m_rng = BoltRandom(99);
    const int frames = 180, kickAt = frames - static_cast<int>(m_preview * 60);
    for (int f = 0; f < frames; ++f) {
      Beat b;
      b.dt = 1.0 / 60;
      b.live = true;
      b.energy = 0.6;
      b.kick = f == kickAt;
      b.strength = 1;
      step(b);
    }
  }

  std::string m_kind = "rain";
  double m_amount = 0.5, m_wind = 0.2, m_speed = 1, m_hits = 0.7, m_preview = -1;
  bool m_themed = false, m_lightning = false, m_live = false, m_warmed = false;
  double m_time = 0, m_gust = 0, m_bolt = -1, m_boltAt = -10;
  uint32_t m_boltSeed = 1;
  float m_boltX0 = 0.5F, m_boltX1 = 0.5F;
  float m_W = 400, m_H = 300;
  Color m_theme;
  BoltRandom m_rng{2026};
  std::vector<Drop> m_drops;
  std::vector<Splash> m_splashes;
  std::vector<Stroke> m_strokes;
  StrokeRenderer m_renderer;
};

}  // namespace

std::unique_ptr<StrokeLook> makeRain() { return std::make_unique<Rain>(); }

}  // namespace undershell
