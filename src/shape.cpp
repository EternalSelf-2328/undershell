// SPDX-License-Identifier: GPL-3.0-or-later
// The shape widget: one quad over the box, the outline found per pixel by a
// signed distance (Inigo Quilez's 2D distance functions, MIT), from which the
// fill, the outline, the glow and the shadow all follow.
#include "shape.hpp"

#include "m3shapes.hpp"

#include <algorithm>
#include <cmath>

namespace undershell {

namespace {
template <typename T>
T get(const toml::table& t, std::string_view key, T def) {
  if (auto v = t[key].value<T>()) return *v;
  return def;
}

const char* kFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 u_size;      // the box, logical px
uniform float u_px;       // one device pixel, in logical px (antialiasing)
uniform int u_kind;       // 0 rect 1 ellipse 2 triangle 3 polygon 4 star 5 heart 6 ring 7 line 8 radial table
uniform vec2 u_half;      // the shape's half size
uniform float u_turn;     // radians
uniform float u_sides, u_inner, u_corner, u_thick, u_arc;
uniform float u_radii[64];
uniform int u_fill;       // 0 solid 1 gradient 2 radial 3 none
uniform vec4 u_c1, u_c2, u_stroke;
uniform float u_angle, u_strokeW, u_glow, u_glowR, u_shadow, u_shadowR, u_opacity;

const float PI = 3.14159265;

float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
float sdEllipse(vec2 p, vec2 ab) {
    if (abs(ab.x - ab.y) < 0.5) return length(p) - ab.x;
    p = abs(p);
    if (p.x > p.y) { p = p.yx; ab = ab.yx; }
    float l = ab.y * ab.y - ab.x * ab.x;
    float m = ab.x * p.x / l, m2 = m * m;
    float n = ab.y * p.y / l, n2 = n * n;
    float c = (m2 + n2 - 1.0) / 3.0, c3 = c * c * c;
    float q = c3 + m2 * n2 * 2.0;
    float d = c3 + m2 * n2;
    float g = m + m * n2;
    float co;
    if (d < 0.0) {
        float h = acos(clamp(q / c3, -1.0, 1.0)) / 3.0;
        float s = cos(h), t = sin(h) * sqrt(3.0);
        float rx = sqrt(max(-c * (s + t + 2.0) + m2, 0.0));
        float ry = sqrt(max(-c * (s - t + 2.0) + m2, 0.0));
        co = (ry + sign(l) * rx + abs(g) / max(rx * ry, 1e-6) - m) / 2.0;
    } else {
        float h = 2.0 * m * n * sqrt(d);
        float s = sign(q + h) * pow(abs(q + h), 1.0 / 3.0);
        float u = sign(q - h) * pow(abs(q - h), 1.0 / 3.0);
        float rx = -s - u - c * 4.0 + 2.0 * m2;
        float ry = (s - u) * sqrt(3.0);
        float rm = sqrt(rx * rx + ry * ry);
        co = (ry / sqrt(max(rm - rx, 1e-6)) + 2.0 * g / max(rm, 1e-6) - m) / 2.0;
    }
    co = clamp(co, 0.0, 1.0);
    vec2 r = ab * vec2(co, sqrt(1.0 - co * co));
    return length(r - p) * sign(p.y - r.y);
}
// apex up, base on the box's bottom
float sdTriangle(vec2 p, vec2 h) {
    vec2 q = vec2(h.x, 2.0 * h.y);
    p.y += h.y;
    p.x = abs(p.x);
    vec2 a = p - q * clamp(dot(p, q) / dot(q, q), 0.0, 1.0);
    vec2 b = p - q * vec2(clamp(p.x / q.x, 0.0, 1.0), 1.0);
    float s = -sign(q.y);
    vec2 d = min(vec2(dot(a, a), s * (p.x * q.y - p.y * q.x)), vec2(dot(b, b), s * (p.y - q.y)));
    return -sqrt(d.x) * sign(d.y);
}
// a star with n points (m = 2 a polygon .. n needles), a point straight up
float sdStar(vec2 p, float r, float n, float m) {
    float an = PI / n, en = PI / m;
    vec2 acs = vec2(cos(an), sin(an)), ecs = vec2(cos(en), sin(en));
    float bn = mod(atan(p.x, -p.y), 2.0 * an) - an;
    p = length(p) * vec2(cos(bn), abs(sin(bn)));
    p -= r * acs;
    p += ecs * clamp(-dot(p, ecs), 0.0, r * acs.y / ecs.y);
    return length(p) * sign(p.x);
}
float dot2(vec2 v) { return dot(v, v); }
float sdHeart(vec2 p) {  // unit heart: tip at the origin, y up
    p.x = abs(p.x);
    if (p.y + p.x > 1.0) return sqrt(dot2(p - vec2(0.25, 0.75))) - sqrt(2.0) / 4.0;
    return sqrt(min(dot2(p - vec2(0.0, 1.0)), dot2(p - 0.5 * max(p.x + p.y, 0.0)))) * sign(p.x - p.y);
}
// a ring's band of half width rb at radius ra, open at the bottom when the arc is short of a turn
float sdArc(vec2 p, float half_, float ra, float rb) {
    p.y = -p.y;
    p.x = abs(p.x);
    vec2 sc = vec2(sin(half_), cos(half_));
    return ((sc.y * p.x > sc.x * p.y) ? length(p - sc * ra) : abs(length(p) - ra)) - rb;
}
float radial(vec2 p, float R) {
    float a = atan(p.y, p.x);
    if (a < 0.0) a += 2.0 * PI;
    float f = a / (2.0 * PI) * 64.0;
    int i0 = int(floor(f)) % 64, i1 = (i0 + 1) % 64;
    float r = mix(u_radii[i0], u_radii[i1], fract(f)) * R;
    return (length(p) - r) * 0.9;
}

float shapeDist(vec2 p) {
    vec2 h = u_half;
    float mn = min(h.x, h.y);
    float k = u_corner * mn * 0.5;  // the rounding
    if (u_kind == 0) return sdRoundBox(p, h, u_corner * mn);
    if (u_kind == 1) return sdEllipse(p, h);
    if (u_kind == 2) return sdTriangle(p, max(h - k, vec2(1.0))) - k;
    // the rest are drawn round: squeezed to the box's shape
    vec2 sq = vec2(mn / h.x, mn / h.y);
    vec2 q = p * sq;
    float scale = min(sq.x, sq.y);
    if (u_kind == 3 || u_kind == 4) {
        // m = 2 is the polygon; nearer n the points get thinner
    float m = u_kind == 3 ? 2.0 : mix(2.0, u_sides - 0.35 * (u_sides - 2.0), u_inner);
        return (sdStar(q, mn - k, u_sides, m) - k) / scale;
    }
    if (u_kind == 5) {  // the heart fills its box
        float s = min(2.0 * h.x / 1.21, 2.0 * h.y / 1.11);
        return sdHeart(vec2(p.x, -p.y) / s + vec2(0.0, 0.555)) * s;
    }
    if (u_kind == 6) {
        float rb = max(u_thick * mn * 0.5, 0.5);
        return sdArc(q, clamp(u_arc, 0.01, 1.0) * PI, mn - rb, rb) / scale;
    }
    if (u_kind == 7) {
        float t = max(u_thick * h.y, 0.5);
        return sdRoundBox(p, vec2(h.x, t), u_corner * t);
    }
    return radial(q, mn) / scale;
}

vec4 over(vec4 top, vec4 under) { return top + under * (1.0 - top.a); }

void main() {
    vec2 p = v_uv * u_size - u_size * 0.5;
    float c = cos(u_turn), s = sin(u_turn);
    p = vec2(c * p.x + s * p.y, -s * p.x + c * p.y);
    float d = shapeDist(p);
    float aa = u_px;
    float inside = clamp(0.5 - d / aa, 0.0, 1.0);
    vec4 col = vec4(0.0);
    if (u_shadow > 0.0) {  // soft, below and a little down
        float ds = shapeDist(p - vec2(0.0, u_shadowR * 0.45));
        float a = u_shadow * 0.55 * (1.0 - smoothstep(-u_shadowR, u_shadowR, ds));
        col = vec4(0.0, 0.0, 0.0, a);
    }
    vec4 glowC = u_strokeW > 0.0 ? u_stroke : u_c1;
    if (u_glow > 0.0 && d > 0.0) {
        float a = u_glow * exp(-d / u_glowR) * (1.0 - inside);
        col = over(vec4(glowC.rgb * a, a), col);
    }
    if (u_fill != 3) {
        vec4 f = u_c1;
        if (u_fill == 1) {
            vec2 dir = vec2(cos(radians(u_angle)), sin(radians(u_angle)));
            float t = dot(p, dir) / max(abs(dir.x) * u_half.x + abs(dir.y) * u_half.y, 1.0) * 0.5 + 0.5;
            f = mix(u_c1, u_c2, clamp(t, 0.0, 1.0));
        } else if (u_fill == 2) {
            f = mix(u_c1, u_c2, clamp(length(p / u_half), 0.0, 1.0));
        }
        float a = inside * f.a;
        col = over(vec4(f.rgb * a, a), col);
    }
    if (u_strokeW > 0.0) {  // inside the edge, so the shape keeps its size
        float band = inside * clamp(0.5 + (d + u_strokeW) / aa, 0.0, 1.0);
        float a = band * u_stroke.a;
        col = over(vec4(u_stroke.rgb * a, a), col);
    }
    fragColor = col * u_opacity;
}
)";

int kindOf(const std::string& s) {
  static const char* names[] = {"rect", "ellipse", "triangle", "polygon", "star", "heart", "ring", "line"};
  for (int i = 0; i < 8; ++i)
    if (s == names[i]) return i;
  return 8;  // a Material shape
}
}  // namespace

