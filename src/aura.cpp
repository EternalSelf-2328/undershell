// SPDX-License-Identifier: GPL-3.0-or-later
// A fantasy aura: tongues of light flowing out of a glowing orb, round an
// empty ring (to frame another widget), or rising from the bottom like a
// hero powering up. Domain-warped noise flowing outwards gives the flames;
// the groove makes them reach further and burn brighter, a kick flares
// them; motes of light drift up through it. Seven palettes (arcane, holy,
// frost, nature, infernal, void) or the theme's colours.
#include "looks.hpp"

#include "gl.hpp"
#include "widget.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace undershell {

namespace {

const char* kFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 u_res;
uniform float u_time;
uniform int u_form;        // 0 orb, 1 ring, 2 rise
uniform float u_size;      // the orb's or the ring's radius, a share of the box
uniform float u_reach;     // how far the flames go, a share of the box
uniform float u_energy;    // the groove, 0..1
uniform float u_flare;     // a kick, fading
uniform float u_motes;     // how many motes of light
uniform vec3 u_inner, u_outer, u_core;
uniform float u_opacity;
uniform sampler2D u_noise;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
// value noise from a texture of random values: two channels, the second the
// first shifted by (37, 17), so one read gives two layers of z to blend --
// one fetch where hashing the eight corners cost eight
float noise(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    vec2 uv = (i.xy + vec2(37.0, 17.0) * i.z) + f.xy;
    vec2 rg = textureLod(u_noise, (uv + 0.5) / 256.0, 0.0).yx;
    return mix(rg.x, rg.y, f.z);
}
float fbm(vec3 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * noise(p);
        p = p * 2.03 + vec3(1.7, 9.2, 3.1);
        a *= 0.5;
    }
    return v;
}
// two octaves: for the slow swell under the tongues
float fbm2(vec3 p) { return 0.5 * noise(p) + 0.25 * noise(p * 2.03 + vec3(1.7, 9.2, 3.1)); }

// the colour of a flame of strength v (0 nothing, 1 the hottest)
vec4 flame(float v) {
    v = clamp(v, 0.0, 1.2);
    vec3 c = mix(u_outer, u_inner, smoothstep(0.2, 0.7, v));
    c = mix(c, u_core, smoothstep(0.85, 1.15, v));
    float a = smoothstep(0.03, 0.7, v) * 0.9;
    return vec4(c * a, a);
}
// thin tongues: the ridges of the noise
float tongues(vec3 p) { return pow(1.0 - abs(fbm(p) * 2.0 - 1.0), 3.0); }

