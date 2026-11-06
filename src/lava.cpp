// SPDX-License-Identifier: GPL-3.0-or-later
// A lava lamp: blobs of wax warm at the bottom, rise, cool at the top and
// sink again, merging and pulling apart as they pass (metaballs: one field
// summed over every blob, the wax where it passes a threshold). The groove
// turns the heat up, so they climb faster and swell; a kick gives a blob a
// push. The blobs move on the CPU (a dozen numbers each); one pass draws
// them, in the lamp's glass or over the whole box.
#include "looks.hpp"

#include "gl.hpp"
#include "widget.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace undershell {

namespace {

constexpr int kMax = 14;

const char* kFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 u_res;
uniform vec3 u_blob[14];   // x (0..1), y (0 bottom .. 1 top), radius (share of the height)
uniform int u_count;
uniform int u_glass;       // 1: inside a lamp's glass
uniform vec3 u_hot, u_cool, u_fluid;
uniform float u_opacity;
void main() {
    vec2 p = vec2(v_uv.x * u_res.x / u_res.y, 1.0 - v_uv.y);  // in heights, y up
    float aspect = u_res.x / u_res.y;
    // the glass: wider at the bottom, narrower at the top, round ends
    float inside = 1.0;
    if (u_glass == 1) {
        float half_w = mix(0.24, 0.15, p.y) * min(1.0, aspect);
        float dx = abs(p.x - aspect * 0.5);
        inside = smoothstep(half_w + 0.004, half_w - 0.004, dx) * smoothstep(0.0, 0.03, p.y) * smoothstep(1.0, 0.97, p.y);
        if (inside <= 0.0) { fragColor = vec4(0.0); return; }
    }
    float f = 0.0;
    for (int i = 0; i < 14; i++) {
        if (i >= u_count) break;
        vec2 c = vec2(u_blob[i].x * aspect, u_blob[i].y);
        vec2 d = p - c;
        f += u_blob[i].z * u_blob[i].z / max(dot(d, d), 1e-5);
    }
    float wax = smoothstep(0.92, 1.08, f);
    // hot at the bottom, cooler (and a little darker) up top; a sheen near the edge
    vec3 waxC = mix(u_hot, u_cool, clamp(p.y * 1.1, 0.0, 1.0));
    float rim = smoothstep(1.0, 1.6, f) * (1.0 - smoothstep(1.6, 3.5, f));
    waxC = mix(waxC, vec3(1.0), (1.0 - rim) * wax * 0.12);
    float glow = clamp(f * 0.35, 0.0, 0.6) * (1.0 - wax);
    vec4 fluid = vec4(u_fluid * (0.5 + glow) + u_hot * 0.04, 0.55 + glow * 0.4) * (u_glass == 1 ? 1.0 : 0.0);
    vec4 col = mix(fluid, vec4(waxC, 1.0), wax);
    col.rgb += u_hot * glow * 0.35;
    fragColor = vec4(col.rgb * col.a, col.a) * inside * u_opacity;
}
)";

struct Palette {
  const char* name;
  Color hot, cool, fluid;
};
const Palette kPalettes[] = {
    {"classic", Color::fromHex("#ffb02e"), Color::fromHex("#ff3d1f"), Color::fromHex("#5a0d1c")},
    {"neon", Color::fromHex("#ff4fd8"), Color::fromHex("#7b3cff"), Color::fromHex("#0d1640")},
    {"ocean", Color::fromHex("#7ff3ff"), Color::fromHex("#2a7bff"), Color::fromHex("#06213a")},
};

