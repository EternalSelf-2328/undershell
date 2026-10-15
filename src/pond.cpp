// SPDX-License-Identifier: GPL-3.0-or-later
// A single water wave in the middle: the bass lifts the water at the centre
// and every kick sends one ring out across it, slowing and flattening as it
// spreads. The surface's height is worked out per pixel and lit like water --
// a bright glint where it faces the light, a soft shadow where it turns away,
// a thin highlight on the crest -- so it reads as a wave on glass over the
// wallpaper. As a ring seen from above (flattened by the perspective), or as
// one wave along a line through the middle.
#include "looks.hpp"

#include "gl.hpp"
#include "widget.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace undershell {

namespace {

const char* kFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 u_res;
uniform int u_line;        // 0 a ring seen from above, 1 a wave along a line
uniform float u_squash;    // perspective: 1 seen from straight above
uniform float u_size;      // the widest the rings go, a share of the box
uniform vec4 u_waves[4];   // radius (0..1 of the size), strength, width, -
uniform float u_mound;     // the bass lifting the middle, 0..1
uniform float u_time;
uniform vec3 u_light, u_shade;
uniform float u_opacity;

uniform sampler2D u_surf;  // the line: the surface's height per column, px
uniform float u_top;       // the line: the most it rises, px

float ysAt(float x) {      // the line's surface at column x, px from the top
    return u_res.y * 0.5 - texelFetch(u_surf, ivec2(clamp(int(x), 0, int(u_res.x) - 1), 0), 0).r;
}

void main() {
    vec2 px = v_uv * u_res;
    vec2 c = u_res * 0.5;
    if (u_line == 1) {
        // one wave along the middle: the water's surface seen from the side
        float halfW = u_res.x * 0.5 * u_size;
        float dy = px.y - c.y;
        if (dy < -(u_top + 40.0) || dy > u_top + u_res.y * 0.4) {
            fragColor = vec4(0.0);
            return;
        }
        // this column: the surface here, and the lowest and highest it gets nearby
        vec4 col0 = texelFetch(u_surf, ivec2(clamp(int(px.x), 0, int(u_res.x) - 1), 0), 0);
        float yHere = c.y - col0.r;
        float halfB = px.x - c.x;
        float fillHere = px.y > yHere ? exp(-(px.y - yHere) / (u_res.y * 0.12)) * 0.16 * smoothstep(1.0, 0.7, abs(halfB / halfW)) : 0.0;
        if (px.y > c.y - col0.g + 40.0 || px.y < c.y - col0.b - 40.0) {
            // far from the surface everywhere it could be nearest: only the water below
            fragColor = vec4(mix(u_shade, u_light, 0.75) * fillHere, fillHere) * u_opacity;
            return;
        }
        float near = 1e9;  // the nearest point of the surface: a coarse look either side, then a fine one
        float bo = 0.0;
        for (int k = -5; k <= 5; k++) {
            float o = float(k) * 4.0;
            float e = px.y - ysAt(px.x + o);
            float d2 = o * o + e * e;
            if (d2 < near) { near = d2; bo = o; }
        }
        // then the segments of the surface round it, a px apart
        float o0 = bo;
        vec2 prev = vec2(o0 - 4.0, px.y - ysAt(px.x + o0 - 4.0));
        for (int k = -3; k <= 4; k++) {
            float o = o0 + float(k);
            vec2 cur = vec2(o, px.y - ysAt(px.x + o));
            vec2 ab = cur - prev;
            float t = clamp(-dot(prev, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
            vec2 q = prev + ab * t;
            float d2 = dot(q, q);
            if (d2 < near) { near = d2; bo = q.x; }
            prev = cur;
        }
        float dist = sqrt(near);
        float xs = px.x + bo;                       // where on the surface
        float slope = (ysAt(xs - 1.0) - ysAt(xs + 1.0)) * 0.5;  // rise per px
        float ends = smoothstep(1.0, 0.7, abs((xs - c.x) / halfW));
        float core = exp(-dist * dist / 2.2);
        float glow = exp(-dist / 7.0) * 0.35;
        // slopes facing the light (upper left) shine; the glow further off evens out
        float lit = mix(0.75, clamp(0.75 - slope * 0.6, 0.35, 1.0), exp(-dist / 4.0));
        float a = clamp((core * lit + glow * lit) * ends + fillHere, 0.0, 1.0);
        vec3 col = mix(u_shade, u_light, clamp(lit + core * 0.3, 0.0, 1.0));
        fragColor = vec4(col * a, a) * u_opacity;
        return;
    }
    // a ring seen from above (flattened by the perspective)
    float halfS = min(u_res.x * 0.5, u_res.y * 0.5 / max(u_squash, 0.05)) * u_size;
    vec2 p = (px - c) / halfS;
    p.y /= max(u_squash, 0.05);
    float d = length(p);
    vec2 dir = d > 0.0 ? p / d : vec2(0.0, -1.0);
    // the side facing the light (upper left on screen, the near side brighter too)
    float facing = 0.62 + 0.38 * dot(dir, normalize(vec2(-0.55, -0.85)));
    float pxPer = halfS;                           // px per unit of d (across)
    float light = 0.0, shadow = 0.0;
    for (int i = 0; i < 4; i++) {
        vec4 w = u_waves[i];
        if (w.y <= 0.0) continue;
        float q = (d - w.x) * pxPer;               // px from the crest
        float wid = w.z * pxPer * 0.45;            // the trough's distance from the crest, px
        float crest = exp(-q * q / 3.0) + 0.45 * exp(-q * q / (wid * wid * 0.35));
        float trough = exp(-pow((abs(q) - wid) / (wid * 0.55), 2.0));
        light += w.y * crest * facing * 1.6;
        shadow += w.y * trough * (1.2 - facing);
    }
    // the bass: the middle swells, a soft glow and a bright rim round it
    float m = u_mound * (0.9 + 0.1 * sin(u_time * 6.0));
    float r0 = 0.07 + 0.05 * m;
    float rimQ = (d - r0) * pxPer;
    light += m * (0.25 * exp(-d * d / (r0 * r0 * 1.5)) * facing + 0.5 * exp(-rimQ * rimQ / 3.5) * facing);
    // and a glint where the swell faces the light
    vec2 g = p - vec2(-0.45, -0.6) * r0;
    light += m * 0.8 * exp(-dot(g, g) / (r0 * r0 * 0.06));
    float fade = smoothstep(1.08, 0.82, d);
    float a = clamp(light, 0.0, 1.0) * fade;
    float sa = clamp(shadow, 0.0, 1.0) * 0.32 * fade * (1.0 - a);
    vec3 col = mix(u_light, vec3(1.0), clamp(light - 0.7, 0.0, 1.0));
    fragColor = (vec4(col * a, a) + vec4(u_shade * sa * 0.3, sa)) * u_opacity;
}
)";

constexpr double kReach = 1.1, kEase = 0.55;  // how far a ring goes (shares of the size), and how soon it slows

class Pond final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    m_line = t["pond_form"].value_or(std::string("ring")) == "line";
    m_themed = t["pond_colors"].value_or(std::string("water")) == "theme";
    m_flatten = std::clamp(t["pond_flatten"].value_or(0.45), 0.0, 0.9);
    m_size = std::clamp(t["pond_size"].value_or(0.9), 0.3, 1.0);
    m_rings = static_cast<int>(std::clamp<int64_t>(t["pond_rings"].value_or(int64_t{1}), 1, 4));
    m_hits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
    m_theme = noct.color("primary");
    m_preview = t["pond_preview"].value_or(-1.0);
    if (m_preview >= 0) {  // a still picture: one ring this far out
      for (Wave& w : m_waves) w.age = -1;
      m_waves[0] = {m_preview, 1.0};
      m_mound = 0.5;
    }
  }

