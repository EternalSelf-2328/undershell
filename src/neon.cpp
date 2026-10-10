// SPDX-License-Identifier: GPL-3.0-or-later
// A neon sign: your words, or a shape (a heart, a star, a ring, a bolt, a
// music note, an arrow), in glass tubes that hum brighter with the music and
// flare on the kicks. Now and then a letter or a piece fails and stutters
// back, the way an old sign does; an unlit tube still shows as dark glass.
#include "looks.hpp"

#include "canvas.hpp"
#include "widget.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace undershell {

namespace {

struct Pt {
  float x, y;
};
using Polyline = std::vector<Pt>;

// a shape in [-1, 1]: its pieces (each fails on its own), closed or not
struct Shape {
  std::vector<Polyline> pieces;
  std::vector<bool> closed;
};

Polyline ellipse(float cx, float cy, float rx, float ry, float tilt, int n) {
  Polyline p;
  for (int i = 0; i < n; ++i) {
    const float a = 2 * std::numbers::pi_v<float> * i / n;
    const float x = std::cos(a) * rx, y = std::sin(a) * ry;
    p.push_back({cx + x * std::cos(tilt) - y * std::sin(tilt), cy + x * std::sin(tilt) + y * std::cos(tilt)});
  }
  return p;
}

Shape shapeOf(const std::string& name) {
  Shape s;
  auto add = [&s](Polyline p, bool closed) {
    s.pieces.push_back(std::move(p));
    s.closed.push_back(closed);
  };
  if (name == "heart") {
    Polyline p;
    for (int i = 0; i < 72; ++i) {
      const float t = 2 * std::numbers::pi_v<float> * i / 72;
      const float x = 16 * std::pow(std::sin(t), 3.0F);
      const float y = 13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t);
      p.push_back({x / 17, -y / 17 - 0.05F});
    }
    add(p, true);
  } else if (name == "star") {
    Polyline p;
    for (int i = 0; i < 10; ++i) {
      const float a = -std::numbers::pi_v<float> / 2 + std::numbers::pi_v<float> * i / 5;
      const float r = i % 2 == 0 ? 1.0F : 0.42F;
      p.push_back({std::cos(a) * r, std::sin(a) * r + 0.08F});
    }
    add(p, true);
  } else if (name == "circle") {
    add(ellipse(0, 0, 0.92F, 0.92F, 0, 72), true);
    add(ellipse(0, 0, 0.7F, 0.7F, 0, 60), true);
  } else if (name == "bolt") {
    add({{0.18F, -1}, {-0.48F, 0.12F}, {-0.04F, 0.12F}, {-0.22F, 1}, {0.48F, -0.16F}, {0.04F, -0.16F}}, true);
  } else if (name == "note") {
    add(ellipse(-0.42F, 0.62F, 0.24F, 0.17F, -0.35F, 28), true);
    add(ellipse(0.52F, 0.46F, 0.24F, 0.17F, -0.35F, 28), true);
    add({{-0.2F, 0.56F}, {-0.2F, -0.68F}, {0.74F, -0.84F}, {0.74F, 0.4F}}, false);  // stems and beam
  } else {  // arrow
    add({{-0.95F, 0}, {0.6F, 0}}, false);
    add({{0.18F, -0.42F}, {0.8F, 0}, {0.18F, 0.42F}}, false);
  }
  return s;
}