void main() {
    vec2 px = v_uv * u_res;
    float m = min(u_res.x, u_res.y);
    float t = u_time;
    float reach = u_reach * (0.55 + 0.7 * u_energy + 0.5 * u_flare);
    float v = 0.0;      // the flames' strength here
    float glow = 0.0;   // the soft light round them
    vec4 core = vec4(0.0);
    if (u_form == 2) {
        // rising from the bottom edge
        float x = (px.x - u_res.x * 0.5) / m, y = (u_res.y - px.y) / m;   // y: height above the bottom
        vec3 q = vec3(x * 4.5, y * 1.4 - t * 1.5, t * 0.3);
        float n = fbm2(q * 0.7) * 1.33;
        float side = smoothstep(0.5 * u_res.x / m, 0.5 * u_res.x / m - 0.12, abs(x));
        float room = u_res.y / m;                                          // up to the top edge
        float rr = min(reach, room / 2.3);                                 // the tallest tongues stop short of it
        float topFade = smoothstep(room, room - 0.15, y);
        if (y > rr * 2.3) {  // above the tallest tongue and its glow: nothing to work out
            fragColor = vec4(0.0);
            return;
        }
        v = (tongues(q) * 1.05 + n * 0.35 - y / max(rr * 1.5, 0.05)) * side * 0.85 * topFade;  // less white at the foot
        glow = exp(-y / max(rr * 0.6, 0.02)) * 0.3 * side * topFade;
    } else {
        vec2 c = u_res * 0.5;
        vec2 p = (px - c) / (m * 0.5);
        float r = length(p);
        float a = atan(p.y, p.x);
        vec2 dir = r > 0.0 ? p / r : vec2(0.0, -1.0);
        float R = u_size;
        // the room past the edge toward the box's side this way: the flames fit in it
        vec2 half_ = u_res / m;
        float toEdge = min(half_.x / max(abs(dir.x), 1e-3), half_.y / max(abs(dir.y), 1e-3));
        float room = max(toEdge - R, 0.05);
        float edgeFade = smoothstep(0.0, 0.15, min(half_.x - abs(p.x), half_.y - abs(p.y)));
        float out_ = r - R;                                    // past the orb's / ring's edge
        float tall = 1.0 + 0.8 * max(-dir.y, 0.0);             // an aura burns taller above
        float reachOut = max(min(reach * tall, room / 1.6), 0.03);
        // past the longest tongue and its glow, or inside the orb: no flames to work out
        if (out_ > reachOut * 2.2 || (u_form == 1 && -out_ > max(reach * 0.35, 0.03) * 2.2)) {
            fragColor = vec4(0.0);
            return;
        }
        float lick = 0.0;
        if (u_form != 0 || out_ > 0.0) {
            vec3 ring = vec3(cos(a), sin(a), 0.0) * 3.0;      // seamless all the way round
            vec3 q = ring + vec3(0.0, 0.0, out_ * 1.6 / tall - t * 1.3);
            lick = tongues(q) * 1.1 + fbm2(q * 0.6 + 3.0) * 0.53;
        }
        if (u_form == 0) {
            float reachHere = reachOut;
            v = out_ > 0.0 ? (lick - out_ / reachHere) * edgeFade : 0.0;
            glow = out_ > 0.0 ? exp(-out_ / (reachHere * 0.45)) * 0.45 * edgeFade : 0.0;
            // the orb itself: white at the heart, its colour toward the rim, swirling
            if (r < R + 0.02) {
                float k = clamp(r / R, 0.0, 1.0);
                float s = fbm(vec3(p * 4.0, t * 0.7));
                // bright to the rim (a dark edge would read as a ball, not a light)
                float lum = (1.0 - k * k * 0.3) * (0.85 + 0.25 * s) * smoothstep(R + 0.02, R - 0.02, r);
                vec3 cc = mix(u_core, u_inner, smoothstep(0.1, 1.0, k));
                core = vec4(cc * lum, lum);
            }
        } else {
            // a band round an empty middle: flames out and a little in
            float d = abs(out_);
            float reachHere = out_ > 0.0 ? reachOut : max(reach * 0.35, 0.03);
            v = lick - d / reachHere;
            v += exp(-d * d * 900.0) * 0.8;                 // the ring's bright line
            v *= edgeFade;
            glow = exp(-d / (reachHere * 0.4)) * 0.4 * edgeFade;
        }
    }
    float boost = 0.8 + 0.45 * u_energy + 0.55 * u_flare;
    vec4 col = flame(v * boost);
    float g = clamp(glow * boost, 0.0, 1.0) * 0.55;
    col += vec4(u_outer * g, g) * (1.0 - col.a);
    col = core + col * (1.0 - core.a);
    // motes of light drifting up through it
    if (u_motes > 0.0) {
        vec2 q = px / m * 7.0;
        q.y += t * 0.8;
        vec2 cell = floor(q);
        vec2 f = fract(q) - 0.5;
        float h = hash(vec3(cell, 7.0));
        if (h < u_motes) {
            vec2 off = vec2(hash(vec3(cell, 1.0)), hash(vec3(cell, 2.0))) - 0.5;
            off.x += sin(t * 1.3 + h * 40.0) * 0.18;
            float d = length(f - off * 0.6);
            float tw = 0.55 + 0.45 * sin(t * (3.0 + h * 5.0) + h * 30.0);
            float near = smoothstep(-0.1, 0.4, v + glow);
            float mote = (exp(-d * d * 140.0) + 0.35 * exp(-d * d * 18.0)) * tw * near;
            col += vec4(mix(u_core, u_inner, 0.3) * mote, mote) * (1.0 - col.a * 0.6);
        }
    }
    fragColor = clamp(col, 0.0, 1.0) * u_opacity;
}
)";

struct Palette {
  const char* name;
  Color inner, outer, core;
};
const Palette kPalettes[] = {
    {"arcane", Color::fromHex("#4fd2ff"), Color::fromHex("#7a3cff"), Color::fromHex("#f2f8ff")},
    {"holy", Color::fromHex("#ffd36b"), Color::fromHex("#ff9a1f"), Color::fromHex("#fffbea")},
    {"frost", Color::fromHex("#9fe8ff"), Color::fromHex("#2f7dd6"), Color::fromHex("#ffffff")},
    {"nature", Color::fromHex("#9cff8a"), Color::fromHex("#1f9e5a"), Color::fromHex("#f4ffe9")},
    {"infernal", Color::fromHex("#ffb12a"), Color::fromHex("#d4200f"), Color::fromHex("#fff1c9")},
    {"void", Color::fromHex("#e07bff"), Color::fromHex("#3a0f78"), Color::fromHex("#ffe9ff")},
};