  void tick(const Beat& b) override {
    m_time += b.dt;
    if (m_preview >= 0) return;
    for (Wave& w : m_waves)
      if (w.age >= 0 && (w.age += b.dt) > 3.0) w.age = -1;
    if (b.kick) launch(std::clamp(b.strength * std::clamp(m_hits / 0.7, 0.3, 1.45), 0.35, 1.0));
    // the bass lifts the middle, falling back slowly
    double bass = 0;
    if (b.bands && !b.bands->empty()) {
      const size_t n = std::min<size_t>(4, b.bands->size());
      for (size_t i = 0; i < n; ++i) bass += (*b.bands)[i];
      bass /= static_cast<double>(n);
    }
    const double target = b.live ? std::clamp(bass * 1.2, 0.0, 1.0) : 0.0;
    m_mound += (target - m_mound) * std::min(1.0, b.dt * (target > m_mound ? 14.0 : 3.0));
    // in silence a slow wave now and then keeps the water alive
    m_idle += b.dt;
    if (!b.live && m_idle > 3.5) launch(0.35);
  }

  void draw(const DrawContext& ctx, float opacity) override {
    if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kFrag, "pond");
    std::array<float, 16> waves{};
    for (size_t i = 0; i < m_waves.size(); ++i) {
      const Wave& w = m_waves[i];
      if (w.age < 0) continue;
      // out fast, then slower; wider and lower as it goes
      const double r = m_preview >= 0 ? w.age : radius(w.age);
      const double life = std::clamp(1 - r / 1.05, 0.0, 1.0);
      waves[i * 4 + 0] = static_cast<float>(r);
      waves[i * 4 + 1] = static_cast<float>(w.strength * 0.6 * life * std::min(1.0, w.age / 0.08 + (m_preview >= 0 ? 1 : 0)));
      waves[i * 4 + 2] = static_cast<float>(0.045 + 0.06 * r);
    }
    if (m_line) surface(ctx.w, ctx.h, waves);
    glUseProgram(m_prog.id());
    auto U = [&](const char* n) { return m_prog.uniform(n); };
    if (m_line) {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, m_surfTex);
      glUniform1i(U("u_surf"), 0);
      glUniform1f(U("u_top"), m_top);
    }
    glUniform2f(U("u_res"), ctx.w, ctx.h);
    glUniform1i(U("u_line"), m_line ? 1 : 0);
    glUniform1f(U("u_squash"), static_cast<float>(1 - m_flatten));
    glUniform1f(U("u_size"), static_cast<float>(m_size));
    glUniform4fv(U("u_waves"), 4, waves.data());
    glUniform1f(U("u_mound"), static_cast<float>(m_mound));
    glUniform1f(U("u_time"), static_cast<float>(m_time));
    const Color light = m_themed ? lighter(m_theme, 0.55F) : Color{0.86F, 0.95F, 1, 1};
    const Color shade = m_themed ? m_theme.mix(Color{0, 0, 0, 1}, 0.6F) : Color{0.05F, 0.16F, 0.3F, 1};
    glUniform3f(U("u_light"), light.r, light.g, light.b);
    glUniform3f(U("u_shade"), shade.r, shade.g, shade.b);
    glUniform1f(U("u_opacity"), opacity);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawUnitQuad();
    glDisable(GL_BLEND);
  }

  [[nodiscard]] bool visible() const override { return true; }
  [[nodiscard]] bool moving() const override {
    if (m_preview >= 0) return false;
    if (m_mound > 0.01) return true;
    for (const Wave& w : m_waves)
      if (w.age >= 0) return true;
    return false;
  }
  [[nodiscard]] double idleFrame() const override { return 0.5; }

  ~Pond() override {
    if (m_surfTex) glDeleteTextures(1, &m_surfTex);
  }

