// SPDX-License-Identifier: GPL-3.0-or-later
// The shape widget: one quad over the box, the outline found per pixel by a
// signed distance (Inigo Quilez's 2D distance functions, MIT), from which the
// fill, the outline, the glow and the shadow all follow.
#include "shape.hpp"

#include "depthfield.hpp"
#include "m3shapes.hpp"
#include "wallstore.hpp"

#include <array>

#include <algorithm>
#include <cmath>
#include <ranges>

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
uniform int u_fill;       // 0 solid 1 gradient 2 radial 3 none 4 wallpaper
uniform sampler2D u_wallpaper;
uniform vec4 u_patch;     // image UV at the donor centre and UV span of the shape
uniform bool u_hasPatch, u_repeat;
uniform float u_feather;
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

float sdPolygon(vec2 p, vec2 vertices[7], int count) {
    float d2 = 8.0;
    bool inside = false;
    vec2 a = vertices[count-1];
    for (int i = 0; i < count; ++i) {
        vec2 b = vertices[i], edge = b-a, nearA = p-a;
        vec2 toEdge = nearA - edge * clamp(dot(nearA,edge) / dot2(edge),0.0,1.0);
        d2 = min(d2,dot2(toEdge));
        if ((a.y > p.y) != (b.y > p.y))
            if (p.x < a.x + (p.y-a.y)*edge.x/edge.y) inside = !inside;
        a = b;
    }
    return sqrt(d2) * (inside ? -1.0 : 1.0);
}

float disc(vec2 p, vec2 center, float radius) { return length(p-center)-radius; }

float softenUnion(float a, float b, float radius) {
    float overlap = max(radius-abs(a-b),0.0);
    return min(a,b)-overlap*overlap/(4.0*radius);
}

float dab(vec2 p, vec2 a, vec2 b, float wide, float narrow) {
    vec2 edge = b-a;
    float t = clamp(dot(p-a,edge)/dot2(edge),0.0,1.0);
    return length(p-a-edge*t)-mix(wide,narrow,t);
}

float paintMark(vec2 p, int kind) {
    if (kind == 22) { // broad impact with a scattering of drops off one side
        float d = softenUnion(disc(p,vec2(-.19,.08),.55),disc(p,vec2(.24,-.14),.37),.19);
        d = softenUnion(d,dab(p,vec2(.04,-.12),vec2(.55,-.55),.11,.04),.12);
        d = softenUnion(d,dab(p,vec2(-.29,.20),vec2(-.76,.49),.14,.035),.10);
        d = min(d,disc(p,vec2(.69,-.70),.058));
        d = min(d,disc(p,vec2(.79,-.33),.084));
        d = min(d,disc(p,vec2(.58,.32),.106));
        d = min(d,disc(p,vec2(.33,.62),.074));
        return min(d,disc(p,vec2(-.17,.75),.043));
    }
    if (kind == 23) { // spray fans out into smaller isolated droplets
        float d = disc(p,vec2(-.41,.30),.38);
        d = softenUnion(d,disc(p,vec2(-.13,.12),.33),.18);
        d = softenUnion(d,dab(p,vec2(-.21,.13),vec2(.38,-.22),.23,.075),.11);
        const vec3 drops[12] = vec3[12](
            vec3(.56,-.34,.105), vec3(.57,-.09,.075), vec3(.79,-.23,.055), vec3(.28,-.48,.085),
            vec3(.46,-.64,.049), vec3(.70,-.56,.050), vec3(.84,-.76,.040), vec3(.06,-.64,.045),
            vec3(.22,.31,.094), vec3(.49,.22,.053), vec3(.72,.11,.035), vec3(-.31,-.39,.063));
        for (int i=0; i<12; ++i) d=min(d,disc(p,drops[i].xy,drops[i].z));
        return d;
    }
    if (kind == 24) { // wet paint runs down in three uneven drips
        float d = softenUnion(disc(p,vec2(-.38,-.28),.33),disc(p,vec2(.18,-.34),.48),.27);
        d = softenUnion(d,dab(p,vec2(-.43,-.19),vec2(-.50,.51),.16,.069),.17);
        d = softenUnion(d,dab(p,vec2(.03,-.26),vec2(.07,.79),.18,.079),.17);
        d = softenUnion(d,dab(p,vec2(.40,-.12),vec2(.46,.31),.12,.055),.12);
        d = min(d,disc(p,vec2(.48,.58),.077));
        return min(d,disc(p,vec2(-.75,-.64),.084));
    }
    // dry brush dragged across the surface, flecked ends and broken bristle marks
    vec2 q = vec2(.92*p.x-.39*p.y,.39*p.x+.92*p.y);
    float d = dab(q,vec2(-.74,.04),vec2(.63,-.06),.26,.28);
    d = min(d,dab(q,vec2(-.75,-.20),vec2(.78,-.25),.043,.025));
    d = min(d,dab(q,vec2(-.61,.23),vec2(.84,.23),.065,.019));
    d = max(d,-dab(q,vec2(.43,-.13),vec2(.94,-.10),.023,.040));
    d = max(d,-dab(q,vec2(-.94,.13),vec2(-.39,.08),.030,.011));
    d = min(d,disc(q,vec2(-.88,-.31),.041));
    return min(d,disc(q,vec2(.81,-.38),.036));
}