class Neon final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    m_shape = t["neon_shape"].value_or(std::string("text"));
    m_text = t["neon_text"].value_or(std::string("LIVE"));
    if (m_text.empty()) m_text = "LIVE";
    m_font = t["neon_font"].value_or(std::string("auto"));
    if (m_font == "auto" || m_font.empty()) m_font = "Inter Display";
    m_colour = noct.color(t["neon_color"].value_or(std::string("#ff3fa4")), Color::fromHex("#ff3fa4"));
    m_size = std::clamp(t["neon_size"].value_or(0.8), 0.3, 1.0);
    m_flicker = std::clamp(t["neon_flicker"].value_or(0.3), 0.0, 1.0);
    m_hits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
    m_preview = t["neon_preview"].value_or(-1.0);
    m_layoutKey.clear();
    // the pieces that can fail: letters, or the shape's parts
    m_letters.clear();
    if (m_shape == "text") {
      for (size_t i = 0; i < m_text.size();) {
        const auto c = static_cast<unsigned char>(m_text[i]);
        const size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        m_letters.push_back(m_text.substr(i, n));
        i += n;
      }
    } else {
      m_figure = shapeOf(m_shape);
    }
    m_fail.assign(std::max<size_t>(1, m_shape == "text" ? m_letters.size() : m_figure.pieces.size()), -1);
    if (m_preview >= 0) {
      m_flash = std::exp(-m_preview / 0.25);
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
    m_flash *= std::exp(-b.dt / 0.25);
    if (m_flash < 0.003) m_flash = 0;
    // failures: each piece now and then, more readily on a hard kick
    for (double& f : m_fail) {
      if (f >= 0 && m_time > f) f = -1;
      const double rate = m_flicker * 0.08 + (b.kick ? m_flicker * 0.5 * b.strength : 0);
      if (f < 0 && m_rng.next() < rate * (b.kick ? 1 : b.dt * 10)) f = m_time + 0.08 + 0.35 * m_rng.next();
    }
  }

  void draw(const DrawContext& ctx, float opacity) override {
    const float W = ctx.w, H = ctx.h;
    const float flash = static_cast<float>(std::min(1.0, m_flash));
    // it hums: a little brighter with the music, flaring on a kick
    const float base = (m_live ? 0.55F + 0.35F * static_cast<float>(m_energy) : 0.5F) + 0.6F * flash +
                       0.03F * std::sin(static_cast<float>(m_time) * 11);
    auto lit = [&](size_t i) {
      if (i < m_fail.size() && m_fail[i] >= 0) {  // failing: it stutters
        const float s = std::sin(static_cast<float>(m_time) * 97 + static_cast<float>(i) * 13);
        return s > 0.35F ? base * 0.7F : 0.06F;
      }
      return base;
    };
    if (m_shape == "text") drawText(ctx, W, H, opacity, lit);
    else drawFigure(W, H, opacity, lit);
  }

  [[nodiscard]] bool visible() const override { return true; }
  [[nodiscard]] bool moving() const override {
    if (m_live || m_flash > 0.01) return true;
    for (double f : m_fail)
      if (f >= 0) return true;
    return false;
  }
  // in silence it still hums, and a failure can strike
  [[nodiscard]] double idleFrame() const override { return m_flicker > 0 ? 0.1 : 0.5; }