private:
  struct Wave {
    double age = -1, strength = 0;
  };
  static double radius(double age) { return kReach * (1 - std::exp(-age / kEase)); }
  // the line's surface depends only on x: worked out once per column here
  // (the bass bulge, and each wave a crest flanked by troughs), not for every
  // pixel down the box
  void surface(float W, float H, const std::array<float, 16>& waves) {
    const int w = std::max(1, static_cast<int>(std::lround(W)));
    const float halfW = W * 0.5F * static_cast<float>(m_size), amp = H * 0.22F;
    const float mound = static_cast<float>(m_mound) * 0.45F * (0.9F + 0.1F * std::sin(static_cast<float>(m_time) * 6.0F));
    m_top = mound;
    for (int i = 0; i < 4; ++i) m_top += std::max(waves[i * 4 + 1], 0.0F);
    m_top *= amp;
    m_surf.assign(static_cast<size_t>(w) * 4, 0.0F);
    for (int x = 0; x < w; ++x) {
      const float ax = std::abs((static_cast<float>(x) + 0.5F - W * 0.5F) / halfW);
      float h = mound * std::exp(-ax * ax / 0.02F);
      for (int i = 0; i < 4; ++i) {
        if (waves[i * 4 + 1] <= 0) continue;
        const float q = (ax - waves[i * 4]) / (waves[i * 4 + 2] * 2.4F);  // along a line a wave is wider than a ring's crest
        h += waves[i * 4 + 1] * std::exp(-q * q * 1.6F) * std::cos(q * 2.6F);
      }
      m_surf[static_cast<size_t>(x) * 4] = h * amp;
    }
    // and the lowest and highest the surface gets within the shader's look either side
    constexpr int kLook = 26;
    for (int x = 0; x < w; ++x) {
      float lo = 1e9F, hi = -1e9F;
      for (int k = std::max(0, x - kLook); k <= std::min(w - 1, x + kLook); ++k)
        lo = std::min(lo, m_surf[static_cast<size_t>(k) * 4]), hi = std::max(hi, m_surf[static_cast<size_t>(k) * 4]);
      m_surf[static_cast<size_t>(x) * 4 + 1] = lo;
      m_surf[static_cast<size_t>(x) * 4 + 2] = hi;
    }
    if (!m_surfTex) {
      glGenTextures(1, &m_surfTex);
      glBindTexture(GL_TEXTURE_2D, m_surfTex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, m_surfTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (w != m_surfW) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, 1, 0, GL_RGBA, GL_FLOAT, m_surf.data());
      m_surfW = w;
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, 1, GL_RGBA, GL_FLOAT, m_surf.data());
    }
  }

  void launch(double strength) {
    m_idle = 0;
    if (m_rings == 1 && m_waves[0].age >= 0) {
      // one wave: a kick sends a new one once this has travelled, or if it hits harder
      const Wave& w = m_waves[0];
      const double life = std::clamp(1 - radius(w.age) / 1.05, 0.0, 1.0);
      if (radius(w.age) < 0.45 && strength < w.strength * life * 1.3) return;
    }
    // with one ring, a new kick restarts it; with more, it takes the oldest's place
    size_t slot = 0;
    for (size_t i = 0; i < static_cast<size_t>(m_rings); ++i) {
      if (m_waves[i].age < 0) {
        slot = i;
        break;
      }
      if (m_waves[i].age > m_waves[slot].age) slot = i;
    }
    m_waves[slot] = {0, strength};
  }

  bool m_line = false, m_themed = false;
  double m_flatten = 0.45, m_size = 0.9, m_hits = 0.7, m_preview = -1;
  double m_mound = 0, m_time = 0, m_idle = 0;
  int m_rings = 1;
  Color m_theme;
  std::array<Wave, 4> m_waves{};
  Program m_prog;
  std::vector<float> m_surf;
  GLuint m_surfTex = 0;
  int m_surfW = 0;
  float m_top = 0;
};

}  // namespace

std::unique_ptr<StrokeLook> makePond() { return std::make_unique<Pond>(); }

}  // namespace undershell
