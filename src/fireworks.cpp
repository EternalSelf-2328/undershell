// SPDX-License-Identifier: GPL-3.0-or-later
// Fireworks: every kick bursts one, at the kick (the shell's trail is shown
// as already risen, so the burst lands on the beat). A peony (a sphere of
// sparks), a willow (gold, long drooping trails) or a ring; sparks slow,
// fall, trail and crackle out. Hard kicks burst two.
#include "looks.hpp"

#include "widget.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace undershell {

namespace {

constexpr int kTrail = 6;           // positions kept per spark
constexpr float kTrailStep = 0.025F;  // seconds between them

class Fireworks final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    m_kind = t["fw_kind"].value_or(std::string("mix"));
    m_manga = t["fw_style"].value_or(std::string("flash")) == "manga";
    m_themed = t["fw_colors"].value_or(std::string("classic")) == "theme";
    m_amount = std::clamp(t["fw_amount"].value_or(0.7), 0.0, 1.0);
    m_size = std::clamp(t["fw_size"].value_or(1.0), 0.3, 1.6);
    m_hits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
    m_theme = noct.color("primary");
    m_preview = t["fw_preview"].value_or(-1.0);
    m_warmed = false;
  }

  void tick(const Beat& b) override {
    if (m_preview >= 0) return;
    step(static_cast<float>(b.dt));
    if (b.kick && b.strength >= (1 - m_amount) * 0.9) {
      const double s = std::clamp(b.strength * std::clamp(m_hits / 0.7, 0.4, 1.3), 0.3, 1.0);
      burst(s, m_rng.range(0.15F, 0.85F), m_rng.range(0.15F, 0.5F));
      if (s > 0.85 && m_amount > 0.6) burst(s * 0.8, m_rng.range(0.15F, 0.85F), m_rng.range(0.15F, 0.55F));
    }
  }

  void draw(const DrawContext& ctx, float opacity) override {
    m_W = ctx.w;
    m_H = ctx.h;
    if (m_preview >= 0 && !m_warmed) {  // one peony, this long after it burst
      m_warmed = true;
      m_bursts.clear();
      m_rng = BoltRandom(5);
      const std::string kind = m_kind;
      if (m_kind == "mix") m_kind = "peony";
      burst(1, 0.5F, 0.45F);
      m_kind = kind;
      for (float t = 0; t < m_preview; t += 1.0F / 60) step(1.0F / 60);
    }
    const float ui = std::clamp(std::min(m_W, m_H) / 300.0F, 0.7F, 2.2F);
    for (const Burst& bu : m_bursts) {
      m_strokes.clear();
      const float fade = std::pow(std::max(0.0F, 1 - bu.age / bu.life), 1.3F);
      // the shell's trail, just arrived
      if (bu.age < 0.14F) {
        const float k = 1 - bu.age / 0.14F;
        m_strokes.push_back({bu.x, bu.y + bu.radius * 1.4F, bu.x, bu.y, 0.6F * ui, 0.5F * k, 1.4F * ui});
      }
      for (const Spark& s : bu.sparks) {
        // crackle: near its end a spark flickers
        float I = s.bright * fade * static_cast<float>(bu.strength);
        if (bu.age > bu.life * 0.6F) I *= 0.4F + 0.6F * (std::sin(bu.age * 70 + s.phase) > -0.2F ? 1.0F : 0.0F);
        if (I < 0.02F) continue;
        const int n = std::min(s.count, kTrail);
        float px = s.x, py = s.y;
        // from the oldest trail point to the spark, so the run is unbroken
        std::array<std::pair<float, float>, kTrail + 1> pts{};
        for (int k = 0; k < n; ++k) pts[static_cast<size_t>(k)] = s.hist[static_cast<size_t>((s.head - n + k + kTrail) % kTrail)];
        pts[static_cast<size_t>(n)] = {px, py};
        // ink draws only the head and a short tail (no greys: a fading spark
        // thins instead, from 0.35 where ink starts to draw)
        const int from = m_manga ? std::max(0, n - 2) : 0;
        for (int k = from; k < n; ++k) {
          const float a = static_cast<float>(k) / static_cast<float>(n), b = static_cast<float>(k + 1) / static_cast<float>(n);
          float v = I * (0.25F + 0.75F * b);
          if (m_manga) v = v > 0.05F ? 0.35F + 0.8F * v : 0.0F;
          const float w = m_manga ? 1.6F : 1.0F;
          m_strokes.push_back({pts[static_cast<size_t>(k)].first, pts[static_cast<size_t>(k)].second, pts[static_cast<size_t>(k + 1)].first,
                               pts[static_cast<size_t>(k + 1)].second, (0.5F + a) * 1.4F * ui * w, v, (0.5F + b) * 1.4F * ui * w});
        }
        if (n == 0) m_strokes.push_back({px, py, px, py, 1.8F * ui, I});
      }
      if (bu.age < 0.08F)  // the flash of the burst
        m_strokes.push_back({bu.x, bu.y, bu.x, bu.y, bu.radius * 0.25F * (1 - bu.age / 0.08F), static_cast<float>(bu.strength)});
      StrokeRenderer::Look look;
      look.mode = m_manga ? StrokeRenderer::Mode::Ink : StrokeRenderer::Mode::Light;
      look.glow = 6 * ui;
      look.ink = 1.1F * ui;
      look.opacity = opacity;
      setColours(look, lighter(bu.colour, 0.65F), bu.colour);
      m_renderer.draw(m_strokes, m_W, m_H, look);
    }
  }

  [[nodiscard]] bool visible() const override { return !m_bursts.empty(); }
  [[nodiscard]] bool moving() const override { return !m_bursts.empty(); }

