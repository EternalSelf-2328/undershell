// SPDX-License-Identifier: GPL-3.0-or-later
// Manga speed lines. Focus lines (集中線): wedges from the box's edges toward a
// clear centre, drawn anew ("boiling") faster the louder the music; a kick
// closes them in on the centre and thickens them. Or motion lines: streaks
// racing across the box, faster with the music.
#include "looks.hpp"

#include "widget.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace undershell {

namespace {

class SpeedLines final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    m_form = t["lines_form"].value_or(std::string("focus"));
    m_colour = t["lines_color"].value_or(std::string("black"));
    m_amount = std::clamp(t["lines_amount"].value_or(0.6), 0.0, 1.0);
    m_clear = std::clamp(t["lines_clear"].value_or(0.45), 0.0, 0.9);
    m_width = std::clamp(t["lines_width"].value_or(1.0), 0.3, 3.0);
    m_hits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
    m_theme = noct.color("primary");
    m_preview = t["lines_preview"].value_or(-1.0);
    if (m_preview >= 0) {
      m_flash = std::exp(-m_preview / 0.2);
      m_live = true;
      m_energy = 0.6;
    }
  }

  void tick(const Beat& b) override {
    if (m_preview >= 0) return;
    m_live = b.live;
    m_energy = b.energy;
    m_time += b.dt;
    if (b.kick) m_flash = std::max(m_flash, b.strength * std::clamp(m_hits / 0.7, 0.0, 1.45));
    m_flash *= std::exp(-b.dt / 0.2);
    if (m_flash < 0.003) m_flash = 0;
    // the boil: anime redraws its lines a dozen times a second
    if (b.kick || m_time >= m_next) {
      m_seed = m_seed * 1664525U + 1013904223U;
      m_next = m_time + (m_live ? 1.0 / (9 + 9 * m_energy) : 1.0 / 8);
    }
    // the streaks race left
    for (Streak& s : m_streaks) s.x -= s.speed * static_cast<float>(b.dt * (m_live ? 0.6 + m_energy + 2.5 * m_flash : 0.15));
    if (b.kick && m_form == "parallel") m_burst = 1;
    m_burst *= std::exp(-b.dt / 0.3);
  }

  void draw(const DrawContext& ctx, float opacity) override {
    const float W = ctx.w, H = ctx.h;
    const float ui = std::clamp(std::min(W, H) / 200.0F, 0.7F, 2.5F);
    const float flash = static_cast<float>(std::min(1.0, m_flash)), energy = static_cast<float>(m_live ? m_energy : 0.0);
    m_strokes.clear();
    BoltRandom r(m_seed);
    if (m_form == "parallel") {
      // keep as many streaks as the amount and the music ask for
      const size_t want = static_cast<size_t>(18 + 70 * m_amount * (0.5 + 0.5 * energy) + 30 * m_burst);
      while (m_streaks.size() < want) m_streaks.push_back(newStreak(W, H, m_rng, true));
      if (m_streaks.size() > want + 20) m_streaks.resize(want);
      for (Streak& s : m_streaks) {
        if (s.x + s.len < 0) s = newStreak(W, H, m_rng, false);
        // the head (left) is thick, the tail thins away
        m_strokes.push_back({s.x, s.y * H, s.x + s.len, s.y * H, s.w * ui * static_cast<float>(m_width) * (1 + 0.5F * flash), 1, 0});
      }
    } else {
      const int n = static_cast<int>(50 + 130 * m_amount + 30 * flash);
      const float cx = W / 2, cy = H / 2, far = std::hypot(W, H) / 2 + 4;
      // the clear centre shrinks with the music and closes in on a kick
      const float clear = static_cast<float>(m_clear) * (1 - 0.45F * flash) * (m_live ? 1 - 0.15F * energy : 1.1F);
      for (int i = 0; i < n; ++i) {
        const float a = (static_cast<float>(i) + r.range(-0.45F, 0.45F)) / static_cast<float>(n) * 2 * std::numbers::pi_v<float>;
        const float ca = std::cos(a), sa = std::sin(a);
        const float c = std::min(0.98F, clear + r.next() * 0.32F);
        float w = ui * static_cast<float>(m_width) * r.range(0.6F, 2.6F) * (1 + 0.6F * flash);
        if (r.next() < 0.12F) w *= 2.2F;  // now and then a heavy one
        m_strokes.push_back({cx + ca * far, cy + sa * far, cx + ca * W / 2 * c, cy + sa * H / 2 * c, w, 1, 0});
      }
    }
    StrokeRenderer::Look look;
    look.mode = StrokeRenderer::Mode::Flat;
    const Color col = m_colour == "white" ? Color{1, 1, 1, 1} : m_colour == "theme" ? m_theme : Color{0, 0, 0, 1};
    setColours(look, col, col);
    look.opacity = opacity;
    m_renderer.draw(m_strokes, W, H, look);
  }

  [[nodiscard]] bool visible() const override { return true; }
  [[nodiscard]] bool moving() const override { return m_live || m_flash > 0.01 || m_burst > 0.01; }
  [[nodiscard]] double idleFrame() const override { return m_form == "parallel" ? 1.0 / 20 : 1.0 / 8; }

private:
  struct Streak {
    float x, y, len, w, speed;
  };
  Streak newStreak(float W, float H, BoltRandom& r, bool anywhere) {
    Streak s;
    s.len = W * r.range(0.15F, 0.6F);
    s.x = anywhere ? r.range(-s.len, W) : W + r.range(0.0F, W * 0.3F);
    s.y = r.range(0.03F, 0.97F);
    s.w = r.range(0.6F, 2.4F);
    s.speed = W * r.range(1.6F, 3.2F);
    (void)H;
    return s;
  }

  std::string m_form = "focus", m_colour = "black";
  double m_amount = 0.6, m_clear = 0.45, m_width = 1, m_hits = 0.7, m_preview = -1;
  double m_time = 0, m_next = 0, m_flash = 0, m_energy = 0, m_burst = 0;
  bool m_live = false;
  uint32_t m_seed = 12345;
  BoltRandom m_rng{4242};
  Color m_theme;
  std::vector<Streak> m_streaks;
  std::vector<Stroke> m_strokes;
  StrokeRenderer m_renderer;
};

}  // namespace

std::unique_ptr<StrokeLook> makeSpeedLines() { return std::make_unique<SpeedLines>(); }

}  // namespace undershell