const std::vector<std::string>& ShapeWidget::shapes() {
  static const std::vector<std::string> all = [] {
    std::vector<std::string> v = {"rect", "ellipse", "triangle", "polygon", "star", "heart", "ring", "line"};
    for (const auto& n : m3ShapeNames())
      if (n != "circle" && n != "square" && n != "oval" && n != "pentagon") v.push_back(n);  // those are above already
    return v;
  }();
  return all;
}

ShapeConfig ShapeConfig::fromTable(const toml::table& t) {
  ShapeConfig c;
  c.shape = get<std::string>(t, "shape", c.shape);
  c.sides = static_cast<int>(std::clamp<int64_t>(get<int64_t>(t, "sides", c.sides), 3, 16));
  auto num = [&](const char* key, double def, double lo, double hi) {
    double v = def;
    if (auto d = t[key].value<double>()) v = *d;
    return std::clamp(v, lo, hi);
  };
  c.sharpness = num("sharpness", c.sharpness, 0, 1);
  c.corner = num("corner", c.corner, 0, 1);
  c.thickness = num("thickness", c.thickness, 0.02, 1);
  c.arc = num("arc", c.arc, 10, 360);
  c.fill = get<std::string>(t, "fill", c.fill);
  c.color = get<std::string>(t, "color", c.color);
  c.color2 = get<std::string>(t, "color2", c.color2);
  c.angle = num("angle", c.angle, 0, 360);
  c.stroke = num("stroke", c.stroke, 0, 48);
  c.strokeColor = get<std::string>(t, "stroke_color", c.strokeColor);
  c.glow = num("glow", c.glow, 0, 1);
  c.shadow = num("shadow", c.shadow, 0, 1);
  c.react = get<std::string>(t, "react", c.react);
  c.spin = num("spin", c.spin, -360, 360);
  c.opacity = num("opacity", c.opacity, 0, 1);
  return c;
}

void ShapeWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) { configure(ShapeConfig::fromTable(cfg.options), noct); }

void ShapeWidget::configure(const ShapeConfig& cfg, const NoctaliaState& noct) {
  m_cfg = cfg;
  m_c1 = noct.color(cfg.color, Color::fromHex("#e2342a"));
  m_c2 = noct.color(cfg.color2, Color::fromHex("#f5c96b"));
  m_strokeC = noct.color(cfg.strokeColor, Color::fromHex("#f4f1ea"));
  m_radii.clear();
  if (kindOf(cfg.shape) == 8) {
    m_radii = m3ShapeRadii(cfg.shape, 64);
    // in units of half the box's longer side: the widest reach is 1
    float most = 0;
    for (float r : m_radii) most = std::max(most, r);
    if (most > 0)
      for (float& r : m_radii) r /= most;
  }
}

void ShapeWidget::tick(const TickContext& ctx) {
  const double dt = ctx.dt;
  m_now = ctx.now;
  if (m_cfg.spin != 0) m_spinAt += m_cfg.spin * dt;
  m_beat *= std::exp(-dt * 7);
  if (m_cfg.react != "none") {
    if (ctx.audio.kicks != m_kicks) {
      m_kicks = ctx.audio.kicks;
      m_beat = std::max(m_beat, std::clamp(0.55 + ctx.audio.kickStrength * 0.45, 0.55, 1.0));
    } else if (!ctx.audio.onsets && !ctx.audio.silent) {  // the demo spectrum has no kicks: follow its level
      m_beat = std::max(m_beat, std::clamp(ctx.audio.energy * 1.4 - 0.3, 0.0, 1.0));
    }
  }
}