class Aura final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    const std::string form = t["aura_form"].value_or(std::string("orb"));
    m_form = form == "ring" ? 1 : form == "rise" ? 2 : 0;
    const std::string colors = t["aura_colors"].value_or(std::string("arcane"));
    m_size = std::clamp(t["aura_size"].value_or(0.32), 0.1, 0.7);
    m_reach = std::clamp(t["aura_reach"].value_or(0.45), 0.1, 1.0);
    m_motes = std::clamp(t["aura_motes"].value_or(0.35), 0.0, 1.0);
    m_speed = std::clamp(t["aura_speed"].value_or(1.0), 0.2, 3.0);
    m_hits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
    m_preview = t["aura_preview"].value_or(-1.0);
    if (colors == "theme") {
      m_inner = lighter(noct.color("primary"), 0.25F);
      m_outer = noct.color("tertiary", noct.color("primary"));
      m_core = lighter(noct.color("primary"), 0.85F);
    } else {
      const Palette* p = &kPalettes[0];
      for (const auto& q : kPalettes)
        if (colors == q.name) p = &q;
      m_inner = p->inner, m_outer = p->outer, m_core = p->core;
    }
    if (m_preview >= 0) {
      m_time = m_preview;
      m_energy = 0.55;
    }
  }

  void tick(const Beat& b) override {
    if (m_preview >= 0) return;
    // the flames flow faster with the groove
    m_time += b.dt * m_speed * (0.7 + 0.8 * m_energy);
    const double target = b.live ? std::clamp(b.energy * 1.5, 0.0, 1.0) : 0.15;
    m_energy += (target - m_energy) * std::min(1.0, b.dt * 3.0);
    m_flare *= std::exp(-b.dt * 4.0);
    if (b.kick) m_flare = std::max(m_flare, std::clamp(b.strength, 0.3, 1.0) * m_hits / 0.7);
  }

  void draw(const DrawContext& ctx, float opacity) override {
    if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kFrag, "aura");
    if (!m_noise) makeNoise();
    glUseProgram(m_prog.id());
    auto U = [&](const char* n) { return m_prog.uniform(n); };
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_noise);
    glUniform1i(U("u_noise"), 0);
    glUniform2f(U("u_res"), ctx.w, ctx.h);
    glUniform1f(U("u_time"), static_cast<float>(std::fmod(m_time, 1000.0)));
    glUniform1i(U("u_form"), m_form);
    glUniform1f(U("u_size"), static_cast<float>(m_size));
    glUniform1f(U("u_reach"), static_cast<float>(m_reach));
    glUniform1f(U("u_energy"), static_cast<float>(m_energy));
    glUniform1f(U("u_flare"), static_cast<float>(std::min(1.0, m_flare)));
    glUniform1f(U("u_motes"), static_cast<float>(m_motes * 0.45));
    glUniform3f(U("u_inner"), m_inner.r, m_inner.g, m_inner.b);
    glUniform3f(U("u_outer"), m_outer.r, m_outer.g, m_outer.b);
    glUniform3f(U("u_core"), m_core.r, m_core.g, m_core.b);
    glUniform1f(U("u_opacity"), opacity);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawUnitQuad();
    glDisable(GL_BLEND);
  }

  [[nodiscard]] bool visible() const override { return true; }
  [[nodiscard]] bool moving() const override { return m_preview < 0; }
  // in silence it still flows, slowly: a few frames a second
  [[nodiscard]] double idleFrame() const override { return 1.0 / 20; }

  ~Aura() override {
    if (m_noise) glDeleteTextures(1, &m_noise);
  }

private:
  void makeNoise() {
    constexpr int N = 256;
    std::vector<unsigned char> r(N * N), px(N * N * 2);
    uint32_t seed = 0x9e3779b9U;
    for (auto& v : r) {
      seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
      v = static_cast<unsigned char>(seed >> 24);
    }
    for (int y = 0; y < N; ++y)
      for (int x = 0; x < N; ++x) {
        px[(y * N + x) * 2] = r[y * N + x];
        px[(y * N + x) * 2 + 1] = r[((y - 17 + N) % N) * N + (x - 37 + N) % N];
      }
    glGenTextures(1, &m_noise);
    glBindTexture(GL_TEXTURE_2D, m_noise);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, N, N, 0, GL_RG, GL_UNSIGNED_BYTE, px.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }

  int m_form = 0;
  double m_size = 0.32, m_reach = 0.45, m_motes = 0.35, m_speed = 1, m_hits = 0.7, m_preview = -1;
  double m_time = 0, m_energy = 0.15, m_flare = 0;
  Color m_inner, m_outer, m_core;
  Program m_prog;
  GLuint m_noise = 0;
};

}  // namespace

std::unique_ptr<StrokeLook> makeAura() { return std::make_unique<Aura>(); }

}  // namespace undershell