private:
  struct Spark {
    float x, y, vx, vy, bright, phase;
    std::array<std::pair<float, float>, kTrail> hist{};
    int head = 0, count = 0;
    float since = 0;
  };
  struct Burst {
    float x, y, radius, age = 0, life, drag, gravity;
    double strength;
    Color colour;
    std::vector<Spark> sparks;
  };

  void burst(double strength, float fx, float fy) {
    if (m_bursts.size() >= 8) m_bursts.erase(m_bursts.begin());  // the oldest makes room
    std::string kind = m_kind;
    if (kind == "mix") {
      const float r = m_rng.next();
      kind = r < 0.5F ? "peony" : r < 0.78F ? "willow" : "ring";
    }
    Burst b;
    b.strength = strength;
    b.radius = std::min(m_W, m_H) * 0.28F * static_cast<float>(m_size) * (0.6F + 0.5F * static_cast<float>(strength));
    b.x = fx * m_W;
    b.y = fy * m_H;
    // in the box: its sparks travel about 1.2 radii (more as they fall)
    b.x = std::clamp(b.x, std::min(m_W / 2, b.radius * 1.2F), std::max(m_W / 2, m_W - b.radius * 1.2F));
    b.y = std::clamp(b.y, std::min(m_H / 2, b.radius * 1.2F), std::max(m_H / 2, m_H - b.radius * 1.6F));
    const Color classic[] = {{1, 0.32F, 0.32F, 1}, {1, 0.78F, 0.3F, 1}, {0.42F, 1, 0.52F, 1},
                             {0.42F, 0.62F, 1, 1},  {0.82F, 0.46F, 1, 1}, {1, 1, 1, 1}};
    const Color gold{1, 0.74F, 0.32F, 1};
    b.colour = m_themed ? lighter(m_theme, 0.2F * m_rng.next()) : kind == "willow" ? gold : classic[static_cast<int>(m_rng.next() * 6) % 6];
    const float v0 = b.radius * 2.6F;
    int n = 56;
    b.life = 1.3F;
    b.drag = 2.2F;
    b.gravity = b.radius * 0.9F;
    if (kind == "willow") {
      n = 48;
      b.life = 2.4F;
      b.drag = 1.7F;
      b.gravity = b.radius * 1.7F;
    } else if (kind == "ring") {
      n = 44;
      b.life = 1.2F;
    }
    const float tilt = m_rng.range(0.2F, 1.1F), turn = m_rng.range(0, 6.2831853F);
    for (int i = 0; i < n; ++i) {
      Spark s{};
      float dx, dy;
      if (kind == "ring") {
        const float a = 2 * std::numbers::pi_v<float> * i / n;
        const float rx = std::cos(a), ry = std::sin(a) * std::cos(tilt);  // a ring seen at a slant
        dx = rx * std::cos(turn) - ry * std::sin(turn);
        dy = rx * std::sin(turn) + ry * std::cos(turn);
      } else {
        // a point on a sphere, seen flat: the burst fills in, not just a rim
        const float z = m_rng.range(-1, 1), a = m_rng.range(0, 6.2831853F), r = std::sqrt(1 - z * z);
        dx = r * std::cos(a);
        dy = r * std::sin(a);
      }
      const float v = v0 * m_rng.range(0.85F, 1.05F);
      s.x = b.x;
      s.y = b.y;
      s.vx = dx * v;
      s.vy = dy * v;
      s.bright = m_rng.range(0.7F, 1.15F);
      s.phase = m_rng.range(0, 6.2831853F);
      b.sparks.push_back(s);
    }
    m_bursts.push_back(std::move(b));
  }

  void step(float dt) {
    for (Burst& b : m_bursts) {
      b.age += dt;
      const float drag = std::exp(-b.drag * dt);
      for (Spark& s : b.sparks) {
        s.since += dt;
        if (s.since >= kTrailStep) {  // where it has been
          s.since = 0;
          s.hist[static_cast<size_t>(s.head)] = {s.x, s.y};
          s.head = (s.head + 1) % kTrail;
          s.count = std::min(kTrail, s.count + 1);
        }
        s.vx *= drag;
        s.vy = s.vy * drag + b.gravity * dt;
        s.x += s.vx * dt;
        s.y += s.vy * dt;
      }
    }
    std::erase_if(m_bursts, [](const Burst& b) { return b.age > b.life; });
  }

  std::string m_kind = "mix";
  bool m_manga = false, m_themed = false, m_warmed = false;
  double m_amount = 0.7, m_size = 1, m_hits = 0.7, m_preview = -1;
  float m_W = 400, m_H = 300;
  Color m_theme;
  BoltRandom m_rng{2027};
  std::vector<Burst> m_bursts;
  std::vector<Stroke> m_strokes;
  StrokeRenderer m_renderer;
};

}  // namespace

std::unique_ptr<StrokeLook> makeFireworks() { return std::make_unique<Fireworks>(); }

}  // namespace undershell