bool ShapeWidget::animating(const TickContext&) const { return m_cfg.spin != 0 || m_beat > 0.004; }

void ShapeWidget::draw(const DrawContext& ctx) {
  if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kFrag, "shape");
  const float W = ctx.w, H = ctx.h, mn = std::min(W, H) / 2;
  // room round the shape for what spills out of it
  const float glowR = static_cast<float>(1.5 + mn * 0.07 * (0.4 + m_cfg.glow));
  const float shadowR = static_cast<float>(1.5 + mn * 0.09 * (0.4 + m_cfg.shadow));
  float pad = 1;
  if (m_cfg.glow > 0) pad = std::max(pad, glowR * 3.2F);
  if (m_cfg.shadow > 0) pad = std::max(pad, shadowR * 1.6F);
  float scale = 1;
  const bool pulse = m_cfg.react == "pulse";
  if (pulse) scale = static_cast<float>(0.9 + 0.1 * m_beat);  // grows into its box on a kick
  const float hx = std::max(2.0F, (W / 2 - pad) * scale), hy = std::max(2.0F, (H / 2 - pad) * scale);
  const double glow = std::clamp(m_cfg.glow + (m_cfg.react == "glow" ? 0.8 * m_beat : 0.0), 0.0, 1.0);

  glUseProgram(m_prog.id());
  glUniform2f(m_prog.uniform("u_size"), W, H);
  glUniform1f(m_prog.uniform("u_px"), 1.0F / std::max(0.25F, ctx.scale));
  glUniform1i(m_prog.uniform("u_kind"), kindOf(m_cfg.shape));
  glUniform2f(m_prog.uniform("u_half"), hx, hy);
  glUniform1f(m_prog.uniform("u_turn"), static_cast<float>(std::fmod(m_spinAt, 360.0) * M_PI / 180));
  glUniform1f(m_prog.uniform("u_sides"), static_cast<float>(m_cfg.shape == "triangle" ? 3 : m_cfg.sides));
  glUniform1f(m_prog.uniform("u_inner"), static_cast<float>(m_cfg.sharpness));
  glUniform1f(m_prog.uniform("u_corner"), static_cast<float>(m_cfg.corner));
  glUniform1f(m_prog.uniform("u_thick"), static_cast<float>(m_cfg.thickness));
  glUniform1f(m_prog.uniform("u_arc"), static_cast<float>(m_cfg.arc / 360.0));
  if (!m_radii.empty()) glUniform1fv(m_prog.uniform("u_radii"), 64, m_radii.data());
  const int fill = m_cfg.fill == "gradient" ? 1 : m_cfg.fill == "radial" ? 2 : m_cfg.fill == "none" ? 3 : 0;
  glUniform1i(m_prog.uniform("u_fill"), fill);
  glUniform4f(m_prog.uniform("u_c1"), m_c1.r, m_c1.g, m_c1.b, 1);
  glUniform4f(m_prog.uniform("u_c2"), m_c2.r, m_c2.g, m_c2.b, 1);
  glUniform4f(m_prog.uniform("u_stroke"), m_strokeC.r, m_strokeC.g, m_strokeC.b, 1);
  glUniform1f(m_prog.uniform("u_angle"), static_cast<float>(m_cfg.angle));
  glUniform1f(m_prog.uniform("u_strokeW"), static_cast<float>(m_cfg.stroke));
  glUniform1f(m_prog.uniform("u_glow"), static_cast<float>(glow));
  glUniform1f(m_prog.uniform("u_glowR"), glowR);
  glUniform1f(m_prog.uniform("u_shadow"), static_cast<float>(m_cfg.shadow));
  glUniform1f(m_prog.uniform("u_shadowR"), shadowR);
  glUniform1f(m_prog.uniform("u_opacity"), static_cast<float>(m_cfg.opacity));
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  drawUnitQuad();
  glDisable(GL_BLEND);
}

}  // namespace undershell