private:
  template <class Lit>
  void drawText(const DrawContext& ctx, float W, float H, float opacity, Lit lit) {
    if (!ctx.text) return;
    Canvas& c = m_canvas;
    c.begin(W, H, ctx.scale, ctx.text);
    c.setTransform(1, 0, 0);
    // the letters' places, measured once for this box (measuring has no cache)
    const std::string key = std::format("{}|{}|{}|{}|{}", m_text, m_font, W, H, m_size);
    if (key != m_layoutKey) {
      m_layoutKey = key;
      TextStyle ref;
      ref.family = m_font;
      ref.size = 100;
      ref.weight = 700;
      const Canvas::Size whole = Canvas::measure(m_text, ref);
      const float px = 100 * std::min(W * static_cast<float>(m_size) * 0.86F / std::max(1.0F, whole.w),
                                      H * static_cast<float>(m_size) * 0.7F / std::max(1.0F, whole.h));
      m_style = ref;
      m_style.size = px;
      const Canvas::Size z = Canvas::measure(m_text, m_style);
      m_x0 = (W - z.w) / 2;
      m_y0 = (H - z.h) / 2;
      m_offsets.clear();
      std::string prefix;
      for (const std::string& l : m_letters) {
        m_offsets.push_back(prefix.empty() ? 0.0F : Canvas::measure(prefix, m_style).w);
        prefix += l;
      }
    }
    const float px = m_style.size, tube = std::max(1.2F, px * 0.045F);
    auto tubeStyle = [&](float stroke) {
      TextStyle s = m_style;
      s.stroke = stroke;
      return s;
    };
    const TextStyle glass = tubeStyle(tube), wide = tubeStyle(tube * 3.0F), mid = tubeStyle(tube * 1.7F), core = tubeStyle(tube * 0.8F);
    const Color hot = lighter(m_colour, 0.7F);
    for (size_t i = 0; i < m_letters.size(); ++i) {
      const std::string& l = m_letters[i];
      if (l == " ") continue;
      const float x = m_x0 + m_offsets[i], I = lit(i);
      c.text(l, glass, x, m_y0, Color{0.22F, 0.22F, 0.25F, 1}, 0.4F * opacity);  // the glass, lit or not
      if (I < 0.1F) continue;
      c.text(l, wide, x, m_y0, m_colour, std::min(1.0F, I * 0.5F) * opacity, 1, px * 0.08F);
      c.text(l, mid, x, m_y0, m_colour, std::min(1.0F, I * 0.85F) * opacity, 1, px * 0.035F);
      c.text(l, core, x, m_y0, hot, std::min(1.0F, I * 1.3F) * opacity);
    }
  }

  template <class Lit>
  void drawFigure(float W, float H, float opacity, Lit lit) {
    const float s = std::min(W, H) * static_cast<float>(m_size) * 0.45F, cx = W / 2, cy = H / 2;
    const float tube = std::max(1.5F, s * 0.035F);
    auto strokesOf = [&](size_t piece, float intensity, float width) {
      const Polyline& p = m_figure.pieces[piece];
      const size_t n = m_figure.closed[piece] ? p.size() : p.size() - 1;
      for (size_t k = 0; k < n; ++k) {
        const Pt a = p[k], b = p[(k + 1) % p.size()];
        m_strokes.push_back({cx + a.x * s, cy + a.y * s, cx + b.x * s, cy + b.y * s, width, intensity});
      }
    };
    // the glass first, all of it
    m_strokes.clear();
    for (size_t i = 0; i < m_figure.pieces.size(); ++i) strokesOf(i, 0.4F, tube);
    StrokeRenderer::Look glass;
    glass.mode = StrokeRenderer::Mode::Flat;
    glass.opacity = opacity;
    setColours(glass, Color{0.22F, 0.22F, 0.25F, 1}, Color{0.22F, 0.22F, 0.25F, 1});
    m_renderer.draw(m_strokes, W, H, glass);
    // then the light
    m_strokes.clear();
    for (size_t i = 0; i < m_figure.pieces.size(); ++i) {
      const float I = lit(i);
      if (I >= 0.1F) strokesOf(i, std::min(1.3F, I), tube * 0.7F);
    }
    StrokeRenderer::Look light;
    light.mode = StrokeRenderer::Mode::Light;
    light.glow = tube * 5.0F;
    light.opacity = opacity;
    setColours(light, lighter(m_colour, 0.7F), m_colour);
    m_renderer.draw(m_strokes, W, H, light);
  }

  std::string m_shape = "text", m_text = "LIVE", m_font = "Inter Display";
  Color m_colour;
  double m_size = 0.8, m_flicker = 0.3, m_hits = 0.7, m_preview = -1;
  double m_time = 0, m_flash = 0, m_energy = 0;
  bool m_live = false;
  std::vector<std::string> m_letters;
  std::vector<double> m_fail;  // per piece: failing until this time (< 0: lit)
  Shape m_figure;
  // the text's layout for the box in hand
  std::string m_layoutKey;
  TextStyle m_style;
  float m_x0 = 0, m_y0 = 0;
  std::vector<float> m_offsets;
  BoltRandom m_rng{31};
  Canvas m_canvas;
  std::vector<Stroke> m_strokes;
  StrokeRenderer m_renderer;
};

}  // namespace

std::unique_ptr<StrokeLook> makeNeon() { return std::make_unique<Neon>(); }

}  // namespace undershell
