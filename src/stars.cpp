// SPDX-License-Identifier: GPL-3.0-or-later
// Stars, three ways:
//   field         - a sky of stars, each twinkling on its own and brightening
//                   with its band of the music; a strong kick sends a
//                   shooting star across
//   warp          - flying through them: they stream out of the middle, faster
//                   with the groove, leaping on a kick
//   constellation - fixed stars joined by faint lines that light up with the
//                   music, a different figure on each kick
// Drawn as strokes of light (dots and short streaks with a glow), so only the
// pixels near a star cost anything.
#include "looks.hpp"

#include "perf.hpp"
#include "widget.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace undershell {

namespace {

class Stars final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    const std::string mode = t["stars_mode"].value_or(std::string("field"));
    const int was = m_mode;
    m_mode = mode == "warp" ? 1 : mode == "constellation" ? 2 : 0;
    const std::string colors = t["stars_colors"].value_or(std::string("night"));
    m_colors = colors == "theme" ? 1 : colors == "gold" ? 2 : 0;
    m_theme = noct.color("primary");
    const int count = static_cast<int>(std::clamp<int64_t>(t["stars_count"].value_or(int64_t{140}), 20, 400));
    m_size = std::clamp(t["stars_size"].value_or(1.0), 0.4, 2.5);
    m_speed = std::clamp(t["stars_speed"].value_or(1.0), 0.2, 3.0);
    m_shooting = t["stars_shooting"].value_or(true);
    m_hits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
    m_preview = t["stars_preview"].value_or(-1.0);
    m_count = count;
    const int want = m_preview >= 0 ? count : std::max(20, static_cast<int>(std::lround(count * perfKnobs().particles)));
    if (want != m_seeded || was != m_mode) seed(want);
    if (m_preview >= 0) {
      m_time = m_preview;
      m_energy = 0.6;
      m_lit = 0.8;
      if (m_mode == 1)  // spread them along the way, as if flying a while
        for (auto& s : m_stars) s.z = 0.05F + 0.95F * s.tw;
      if (m_mode == 0) m_shoot = {0.35F, 0.25F, 0.4F, 1};
    }
  }

  void tick(const Beat& b) override {
    if (m_preview >= 0) return;
    // the performance profile scales the sky (a new sky only when it changes)
    if (const int want = std::max(20, static_cast<int>(std::lround(m_count * perfKnobs().particles))); want != m_seeded) seed(want);
    const double dt = b.dt;
    m_time += dt;
    m_live = b.live;
    m_bands = b.bands;
    const double target = b.live ? std::clamp(b.energy * 1.4, 0.0, 1.0) : 0.1;
    m_energy += (target - m_energy) * std::min(1.0, dt * 3.0);
    m_flash *= std::exp(-dt * 5.0);
    m_lit *= std::exp(-dt * 1.4);
    m_boost *= std::exp(-dt * 3.0);
    if (b.kick) {
      const double k = std::clamp(b.strength, 0.3, 1.0) * m_hits / 0.7;
      m_flash = std::max(m_flash, k);
      m_boost = std::max(m_boost, k);
      if (m_mode == 2) {  // another figure lights up
        m_figure = static_cast<int>(m_rng.next() * 1000);
        m_lit = std::max(m_lit, k);
      }
      if (m_mode == 0 && m_shooting && k > 0.55 && m_shoot.life <= 0)
        m_shoot = {m_rng.range(0.1F, 0.6F), m_rng.range(0.05F, 0.4F), m_rng.range(0.25F, 0.55F), 1};
    }
    if (m_shoot.life > 0) m_shoot.life -= static_cast<float>(dt) / 0.9F;
    if (m_mode == 1) {
      // forward: nearer each frame, faster with the music and on a kick
      const float v = static_cast<float>(m_speed * (0.08 + 0.55 * m_energy + 1.6 * m_boost) * dt);
      for (auto& s : m_stars) {
        s.prevZ = s.z;
        s.z -= v;
        if (s.z <= 0.02F) {
          s.x = m_rng.range(-1, 1), s.y = m_rng.range(-1, 1);
          s.z = s.prevZ = 1;
        }
      }
    }
  }

  void draw(const DrawContext& ctx, float opacity) override {
    const float W = ctx.w, H = ctx.h, ui = std::clamp(std::min(W, H) / 300.0F, 0.6F, 2.0F) * static_cast<float>(m_size);
    m_strokes.clear();
    auto band = [&](float u) {  // the band a star listens to
      if (!m_bands || m_bands->empty()) return 0.0F;
      const size_t i = std::min(m_bands->size() - 1, static_cast<size_t>(u * static_cast<float>(m_bands->size()) * 0.8F));
      return std::clamp((*m_bands)[i], 0.0F, 1.0F);
    };
    const auto t = static_cast<float>(m_time);
    if (m_mode == 1) {
      const float cx = W / 2, cy = H / 2, f = std::min(W, H) * 0.45F;
      for (const auto& s : m_stars) {
        const float x1 = cx + s.x / s.z * f, y1 = cy + s.y / s.z * f;
        const float x0 = cx + s.x / s.prevZ * f, y0 = cy + s.y / s.prevZ * f;
        if (x1 < -20 || y1 < -20 || x1 > W + 20 || y1 > H + 20) continue;
        const float near = 1 - s.z;
        // a streak as long as it moved (at least a dot), brighter as it nears
        const float tail = std::min(1.0F, 0.15F + 3.0F * static_cast<float>(m_energy + m_boost));
        m_strokes.push_back({x0 + (x1 - x0) * (1 - tail), y0 + (y1 - y0) * (1 - tail), x1, y1, (0.6F + 1.8F * near) * ui,
                             std::min(1.0F, 0.25F + near * near * 1.1F)});
      }
    } else {
      for (size_t i = 0; i < m_stars.size(); ++i) {
        const auto& s = m_stars[i];
        const float x = (s.x * 0.5F + 0.5F) * W, y = (s.y * 0.5F + 0.5F) * H;
        const float tw = 0.55F + 0.45F * std::sin(t * (1.2F + 2.5F * s.tw) + s.tw * 40);
        float v = s.base * tw * (0.6F + 0.4F * static_cast<float>(m_live ? 1 : 0.6));
        v += band(s.tw) * 0.6F * s.base + static_cast<float>(m_flash) * 0.35F * s.base;
        m_strokes.push_back({x, y, x, y, (0.8F + 2.4F * s.base) * ui * (1 + 0.25F * band(s.tw)), std::min(1.2F, v)});
        // the brightest ones sparkle: a small cross
        if (s.base > 0.8F && v > 0.75F) {
          const float l = (3 + 7 * (v - 0.75F)) * ui;
          m_strokes.push_back({x - l, y, x + l, y, 0.5F * ui, v * 0.5F});
          m_strokes.push_back({x, y - l, x, y + l, 0.5F * ui, v * 0.5F});
        }
      }
      if (m_mode == 2) {
        // the lines: each star to its nearest neighbours; this figure brighter
        for (const auto& [a, b] : m_links) {
          const auto& p = m_stars[a];
          const auto& q = m_stars[b];
          const bool in = ((a * 7 + b * 3 + m_figure) % 5) == 0;
          const float v = 0.07F + 0.25F * band(p.tw) + (in ? 0.75F * static_cast<float>(m_lit) : 0.0F);
          m_strokes.push_back({(p.x * 0.5F + 0.5F) * W, (p.y * 0.5F + 0.5F) * H, (q.x * 0.5F + 0.5F) * W, (q.y * 0.5F + 0.5F) * H,
                               0.7F * ui, std::min(1.0F, v)});
        }
      }
      if (m_mode == 0 && m_shoot.life > 0) {
        // a shooting star: a bright head and a tail thinning behind it
        const float age = 1 - m_shoot.life;
        const float hx = (m_shoot.x + std::cos(m_shoot.angle) * age * 0.8F) * W;
        const float hy = (m_shoot.y + std::sin(m_shoot.angle) * age * 0.8F) * H;
        const float len = std::min(W, H) * 0.25F;
        const float a = std::min(1.0F, m_shoot.life * 2.5F);
        m_strokes.push_back({hx - std::cos(m_shoot.angle) * len, hy - std::sin(m_shoot.angle) * len * H / W, hx, hy, 0.2F * ui, a * 0.8F,
                             2.2F * ui});
      }
    }
    StrokeRenderer::Look look;
    look.mode = StrokeRenderer::Mode::Light;
    look.glow = 6 * ui;
    look.opacity = opacity;
    if (m_colors == 1) setColours(look, lighter(m_theme, 0.8F), m_theme);
    else if (m_colors == 2) setColours(look, Color{1, 0.97F, 0.85F, 1}, Color{1, 0.72F, 0.25F, 1});
    else setColours(look, Color{0.97F, 0.98F, 1, 1}, Color{0.55F, 0.68F, 1, 1});
    m_renderer.draw(m_strokes, W, H, look);
  }

  [[nodiscard]] bool visible() const override { return true; }
  [[nodiscard]] bool moving() const override { return m_preview < 0; }
  // in silence the stars still twinkle (or drift): a few frames a second
  [[nodiscard]] double idleFrame() const override { return m_mode == 1 ? 1.0 / 30 : 1.0 / 12; }