float nativeSilhouette(vec2 p, int kind) {
    if (kind == 10) return (abs(p.x)+abs(p.y)-.95)*.7071; // diamond
    if (kind == 15) return min(sdRoundBox(p,vec2(.93,.31),.08),sdRoundBox(p,vec2(.31,.93),.08)); // cross
    if (kind == 16) return max(disc(p,vec2(-.08,.01),.87),-disc(p,vec2(.42,-.19),.76)); // crescent
    if (kind == 17) { // drop
        float base = disc(p,vec2(0.0,.24),.66);
        float tip = sdTriangle(p-vec2(0.0,-.246),vec2(.58,.70));
        return softenUnion(base,tip,.12);
    }
    if (kind == 18) { // cloud
        float base = sdRoundBox(p-vec2(0.0,.28),vec2(.94,.34),.32);
        base = softenUnion(base,disc(p,vec2(-.46,.0),.38),.10);
        base = softenUnion(base,disc(p,vec2(.01,-.19),.52),.14);
        return softenUnion(base,disc(p,vec2(.48,.03),.34),.10);
    }
    if (kind == 19) { // ticket with semicircular cut-outs
        float base = sdRoundBox(p,vec2(.95,.70),.10);
        return max(base,-disc(vec2(abs(p.x),p.y),vec2(.95,0.0),.22));
    }
    if (kind == 21) { // arch with a flat bottom
        float dome = disc(p,vec2(0.0,-.12),.81);
        float plinth = sdRoundBox(p-vec2(0.0,.405),vec2(.81,.525),0.0);
        return min(dome,plinth);
    }
    if (kind >= 22) return paintMark(p,kind);
    vec2 points[7];
    int count = 4;
    if (kind == 11) {
        points[0]=vec2(-.58,-.80); points[1]=vec2(.58,-.80); points[2]=vec2(.97,.80); points[3]=vec2(-.97,.80);
    } else if (kind == 12) {
        points[0]=vec2(-.49,-.80); points[1]=vec2(.95,-.80); points[2]=vec2(.49,.80); points[3]=vec2(-.95,.80);
    } else if (kind == 13) { // chevron
        points[0]=vec2(-.96,-.28); points[1]=vec2(0.0,-.90); points[2]=vec2(.96,-.28);
        points[3]=vec2(.96,.46); points[4]=vec2(0.0,-.10); points[5]=vec2(-.96,.46); count=6;
    } else if (kind == 14) {
        points[0]=vec2(-.92,-.32); points[1]=vec2(.04,-.32); points[2]=vec2(.04,-.84); points[3]=vec2(.94,0.0);
        points[4]=vec2(.04,.84); points[5]=vec2(.04,.32); points[6]=vec2(-.92,.32); count=7;
    } else { // shield
        points[0]=vec2(-.82,-.74); points[1]=vec2(0.0,-.92); points[2]=vec2(.82,-.74);
        points[3]=vec2(.72,.34); points[4]=vec2(0.0,.91); points[5]=vec2(-.72,.34); count=6;
    }
    return sdPolygon(p,points,count);
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
    if (u_kind == 9) {
        vec2 z = q / mn + vec2(0.025, -0.048);
        float a = atan(z.y, z.x);
        float r = length(z);
        float edge = 0.43 + 0.060*cos(3.0*a-0.6) + 0.038*sin(7.0*a+1.1) + 0.021*cos(15.0*a);
        const vec4 jets[6] = vec4[6](
            vec4(0.91, -0.40, 0.34, 0.082), vec4(0.34, 0.94, 0.31, 0.125),
            vec4(-0.46, 0.89, 0.35, 0.105), vec4(-0.95, -0.32, 0.26, 0.083),
            vec4(-0.35, -0.94, 0.36, 0.078), vec4(0.66, 0.75, 0.25, 0.067));
        float d = r - edge;
        for (int i = 0; i < 6; ++i) {
            vec4 jet = jets[i];
            float along = dot(z, jet.xy);
            float crosswise = dot(z, vec2(-jet.y, jet.x));
            vec2 streak = vec2((along - (0.38 + jet.z * 0.48)) / jet.z,
                               (crosswise + 0.018 * sin(along * 9.0 + float(i))) / jet.w);
            d = min(d, (length(streak) - 1.0) * jet.w);
        }
        d = min(d, length(z - vec2(-0.81, 0.32)) - 0.071);
        d = min(d, length(z - vec2(0.70, 0.56)) - 0.048);
        d = min(d, length(z - vec2(0.57, -0.61)) - 0.048);
        d = min(d, length(z - vec2(-0.15, -0.86)) - 0.045);
        d = min(d, length(z - vec2(-0.76, -0.53)) - 0.031);
        return d * mn / scale;
    }
    if (u_kind >= 10) return nativeSilhouette(q / mn, u_kind) * mn / scale;
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
        if (u_fill == 4) {
            f = vec4(0.0);
            if (u_hasPatch && inside > 0.0) {
                vec2 uv = u_patch.xy + (v_uv - .5) * u_patch.zw;
                if (u_repeat || all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)))) {
                    vec2 halfTexel = 0.5 / vec2(textureSize(u_wallpaper, 0));
                    f = texture(u_wallpaper, u_repeat ? uv : clamp(uv, halfTexel, 1.0-halfTexel));
                    if (u_feather > 0.0) f.a *= smoothstep(0.0, u_feather, -d);
                }
            }
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