class Lava final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    m_count = static_cast<int>(std::clamp<int64_t>(t["lava_blobs"].value_or(int64_t{8}), 3, kMax));
    m_glass = t["lava_glass"].value_or(true);
    m_speed = std::clamp(t["lava_speed"].value_or(1.0), 0.2, 3.0);
    m_hits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
    m_preview = t["lava_preview"].value_or(-1.0);
    const std::string colors = t["lava_colors"].value_or(std::string("classic"));
    if (colors == "theme") {
      m_hot = lighter(noct.color("primary"), 0.2F);
      m_cool = noct.color("tertiary", noct.color("primary"));
      m_fluid = noct.color("surface", Color::fromHex("#101014"));
    } else {
      const Palette* p = &kPalettes[0];
      for (const auto& q : kPalettes)
        if (colors == q.name) p = &q;
      m_hot = p->hot, m_cool = p->cool, m_fluid = p->fluid;
    }
    if (!m_seeded || m_preview >= 0) seed();
    if (m_preview >= 0)
      for (int i = 0; i < static_cast<int>(m_preview * 30); ++i) move(1.0 / 30, 0.4);
  }

  void tick(const Beat& b) override {
    if (m_preview >= 0) return;
    const double heat = b.live ? std::clamp(b.energy * 1.4, 0.0, 1.0) : 0.15;
    m_heat += (heat - m_heat) * std::min(1.0, b.dt * 2.0);
    if (b.kick && b.live) {  // a push to the blob lowest down
      int low = 0;
      for (int i = 1; i < m_count; ++i)
        if (m_b[i].y < m_b[low].y) low = i;
      m_b[low].vy += 0.12 * std::clamp(b.strength, 0.3, 1.0) * m_hits / 0.7;
      m_b[low].swell = 1;
    }
    move(b.dt, m_heat);
  }

  void draw(const DrawContext& ctx, float opacity) override {
    if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kFrag, "lava");
    glUseProgram(m_prog.id());
    auto U = [&](const char* n) { return m_prog.uniform(n); };
    std::array<float, kMax * 3> blobs{};
    const float aspect = ctx.h > 0 ? ctx.w / ctx.h : 1;
    const float narrow = m_glass ? 0.32F / std::max(0.5F, aspect) : 1.0F;  // inside the glass, keep to its width
    for (int i = 0; i < m_count; ++i) {
      blobs[i * 3] = static_cast<float>(m_glass ? 0.5 + (m_b[i].x - 0.5) * narrow : m_b[i].x);
      blobs[i * 3 + 1] = static_cast<float>(m_b[i].y);
      blobs[i * 3 + 2] = static_cast<float>(m_b[i].r * (1 + 0.25 * m_b[i].swell) * (0.85 + 0.35 * m_heat));
    }
    glUniform2f(U("u_res"), ctx.w, ctx.h);
    glUniform3fv(U("u_blob"), kMax, blobs.data());
    glUniform1i(U("u_count"), m_count);
    glUniform1i(U("u_glass"), m_glass ? 1 : 0);
    glUniform3f(U("u_hot"), m_hot.r, m_hot.g, m_hot.b);
    glUniform3f(U("u_cool"), m_cool.r, m_cool.g, m_cool.b);
    glUniform3f(U("u_fluid"), m_fluid.r, m_fluid.g, m_fluid.b);
    glUniform1f(U("u_opacity"), opacity);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawUnitQuad();
    glDisable(GL_BLEND);
  }

  [[nodiscard]] bool visible() const override { return true; }
  [[nodiscard]] bool moving() const override { return m_preview < 0; }
  // in silence the wax still drifts, slowly: a few frames a second
  [[nodiscard]] double idleFrame() const override { return 1.0 / 15; }

private:
  struct Blob {
    double x = 0.5, y = 0.2, r = 0.08, vx = 0, vy = 0, temp = 0.5, swell = 0;
  };
  [[nodiscard]] double rnd() {
    m_rng ^= m_rng << 13, m_rng ^= m_rng >> 7, m_rng ^= m_rng << 17;
    return static_cast<double>(m_rng >> 11) / static_cast<double>(1ULL << 53);
  }
  void seed() {
    m_rng = 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < kMax; ++i) {
      Blob& b = m_b[i];
      b.x = 0.2 + 0.6 * rnd(), b.y = 0.05 + 0.9 * rnd(), b.r = 0.032 + 0.03 * rnd();
      b.temp = rnd(), b.vx = b.vy = b.swell = 0;
    }
    m_seeded = true;
  }
  // warm at the bottom, cool at the top: the warm rise, the cool sink
  void move(double dt, double heat) {
    dt = std::min(dt, 0.1) * m_speed;
    for (int i = 0; i < m_count; ++i) {
      Blob& b = m_b[i];
      // the heater warms the bottom fifth; the top fifth cools it again
      const double target = b.y < 0.22 ? 1.0 : b.y > 0.72 ? 0.0 : b.temp;
      b.temp += (target - b.temp) * dt * (0.5 + 0.8 * heat);
      b.vy += (b.temp - 0.5) * 0.07 * (0.6 + heat) * dt;
      b.vx += (rnd() - 0.5) * 0.02 * dt + (0.5 - b.x) * 0.004 * dt;
      b.vx *= std::exp(-dt * 1.5), b.vy *= std::exp(-dt * 0.9);
      b.x = std::clamp(b.x + b.vx * dt * 10, 0.12, 0.88);
      b.y += b.vy * dt * 10;
      if (b.y < 0.02) b.y = 0.02, b.vy = std::abs(b.vy) * 0.3;
      if (b.y > 0.98) b.y = 0.98, b.vy = -std::abs(b.vy) * 0.3;
      b.swell *= std::exp(-dt * 3);
    }
  }

  int m_count = 8;
  bool m_glass = true, m_seeded = false;
  double m_speed = 1, m_hits = 0.7, m_preview = -1, m_heat = 0.15;
  std::array<Blob, kMax> m_b{};
  Color m_hot, m_cool, m_fluid;
  uint64_t m_rng = 1;
  Program m_prog;
};

}  // namespace

std::unique_ptr<StrokeLook> makeLava() { return std::make_unique<Lava>(); }

}  // namespace undershell
