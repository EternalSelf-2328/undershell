// SPDX-License-Identifier: GPL-3.0-or-later
// Ripples on water: every kick drops a drop that opens rings, slowing as they
// spread and fading; smaller drops fall between the kicks with the music.
// The rings are flattened into ellipses, as water seen at an angle, and their
// near half (the bottom) catches more light.
#include "looks.hpp"

#include "widget.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace undershell {

namespace {

constexpr double kLife = 1.8;

class Ripples final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    m_manga = t["ripple_style"].value_or(std::string("flash")) == "manga";
    m_themed = t["ripple_colors"].value_or(std::string("water")) == "theme";
    m_flatten = std::clamp(t["ripple_flatten"].value_or(0.6), 0.0, 0.9);
    m_rings = static_cast<int>(std::clamp<int64_t>(t["ripple_rings"].value_or(int64_t{3}), 1, 4));
    m_amount = std::clamp(t["ripple_amount"].value_or(0.3), 0.0, 1.0);
    m_size = std::clamp(t["ripple_size"].value_or(0.6), 0.2, 1.0);
    m_hits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
    m_theme = noct.color("primary");
    m_preview = t["ripple_preview"].value_or(-1.0);
    if (m_preview >= 0) {
      for (Drop& d : m_drops) d.age = -1;
      m_drops[0] = {0.5F, 0.5F, m_preview, 1.0};
    }
  }

  void tick(const Beat& b) override {
    if (m_preview >= 0) return;
    m_live = b.live;
    for (Drop& d : m_drops)
      if (d.age >= 0 && (d.age += b.dt) > kLife) d.age = -1;
    if (b.kick) drop(std::clamp(b.strength * std::clamp(m_hits / 0.7, 0.3, 1.45), 0.3, 1.0));
    // small drops between the kicks, more with the music
    m_rain += b.dt * m_amount * (b.live ? 1 + 3 * b.energy : 0.5);
    while (m_rain >= 1) {
      m_rain -= 1;
      drop(0.3 + 0.15 * m_rng.next());
    }
  }

  void draw(const DrawContext& ctx, float opacity) override {
    const float W = ctx.w, H = ctx.h;
    const float ui = std::clamp(std::min(W, H) / 250.0F, 0.7F, 2.2F);
    const float squash = 1 - static_cast<float>(m_flatten);
    const float maxR = static_cast<float>(m_size) * std::min(W / 2, H / 2 / squash);
    m_strokes.clear();
    for (const Drop& d : m_drops) {
      if (d.age < 0) continue;
      const float s = static_cast<float>(d.strength), R = maxR * std::sqrt(s);
      // its centre, so that its widest ring stays in the box where it can
      const float cx = std::clamp(d.x * W, std::min(W / 2, R), std::max(W / 2, W - R));
      const float cy = std::clamp(d.y * H, std::min(H / 2, R * squash), std::max(H / 2, H - R * squash));
      for (int k = 0; k < m_rings; ++k) {
        const double t = d.age - 0.13 * k;
        if (t <= 0) continue;
        const float r = R * static_cast<float>(1 - std::exp(-t / 0.6));  // it slows as it spreads
        const float I = s * static_cast<float>(std::pow(std::max(0.0, 1 - d.age / kLife), 1.6) * std::pow(0.82, k) *
                                               std::min(1.0, t / 0.05));
        if (I < 0.02F || r < 1) continue;
        const int n = std::clamp(static_cast<int>(r / 4), 16, 72);
        for (int i = 0; i < n; ++i) {
          const float a0 = 2 * std::numbers::pi_v<float> * i / n, a1 = 2 * std::numbers::pi_v<float> * (i + 1) / n;
          const float lit = 0.55F + 0.45F * std::max(0.0F, std::sin((a0 + a1) / 2));  // the near side
          // ink has no greys: a fading ring thins instead (from 0.35, where ink starts to draw)
          const float v = m_manga ? (I * lit > 0.06F ? 0.35F + 0.8F * I * lit : 0.0F) : I * lit;
          m_strokes.push_back({cx + std::cos(a0) * r, cy + std::sin(a0) * r * squash, cx + std::cos(a1) * r,
                               cy + std::sin(a1) * r * squash, (m_manga ? 2.0F : 1.1F) * ui, v});
        }
      }
      if (d.age < 0.12) {  // the splash where it fell
        const float k = static_cast<float>(1 - d.age / 0.12);
        m_strokes.push_back({cx, cy, cx, cy, (2 + 4 * s) * ui * k, s});
      }
    }
    StrokeRenderer::Look look;
    look.mode = m_manga ? StrokeRenderer::Mode::Ink : StrokeRenderer::Mode::Light;
    look.glow = 5 * ui;
    look.ink = 1.2F * ui;
    look.opacity = opacity;
    if (m_themed) setColours(look, lighter(m_theme, 0.75F), m_theme);
    else setColours(look, Color{0.92F, 0.97F, 1, 1}, Color{0.45F, 0.75F, 0.95F, 1});
    m_renderer.draw(m_strokes, W, H, look);
  }

  [[nodiscard]] bool visible() const override { return alive(); }
  [[nodiscard]] bool moving() const override { return alive(); }
  // in silence a drop now and then (at a few frames a second until one falls)
  [[nodiscard]] double idleFrame() const override { return m_amount > 0 ? 0.25 : 1e18; }

private:
  struct Drop {
    float x = 0.5F, y = 0.5F;
    double age = -1, strength = 0;
  };
  [[nodiscard]] bool alive() const {
    for (const Drop& d : m_drops)
      if (d.age >= 0) return true;
    return false;
  }
  void drop(double strength) {
    size_t slot = 0;  // a free one, else the oldest
    for (size_t i = 0; i < m_drops.size(); ++i) {
      if (m_drops[i].age < 0) {
        slot = i;
        break;
      }
      if (m_drops[i].age > m_drops[slot].age) slot = i;
    }
    m_drops[slot] = {0.12F + 0.76F * m_rng.next(), 0.15F + 0.7F * m_rng.next(), 0, strength};
  }

  bool m_manga = false, m_themed = false, m_live = false;
  double m_flatten = 0.6, m_amount = 0.3, m_size = 0.6, m_hits = 0.7, m_preview = -1, m_rain = 0;
  int m_rings = 3;
  Color m_theme;
  BoltRandom m_rng{77};
  std::array<Drop, 10> m_drops{};
  std::vector<Stroke> m_strokes;
  StrokeRenderer m_renderer;
};

}  // namespace

std::unique_ptr<StrokeLook> makeRipples() { return std::make_unique<Ripples>(); }

}  // namespace undershell