// Indices are shared with the shader. Slot 8 is the Material silhouette table.
constexpr std::array<std::string_view, 26> kShapeKinds = {
    "rect", "ellipse", "triangle", "polygon", "star", "heart", "ring", "line", "", "splatter",
    "diamond", "trapezoid", "parallelogram", "chevron", "arrow", "cross", "crescent", "drop", "cloud", "ticket", "shield", "arch",
    "splatter_ink", "splatter_spray", "splatter_drip", "splatter_brush"};

int kindOf(std::string_view shape) {
  const auto match = std::ranges::find(kShapeKinds, shape);
  return match == kShapeKinds.end() ? 8 : static_cast<int>(match - kShapeKinds.begin());
}
}  // namespace

const std::vector<std::string>& ShapeWidget::shapes() {
  static const std::vector<std::string> all = [] {
    std::vector<std::string> v;
    for (const auto& name : kShapeKinds | std::views::take(8)) v.emplace_back(name);
    for (const auto& name : kShapeKinds)
      if (name.starts_with("splatter")) v.emplace_back(name);
    for (const auto& name : kShapeKinds | std::views::drop(10))
      if (!name.starts_with("splatter")) v.emplace_back(name);
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
  if (const auto* sample = t["wallpaper_sample"].as_array(); sample && sample->size() == 2) {
    const double u = (*sample)[0].value_or(-1.0), v = (*sample)[1].value_or(-1.0);
    if (u >= 0 && u <= 1 && v >= 0 && v <= 1) c.wallpaperSample = std::array{u, v};
  }
  c.wallpaperFeather = num("wallpaper_feather", c.wallpaperFeather, 0, 64);
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
  if (m_cfg.fill != "wallpaper_clone") m_spinAt += m_cfg.spin * dt;
  m_beat *= std::exp(-dt * 7);
  if (usesAudio()) {
    if (ctx.audio.kicks != m_kicks) {
      m_kicks = ctx.audio.kicks;
      m_beat = std::max(m_beat, std::clamp(0.55 + ctx.audio.kickStrength * 0.45, 0.55, 1.0));
    } else if (!ctx.audio.onsets && !ctx.audio.silent) {  // the demo spectrum has no kicks: follow its level
      m_beat = std::max(m_beat, std::clamp(ctx.audio.energy * 1.4 - 0.3, 0.0, 1.0));
    }
  }
}

bool ShapeWidget::animating(const TickContext&) const {
  return m_cfg.fill != "wallpaper_clone" && (m_cfg.spin != 0 || m_beat > 0.004);
}

void ShapeWidget::draw(const DrawContext& ctx) {
  if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kFrag, "shape");
  const float W = ctx.w, H = ctx.h, mn = std::min(W, H) / 2;
  const bool clone = m_cfg.fill == "wallpaper_clone";
  const double shadow = clone ? 0 : m_cfg.shadow;
  const double glowBase = clone ? 0 : m_cfg.glow;
  // room round the shape for what spills out of it
  const float glowR = static_cast<float>(1.5 + mn * 0.07 * (0.4 + glowBase));
  const float shadowR = static_cast<float>(1.5 + mn * 0.09 * (0.4 + shadow));
  float pad = 1;
  if (glowBase > 0) pad = std::max(pad, glowR * 3.2F);
  if (shadow > 0) pad = std::max(pad, shadowR * 1.6F);
  float scale = 1;
  const bool pulse = !clone && m_cfg.react == "pulse";
  if (pulse) scale = static_cast<float>(0.9 + 0.1 * m_beat);  // grows into its box on a kick
  const float hx = std::max(2.0F, (W / 2 - pad) * scale), hy = std::max(2.0F, (H / 2 - pad) * scale);
  const double glow = std::clamp(glowBase + (!clone && m_cfg.react == "glow" ? 0.8 * m_beat : 0.0), 0.0, 1.0);

  glUseProgram(m_prog.id());
  glUniform2f(m_prog.uniform("u_size"), W, H);
  glUniform1f(m_prog.uniform("u_px"), 1.0F / std::max(0.25F, ctx.scale));
  glUniform1i(m_prog.uniform("u_kind"), kindOf(m_cfg.shape));
  glUniform2f(m_prog.uniform("u_half"), hx, hy);
  glUniform1f(m_prog.uniform("u_turn"), clone ? 0 : static_cast<float>(std::fmod(m_spinAt, 360.0) * M_PI / 180));
  glUniform1f(m_prog.uniform("u_sides"), static_cast<float>(m_cfg.shape == "triangle" ? 3 : m_cfg.sides));
  glUniform1f(m_prog.uniform("u_inner"), static_cast<float>(m_cfg.sharpness));
  glUniform1f(m_prog.uniform("u_corner"), static_cast<float>(m_cfg.corner));
  glUniform1f(m_prog.uniform("u_thick"), static_cast<float>(m_cfg.thickness));
  glUniform1f(m_prog.uniform("u_arc"), static_cast<float>(m_cfg.arc / 360.0));
  if (!m_radii.empty()) glUniform1fv(m_prog.uniform("u_radii"), 64, m_radii.data());
  const int fill = m_cfg.fill == "gradient" ? 1 : m_cfg.fill == "radial" ? 2 : m_cfg.fill == "none" ? 3 : clone ? 4 : 0;
  m_waitingForWallpaper = false;
  const WallPic* picture = nullptr;
  if (fill == 4 && m_cfg.wallpaperSample && !ctx.movingWallpaper && !ctx.wallpaper.empty() &&
      !ctx.wallpaper.starts_with("color:") && ctx.outputW > 0 && ctx.outputH > 0) {
    picture = wallpaperStore().get(ctx.wallpaper, wallpaperTextureSide(ctx.outputW, ctx.outputH, ctx.scale), ctx.now);
    m_waitingForWallpaper = !picture;
  }
  const bool hasPatch = picture && picture->tex;
  glUniform1i(m_prog.uniform("u_hasPatch"), hasPatch);
  if (hasPatch) {
    const auto [u, v] = *m_cfg.wallpaperSample;
    // The donor is in image space. Match the scale of the wallpaper as shown
    // on this output, so the patch preserves the size of features in the image.
    double du = 1.0 / picture->imgW, dv = 1.0 / picture->imgH;
    const bool repeat = ctx.fillMode == 4;
    if (!repeat) {
      double u0 = 0, v0 = 0;
      wallpaperUv(0, 0, ctx.outputW, ctx.outputH, picture->imgW, picture->imgH, ctx.fillMode, u0, v0);
      wallpaperUv(W, H, ctx.outputW, ctx.outputH, picture->imgW, picture->imgH, ctx.fillMode, du, dv);
      du -= u0;
      dv -= v0;
    } else {
      du *= W;
      dv *= H;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, picture->tex);
    glUniform1i(m_prog.uniform("u_wallpaper"), 0);
    glUniform4f(m_prog.uniform("u_patch"), u, v, du, dv);
    glUniform1i(m_prog.uniform("u_repeat"), repeat);
    glUniform1f(m_prog.uniform("u_feather"), std::min<float>(m_cfg.wallpaperFeather, std::min(hx, hy) * 0.5F));
  }
  glUniform1i(m_prog.uniform("u_fill"), fill);
  glUniform4f(m_prog.uniform("u_c1"), m_c1.r, m_c1.g, m_c1.b, 1);
  glUniform4f(m_prog.uniform("u_c2"), m_c2.r, m_c2.g, m_c2.b, 1);
  glUniform4f(m_prog.uniform("u_stroke"), m_strokeC.r, m_strokeC.g, m_strokeC.b, 1);
  glUniform1f(m_prog.uniform("u_angle"), static_cast<float>(m_cfg.angle));
  glUniform1f(m_prog.uniform("u_strokeW"), clone ? 0 : static_cast<float>(m_cfg.stroke));
  glUniform1f(m_prog.uniform("u_glow"), static_cast<float>(glow));
  glUniform1f(m_prog.uniform("u_glowR"), glowR);
  glUniform1f(m_prog.uniform("u_shadow"), static_cast<float>(shadow));
  glUniform1f(m_prog.uniform("u_shadowR"), shadowR);
  glUniform1f(m_prog.uniform("u_opacity"), static_cast<float>(m_cfg.opacity));
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  drawUnitQuad();
  glDisable(GL_BLEND);
}

}  // namespace undershell