private:
  struct Star {
    float x = 0, y = 0;       // -1..1 (warp: the direction from the middle)
    float z = 1, prevZ = 1;   // warp: how far
    float base = 0.5F;        // how bright
    float tw = 0;             // its own phase and band, 0..1
  };
  struct Shoot {
    float x = 0, y = 0, angle = 0, life = 0;
  };
  void seed(int count) {
    m_seeded = count;
    BoltRandom r(1234);
    m_stars.assign(static_cast<size_t>(m_mode == 2 ? std::min(count, 60) : count), {});
    for (auto& s : m_stars) {
      s.x = r.range(-0.96F, 0.96F);
      s.y = r.range(-0.92F, 0.92F);
      s.z = s.prevZ = r.range(0.05F, 1);
      // most are faint, a few bright
      const float k = r.next();
      s.base = 0.2F + 0.8F * k * k * k;
      s.tw = r.next();
    }
    // constellation lines: each star to its two nearest
    m_links.clear();
    if (m_mode == 2)
      for (size_t i = 0; i < m_stars.size(); ++i) {
        std::array<std::pair<float, size_t>, 2> best{{{1e9F, i}, {1e9F, i}}};
        for (size_t j = 0; j < m_stars.size(); ++j) {
          if (j == i) continue;
          const float dx = m_stars[i].x - m_stars[j].x, dy = (m_stars[i].y - m_stars[j].y) * 0.6F;
          const float d = dx * dx + dy * dy;
          if (d < best[0].first) best[1] = best[0], best[0] = {d, j};
          else if (d < best[1].first) best[1] = {d, j};
        }
        for (const auto& [d, j] : best)
          if (j != i && d < 0.12F && i < j) m_links.emplace_back(i, j);
      }
  }

  int m_mode = 0, m_colors = 0, m_figure = 0;
  double m_size = 1, m_speed = 1, m_hits = 0.7, m_preview = -1;
  double m_time = 0, m_energy = 0.1, m_flash = 0, m_lit = 0, m_boost = 0;
  bool m_shooting = true, m_live = false;
  Color m_theme;
  const std::vector<float>* m_bands = nullptr;
  std::vector<Star> m_stars;
  int m_count = 140, m_seeded = 0;  // stars asked for, and the count the sky was made with
  std::vector<std::pair<size_t, size_t>> m_links;
  Shoot m_shoot;
  BoltRandom m_rng{99};
  std::vector<Stroke> m_strokes;
  StrokeRenderer m_renderer;
};

}  // namespace

std::unique_ptr<StrokeLook> makeStars() { return std::make_unique<Stars>(); }

}  // namespace undershell
