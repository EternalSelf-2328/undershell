// SPDX-License-Identifier: GPL-3.0-or-later
// Rigged companions (petrig.hpp): the creature is rasterised part by part on
// a small grid, back to front, each part shaded like a lit volume and
// quantised to a ramp of four shades; then the parts that overlap get a
// contour, the silhouette an outline in a darker shade of what it borders,
// and the eyes, nose and mouth are placed pixel by pixel.
#include "petrig.hpp"

#include <algorithm>
#include <cmath>

namespace undershell {

// ── Colour ──────────────────────────────────────────────────────────────────

namespace {
float lin(float c) { return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F); }
float gam(float c) {
  c = std::clamp(c, 0.0F, 1.0F);
  return c <= 0.0031308F ? c * 12.92F : 1.055F * std::pow(c, 1 / 2.4F) - 0.055F;
}
}  // namespace

Oklch toOklch(Color c) {
  const float r = lin(c.r), g = lin(c.g), b = lin(c.b);
  const float l = std::cbrt(0.4122214708F * r + 0.5363325363F * g + 0.0514459929F * b);
  const float m = std::cbrt(0.2119034982F * r + 0.6806995451F * g + 0.1073969566F * b);
  const float s = std::cbrt(0.0883024619F * r + 0.2817188376F * g + 0.6299787005F * b);
  const float L = 0.2104542553F * l + 0.7936177850F * m - 0.0040720468F * s;
  const float A = 1.9779984951F * l - 2.4285922050F * m + 0.4505937099F * s;
  const float B = 0.0259040371F * l + 0.7827717662F * m - 0.8086757660F * s;
  float h = std::atan2(B, A) * 57.29578F;
  if (h < 0) h += 360;
  return {L, std::hypot(A, B), h};
}

namespace {
Color oklabToRgb(float L, float A, float B, bool& inGamut) {
  const float l = std::pow(L + 0.3963377774F * A + 0.2158037573F * B, 3.0F);
  const float m = std::pow(L - 0.1055613458F * A - 0.0638541728F * B, 3.0F);
  const float s = std::pow(L - 0.0894841775F * A - 1.2914855480F * B, 3.0F);
  const float r = 4.0767416621F * l - 3.3077115913F * m + 0.2309699292F * s;
  const float g = -1.2684380046F * l + 2.6097574011F * m - 0.3413193965F * s;
  const float b = -0.0041960863F * l - 0.7034186147F * m + 1.7076147010F * s;
  inGamut = r >= -0.001F && r <= 1.001F && g >= -0.001F && g <= 1.001F && b >= -0.001F && b <= 1.001F;
  return {gam(r), gam(g), gam(b), 1};
}
}  // namespace

Color fromOklch(Oklch o) {
  o.L = std::clamp(o.L, 0.0F, 1.0F);
  // keep the lightness and the hue, give up chroma until it fits
  for (int i = 0; i < 24; ++i) {
    bool ok = false;
    const float a = o.C * std::cos(o.h / 57.29578F), b = o.C * std::sin(o.h / 57.29578F);
    const Color c = oklabToRgb(o.L, a, b, ok);
    if (ok || o.C < 0.002F) return c;
    o.C *= 0.86F;
  }
  bool ok = false;
  return oklabToRgb(o.L, 0, 0, ok);
}

Color rigTint(Color c, float L) {
  Oklch o = toOklch(c);
  o.L = L;
  o.C = std::min(o.C * 0.25F, 0.035F);
  return fromOklch(o);
}

RigPalette rigPaletteFrom(const std::vector<Color>& dominant, const RigPalette& fallback) {
  if (dominant.empty()) return fallback;
  std::vector<Oklch> d;
  for (const Color& c : dominant) d.push_back(toOklch(c));
  // the body: lively and common (earlier = more common), neither near black nor near white
  int body = 0;
  float best = -1;
  for (size_t i = 0; i < d.size(); ++i) {
    const float mid = 1 - std::abs(d[i].L - 0.65F) * 1.6F;
    const float score = (d[i].C + 0.02F) * std::max(0.1F, mid) * (1.0F - 0.08F * static_cast<float>(i));
    if (score > best) best = score, body = static_cast<int>(i);
  }
  auto hueGap = [](float a, float b) { return std::abs(std::fmod(a - b + 540.0F, 360.0F) - 180.0F); };
  Oklch b = d[body];
  b.L = std::clamp(b.L, 0.5F, 0.82F);
  b.C = std::max(b.C, 0.06F);
  // markings: another common hue, deeper; failing that, the body's own hue, deeper
  Oklch m{std::max(0.28F, b.L - 0.3F), b.C * 0.8F, b.h};
  for (size_t i = 0; i < d.size(); ++i)
    if (static_cast<int>(i) != body && d[i].C > 0.04F && hueGap(d[i].h, b.h) > 35) {
      m = d[i];
      m.L = std::clamp(m.L, 0.25F, b.L - 0.12F);
      break;
    }
  // eyes: the hue furthest from the body's among them (or its complement), clear
  Oklch e{0.55F, 0.14F, std::fmod(b.h + 180.0F, 360.0F)};
  float far = 60;
  for (const Oklch& o : d)
    if (o.C > 0.05F && hueGap(o.h, b.h) > far) far = hueGap(o.h, b.h), e = {0.58F, std::max(0.1F, o.C), o.h};
  RigPalette p;
  p.body = fromOklch(b);
  p.belly = rigTint(p.body, 0.95F);
  p.mark = fromOklch(m);
  p.eyes = fromOklch(e);
  p.extra = fromOklch({0.84F, 0.12F, std::fmod(b.h + 60.0F, 360.0F)});
  return p;
}

namespace {

// a hue moved towards `to` by at most `deg`
float hueTowards(float h, float to, float deg) {
  float d = std::fmod(to - h + 540.0F, 360.0F) - 180.0F;
  d = std::clamp(d, -deg, deg);
  return std::fmod(h + d + 360.0F, 360.0F);
}

struct Ramp {
  std::array<std::uint32_t, 4> shade{};  // dark .. light (0xAABBGGRR as bytes r g b a)
  std::uint32_t contour = 0, outline = 0;
};

std::uint32_t pack(Color c) {
  auto b = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255)); };
  return b(c.r) | b(c.g) << 8 | b(c.b) << 16 | 0xFFu << 24;
}

// Pixel-art shading: darker shades turn towards blue-violet and lose a little
// chroma, the light turns towards warm yellow. Very light or very dark bases
// still get four distinct steps.
Ramp makeRamp(Color base, float spread = 1) {
  const Oklch o = toOklch(base);
  const bool grey = o.C < 0.03F;
  auto at = [&](float dL, float cMul, float hueDeg, float to) {
    Oklch s = o;
    s.L = o.L + dL * spread;
    if (s.L > 0.97F) s.L = 0.97F;
    if (s.L < 0.08F) s.L = 0.08F;
    s.C = grey ? o.C + std::abs(dL) * 0.04F : o.C * cMul;
    s.h = grey ? to : hueTowards(o.h, to, hueDeg);
    return fromOklch(s);
  };
  // pale bases spread downwards, dark ones upwards, so no step is lost against a limit
  const float up = o.L > 0.86F ? 0.03F : 0.075F, down = o.L < 0.3F ? 0.6F : 1.0F;
  Ramp r;
  r.shade[0] = pack(at(-0.25F * down, 0.92F, 22, 285));
  r.shade[1] = pack(at(-0.12F * down, 0.98F, 11, 285));
  r.shade[2] = pack(base);
  r.shade[3] = pack(at(up + (o.L < 0.3F ? 0.05F : 0), 0.9F, 12, 95));
  r.contour = pack(at(-0.33F * down, 0.9F, 26, 285));
  Oklch ol = o;
  ol.L = std::max(0.1F, std::min(o.L - 0.45F, 0.3F));
  ol.C = std::min(o.C * 0.75F + 0.02F, 0.09F);
  ol.h = grey ? 290 : hueTowards(o.h, 290, 30);
  r.outline = pack(fromOklch(ol));
  return r;
}


// ── The grid ────────────────────────────────────────────────────────────────

constexpr int N = kRigPixels;
constexpr float U = kRigUnit;
constexpr float G = (N - 1) / U;  // the ground line in units: feet end on row N-2, their outline on N-1
constexpr float PI = 3.14159265F;

enum Mat : std::uint8_t {
  mNone, mBody, mBelly, mMark, mInner, mExtra, mEye, mPupil, mShine, mNose, mMouth, mTongue, mBlush, mWhite, mGlow, mShadow, mCount
};
constexpr std::uint8_t kContour = 4;  // a shade index: the darkened edge of a part behind another
constexpr std::uint8_t kStamp = 248;  // layers from here up are drawn pixel by pixel (eyes, nose)

// parts: each is shaded as one volume, and gets a contour where it passes behind another
enum Grp : std::uint8_t {
  gBody = 1, gHead2, gHead3, gLegNF, gLegFF, gLegNH, gLegFH, gArmN, gArmF, gTail, gTail2, gTail3, gRobe, gCape, gEarN, gEarF, gHorn,
  gHair, gWingN, gWingF, gBeak, gProp, gGlow, gGroups
};

struct Px {
  std::uint8_t mat = mNone, shade = 0, layer = 0, group = 0;
  std::int8_t bias = 0;  // hundredths added to the light (far limbs are darker)
};

V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
V2 operator*(V2 a, float k) { return {a.x * k, a.y * k}; }
float dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
float len(V2 a) { return std::sqrt(dot(a, a)); }
V2 rot(V2 v, float a) {
  const float c = std::cos(a), s = std::sin(a);
  return {v.x * c - v.y * s, v.x * s + v.y * c};
}
V2 dir(float a) { return {std::cos(a), std::sin(a)}; }
V2 lerp(V2 a, V2 b, float t) { return a + (b - a) * t; }

// units -> pixels for the frame being drawn: scaled about the ground line (the feet stay on
// the bottom row) and centred on the species' own middle (rigFit), so each one fits its grid
struct Xf {
  float k = U, cx = 32;
};
thread_local Xf g_xf;
V2 P(V2 u) { return {(u.x - g_xf.cx) * g_xf.k + N * 0.5F, (N - 1) - (G - u.y) * g_xf.k}; }
float K() { return g_xf.k; }

struct Grid {
  std::array<Px, N * N> px{};
  std::array<std::uint32_t, N * N> cover{};
  int bx0[gGroups], by0[gGroups], bx1[gGroups], by1[gGroups];  // each part's bounding box
  float lx = -0.5F, ly = -0.78F, lz = 0.6F;  // the light, in grid space

  explicit Grid(bool flip) {
    // the picture is mirrored at the end: light it from the other side so it ends up top-left
    if (flip) lx = -lx;
    for (int b = 0; b < gGroups; ++b) bx0[b] = by0[b] = N, bx1[b] = by1[b] = -1;
    const float n = std::sqrt(lx * lx + ly * ly + lz * lz);
    lx /= n, ly /= n, lz /= n;
  }

  static std::uint8_t level(float lam) { return lam > 0.74F ? 3 : lam > 0.3F ? 2 : lam > -0.08F ? 1 : 0; }

  void put(int x, int y, std::uint8_t mat, std::uint8_t shade, std::uint8_t layer, std::uint8_t group, float bias = 0) {
    if (x < 0 || y < 0 || x >= N || y >= N || mat == mNone) return;
    if (group < gGroups) {
      cover[y * N + x] |= 1u << group;
      bx0[group] = std::min(bx0[group], x), by0[group] = std::min(by0[group], y);
      bx1[group] = std::max(bx1[group], x), by1[group] = std::max(by1[group], y);
    }
    Px& p = px[y * N + x];
    if (p.mat != mNone && p.layer > layer) return;
    p = {mat, shade, layer, group, static_cast<std::int8_t>(std::clamp(bias * 100.0F, -120.0F, 120.0F))};
  }
  Px& at(int x, int y) { return px[y * N + x]; }

  // Primitives take units and draw in pixels. `mat(u, v)` picks the material
  // from local coordinates; `bias` lightens or darkens.
  template <class F>
  void ellipse(V2 c, float rx, float ry, float ang, std::uint8_t layer, std::uint8_t group, F mat, float bias = 0) {
    c = P(c), rx *= K(), ry *= K();
    rx = std::max(rx, 0.6F), ry = std::max(ry, 0.6F);
    const float r = std::max(rx, ry) + 1;
    const float ca = std::cos(ang), sa = std::sin(ang);
    for (int y = static_cast<int>(std::floor(c.y - r)); y <= static_cast<int>(std::ceil(c.y + r)); ++y)
      for (int x = static_cast<int>(std::floor(c.x - r)); x <= static_cast<int>(std::ceil(c.x + r)); ++x) {
        const float dx = x + 0.5F - c.x, dy = y + 0.5F - c.y;
        const float u = (dx * ca + dy * sa) / rx, v = (-dx * sa + dy * ca) / ry;
        const float q = u * u + v * v;
        if (q > 1) continue;
        const float nx = u * ca - v * sa, ny = u * sa + v * ca, nz = std::sqrt(1 - std::min(q, 1.0F));
        put(x, y, mat(u, v), level((nx * lx + ny * ly) * 0.9F + nz * lz + bias), layer, group, bias);
      }
  }
  // a tapered limb from a (radius ra) to b (rb); mat(t, s): t 0..1 along, s -1..1 across
  template <class F>
  void capsule(V2 a, V2 b, float ra, float rb, std::uint8_t layer, std::uint8_t group, F mat, float bias = 0) {
    a = P(a), b = P(b), ra *= K(), rb *= K();
    ra = std::max(ra, 0.55F), rb = std::max(rb, 0.55F);
    const V2 ab = b - a;
    const float l2 = std::max(dot(ab, ab), 1e-4F);
    const float r = std::max(ra, rb) + 1;
    const int x0 = static_cast<int>(std::floor(std::min(a.x, b.x) - r)), x1 = static_cast<int>(std::ceil(std::max(a.x, b.x) + r));
    const int y0 = static_cast<int>(std::floor(std::min(a.y, b.y) - r)), y1 = static_cast<int>(std::ceil(std::max(a.y, b.y) + r));
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        const V2 p{x + 0.5F - a.x, y + 0.5F - a.y};
        const float t = std::clamp(dot(p, ab) / l2, 0.0F, 1.0F);
        const float rad = ra + (rb - ra) * t;
        const V2 off = p - ab * t;
        const float d = len(off);
        if (d > rad) continue;
        const float nx = off.x / rad, ny = off.y / rad, nz = std::sqrt(std::max(0.0F, 1 - (nx * nx + ny * ny)));
        const float s = (ab.x * off.y - ab.y * off.x) / (std::sqrt(l2) * rad);
        put(x, y, mat(t, s), level((nx * lx + ny * ly) * 0.9F + nz * lz + bias), layer, group, bias);
      }
  }
  // a triangle (ears, horns, beaks, spikes); mat(t, side): t 0 at the base .. 1 at the tip
  template <class F>
  void triangle(V2 a, V2 b, V2 tip, std::uint8_t layer, std::uint8_t group, F mat, float bias = 0) {
    a = P(a), b = P(b), tip = P(tip);
    const int x0 = static_cast<int>(std::floor(std::min({a.x, b.x, tip.x}))), x1 = static_cast<int>(std::ceil(std::max({a.x, b.x, tip.x})));
    const int y0 = static_cast<int>(std::floor(std::min({a.y, b.y, tip.y}))), y1 = static_cast<int>(std::ceil(std::max({a.y, b.y, tip.y})));
    auto edge = [](V2 p, V2 q, V2 r) { return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x); };
    const float area = edge(a, b, tip);
    if (std::abs(area) < 1e-3F) return;
    const V2 base = (a + b) * 0.5F, axis = tip - base;
    const float al2 = std::max(dot(axis, axis), 1e-4F);
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        const V2 p{x + 0.5F, y + 0.5F};
        const float w0 = edge(b, tip, p) / area, w1 = edge(tip, a, p) / area, w2 = edge(a, b, p) / area;
        if (w0 < -0.02F || w1 < -0.02F || w2 < -0.02F) continue;
        const float t = std::clamp(dot(p - base, axis) / al2, 0.0F, 1.0F);
        put(x, y, mat(t, w0 - w1), level(0.45F + bias), layer, group, bias);
      }
  }
  // a rounded box (armour, a visor): centre, half sizes, angle, corner radius
  template <class F>
  void box(V2 c, float hw, float hh, float ang, float round, std::uint8_t layer, std::uint8_t group, F mat, float bias = 0) {
    c = P(c), hw *= K(), hh *= K(), round *= K();
    const float r = std::hypot(hw, hh) + 1;
    const float ca = std::cos(ang), sa = std::sin(ang);
    for (int y = static_cast<int>(std::floor(c.y - r)); y <= static_cast<int>(std::ceil(c.y + r)); ++y)
      for (int x = static_cast<int>(std::floor(c.x - r)); x <= static_cast<int>(std::ceil(c.x + r)); ++x) {
        const float dx = x + 0.5F - c.x, dy = y + 0.5F - c.y;
        const float u = dx * ca + dy * sa, v = -dx * sa + dy * ca;
        const float qx = std::abs(u) - (hw - round), qy = std::abs(v) - (hh - round);
        const float d = std::hypot(std::max(qx, 0.0F), std::max(qy, 0.0F)) + std::min(std::max(qx, qy), 0.0F) - round;
        if (d > 0) continue;
        put(x, y, mat(u / hw, v / hh), level(0.45F + bias), layer, group, bias);
      }
  }
  // one pixel at a point given in units
  void dot1(V2 p, std::uint8_t mat, std::uint8_t shade = 2, std::uint8_t layer = 250) {
    const V2 q = P(p);
    put(static_cast<int>(std::floor(q.x)), static_cast<int>(std::floor(q.y)), mat, shade, layer, 250);
  }

  // Shade every part as one rounded volume: its height rises from its edge
  // (a distance transform of its whole coverage, the hidden part included),
  // so a body made of several ellipses is lit as one body, not as beads.
  void shade() {
    std::array<float, N * N> d;
    std::array<float, N * N> h;
    for (int b = 1; b < gGroups; ++b) {
      if (bx1[b] < 0) continue;
      // only over the part's own box, a pixel round it (outside, nothing of it: distance 0)
      const int x0 = std::max(0, bx0[b] - 1), x1 = std::min(N - 1, bx1[b] + 1), y0 = std::max(0, by0[b] - 1), y1 = std::min(N - 1, by1[b] + 1);
      const std::uint32_t bit = 1u << b;
      auto D = [&](int x, int y) { return x < x0 || x > x1 || y < y0 || y > y1 ? 0.0F : d[y * N + x]; };
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) d[y * N + x] = (cover[y * N + x] & bit) ? 1e4F : 0;
      constexpr float Q = 1.41421F;
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          float& v = d[y * N + x];
          if (v == 0) continue;
          v = std::min({v, D(x - 1, y) + 1, D(x, y - 1) + 1, D(x - 1, y - 1) + Q, D(x + 1, y - 1) + Q});
        }
      float R = 0;
      for (int y = y1; y >= y0; --y)
        for (int x = x1; x >= x0; --x) {
          float& v = d[y * N + x];
          if (v == 0) continue;
          v = std::min({v, D(x + 1, y) + 1, D(x, y + 1) + 1, D(x + 1, y + 1) + Q, D(x - 1, y + 1) + Q});
          R = std::max(R, v);
        }
      R = std::max(1.5F, R - 0.5F);
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          const float dv = d[y * N + x];
          const float t = dv == 0 ? 0 : std::clamp((dv - 0.5F) / R, 0.0F, 1.0F);
          h[y * N + x] = dv == 0 ? 0 : R * std::sqrt(1 - (1 - t) * (1 - t));
        }
      auto H = [&](int x, int y) { return x < x0 || x > x1 || y < y0 || y > y1 ? 0.0F : h[y * N + x]; };
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          Px& p = px[y * N + x];
          if (p.mat == mNone || p.group != b || p.mat == mGlow) continue;
          float nx = -(H(x + 1, y) - H(x - 1, y)) * 0.5F, ny = -(H(x, y + 1) - H(x, y - 1)) * 0.5F, nz = 1;
          const float n = std::sqrt(nx * nx + ny * ny + nz * nz);
          nx /= n, ny /= n, nz /= n;
          p.shade = level(nx * lx + ny * ly + nz * lz + p.bias / 100.0F);
        }
    }
  }
};

template <std::uint8_t M>
std::uint8_t solid(float, float) {
  return M;
}

// 2-bone IK: the knee between joint j and foot f; bend +1 puts it to the left of j->f
V2 knee(V2 j, V2 f, float l1, float l2, float bend) {
  V2 d = f - j;
  float dl = len(d);
  const float maxL = (l1 + l2) * 0.999F;
  if (dl > maxL) d = d * (maxL / dl), dl = maxL;
  dl = std::max(dl, std::abs(l1 - l2) + 0.01F);
  const float a = (l1 * l1 - l2 * l2 + dl * dl) / (2 * dl);
  const float h = std::sqrt(std::max(0.0F, l1 * l1 - a * a));
  const V2 u = d * (1 / dl);
  const V2 n{-u.y, u.x};
  return j + u * a + n * (h * bend);
}

struct Frame {
  const RigSpec& s;
  const RigPose& p;
  Grid& g;
};

// ── Faces ───────────────────────────────────────────────────────────────────

// An eye at a point (units): its kind, how open, its mood. Drawn pixel by
// pixel: p pupil, s shine, i iris, g glow.
void eye(Grid& g, V2 at, RigEye kind, const RigPose& p, bool far) {
  const V2 q = P(at);
  const int ex = static_cast<int>(std::floor(q.x)) - 1, ey = static_cast<int>(std::floor(q.y)) - 1;
  auto stamp = [&](std::initializer_list<const char*> rows) {
    int r = 0;
    for (const char* row : rows) {
      for (int c = 0; row[c]; ++c) {
        const char ch = row[c];
        if (ch == '.') continue;
        const std::uint8_t m = ch == 'p' ? mPupil : ch == 's' ? (far ? mPupil : mShine) : ch == 'g' ? mGlow : mEye;
        g.put(ex + c, ey + r, m, ch == 'I' ? 3 : 2, 251, 251);
      }
      ++r;
    }
  };
  const bool happy = p.eyeStyle == 1, sleepy = p.eyeStyle == 2;
  const float open = std::clamp(p.eyes, 0.0F, 1.0F);
  if (kind == RigEye::Visor) return;  // drawn with the helmet
  if (kind == RigEye::Socket) {
    stamp({"ppp", open < 0.34F ? "ppp" : "pgp", "ppp"});
    return;
  }
  if (happy) {
    if (kind == RigEye::Anime) stamp({"....", ".pp.", "p..p"});
    else stamp({"...", ".p.", "p.p"});
    return;
  }
  if (open < 0.34F) {
    if (sleepy) {
      if (kind == RigEye::Anime) stamp({"....", "....", "p..p", ".pp."});
      else stamp({"...", "...", "p.p", ".p."});
    } else {
      if (kind == RigEye::Anime) stamp({"....", "....", "pppp"});
      else stamp({"...", "...", "ppp"});
    }
    return;
  }
  const bool half = open < 0.75F;
  switch (kind) {
    case RigEye::Anime:
      if (half) stamp({"....", "pppp", "piip", ".ii."});
      else stamp({".pp.", "pspp", "pppp", "piip", ".ii."});
      break;
    case RigEye::Bead:
      if (half) stamp({"..", "pp"});
      else stamp({"sp", "pp"});
      break;
    default:
      if (half) stamp({"...", "ppp", "pii"});
      else stamp({"spp", "ppp", "pii"});
      break;
  }
}

// ── Animal parts ────────────────────────────────────────────────────────────

void leg(Frame& f, int i, V2 joint, bool front, bool far) {
  const RigSpec& s = f.s;
  const RigPose& p = f.p;
  const float l1 = front ? s.frontUp : s.hindUp, l2 = front ? s.frontLow : s.hindLow;
  const float w0 = front ? s.frontW : s.hindW, w1 = front ? s.frontFootW : s.hindFootW;
  const float pawR = s.paw;
  const V2 ft{joint.x + p.foot[i].x, G - pawR * 0.85F - std::max(0.0F, p.foot[i].y) - p.lift};
  // front knees (wrists) fold forward, hind hocks back
  const V2 k = knee(joint, ft, l1, l2, front ? -1.0F : 1.0F);
  const std::uint8_t layer = far ? (front ? 12 : 10) : (front ? 62 : 60);
  const std::uint8_t group = static_cast<std::uint8_t>(gLegNF + i);
  const float bias = far ? -0.42F : 0;
  const bool sock = s.socks;
  f.g.capsule(joint, k, w0, (w0 + w1) * 0.5F, layer, group, solid<mBody>, bias);
  f.g.capsule(k, ft, (w0 + w1) * 0.5F, w1, layer, group, [sock](float t, float) { return std::uint8_t(sock && t > 0.25F ? mMark : mBody); }, bias);
  const float curl = p.pawCurl[i];
  const V2 toe = ft + V2{pawR * 0.55F * (1 - curl * 1.6F), pawR * 0.15F + curl * 0.6F};
  const RigFoot foot = front && s.frontTalons ? RigFoot::Talon : s.foot;
  if (foot == RigFoot::Hoof) {
    f.g.capsule(ft + V2{0, -0.5F}, ft + V2{0.4F, pawR * 0.45F}, w1 * 1.08F, w1 * 1.18F, layer, group, solid<mExtra>, bias - 0.1F);
  } else if (foot == RigFoot::Talon) {
    f.g.capsule(ft, ft + V2{2.2F, 0.6F}, 0.7F, 0.45F, layer, group, solid<mExtra>, bias);
    f.g.capsule(ft, ft + V2{0.8F, 1.0F}, 0.7F, 0.45F, layer, group, solid<mExtra>, bias);
    f.g.capsule(ft, ft + V2{-1.2F, 0.7F}, 0.6F, 0.4F, layer, group, solid<mExtra>, bias);
  } else {
    f.g.ellipse(toe, pawR * 1.25F, pawR * 0.85F, curl * 0.9F, layer, group, sock ? solid<mMark> : solid<mBody>, bias);
  }
}

// One tail from `root`, the root angle `a0` (pi = straight back), its own sway `k`.
void tailChain(Frame& f, V2 root, float a0, float offset, float lenMul, float bias, std::uint8_t layer, std::uint8_t group) {
  const RigSpec& s = f.s;
  const RigPose& p = f.p;
  const int n = std::clamp(s.tailSegs, 2, 12);
  const float seg = s.tailLen * lenMul / static_cast<float>(n);
  float a = a0;
  V2 pts[13];
  pts[0] = root;
  for (int i = 0; i < n; ++i) {
    a += p.tail[i] * (1 + offset * 0.4F) + s.tailCurl;
    pts[i + 1] = pts[i] + dir(a) * seg;
    // a tail that reaches the ground lies along it
    const float floor = G - 1.2F - s.tailW;
    if (pts[i + 1].y > floor) pts[i + 1].y = floor, a = PI + (a > PI ? 0.02F : -0.02F);
  }
  auto width = [&](float t) {
    switch (s.tail) {
      case RigTail::Brush: return s.tailW + (s.tailFat - s.tailW) * std::sin(std::min(1.0F, t * 1.15F) * PI * 0.85F) * (t < 0.92F ? 1.0F : 0.7F);
      case RigTail::Fan: return s.tailW + (s.tailFat - s.tailW) * t;
      case RigTail::Thin:
      case RigTail::Long: return s.tailW * (1 - 0.45F * t);
      case RigTail::Plume: return s.tailW * (1 - 0.3F * t) + (t > 0.7F ? (t - 0.7F) * s.tailFat * 2 : 0);
      case RigTail::Tuft: return t > 0.8F ? s.tailFat : s.tailW * (1 - 0.3F * t);
      case RigTail::Curl: return s.tailW + (s.tailFat - s.tailW) * std::sin(t * PI);
      default: return s.tailW;
    }
  };
  for (int i = 0; i < n; ++i) {
    const float t0 = static_cast<float>(i) / n, t1 = static_cast<float>(i + 1) / n;
    auto mat = [&, t0, t1](float t, float) {
      const float tt = t0 + (t1 - t0) * t;
      if ((s.tailTip || s.tail == RigTail::Tuft) && tt > 0.74F) return std::uint8_t(s.tail == RigTail::Tuft || s.tailTipMark ? mMark : mBelly);
      if (s.tail == RigTail::Plume && tt > 0.6F) return std::uint8_t(std::fmod(tt * 9, 1.0F) > 0.5F ? mMark : mBelly);
      if ((s.stripes || s.ringTail) && std::fmod(tt * 5.0F, 1.0F) > 0.6F) return std::uint8_t(mMark);
      return std::uint8_t(mBody);
    };
    f.g.capsule(pts[i], pts[i + 1], width(t0), std::max(0.7F, width(t1)), layer, group, mat, bias);
  }
}

void tails(Frame& f, V2 root, float pitch) {
  const RigSpec& s = f.s;
  const RigPose& p = f.p;
  if (s.tail == RigTail::None) return;
  if (s.tail == RigTail::Puff) {
    f.g.ellipse(root + V2{-1, -0.5F}, s.tailFat, s.tailFat * 0.9F, 0, 8, gTail, s.tailTip ? solid<mBelly> : solid<mBody>, 0.05F);
    return;
  }
  const float a = PI + pitch * 0.35F + p.tailUp;
  const int n = std::clamp(s.tails, 1, 9);
  // several tails fan out: the middle one in front, the others behind it, darker, each
  // set apart from the next by its own contour
  for (int rank = (n - 1) / 2 + (n % 2 == 0); rank >= 0; --rank)
    for (int k = 0; k < n; ++k) {
      const float side = n == 1 ? 0 : (static_cast<float>(k) - (n - 1) * 0.5F) / ((n - 1) * 0.5F);
      if (static_cast<int>(std::abs(side) * (n - 1) * 0.5F + 0.5F) != rank) continue;
      static constexpr std::uint8_t groups[3] = {gTail, gTail2, gTail3};
      tailChain(f, root, a + side * s.tailSpread * (1 + 0.15F * static_cast<float>(n)), side, 1 - 0.06F * rank, -0.14F * rank,
                static_cast<std::uint8_t>(std::max(2, 22 - 6 * rank)), groups[rank % 3]);
    }
}

void ears(Frame& f, V2 hc, float headAng, bool far, std::uint8_t lb, float hb) {
  const RigSpec& s = f.s;
  const RigPose& p = f.p;
  if (s.ear == RigEar::None) return;
  const float yaw = p.yaw;
  const float side = far ? -1.0F : 1.0F;
  const float bx = (far ? -1.8F : -0.6F) * (1 - yaw) + side * yaw * s.headR * 0.55F;
  const V2 base = hc + rot(V2{bx, -s.headRy * 0.72F}, headAng);
  const float tilt = s.earTilt + p.ears * 0.35F - p.earFlop * 0.5F + side * yaw * 0.35F;
  const float up = headAng - PI * 0.5F + tilt;
  const std::uint8_t layer = far ? static_cast<std::uint8_t>(lb - 4) : static_cast<std::uint8_t>(lb + 6);
  const std::uint8_t group = far ? gEarF : gEarN;
  const float bias = (far ? -0.35F : 0) + hb;
  const bool tips = s.earTips;
  switch (s.ear) {
    case RigEar::Pointed:
    case RigEar::Tuft: {
      const V2 across = dir(up + PI * 0.5F) * (s.earW * 0.5F);
      const V2 tip = base + dir(up) * s.earLen;
      f.g.triangle(base - across, base + across, tip, layer, group, [tips, &s](float t, float) { return std::uint8_t(tips && t > 0.62F ? (s.tipsExtra ? mExtra : mMark) : mBody); },
                   bias);
      if (!far || yaw > 0.5F) {
        const V2 ib = base + dir(up) * 0.9F + across * 0.12F;
        f.g.triangle(ib - across * 0.45F, ib + across * 0.45F, ib + dir(up) * (s.earLen * 0.6F), static_cast<std::uint8_t>(layer + 1), group,
                     solid<mInner>, bias - 0.2F);
      }
      if (s.ear == RigEar::Tuft) f.g.capsule(tip, tip + dir(up - 0.3F) * 2.0F, 0.7F, 0.5F, layer, group, solid<mMark>, bias);
      break;
    }
    case RigEar::Round: {
      const V2 c = base + dir(up) * (s.earLen * 0.45F);
      f.g.ellipse(c, s.earW * 0.5F, s.earLen * 0.5F, up + PI * 0.5F, layer, group, tips ? solid<mMark> : solid<mBody>, bias);
      f.g.ellipse(c + dir(up) * 0.4F, s.earW * 0.26F, s.earLen * 0.27F, up + PI * 0.5F, static_cast<std::uint8_t>(layer + 1), group, solid<mInner>,
                  bias - 0.2F);
      break;
    }
    case RigEar::Long: {
      const V2 tip = base + dir(up) * s.earLen;
      f.g.capsule(base, tip, s.earW * 0.42F, s.earW * 0.5F, layer, group, [tips](float t, float) { return std::uint8_t(tips && t > 0.8F ? mMark : mBody); },
                  bias);
      if (!far || yaw > 0.5F)
        f.g.capsule(base + dir(up) * 1.5F, tip - dir(up) * 1.4F, s.earW * 0.16F, s.earW * 0.22F, static_cast<std::uint8_t>(layer + 1), group, solid<mInner>,
                    bias - 0.2F);
      break;
    }
    case RigEar::Floppy: {
      const float hang = headAng + PI * 0.5F - 0.25F + p.earFlop * 0.6F - p.ears * 0.5F;
      const V2 root = hc + rot(V2{bx - 0.5F, -s.headRy * 0.6F}, headAng);
      f.g.capsule(root, root + dir(hang) * s.earLen, s.earW * 0.38F, s.earW * 0.55F, far ? layer : static_cast<std::uint8_t>(lb + 8), group,
                  tips ? solid<mMark> : solid<mBody>, bias - 0.12F);
      break;
    }
    case RigEar::Fin: {
      // a dragon's frill: a fan of spines with skin between
      for (int k = 0; k < 3; ++k) {
        const float a = up - 0.5F - 0.35F * static_cast<float>(k);
        f.g.triangle(base + rot(V2{-1.2F, 0}, headAng), base + rot(V2{1.2F, 0}, headAng), base + dir(a) * (s.earLen * (1 - 0.15F * k)), layer, group,
                     solid<mMark>, bias - 0.05F * k);
      }
      break;
    }
    default: break;
  }
}

void horns(Frame& f, V2 hc, float headAng, bool front, std::uint8_t lb, float hb) {
  const RigSpec& s = f.s;
  const std::uint8_t lay = front ? static_cast<std::uint8_t>(lb + 7) : static_cast<std::uint8_t>(lb - 3);
  const float bias = (front ? 0 : -0.35F) + hb;
  switch (s.horn) {
    case RigHorn::Unicorn: {
      if (!front) return;
      const V2 b = hc + rot(V2{s.headR * 0.45F, -s.headRy * 0.8F}, headAng);
      const V2 tip = b + rot(V2{3.5F, -7.5F}, headAng);
      f.g.triangle(b + rot(V2{-1.3F, 0.4F}, headAng), b + rot(V2{1.3F, 0.4F}, headAng), tip, lay, gHorn,
                   [](float t, float) { return std::uint8_t(std::fmod(t * 4.0F, 1.0F) > 0.72F ? mWhite : mExtra); }, bias);
      break;
    }
    case RigHorn::Nubs:
    case RigHorn::Dragon: {
      const float L = s.horn == RigHorn::Dragon ? 6.5F : 3.0F;
      const V2 b = hc + rot(V2{front ? 0.6F : -1.4F, -s.headRy * 0.85F}, headAng);
      const V2 m = b + rot(V2{-L * 0.55F, -L * 0.45F}, headAng);
      const V2 tip = m + rot(V2{-L * 0.5F, -L * 0.1F}, headAng);
      f.g.capsule(b, m, 1.3F, 0.95F, lay, gHorn, solid<mExtra>, bias);
      f.g.capsule(m, tip, 0.95F, 0.5F, lay, gHorn, solid<mExtra>, bias);
      break;
    }
    case RigHorn::Antlers:
    case RigHorn::Qilin: {
      const bool q = s.horn == RigHorn::Qilin;
      const V2 b = hc + rot(V2{front ? 0.4F : -1.6F, -s.headRy * 0.85F}, headAng);
      const V2 m = b + rot(V2{-1.5F, q ? -3.5F : -4.5F}, headAng);
      const V2 tip = m + rot(V2{-2.5F, q ? -2.5F : -3.5F}, headAng);
      f.g.capsule(b, m, 0.95F, 0.75F, lay, gHorn, solid<mExtra>, bias);
      f.g.capsule(m, tip, 0.75F, 0.5F, lay, gHorn, solid<mExtra>, bias);
      f.g.capsule(m, m + rot(V2{2.5F, -2.5F}, headAng), 0.7F, 0.45F, lay, gHorn, solid<mExtra>, bias);
      if (!q) f.g.capsule(b + rot(V2{-0.8F, -2.0F}, headAng), b + rot(V2{-3.5F, -2.5F}, headAng), 0.65F, 0.45F, lay, gHorn, solid<mExtra>, bias);
      break;
    }
    default: break;
  }
}

// A wing from its shoulder. Feathers: an arm, primaries fanned from the hand and
// secondaries along the forearm; folded, they lie along the back. Membrane: the
// arm, three finger bones and skin between them.
void wing(Frame& f, V2 root, bool far) {
  const RigSpec& s = f.s;
  const RigPose& p = f.p;
  if (s.wings == RigWing::None) return;
  const float open = std::clamp(p.wingOpen, 0.0F, 1.0F);
  const std::uint8_t layer = far ? 9 : 70, group = far ? gWingF : gWingN;
  const float bias = far ? -0.4F : 0.05F;
  const float L = s.wingLen;
  if (open < 0.3F) {
    // folded: it lies along the back (the far one hides behind the body)
    if (far) return;
    if (s.wings == RigWing::Feather) {
      const V2 c = root + V2{-L * 0.3F, 1.0F + p.wing * 0.6F};
      f.g.ellipse(c, L * 0.42F, 2.6F, 0.14F, layer, group, [](float u, float v) {
        if (u < -0.45F) return std::uint8_t(mMark);
        return std::uint8_t(v > 0.35F && u > -0.2F ? mBelly : mBody);
      }, bias);
      for (int k = 0; k < 3; ++k) {
        const V2 b = c + V2{-L * 0.3F - k * 0.8F, -1.0F + k * 1.1F};
        f.g.triangle(b + V2{0, -1.0F}, b + V2{0, 1.0F}, b + V2{-3.2F + k * 0.6F, 0.4F + k * 0.5F}, layer, group, solid<mMark>, bias - 0.05F);
      }
    } else {
      const V2 e = root + V2{-L * 0.18F, -1.6F};
      const V2 h = e + V2{L * 0.12F, -0.6F};
      for (int k = 0; k < 3; ++k) {
        const V2 tip = h + dir(PI + 0.12F + 0.16F * k) * (L * (0.52F - 0.06F * k));
        f.g.triangle(h, tip, root + V2{-L * 0.25F, 1.5F}, static_cast<std::uint8_t>(layer - 1), group, solid<mBelly>, bias - 0.05F);
        f.g.capsule(h, tip, 0.6F, 0.35F, layer, group, solid<mMark>, bias);
      }
      f.g.capsule(root, e, 1.3F, 1.0F, layer, group, solid<mMark>, bias);
      f.g.capsule(e, h, 1.0F, 0.8F, layer, group, solid<mMark>, bias);
      f.g.capsule(h, h + V2{0.8F, -1.2F}, 0.5F, 0.3F, layer, group, solid<mExtra>, bias);
    }
    return;
  }
  // the arm's angle: folded back along the body, or raised and beating (wing -1 up .. 1 down)
  const float fold = PI + 0.12F;
  const float spread = PI + 1.25F - p.wing * 1.0F - (far ? 0.12F : 0);
  const float a = fold + (spread - fold) * open;
  const V2 elbow = root + dir(a) * (L * 0.38F);
  const float handA = a - (0.25F + 0.7F * open) * (1 - 0.3F * p.wing);
  const V2 hand = elbow + dir(handA) * (L * 0.3F);
  if (s.wings == RigWing::Feather) {
    // secondaries: from the forearm, hanging back and down
    for (int k = 0; k < 4; ++k) {
      const float t = static_cast<float>(k) / 3.0F;
      const V2 at = lerp(root, elbow, 0.3F + 0.7F * t);
      const float fa = a - PI * 0.5F * (1 - open) + PI * 0.62F + 0.1F * t + p.wing * 0.15F;
      f.g.capsule(at, at + dir(fa) * (L * (0.32F + 0.08F * t)), 1.2F, 0.7F, static_cast<std::uint8_t>(layer - 1), group,
                  [](float tt, float) { return std::uint8_t(tt > 0.65F ? mMark : mBody); }, bias - 0.05F);
    }
    // primaries: from the hand, long, fanning out
    for (int k = 0; k < 5; ++k) {
      const float t = static_cast<float>(k) / 4.0F;
      const float fa = handA + (0.15F + 0.75F * t) * (0.35F + 0.65F * open) + p.wing * 0.1F * t;
      f.g.capsule(hand, hand + dir(fa) * (L * (0.62F - 0.12F * t)), 1.0F, 0.55F, layer, group,
                  [](float tt, float) { return std::uint8_t(tt > 0.6F ? mMark : mBody); }, bias);
    }
    f.g.capsule(root, elbow, 1.8F, 1.3F, static_cast<std::uint8_t>(layer + 1), group, solid<mBody>, bias + 0.05F);
    f.g.capsule(elbow, hand, 1.3F, 1.0F, static_cast<std::uint8_t>(layer + 1), group, solid<mBody>, bias + 0.05F);
  } else {
    // membrane: three fingers from the hand, the skin between them and back to the body
    V2 tips[3];
    for (int k = 0; k < 3; ++k) {
      const float fa = handA + (0.25F + 0.5F * static_cast<float>(k)) * (0.4F + 0.6F * open);
      tips[k] = hand + dir(fa) * (L * (0.62F - 0.12F * static_cast<float>(k)));
    }
    const V2 back = root + V2{-L * 0.35F, 2.0F} * (0.5F + 0.5F * open);
    f.g.triangle(hand, tips[0], tips[1], static_cast<std::uint8_t>(layer - 1), group, solid<mBelly>, bias - 0.05F);
    f.g.triangle(hand, tips[1], tips[2], static_cast<std::uint8_t>(layer - 1), group, solid<mBelly>, bias - 0.05F);
    f.g.triangle(hand, tips[2], back, static_cast<std::uint8_t>(layer - 1), group, solid<mBelly>, bias - 0.05F);
    f.g.triangle(root, elbow, back, static_cast<std::uint8_t>(layer - 1), group, solid<mBelly>, bias - 0.05F);
    for (const V2& t : tips) f.g.capsule(hand, t, 0.7F, 0.4F, layer, group, solid<mMark>, bias);
    f.g.capsule(root, elbow, 1.4F, 1.0F, layer, group, solid<mMark>, bias);
    f.g.capsule(elbow, hand, 1.0F, 0.8F, layer, group, solid<mMark>, bias);
    f.g.capsule(hand, hand + dir(handA - 0.9F) * 1.6F, 0.6F, 0.3F, layer, group, solid<mExtra>, bias);  // the thumb's claw
  }
}

// An animal's head: skull, muzzle and jaw, ears, horns, eyes and nose. `lb` is
// its layer (several heads stack), `hb` darkens a head further back.
void animalHead(Frame& f, V2 hc, float headAng, std::uint8_t lb, std::uint8_t group, float hb, bool main) {
  const RigSpec& s = f.s;
  const RigPose& p = f.p;
  Grid& g = f.g;
  ears(f, hc, headAng, true, lb, hb);
  horns(f, hc, headAng, false, lb, hb);
  g.ellipse(hc, s.headR, s.headRy, headAng, lb, group, [&s](float u, float v) {
    if (s.bandit && v > -0.42F && v < 0.12F && u > -0.15F) return std::uint8_t(mMark);
    if (s.muzzleExtra && u > 0.1F && v > 0.15F) return std::uint8_t(mExtra);
    if (s.muzzleExtra && v < -0.35F && u > 0.05F && u < 0.55F) return std::uint8_t(mExtra);  // the brows
    if (s.mask && u > 0.25F && v > -0.2F) return std::uint8_t(mBelly);
    if (s.headBelly) return std::uint8_t(mBelly);
    if (s.stripes && v < -0.45F && std::fmod((u + 1.0F) * 2.5F, 1.0F) > 0.6F) return std::uint8_t(mMark);
    if (s.scales && v < -0.3F && std::fmod((u + 2.0F) * 3.0F, 1.0F) > 0.66F) return std::uint8_t(mMark);
    return std::uint8_t(mBody);
  }, hb);
  if (s.cheeks) {
    const V2 c = hc + rot(V2{-s.headR * 0.45F, s.headRy * 0.55F}, headAng);
    g.triangle(c + rot(V2{-1.5F, -2.0F}, headAng), c + rot(V2{2.5F, 0.5F}, headAng), c + rot(V2{-3.2F, 2.3F}, headAng),
               static_cast<std::uint8_t>(lb + 1), group, s.muzzleExtra ? solid<mExtra> : solid<mBelly>, hb);
  }
  if (s.mane || s.flame) {
    // a mane down the back of the head: tufts, or flames that flicker
    for (int k = 0; k < 4; ++k) {
      const float t = static_cast<float>(k) / 3.0F;
      const V2 at = hc + rot(V2{-s.headR * (0.4F + 0.5F * t), -s.headRy * (0.6F - 0.9F * t)}, headAng);
      const float fl = s.flame ? 0.6F * std::sin(p.wavePhase * 3 + k * 1.7F) : 0;
      g.triangle(at + rot(V2{0, -1.4F}, headAng), at + rot(V2{0, 1.4F}, headAng), at + rot(V2{-3.2F - t, -1.6F + fl}, headAng),
                 static_cast<std::uint8_t>(lb - 1), group, solid<mMark>, hb - 0.05F);
    }
  }
  const float yaw = std::clamp(p.yaw, 0.0F, 1.0F);
  const float sn = s.snout * (1 - yaw * 0.8F);
  const V2 snc = hc + rot(V2{s.headR * 0.55F * (1 - yaw) + sn * 0.45F, s.snoutDrop + yaw * 1.2F}, headAng);
  const float jaw = p.mouth * 1.6F;
  if (s.beak) {
    // an eagle's hooked beak
    const V2 bk = hc + rot(V2{s.headR * 0.7F * (1 - yaw) + 0.3F, 0.6F}, headAng);
    const float bl = (s.snout + 1.5F) * (1 - yaw * 0.6F);
    g.triangle(bk + rot(V2{-0.6F, -1.8F}, headAng), bk + rot(V2{-0.6F, 1.4F}, headAng), bk + rot(V2{bl, 0.9F}, headAng), static_cast<std::uint8_t>(lb + 3),
               gBeak, solid<mExtra>, hb);
    g.triangle(bk + rot(V2{bl * 0.6F, -0.2F}, headAng), bk + rot(V2{bl * 0.6F, 1.6F}, headAng), bk + rot(V2{bl + 0.3F, 2.2F}, headAng),
               static_cast<std::uint8_t>(lb + 3), gBeak, solid<mExtra>, hb - 0.2F);
  } else if (s.snout > 0.5F) {
    g.ellipse(snc, s.snoutRx * (1 - yaw * 0.35F), s.snoutRy, headAng, static_cast<std::uint8_t>(lb + 2), group,
              [&s](float, float v) { return std::uint8_t(s.muzzleExtra ? mExtra : v > -0.35F || s.mask ? mBelly : mBody); }, hb);
    if (jaw > 0.2F) {
      const V2 jc = snc + rot(V2{-0.6F, s.snoutRy * 0.6F + jaw}, headAng);
      g.ellipse(jc, s.snoutRx * 0.8F, s.snoutRy * 0.55F, headAng + jaw * 0.12F, static_cast<std::uint8_t>(lb + 1), group, solid<mBelly>, hb);
      for (float k = 0; k <= jaw; k += 0.66F)
        for (float x = -s.snoutRx * 0.5F; x <= s.snoutRx * 0.45F; x += 0.66F) {
          const V2 q = snc + rot(V2{x, s.snoutRy * 0.35F + k}, headAng);
          g.put(static_cast<int>(std::floor(P(q).x)), static_cast<int>(std::floor(P(q).y)), mMouth, 2, static_cast<std::uint8_t>(lb + 3), group, hb);
        }
      if (p.tongue > 0.3F) {
        const V2 tp = snc + rot(V2{0.6F, s.snoutRy * 0.6F + jaw + 0.6F}, headAng);
        g.ellipse(tp, 1.4F, 1.0F + p.tongue, headAng + 0.4F, static_cast<std::uint8_t>(lb + 4), group, solid<mTongue>, hb);
      }
    }
    if (s.whiskers) {
      // barbels from the muzzle, trailing (the hem springs move them)
      for (int k = 0; k < 2; ++k) {
        V2 a = snc + rot(V2{s.snoutRx * 0.4F, s.snoutRy * 0.2F}, headAng);
        float ang = headAng + PI * 0.75F + (k ? 0.5F : 0);
        for (int sgm = 0; sgm < 4; ++sgm) {
          ang += 0.25F + p.hem[sgm] * 0.8F;
          const V2 b = a + dir(ang) * 2.4F;
          g.capsule(a, b, 0.35F, 0.3F, static_cast<std::uint8_t>(lb + 5), gHorn, solid<mExtra>, hb);
          a = b;
        }
      }
    }
  }
  ears(f, hc, headAng, false, lb, hb);
  horns(f, hc, headAng, true, lb, hb);
  // the face
  const V2 nose = snc + rot(V2{s.snoutRx * 0.85F, -s.snoutRy * 0.45F}, headAng);
  if (s.snout > 0.5F && !s.beak) {
    g.dot1(nose, mNose, 1);
    if (yaw < 0.5F) g.dot1(nose + V2{-1 / K(), 0}, mNose, 2);
    g.dot1(nose + V2{0, 1 / K()}, mNose, 2);
  }
  const float ex = s.eyeX * (1 - yaw) + yaw * s.headR * 0.42F;
  eye(g, hc + rot(V2{ex, s.eyeY}, headAng), s.eye, p, false);
  if (yaw > 0.4F) eye(g, hc + rot(V2{-s.headR * 0.42F * yaw + s.eyeX * (1 - yaw) - 2.5F * (1 - yaw), s.eyeY}, headAng), s.eye, p, true);
  if (p.blush > 0.3F && main) {
    const V2 b = hc + rot(V2{ex - 1.0F, s.eyeY + 2.6F}, headAng);
    g.dot1(b, mBlush, 2, 249), g.dot1(b + V2{-1 / K(), 0}, mBlush, 2, 249), g.dot1(b + V2{-2 / U, 0}, mBlush, 2, 249);
  }
}

// ── Quadrupeds ──────────────────────────────────────────────────────────────

void quad(Frame& f) {
  const RigSpec& s = f.s;
  const RigPose& p = f.p;
  Grid& g = f.g;
  const float hx = 27 + p.x;
  const float hipY = G - p.hipH - p.lift, shY = G - p.shoulderH - p.lift;
  const float dy = shY - hipY;
  const float run = std::sqrt(std::max(4.0F, s.spine * s.spine - dy * dy));
  const V2 hip{hx, hipY}, sh{hx + run, shY};
  const float pitch = std::atan2(dy, run);
  const V2 fwd = dir(pitch);
  const V2 down{-fwd.y, fwd.x};
  const float squash = std::clamp(p.squash, -0.4F, 0.4F);
  const float sy = 1 - squash * 0.5F, sx = 1 + squash * 0.35F;

  const V2 fJoint = sh + down * (s.chestRy * 0.35F) + V2{1.2F, 0};
  const V2 hJoint = hip + down * (s.rumpRy * 0.2F) + V2{1.4F, 0};
  leg(f, 1, fJoint, true, true);
  leg(f, 3, hJoint, false, true);
  wing(f, sh + down * (-s.chestRy * 0.75F) + fwd * -1.5F, true);
  tails(f, hip + rot(V2{-s.rumpRx * 0.8F, -s.rumpRy * 0.35F}, pitch), pitch);

  auto torsoMat = [&s](float t, float a) {
    if (a > 0.48F) return std::uint8_t(s.scales && std::fmod(t * 6, 1.0F) > 0.7F ? mMark : mBelly);
    if (s.stripes && a < 0.3F && std::fmod(t * s.spine * s.stripeFreq * 0.33F + 0.2F, 1.0F) > 0.66F) return std::uint8_t(mMark);
    if (s.spots && std::fmod(t * 7.3F, 1.0F) > 0.75F && a > -0.6F && a < 0.2F) return std::uint8_t(mMark);
    if (s.scales && a < 0.2F && std::fmod(t * 9 + (a > -0.3F ? 0.5F : 0), 1.0F) > 0.75F) return std::uint8_t(mMark);
    return std::uint8_t(mBody);
  };
  g.capsule(hip + fwd * 1.0F, sh - fwd * 0.5F, s.waist * sy, s.waist * 1.06F * sy, 30, gBody, torsoMat);
  g.ellipse(hip + down * (-0.3F), s.rumpRx * sx, s.rumpRy * sy, pitch * 0.45F, 31, gBody, [&s](float u, float v) {
    if (v > 0.62F) return std::uint8_t(mBelly);
    if (s.stripes && u > -0.6F && v < 0.3F && std::fmod(u * 2.2F + 4.3F, 1.0F) > 0.62F) return std::uint8_t(mMark);
    if (s.spots && v < 0.2F && std::fmod((u + 3) * 2.6F, 1.0F) > 0.7F && std::fmod((v + 3) * 2.2F, 1.0F) > 0.55F) return std::uint8_t(mMark);
    return std::uint8_t(mBody);
  });
  g.ellipse(sh + fwd * 0.6F + down * 0.4F, s.chestRx * sx, s.chestRy * sy, pitch * 0.7F, 32, gBody, [&s](float u, float v) {
    if (v > 0.55F && u > -0.3F) return std::uint8_t(mBelly);
    if (s.bib && u > 0.35F && v > -0.1F) return std::uint8_t(mBelly);
    return std::uint8_t(mBody);
  });

  const V2 neckBase = sh + fwd * (s.chestRx * 0.45F) + down * (-s.chestRy * 0.35F);
  const V2 hc = neckBase + dir(p.neck + pitch * 0.3F) * s.neckLen;
  g.capsule(neckBase, hc, s.neckW * 1.15F, s.neckW, 34, gBody,
            [&s](float, float a) { return std::uint8_t(s.bib && a > 0.25F ? mBelly : (s.mane && a < -0.35F ? mMark : mBody)); });
  if (s.mane) {
    const V2 back = rot(dir(p.neck + pitch * 0.3F), -PI * 0.5F);
    for (int k = 0; k < 5; ++k) {
      const float t = static_cast<float>(k) / 4.0F;
      const V2 at = lerp(neckBase, hc, 0.15F + t * 0.85F);
      const float sway = -p.hem[k % 4] * 2.5F;
      g.capsule(at + back * (s.neckW * 0.6F), at + back * (s.neckW * 0.9F + 1.6F) + V2{-1.8F + sway, 1.0F}, 1.5F, 0.7F, 35, gBody, solid<mMark>);
    }
  }

  leg(f, 2, hJoint, false, false);
  g.ellipse(hip + V2{0.8F, s.rumpRy * 0.15F}, s.haunchRx, s.haunchRy, pitch - 0.25F, 61, gLegNH,
            [](float u, float v) { return std::uint8_t(v > 0.7F && u > 0 ? mBelly : mBody); });
  leg(f, 0, fJoint, true, false);
  wing(f, sh + down * (-s.chestRy * 0.75F) + fwd * -1.0F, false);

  // the heads: Cerberus's two more stand behind and above, each on its own neck
  const float headAng = p.head + pitch * 0.4F;
  if (s.heads >= 3) {
    const V2 hc3 = neckBase + dir(p.neck + pitch * 0.3F - 0.5F) * (s.neckLen * 1.1F) + V2{-3.0F, -2.2F};
    g.capsule(neckBase, hc3, s.neckW * 1.05F, s.neckW * 0.9F, 33, gHead3, solid<mBody>, -0.3F);
    animalHead(f, hc3, headAng + p.heads[2] - 0.15F, 44, gHead3, -0.3F, false);
  }
  if (s.heads >= 2) {
    const V2 hc2 = neckBase + dir(p.neck + pitch * 0.3F + 0.45F) * (s.neckLen * 1.0F) + V2{2.8F, 2.4F};
    g.capsule(neckBase, hc2, s.neckW * 1.05F, s.neckW * 0.9F, 36, gHead2, solid<mBody>, -0.15F);
    animalHead(f, hc2, headAng + p.heads[1] + 0.1F, 66, gHead2, -0.15F, false);
  }
  animalHead(f, hc, headAng + p.heads[0], 76, gBody, 0, true);
}

// ── Birds ───────────────────────────────────────────────────────────────────

void bird(Frame& f) {
  const RigSpec& s = f.s;
  const RigPose& p = f.p;
  Grid& g = f.g;
  const float sq = std::clamp(p.squash, -0.4F, 0.4F);
  const float rx = s.bodyRx * (1 + sq * 0.3F), ry = s.bodyRy * (1 - sq * 0.4F);
  const float legH = std::max(0.5F, p.hipH);
  const V2 c{30 + p.x, G - legH - ry - p.lift};
  const float tilt = -0.15F + (p.shoulderH - p.hipH) * 0.04F + p.lean;
  for (int i : {3, 2}) {
    const bool far = i == 3;
    const V2 hipJ = c + V2{far ? 1.5F : -0.5F, ry * 0.8F};
    const V2 ft{hipJ.x + p.foot[i].x, G - 1.0F - std::max(0.0F, p.foot[i].y) - p.lift};
    const V2 k = knee(hipJ, ft, s.hindUp, s.hindLow, 1);
    const std::uint8_t layer = far ? 10 : 60;
    const float b = far ? -0.4F : 0;
    g.capsule(hipJ, k, 1.1F, 0.8F, layer, gLegNH, solid<mExtra>, b);
    g.capsule(k, ft, 0.8F, 0.7F, layer, gLegNH, solid<mExtra>, b);
    if (s.foot == RigFoot::Paw) {  // webbed: a duck's, a penguin's
      g.ellipse(ft + V2{1.0F, 0.3F}, 2.2F, 0.9F, 0, layer, gLegNH, solid<mExtra>, b);
    } else {
      g.capsule(ft + V2{-1.0F, 0.2F}, ft + V2{2.2F, 0.4F}, 0.6F, 0.55F, layer, gLegNH, solid<mExtra>, b);
      g.capsule(ft, ft + V2{1.2F, 0.9F}, 0.55F, 0.5F, layer, gLegNH, solid<mExtra>, b);
    }
  }
  if (s.tail != RigTail::None) {
    // tail feathers; a phoenix's trail several plumes long
    const int plumes = s.tail == RigTail::Plume ? 3 : 1;
    for (int k = plumes - 1; k >= 0; --k) {
      float a = PI + p.tailUp + (static_cast<float>(k) - (plumes - 1) * 0.5F) * 0.25F;
      V2 at = c + V2{-rx * 0.8F, ry * 0.1F};
      const int n = std::clamp(s.tailSegs, 2, 12);
      for (int i = 0; i < n; ++i) {
        a += p.tail[i] * (1 + 0.3F * k) + s.tailCurl;
        const V2 nx = at + dir(a) * (s.tailLen * (1 - 0.15F * k) / static_cast<float>(n));
        const float t = static_cast<float>(i) / static_cast<float>(n);
        const float w0 = s.tailW + (s.tailFat - s.tailW) * t, w1 = s.tailW + (s.tailFat - s.tailW) * (t + 1.0F / n);
        g.capsule(at, nx, w0, w1, static_cast<std::uint8_t>(8 - k), gTail, [&s, t](float, float) {
          if (s.tail == RigTail::Plume) return std::uint8_t(t > 0.75F ? mExtra : t > 0.45F ? mMark : mBody);
          return std::uint8_t(s.tailTip && t > 0.5F ? mMark : mBody);
        }, -0.1F * k);
        at = nx;
      }
    }
  }
  wing(f, c + V2{0, -ry * 0.2F}, true);
  g.ellipse(c, rx, ry, tilt, 40, gBody, [&s](float u, float v) {
    if (v > 0.15F && u > -0.55F) return std::uint8_t(mBelly);
    if (s.spots && v > 0.2F && std::fmod((u + 2) * 3.1F, 1.0F) > 0.7F && std::fmod((v + 2) * 3.0F, 1.0F) > 0.5F) return std::uint8_t(mMark);
    return std::uint8_t(mBody);
  });
  if (p.wingOpen < 0.25F) {
    g.ellipse(c + V2{-rx * 0.15F, ry * 0.05F + p.wing * 0.8F}, rx * 0.62F, ry * 0.45F, tilt - 0.35F, 66, gWingN,
              [](float u, float) { return std::uint8_t(u < -0.45F ? mMark : mBody); }, -0.05F);
  } else {
    wing(f, c + V2{-1.0F, -ry * 0.3F}, false);
  }
  const float headAng = p.head;
  const V2 hc = s.neckLen > 0.5F ? c + dir(p.neck) * s.neckLen : c + V2{rx * 0.25F, -ry * 0.55F};
  if (s.neckLen > 0.5F) {
    g.capsule(c + V2{rx * 0.5F, -ry * 0.4F}, hc, s.neckW, s.neckW * 0.8F, 41, gBody, [](float, float a) { return std::uint8_t(a > 0.3F ? mBelly : mBody); });
    g.ellipse(hc, s.headR, s.headRy, headAng, 76, gBody, [&s](float u, float v) { return std::uint8_t(s.mask && u > 0.1F && v > -0.4F ? mBelly : mBody); });
  } else if (s.mask) {
    g.ellipse(hc + V2{1.5F * (1 - p.yaw), 0.5F}, s.headR * 0.85F, s.headRy * 0.75F, 0, 77, gBody, solid<mBelly>, 0.15F);
  }
  if (s.mane) {
    // a crest: a few feathers rising from the crown
    for (int k = 0; k < 3; ++k) {
      const V2 b = hc + rot(V2{-1.0F - k, -s.headRy * 0.85F}, headAng);
      g.capsule(b, b + rot(V2{-2.5F - k, -4.5F + k + p.hem[k] * 2}, headAng), 0.8F, 0.4F, 74, gHair, solid<mMark>);
    }
  }
  ears(f, hc, headAng, true, 76, 0);
  ears(f, hc, headAng, false, 76, 0);
  const float yaw = std::clamp(p.yaw, 0.0F, 1.0F);
  const V2 bk = hc + rot(V2{s.headR * 0.75F * (1 - yaw) + 0.3F, s.snoutDrop}, headAng);
  const float bl = s.snout * (1 - yaw * 0.7F);
  const float gape = p.mouth * 1.5F;
  g.triangle(bk + rot(V2{-0.5F, -1.4F}, headAng), bk + rot(V2{-0.5F, 0.6F}, headAng), bk + rot(V2{bl, -0.2F - gape * 0.3F}, headAng), 84, gBeak,
             solid<mExtra>);
  if (gape > 0.3F)
    g.triangle(bk + rot(V2{-0.5F, 0.6F}, headAng), bk + rot(V2{-0.5F, 1.6F + gape * 0.3F}, headAng), bk + rot(V2{bl * 0.8F, 0.6F + gape}, headAng), 84,
               gBeak, solid<mExtra>, -0.3F);
  const float ex = s.eyeX * (1 - yaw) + yaw * s.headR * 0.4F;
  eye(g, hc + rot(V2{ex, s.eyeY}, headAng), s.eye, p, false);
  if (yaw > 0.4F) eye(g, hc + rot(V2{-s.headR * 0.4F * yaw + s.eyeX * (1 - yaw) - 2.5F * (1 - yaw), s.eyeY}, headAng), s.eye, p, true);
  if (p.blush > 0.3F) g.dot1(hc + rot(V2{ex - 0.5F, s.eyeY + 2.8F}, headAng), mBlush, 2, 249), g.dot1(hc + rot(V2{ex - 1.1F, s.eyeY + 2.8F}, headAng), mBlush, 2, 249);
}

// ── Blobs: a slime, a ghost ─────────────────────────────────────────────────

void blob(Frame& f) {
  const RigSpec& s = f.s;
  const RigPose& p = f.p;
  Grid& g = f.g;
  const float sq = std::clamp(p.squash, -0.45F, 0.45F);
  const float rx = s.bodyRx * (1 + sq * 0.45F), ry = s.bodyRy * (1 - sq * 0.55F);
  const bool ghost = s.floats;
  const float hover = ghost ? 5 + p.lift : p.lift;
  const V2 c{32 + p.x, G - ry - hover - (ghost ? 3 : 0.2F)};
  if (!ghost) {
    g.ellipse(c, rx, ry, 0, 40, gBody, [&s](float u, float v) {
      if (s.spots && v < 0.1F && std::fmod((u + 2) * 2.7F, 1.0F) > 0.72F && std::fmod((v + 2) * 2.4F, 1.0F) > 0.6F) return std::uint8_t(mMark);
      return std::uint8_t(v > 0.55F && s.bib ? mBelly : mBody);
    });
    // the bottom spreads flat on the ground
    const float floorY = G - p.lift;
    for (float x = -rx * 1.02F; x <= rx * 1.02F; x += 0.5F)
      for (float y = c.y; y < floorY; y += 0.5F) {
        const float u = x / rx;
        if (std::abs(u) < 1.02F - (y - c.y) / (ry * 8)) g.put(static_cast<int>(P({c.x + x, y}).x), static_cast<int>(P({c.x + x, y}).y), mBody, 1, 39, gBody);
      }
  } else {
    // a ghost: a dome over a wavy hem that trails behind as it drifts
    g.ellipse(c, rx, ry, 0, 40, gBody, solid<mBody>);
    g.box(c + V2{0, ry * 0.45F}, rx, ry * 0.5F, 0, 0.5F, 40, gBody, solid<mBody>);
    for (int k = 0; k < 4; ++k) {
      const float x = c.x - rx * 0.75F + static_cast<float>(k) * rx * 0.5F;
      const float wave = std::sin(p.wavePhase + k * 1.6F) * 1.2F;
      g.ellipse(V2{x - p.hem[k % 4] * 2, c.y + ry * 0.95F + wave}, rx * 0.27F, ry * 0.3F, 0, 39, gBody, solid<mBody>, -0.1F);
    }
    // its little arms
    for (int k = 0; k < 2; ++k) {
      const V2 a = c + V2{k ? rx * 0.85F : -rx * 0.9F, ry * 0.3F};
      const V2 h = a + V2{(k ? 2.5F : -2.0F) + p.hand[k].x, 1.5F + p.hand[k].y};
      g.capsule(a, h, 1.6F, 1.2F, k ? 41 : 38, k ? gArmN : gArmF, solid<mBody>, k ? 0 : -0.3F);
    }
    // its shadow, a dithered oval on the ground
    for (float x = -rx * 0.8F; x <= rx * 0.8F; x += 1 / K())
      for (int r = 0; r < 2; ++r) {
        const int px = static_cast<int>(P({c.x + x, 0}).x), py = N - 2 - r;
        if (((px + py) & 1) == 0 && std::abs(x) < rx * (0.8F - r * 0.25F)) g.put(px, py, mShadow, 2, 1, 250);
      }
  }
  ears(f, c + V2{0, ry * 0.25F}, 0, true, 40, 0);
  ears(f, c + V2{0, ry * 0.25F}, 0, false, 40, 0);
  g.dot1(c + V2{-rx * 0.5F, -ry * 0.55F}, mWhite, 2, 248), g.dot1(c + V2{-rx * 0.5F + 1 / K(), -ry * 0.62F}, mWhite, 2, 248);
  g.dot1(c + V2{-rx * 0.55F, -ry * 0.45F}, mWhite, 2, 248);
  const float yaw = std::clamp(p.yaw, 0.0F, 1.0F);
  const float face = rx * 0.32F * (1 - yaw * 0.9F);
  const V2 ec = c + V2{face, -ry * 0.1F + s.eyeY};
  eye(g, ec + V2{2.6F, 0}, s.eye, p, false);
  eye(g, ec + V2{-2.6F - (1 - yaw) * 1.0F, 0}, s.eye, p, false);
  const int mx = static_cast<int>(P(ec).x), my = static_cast<int>(P({ec.x, ec.y + 3.0F}).y);
  if (p.mouth > 0.2F) {
    for (int k = 0; k < 1 + static_cast<int>(p.mouth * 3); ++k)
      for (int x = -1; x <= 1; ++x) g.put(mx + x, my + k, k > 1 ? mTongue : mMouth, 2, 250, 250);
  } else {
    g.put(mx - 1, my, mMouth, 1, 250, 250), g.put(mx, my + 1, mMouth, 1, 250, 250), g.put(mx + 1, my, mMouth, 1, 250, 250);
  }
  if (p.blush > 0.3F || !ghost) {
    for (int k : {-1, 1}) {
      const int bx = mx + k * static_cast<int>(5.5F * K()), by = my - 1;
      g.put(bx, by, mBlush, 2, 249, 249), g.put(bx + 1, by, mBlush, 2, 249, 249);
    }
  }
}

// ── Serpents: a lung dragon ─────────────────────────────────────────────────

void serpent(Frame& f) {
  const RigSpec& s = f.s;
  const RigPose& p = f.p;
  Grid& g = f.g;
  // the body: a path behind the head that undulates, the wave running head to tail
  const int n = 22;
  const float seg = s.length / static_cast<float>(n);
  const V2 head{51 + p.x, G - 21 - p.lift};
  V2 pts[n + 1];
  pts[0] = head;
  float a = PI + 0.15F;
  for (int i = 0; i < n; ++i) {
    const float t = static_cast<float>(i) / n;
    a = PI + 0.12F + (0.55F + 0.35F * t) * std::cos(2 * PI * 1.35F * t - p.wavePhase) * (0.4F + p.wave);
    pts[i + 1] = pts[i] + dir(a) * seg;
    pts[i + 1].y = std::min(pts[i + 1].y, G - 2.5F);
  }
  auto width = [&s](float t) { return s.girth * (t < 0.12F ? 0.8F + t * 1.8F : 1.0F - 0.75F * (t - 0.12F) / 0.88F); };
  // legs: two pairs, small, paddling as it swims
  auto legAt = [&](float t, bool far, float ph) {
    const int i = static_cast<int>(t * n);
    const V2 base = pts[i];
    const float sw = std::sin(p.wavePhase * 1.0F + ph) * 0.6F;
    const V2 k = base + V2{-1.0F + sw, 3.0F};
    const V2 ft = k + V2{1.8F + sw, 1.8F};
    const std::uint8_t layer = far ? 10 : 60;
    const std::uint8_t grp = far ? gLegFF : gLegNF;
    const float b = far ? -0.4F : 0;
    g.capsule(base + V2{0, 1.0F}, k, 1.1F, 0.9F, layer, grp, solid<mBody>, b);
    g.capsule(k, ft, 0.9F, 0.7F, layer, grp, solid<mBody>, b);
    for (int c = 0; c < 3; ++c) g.capsule(ft, ft + V2{0.8F + 0.5F * c, 0.4F + 0.3F * c}, 0.4F, 0.3F, layer, grp, solid<mExtra>, b);
  };
  legAt(0.2F, true, 1.0F);
  legAt(0.55F, true, 2.6F);
  // the tail's tuft of flame-like hair
  {
    const V2 e = pts[n], d = pts[n] - pts[n - 1];
    const float ta = std::atan2(d.y, d.x);
    for (int k = -1; k <= 1; ++k)
      g.triangle(e + rot(V2{0, -1.2F}, ta), e + rot(V2{0, 1.2F}, ta), e + rot(V2{4.5F, k * 2.0F + p.hem[k + 1] * 2}, ta), 8, gTail, solid<mMark>,
                 -0.05F * std::abs(k));
  }
  for (int i = n - 1; i >= 0; --i) {
    const float t0 = static_cast<float>(i) / n, t1 = static_cast<float>(i + 1) / n;
    g.capsule(pts[i], pts[i + 1], width(t0), width(t1), static_cast<std::uint8_t>(30 + (n - i)), gBody, [t0, t1, &s](float t, float side) {
      const float tt = t0 + (t1 - t0) * t;
      // the belly plates on the underside, the back's scales above
      if (side < -0.4F) return std::uint8_t(std::fmod(tt * 40, 1.0F) > 0.75F ? mBody : mBelly);
      if (s.scales && side > 0.2F && std::fmod(tt * 30, 1.0F) > 0.7F) return std::uint8_t(mMark);
      return std::uint8_t(mBody);
    });
    // a dorsal fringe
    if (i % 2 == 0 && i > 1) {
      const V2 d = pts[i + 1] - pts[i];
      const V2 up = V2{d.y, -d.x} * (1 / std::max(0.01F, len(d)));
      const V2 m = lerp(pts[i], pts[i + 1], 0.5F);
      const V2 tip = m + up * (width(t0) + 1.8F) + d * (-0.5F / std::max(0.01F, len(d)));
      // the up vector must point up on screen
      const V2 tip2 = up.y > 0 ? m - up * (width(t0) + 1.8F) : tip;
      g.triangle(m - d * 0.25F, m + d * 0.25F, tip2, 29, gTail, solid<mMark>, -0.05F);
    }
  }
  legAt(0.2F, false, 0.0F);
  legAt(0.55F, false, 1.6F);
  // the head, at the front of the path
  const V2 d0 = pts[0] - pts[1];
  const float headAng = std::atan2(d0.y, d0.x) * 0.5F + p.head;
  animalHead(f, head + V2{2.0F, -1.0F}, headAng, 76, gBody, 0, true);
  // its shadow
  for (float x = 8; x <= 56; x += 1 / K())
    for (int r = 0; r < 2; ++r) {
      const int px = static_cast<int>(P({x, 0}).x), py = N - 2 - r;
      if (((px + py) & 1) == 0 && std::abs(x - 32) < 22 - r * 6) g.put(px, py, mShadow, 2, 1, 250);
    }
}

// ── Bipeds: people, a monkey king, a skeleton, mechs ────────────────────────

void biped(Frame& f) {
  const RigSpec& s = f.s;
  const RigPose& p = f.p;
  Grid& g = f.g;
  const bool armor = s.outfit == RigOutfit::Armor, bones = s.outfit == RigOutfit::Bones, robe = s.robe || s.outfit == RigOutfit::Robe;
  const float hover = s.floats ? 4 : 0;
  const V2 pelvis{32 + p.x, G - p.hipH - p.lift - hover};
  const V2 up{std::sin(p.lean), -std::cos(p.lean)};
  const V2 fwdT{-up.y, up.x};
  const float sq = std::clamp(p.squash, -0.3F, 0.3F);
  const V2 neck = pelvis + up * (s.torsoH * (1 - sq * 0.4F));
  const float headTilt = p.lean * 0.4F + p.head;
  const V2 hc = neck + rot(V2{0.6F, -s.headR * 0.95F}, headTilt);
  const float yaw = std::clamp(p.yaw, 0.0F, 1.0F);
  // 3/4 view, facing right: the near limbs are on the front side
  const V2 hipN = pelvis + V2{s.hipW * 0.3F, 0}, hipF = pelvis + V2{-s.hipW * 0.3F, -0.3F};
  const V2 shN = neck + fwdT * (s.torsoW * 0.5F) + up * -1.6F, shF = neck + fwdT * (-s.torsoW * 0.55F) + up * -1.8F;
  const std::uint8_t clothM = armor ? mExtra : mBody;

  auto legDraw = [&](int i, V2 hip, bool far) {
    const V2 ft{hip.x + p.foot[i].x, G - 1.0F - std::max(0.0F, p.foot[i].y) - p.lift - hover};
    const V2 k = knee(hip, ft, s.legUp, s.legLow, -1);  // knees forward
    const std::uint8_t layer = far ? 12 : 58, grp = far ? gLegFH : gLegNH;
    const float b = far ? -0.38F : 0;
    const float w = bones ? 0.7F : armor ? s.limbW * 1.3F : s.limbW;
    const std::uint8_t legM = bones ? mBody : armor ? mExtra : robe ? mBelly : s.outfit == RigOutfit::Fur ? mMark : mBody;
    const float legBias = s.outfit == RigOutfit::Tunic ? -0.3F : 0;
    g.capsule(hip, k, w * 1.15F, w, layer, grp, [legM](float, float) { return legM; }, b + legBias);
    g.capsule(k, ft, w, w * 0.9F, layer, grp, [legM](float, float) { return legM; }, b + legBias);
    if (bones) g.ellipse(k, 0.9F, 0.9F, 0, static_cast<std::uint8_t>(layer + 1), grp, solid<mBody>, b);
    if (armor) g.box(k, 1.6F, 1.3F, 0, 0.5F, static_cast<std::uint8_t>(layer + 1), grp, solid<mBody>, b);
    // the foot: a shoe, a boot, a bony foot
    const float curl = p.pawCurl[i];
    const std::uint8_t footM = bones ? mBody : armor ? mBody : mExtra;
    if (armor) g.box(ft + V2{0.8F, 0.3F}, 2.2F, 1.0F, curl * 0.4F, 0.4F, layer, grp, [footM](float, float) { return footM; }, b);
    else g.ellipse(ft + V2{0.8F - curl * 0.5F, 0.2F}, bones ? 1.5F : 1.9F, 0.95F, curl * 0.5F, layer, grp, [footM](float, float) { return footM; }, b);
  };
  auto armDraw = [&](int i, V2 sh, bool far) {
    const V2 hand = sh + p.hand[i];
    const V2 e = knee(sh, hand, s.armUp, s.armLow, 1);  // elbows back
    const std::uint8_t layer = far ? 11 : 86, grp = far ? gArmF : gArmN;
    const float b = far ? -0.38F : 0;
    const float w = bones ? 0.6F : armor ? s.limbW * 1.2F : s.limbW * 0.9F;
    const std::uint8_t sleeve = bones ? std::uint8_t(mBody) : armor ? std::uint8_t(mExtra) : clothM;
    const float sw = robe && !armor ? 1.4F : 1.0F;  // wide sleeves
    g.capsule(sh, e, w * 1.1F * sw, w * sw, layer, grp, [sleeve](float, float) { return sleeve; }, b);
    g.capsule(e, hand, w * sw, w * (robe ? 1.25F : 0.9F), layer, grp, [sleeve](float, float) { return sleeve; }, b);
    if (armor) g.box(sh, 2.3F, 1.8F, p.lean, 0.6F, static_cast<std::uint8_t>(layer + 1), grp, solid<mBody>, b);  // a pauldron
    const std::uint8_t handM = bones ? mBody : armor ? mBody : s.outfit == RigOutfit::Fur ? mMark : mBelly;
    g.ellipse(hand, bones ? 0.9F : 1.15F, bones ? 0.9F : 1.15F, 0, static_cast<std::uint8_t>(layer + 1), grp, [handM](float, float) { return handM; }, b);
  };

  // behind: the far arm and leg, the cape, a monkey's tail, a flying sword
  if (s.cape) {
    V2 a = neck + fwdT * (-s.torsoW * 0.3F);
    float ang = PI * 0.5F + 0.25F;
    for (int k = 0; k < 4; ++k) {
      ang += 0.06F + p.hem[k] * 0.5F;
      const V2 b = a + dir(ang) * (s.torsoH * 0.32F);
      g.capsule(a, b, 1.5F + k * 0.45F, 1.8F + k * 0.5F, 6, gCape, solid<mMark>, -0.1F);
      a = b;
    }
  }
  if (s.monkeyTail) {
    V2 a = pelvis + V2{-s.hipW * 0.4F, -0.5F};
    float ang = PI + 0.4F + p.tailUp;
    for (int k = 0; k < 8; ++k) {
      ang += -0.28F + p.tail[k];
      const V2 b = a + dir(ang) * 1.6F;
      g.capsule(a, b, 0.9F, 0.85F, 7, gTail, solid<mMark>);
      a = b;
    }
  }
  if (s.prop == RigProp::Sword) {
    // a flying sword: it floats by its master, point up, and circles when it is called
    const float ph = p.prop * 2 * PI;
    const V2 c = pelvis + V2{-6.5F + std::cos(ph) * 7 * std::min(1.0F, p.aura * 2), -s.torsoH * 0.9F + std::sin(ph) * 3 + std::sin(p.wavePhase) * 0.6F};
    const float ang = -PI * 0.5F + 0.25F + p.aura * 0.6F * std::sin(ph);
    const bool front = p.aura > 0.3F && std::sin(ph) > 0;
    const std::uint8_t lay = front ? 95 : 5;
    g.capsule(c, c + dir(ang) * 8.5F, 0.85F, 0.3F, lay, gProp, solid<mWhite>, 0.1F);
    g.capsule(c + dir(ang) * -0.4F + dir(ang + PI * 0.5F) * 1.7F, c + dir(ang) * -0.4F - dir(ang + PI * 0.5F) * 1.7F, 0.5F, 0.5F, lay, gProp, solid<mExtra>);
    g.capsule(c, c - dir(ang) * 2.4F, 0.55F, 0.55F, lay, gProp, solid<mMark>);
  }
  armDraw(1, shF, true);
  legDraw(3, hipF, true);
  // the hair's back part (long hair, a ponytail) falls behind the body
  if (s.hair == RigHair::Long || s.hair == RigHair::Ponytail) {
    V2 a = s.hair == RigHair::Long ? hc + V2{-s.headR * 0.55F, 0.5F} : hc + V2{-s.headR * 0.75F, -s.headR * 0.45F};
    float ang = PI * 0.5F + (s.hair == RigHair::Long ? 0.25F : 0.9F);
    for (int k = 0; k < 4; ++k) {
      ang += (s.hair == RigHair::Long ? 0.05F : -0.15F) + p.hem[k] * 0.6F;
      const V2 b = a + dir(ang) * (s.hair == RigHair::Long ? 2.6F : 2.4F);
      g.capsule(a, b, s.hair == RigHair::Long ? 2.6F - k * 0.25F : 1.7F - k * 0.3F, s.hair == RigHair::Long ? 2.4F - k * 0.25F : 1.4F - k * 0.3F, 14, gHair,
                solid<mMark>, -0.08F);
      a = b;
    }
  }

  // the body
  if (bones) {
    // spine, ribs, pelvis
    for (int k = 0; k < 5; ++k) g.ellipse(lerp(pelvis, neck, 0.1F + 0.2F * k), 0.8F, 0.65F, 0, 30, gBody, solid<mBody>);
    for (int k = 0; k < 3; ++k) {
      const V2 r0 = lerp(pelvis, neck, 0.48F + 0.16F * k);
      g.capsule(r0, r0 + fwdT * (s.torsoW * 0.7F) + up * -0.8F, 0.55F, 0.45F, 32, gBody, solid<mBody>);
      g.capsule(r0 + fwdT * (s.torsoW * 0.7F) + up * -0.8F, r0 + fwdT * (s.torsoW * 0.35F) + up * -1.6F, 0.45F, 0.4F, 32, gBody, solid<mBody>);
    }
    g.ellipse(pelvis + up * 0.6F, s.hipW * 0.55F, 1.4F, p.lean, 31, gBody, solid<mBody>);
  } else if (armor) {
    g.box(lerp(pelvis, neck, 0.5F), s.torsoW * 0.75F, s.torsoH * 0.55F, p.lean, 1.2F, 30, gBody,
          [](float u, float v) { return std::uint8_t(v > 0.55F ? mMark : (std::abs(u) < 0.25F && v < 0.2F && v > -0.6F) ? mGlow : mBody); });
    g.box(pelvis + up * 0.3F, s.hipW * 0.7F, 1.6F, p.lean, 0.6F, 31, gBody, solid<mExtra>);
  } else {
    const std::uint8_t torsoM = s.outfit == RigOutfit::Fur ? mMark : mBody;
    g.capsule(pelvis + up * 0.8F, neck + up * -1.0F, s.torsoW * 0.62F, s.torsoW * 0.72F, 30, gBody, [torsoM, &s](float t, float a) {
      if (s.outfit == RigOutfit::Fur && a > 0.15F && t > 0.2F && t < 0.85F) return std::uint8_t(mBelly);
      if (s.outfit == RigOutfit::Robe && std::abs(a + 0.15F) < 0.14F && t > 0.3F) return std::uint8_t(mMark);  // the robe's crossed collar
      return torsoM;
    });
    // a belt or sash
    if (s.outfit != RigOutfit::Fur) g.capsule(pelvis + up * 1.6F + fwdT * (-s.torsoW * 0.6F), pelvis + up * 1.6F + fwdT * (s.torsoW * 0.62F), 0.75F, 0.75F, 31, gBody, solid<mExtra>);
  }
  legDraw(2, hipN, false);
  if (robe) {
    // the robe: from the waist to the shins, its hem swinging with the stride
    const V2 w0 = pelvis + up * 1.5F;
    const float hemY = std::min(G - 1.5F - hover, w0.y + s.legUp + s.legLow * 0.7F);
    const float sway = -p.hem[0] * 3;  // the hem trails the stride
    const V2 hl{w0.x - s.hipW * 0.85F + sway * 0.6F, hemY}, hr{w0.x + s.hipW * 1.0F + sway, hemY + 0.4F};
    g.triangle(w0 + V2{-s.torsoW * 0.65F, 0}, w0 + V2{s.torsoW * 0.65F, 0}, hl, 62, gRobe, solid<mBody>);
    g.triangle(w0 + V2{s.torsoW * 0.65F, 0}, hr, hl, 62, gRobe, [](float t, float) { return std::uint8_t(t < 0.12F ? mMark : mBody); });
    g.capsule(hl, hr, 0.8F, 0.8F, 63, gRobe, solid<mMark>);
  }

  // the head
  if (armor) {
    g.box(hc, s.headR * 0.9F, s.headR * 0.8F, headTilt, 1.4F, 76, gBody, solid<mBody>);
    // the visor: a lit band across the face, a glint crossing it now and then
    const V2 vc = hc + rot(V2{s.headR * 0.35F * (0.4F + yaw * 0.6F), -0.4F}, headTilt);
    g.box(vc, s.headR * 0.55F, 1.3F, headTilt, 0.5F, 78, gBody, [&p](float u, float) {
      return std::uint8_t(p.aura > 0 && std::abs(u - (p.aura * 2.6F - 1.3F)) < 0.22F ? mWhite : mGlow);
    });
    g.box(hc + rot(V2{-1.0F, -s.headR * 0.85F}, headTilt), 0.6F, 1.6F, headTilt, 0.3F, 75, gHorn, solid<mMark>);  // the antenna
    g.dot1(hc + rot(V2{-1.0F, -s.headR * 0.85F - 1.6F}, headTilt), mGlow, 3, 252);
  } else {
    const std::uint8_t skin = bones ? mBody : s.outfit == RigOutfit::Fur ? mMark : mBelly;
    // ears at the side (a monkey's round ones, a person's)
    if (s.ear == RigEar::Round || s.ear == RigEar::Pointed) {
      const V2 ec = hc + rot(V2{-s.headR * 0.55F + yaw * 1.2F, 0.6F}, headTilt);
      g.ellipse(ec, s.ear == RigEar::Round ? 1.9F : 1.2F, s.ear == RigEar::Round ? 2.1F : 1.6F, 0, 75, gEarN,
                [skin](float u, float v) { return u * u + v * v < 0.3F ? std::uint8_t(mInner) : skin; });
    }
    g.ellipse(hc, s.headR, s.headR * 0.95F, headTilt, 76, gBody, [&s, yaw, skin](float u, float v) {
      // a monkey's face: a pale heart-shaped mask on the dark fur
      if (s.outfit == RigOutfit::Fur) {
        const float fu = u - (0.15F + 0.35F * yaw);
        if (v > -0.45F && fu * fu * 1.3F + (v - 0.25F) * (v - 0.25F) < 0.42F) return std::uint8_t(mBelly);
        if (v < 0 && (fu + 0.22F) * (fu + 0.22F) + (v + 0.05F) * (v + 0.05F) < 0.1F) return std::uint8_t(mBelly);
        if (v < 0 && (fu - 0.22F) * (fu - 0.22F) + (v + 0.05F) * (v + 0.05F) < 0.1F) return std::uint8_t(mBelly);
      }
      return skin;
    });
    if (bones) g.ellipse(hc + rot(V2{1.0F, s.headR * 0.75F}, headTilt), s.headR * 0.5F, s.headR * 0.32F, headTilt, 75, gBody, solid<mBody>, -0.1F);  // the jaw
  }
  // the hair and hats on top
  const float hairBob = p.hem[1] * 0.6F;
  switch (s.hair) {
    case RigHair::Short:
    case RigHair::Long:
    case RigHair::Ponytail:
    case RigHair::Bun:
    case RigHair::Circlet:
      g.ellipse(hc + rot(V2{-0.9F, -s.headR * 0.35F}, headTilt), s.headR * 1.02F, s.headR * 0.72F, headTilt - 0.2F, 79, gHair,
                [](float u, float v) { return std::uint8_t(v < 0.35F || u < -0.2F ? mMark : mNone); });
      // a fringe over the brow
      for (int k = 0; k < 3; ++k)
        g.triangle(hc + rot(V2{-1.0F + k * 1.7F, -s.headR * 0.7F}, headTilt), hc + rot(V2{0.8F + k * 1.7F, -s.headR * 0.6F}, headTilt),
                   hc + rot(V2{0.6F + k * 1.6F + hairBob, -s.headR * 0.12F}, headTilt), 80, gHair, solid<mMark>);
      if (s.hair == RigHair::Bun) {
        g.ellipse(hc + rot(V2{-1.2F, -s.headR * 1.05F}, headTilt), 2.2F, 2.0F, 0, 80, gHair, solid<mMark>);
        g.capsule(hc + rot(V2{-4.2F, -s.headR * 0.95F}, headTilt), hc + rot(V2{1.8F, -s.headR * 1.25F}, headTilt), 0.35F, 0.35F, 81, gProp, solid<mExtra>);
      }
      if (s.hair == RigHair::Circlet) {
        // the golden fillet, its ends curling at the temple
        g.capsule(hc + rot(V2{-s.headR * 0.9F, -s.headR * 0.42F}, headTilt), hc + rot(V2{s.headR * 0.85F, -s.headR * 0.42F}, headTilt), 0.6F, 0.6F, 81,
                  gProp, solid<mExtra>);
        g.ellipse(hc + rot(V2{s.headR * 0.25F, -s.headR * 0.42F}, headTilt), 0.9F, 0.8F, 0, 82, gProp, solid<mExtra>, 0.2F);
      }
      break;
    case RigHair::Spiky:
      for (int k = 0; k < 5; ++k) {
        const float a = -PI * 0.5F - 1.0F + 0.42F * k;
        const V2 b = hc + rot(V2{-0.6F, -s.headR * 0.2F}, headTilt) + dir(a + headTilt) * (s.headR * 0.65F);
        g.triangle(b + dir(a + headTilt + PI * 0.5F) * 1.6F, b - dir(a + headTilt + PI * 0.5F) * 1.6F,
                   b + dir(a + headTilt - 0.25F + hairBob * 0.2F) * (3.2F + (k == 2 ? 1.0F : 0)), 80, gHair, solid<mMark>);
      }
      g.ellipse(hc + rot(V2{-0.6F, -s.headR * 0.4F}, headTilt), s.headR * 0.98F, s.headR * 0.62F, headTilt, 79, gHair,
                [](float, float v) { return std::uint8_t(v < 0.3F ? mMark : mNone); });
      break;
    case RigHair::Hood:
      g.ellipse(hc + rot(V2{-0.6F, -0.3F}, headTilt), s.headR * 1.18F, s.headR * 1.12F, headTilt, 74, gHair, solid<mMark>, -0.05F);
      g.ellipse(hc + rot(V2{0.8F + yaw * 0.6F, 0.6F}, headTilt), s.headR * 0.78F, s.headR * 0.72F, headTilt, 77, gBody, solid<mBelly>);
      break;
    case RigHair::WizardHat: {
      const V2 brim = hc + rot(V2{-0.3F, -s.headR * 0.62F}, headTilt);
      g.ellipse(brim, s.headR * 1.45F, 1.3F, headTilt, 81, gHair, solid<mBody>);
      const V2 tip = brim + rot(V2{-4.5F + p.hem[2] * 3, -10.0F}, headTilt);
      g.triangle(brim + rot(V2{-s.headR * 0.8F, 0}, headTilt), brim + rot(V2{s.headR * 0.8F, 0}, headTilt), tip, 80, gHair,
                 [](float t, float) { return std::uint8_t(t > 0.12F && t < 0.26F ? mExtra : mBody); });
      g.dot1(tip, mGlow, 3, 252);
      // and the hair beneath it
      g.ellipse(hc + rot(V2{-s.headR * 0.65F, 0.5F}, headTilt), 2.2F, 3.2F, 0, 78, gHair, solid<mMark>);
      break;
    }
    case RigHair::Helmet:
    default: break;
  }
  // the face (3/4 view: both eyes, the near one larger and further out)
  if (!armor) {
    const float fx = s.eyeX * (1 - yaw * 0.3F);
    const V2 e1 = hc + rot(V2{fx + 1.4F + yaw * 0.6F, s.eyeY}, headTilt), e2 = hc + rot(V2{fx - 2.6F + yaw * 0.9F, s.eyeY}, headTilt);
    eye(g, e1, s.eye, p, false);
    if (yaw > 0.25F) eye(g, e2, s.eye, p, true);
    const V2 m = hc + rot(V2{fx + 0.2F + yaw * 0.7F, s.eyeY + 3.2F}, headTilt);
    const int mx = static_cast<int>(P(m).x), my = static_cast<int>(P(m).y);
    if (bones) {
      for (int k = -2; k <= 2; ++k) g.put(mx + k, my + 1, (k & 1) ? mPupil : mBody, 1, 250, 250);  // teeth
    } else if (p.mouth > 0.25F) {
      g.put(mx, my, mMouth, 2, 250, 250), g.put(mx + 1, my, mMouth, 2, 250, 250);
      g.put(mx, my + 1, p.mouth > 0.6F ? mTongue : mMouth, 2, 250, 250), g.put(mx + 1, my + 1, mMouth, 2, 250, 250);
    } else if (p.eyeStyle == 1 || p.blush > 0.3F) {
      g.put(mx - 1, my, mMouth, 2, 250, 250), g.put(mx, my + 1, mMouth, 2, 250, 250), g.put(mx + 1, my, mMouth, 2, 250, 250);
    } else {
      g.put(mx, my, mMouth, 1, 250, 250);
    }
    if (p.blush > 0.3F) {
      const V2 b = e1 + V2{0.2F, 2.4F};
      g.dot1(b, mBlush, 2, 249), g.dot1(b + V2{1 / K(), 0}, mBlush, 2, 249);
    }
  }
  armDraw(0, shN, false);
  // what it carries
  const V2 handN = shN + p.hand[0];
  switch (s.prop) {
    case RigProp::Staff: {
      // held across the hand; it spins when it plays with it
      const float a = -PI * 0.5F + 0.35F + p.prop * 2 * PI;
      const V2 d = dir(a);
      g.capsule(handN - d * 9.5F, handN + d * 11.5F, 0.7F, 0.7F, 90, gProp, [](float t, float) { return std::uint8_t(t < 0.1F || t > 0.9F ? mExtra : mMark); });
      break;
    }
    case RigProp::Lance: {
      const V2 d = dir(-PI * 0.5F + 0.15F + p.prop * 0.6F);
      g.capsule(handN - d * 6, handN + d * 13, 0.55F, 0.55F, 90, gProp, solid<mMark>);
      g.triangle(handN + d * 12.5F + V2{d.y, -d.x} * 1.3F, handN + d * 12.5F - V2{d.y, -d.x} * 1.3F, handN + d * 16.5F, 91, gProp, solid<mExtra>);
      break;
    }
    case RigProp::Orb: {
      // an orb floating over the open hand, glowing
      const V2 c = handN + V2{0.8F, -3.2F + std::sin(p.wavePhase) * 0.6F};
      g.ellipse(c, 2.0F, 2.0F, 0, 92, gGlow, [&p](float u, float v) { return std::uint8_t(u * u + v * v < 0.35F + 0.3F * p.aura ? mWhite : mGlow); });
      break;
    }
    case RigProp::Fan: {
      for (int k = 0; k < 5; ++k)
        g.capsule(handN, handN + dir(-PI * 0.5F - 0.6F + 0.3F * k + p.prop) * 4.5F, 0.4F, 1.0F, 90, gProp, solid<mExtra>);
      break;
    }
    default: break;
  }
  // a glow round it: qi as it meditates, sparks of an orb
  if (p.aura > 0.05F && !armor) {
    const V2 c0 = P({pelvis.x, pelvis.y - s.torsoH * 0.5F});
    const int cx = static_cast<int>(c0.x), cy = static_cast<int>(c0.y);
    const float R = (s.torsoH + s.headR * 2) * K() * 0.85F;
    for (int k = 0; k < 28; ++k) {
      const float a = k * 0.2244F + p.prop * 2 * PI * 0.5F;
      const float r = R * (0.92F + 0.08F * std::sin(k * 2.3F + p.wavePhase * 2));
      if (std::fmod(k * 0.618F + p.prop * 3, 1.0F) > p.aura) continue;
      g.put(cx + static_cast<int>(std::cos(a) * r), cy + static_cast<int>(std::sin(a) * r * 0.9F), mGlow, 3, 247, 247);
    }
  }
  if (s.floats) {
    for (float x = -5; x <= 5; x += 1 / K())
      for (int r = 0; r < 2; ++r) {
        const int px = static_cast<int>(P({pelvis.x + x, 0}).x), py = N - 2 - r;
        if (((px + py) & 1) == 0 && std::abs(x) < 5 - r * 1.5F) g.put(px, py, mShadow, 2, 1, 250);
      }
  }
}

}  // namespace

// ── Poses ───────────────────────────────────────────────────────────────────

RigPose rigStand(const RigSpec& s) {
  RigPose p;
  p.shoulderH = s.shoulderH;
  p.hipH = s.hipH;
  p.neck = s.neckAng;
  p.tailUp = s.tailUp;
  p.foot[0] = {0.4F, 0};
  p.foot[1] = {1.2F, 0};
  p.foot[2] = {-0.6F, 0};
  p.foot[3] = {0.3F, 0};
  switch (s.plan) {
    case RigPlan::Bird: p.hipH = s.hindUp + s.hindLow - 1.0F, p.shoulderH = p.hipH; break;
    case RigPlan::Biped:
      p.hipH = s.legUp + s.legLow - 0.3F;
      p.yaw = 0.5F;
      p.foot[2] = {0.6F, 0}, p.foot[3] = {-0.4F, 0};
      p.hand[0] = {0.6F, s.armUp + s.armLow - 0.9F};
      p.hand[1] = {-0.4F, s.armUp + s.armLow - 0.9F};
      p.tailUp = 0;
      break;
    case RigPlan::Blob: p.yaw = 0.7F; break;
    case RigPlan::Serpent: p.wave = 0.6F; break;
    default: break;
  }
  return p;
}

float rigCycle(const RigSpec& s, double trot) { return s.stride * static_cast<float>(1.0 + 0.45 * trot); }

void rigGait(const RigSpec& s, RigPose& p, double phase, double trot) {
  const double tr = std::clamp(trot, 0.0, 1.0);
  const float stride = rigCycle(s, tr);
  const RigPose rest = rigStand(s);
  if (s.plan == RigPlan::Serpent) {
    // it swims: the wave runs down its body as fast as it goes
    p.wavePhase = static_cast<float>(phase * 2 * M_PI);
    return;
  }
  if (s.plan == RigPlan::Blob) {
    // hops: squash, spring, stretch in the air, squash again
    const double ph = phase - std::floor(phase);
    const double air = std::max(0.0, std::sin(ph * 2 * M_PI));
    p.lift += static_cast<float>(air * s.lift * 2.2);
    p.squash = static_cast<float>(air > 0.05 ? -0.22 * air : 0.3 * std::sin(ph * 2 * M_PI - M_PI) * 1.0);
    if (s.floats) p.lift = static_cast<float>(std::sin(ph * 2 * M_PI) * 1.2), p.squash = 0, p.wavePhase = static_cast<float>(phase * 2 * M_PI);
    return;
  }
  if (s.plan == RigPlan::Biped) {
    // a walk: the legs in turn, the opposite arm swinging, the body lowest on each footfall
    if (s.floats) {
      p.lift = static_cast<float>(std::sin(phase * 2 * M_PI) * 0.8);
      p.lean = 0.12F;
      p.hand[0].x += static_cast<float>(std::sin(phase * 2 * M_PI) * 0.8);
      p.wavePhase = static_cast<float>(phase * 2 * M_PI);
      return;
    }
    const double duty = 0.6 - 0.1 * tr;
    for (int i = 2; i < 4; ++i) {
      const double ph = phase + (i == 2 ? 0.0 : 0.5) - std::floor(phase + (i == 2 ? 0.0 : 0.5));
      double x, y = 0, curl = 0;
      if (ph < duty) {
        x = stride * (0.5 - ph / duty);
      } else {
        const double u = (ph - duty) / (1 - duty), e = u * u * (3 - 2 * u);
        x = stride * (-0.5 + e);
        y = s.lift * std::sin(M_PI * u);
        curl = std::sin(M_PI * u) * 0.6;
      }
      p.foot[i] = {rest.foot[i].x + static_cast<float>(x), static_cast<float>(y)};
      p.pawCurl[i] = static_cast<float>(curl);
    }
    const float swing = static_cast<float>(std::sin(phase * 2 * M_PI)) * (1.8F + 1.2F * static_cast<float>(tr));
    p.hand[0].x += swing, p.hand[1].x -= swing;
    p.hand[0].y -= std::abs(swing) * 0.15F, p.hand[1].y -= std::abs(swing) * 0.15F;
    p.hipH -= static_cast<float>(0.7 * (0.5 + 0.5 * std::cos(phase * 4 * M_PI)));
    p.lean += static_cast<float>(0.06 + 0.12 * tr);
    p.head += static_cast<float>(0.03 * std::cos(phase * 4 * M_PI + 0.8));
    return;
  }
  // four legs (two for a bird): walk, a four-beat lateral sequence (LH, LF, RH, RF),
  // duty 0.66; trot, diagonal pairs, duty 0.5; a hopper bounds, hind feet then front
  static constexpr double walkOff[4] = {0.25, 0.75, 0.0, 0.5};
  static constexpr double trotOff[4] = {0.0, 0.5, 0.5, 0.0};
  static constexpr double hopOff[4] = {0.12, 0.12, 0.0, 0.0};
  const double duty = s.hops ? 0.45 : 0.66 - 0.16 * tr;
  for (int i = 0; i < 4; ++i) {
    const double off = s.hops ? hopOff[i] : walkOff[i] * (1 - tr) + trotOff[i] * tr;
    const double ph = phase + off - std::floor(phase + off);
    double x, y = 0, curl = 0;
    if (ph < duty) {
      x = stride * (0.5 - ph / duty);
    } else {
      const double u = (ph - duty) / (1 - duty);
      const double e = u * u * (3 - 2 * u);
      x = stride * (-0.5 + e);
      y = s.lift * std::sin(M_PI * u) * (1 + 0.3 * tr);
      curl = std::sin(M_PI * std::min(1.0, u * 1.3));
    }
    p.foot[i] = {rest.foot[i].x + static_cast<float>(x), static_cast<float>(y)};
    p.pawCurl[i] = static_cast<float>(curl);
  }
  if (s.plan == RigPlan::Bird && !s.hops) {
    // a waddle: the body rolls over each foot
    p.lean = static_cast<float>(0.08 * std::sin(phase * 2 * M_PI));
    p.head += static_cast<float>(0.05 * std::sin(phase * 4 * M_PI));
  }
  const double dip = std::cos(phase * 4 * M_PI);
  if (s.hops) {
    const double air = std::max(0.0, std::sin(phase * 2 * M_PI));
    p.lift += static_cast<float>(air * s.lift * 1.6);
    p.hipH -= static_cast<float>((1 - air) * 1.2);
    p.shoulderH += static_cast<float>(std::sin(phase * 2 * M_PI + 0.8) * 1.4);
    p.squash = static_cast<float>((1 - air) * 0.25 - air * 0.15);
  } else {
    p.hipH -= static_cast<float>((0.55 + 0.35 * tr) * (0.5 + 0.5 * dip));
    p.shoulderH -= static_cast<float>((0.55 + 0.35 * tr) * (0.5 + 0.5 * std::cos(phase * 4 * M_PI + 0.6)));
    p.head += static_cast<float>(0.06 * std::cos(phase * 4 * M_PI + 1.2));
  }
}

RigPose rigMix(const RigPose& a, const RigPose& b, float w) {
  auto m = [w](float x, float y) { return x + (y - x) * w; };
  RigPose r = w < 0.5F ? a : b;
  r.x = m(a.x, b.x);
  r.shoulderH = m(a.shoulderH, b.shoulderH);
  r.hipH = m(a.hipH, b.hipH);
  r.lean = m(a.lean, b.lean);
  r.neck = m(a.neck, b.neck);
  r.head = m(a.head, b.head);
  for (int i = 0; i < 3; ++i) r.heads[i] = m(a.heads[i], b.heads[i]);
  r.yaw = m(a.yaw, b.yaw);
  r.ears = m(a.ears, b.ears);
  r.earFlop = m(a.earFlop, b.earFlop);
  r.mouth = m(a.mouth, b.mouth);
  r.tongue = m(a.tongue, b.tongue);
  r.eyes = m(a.eyes, b.eyes);
  r.blush = m(a.blush, b.blush);
  for (int i = 0; i < 12; ++i) r.tail[i] = m(a.tail[i], b.tail[i]);
  for (int i = 0; i < 4; ++i) r.hem[i] = m(a.hem[i], b.hem[i]);
  r.tailUp = m(a.tailUp, b.tailUp);
  for (int i = 0; i < 4; ++i) {
    r.foot[i] = {m(a.foot[i].x, b.foot[i].x), m(a.foot[i].y, b.foot[i].y)};
    r.pawCurl[i] = m(a.pawCurl[i], b.pawCurl[i]);
  }
  for (int i = 0; i < 2; ++i) r.hand[i] = {m(a.hand[i].x, b.hand[i].x), m(a.hand[i].y, b.hand[i].y)};
  r.wing = m(a.wing, b.wing);
  r.wingOpen = m(a.wingOpen, b.wingOpen);
  r.squash = m(a.squash, b.squash);
  r.lift = m(a.lift, b.lift);
  r.wave = m(a.wave, b.wave);
  r.aura = m(a.aura, b.aura);
  return r;
}

// ── Drawing ─────────────────────────────────────────────────────────────────

namespace {
thread_local bool g_guide = false;
// the mannequin's flat colours, by part (see rigGuideMode)
std::uint32_t guideColour(const Px& p) {
  auto rgb = [](std::uint32_t c) { return (c >> 16 & 0xFF) | (c & 0xFF00) | (c & 0xFF) << 16; };
  if (p.mat == mEye || p.mat == mPupil || p.mat == mShine || p.mat == mWhite || p.mat == mNose) return rgb(0x3a3e48);
  switch (p.group) {
    case gLegNF: case gArmN: return rgb(0x2f74d0);
    case gLegFF: case gArmF: return rgb(0x9cc3f2);
    case gLegNH: return rgb(0x239a6c);
    case gLegFH: return rgb(0x96dbbd);
    case gTail: case gTail2: case gTail3: return rgb(0xe8924a);
    case gWingN: return rgb(0xa66fd6);
    case gWingF: return rgb(0xdcc4f2);
    case gRobe: case gCape: return rgb(0xd9c27a);
    case gProp: return rgb(0x7d828c);
    case gGlow: return rgb(0xf2e27a);
    default: return rgb(0xc9cdd5);
  }
}
}  // namespace

void rigGuideMode(bool on) { g_guide = on; }

void rigDraw(const RigSpec& spec, const RigPose& pose, const RigPalette& pal, bool flip, RigPixels& out) {
  Grid g(flip);
  g_xf = {U * spec.fitScale, spec.fitX};
  Frame f{spec, pose, g};
  switch (spec.plan) {
    case RigPlan::Quad: quad(f); break;
    case RigPlan::Bird: bird(f); break;
    case RigPlan::Blob: blob(f); break;
    case RigPlan::Serpent: serpent(f); break;
    case RigPlan::Biped: biped(f); break;
  }
  g.shade();

  // contours: the edge of a part that passes behind another gets the darkest shade
  static constexpr int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) {
      Px& p = g.at(x, y);
      if (p.mat == mNone || p.layer >= kStamp || p.mat == mGlow || p.mat == mShadow) continue;
      for (const auto& d : nb) {
        const int qx = x + d[0], qy = y + d[1];
        if (qx < 0 || qy < 0 || qx >= N || qy >= N) continue;
        const Px& q = g.at(qx, qy);
        // a near leg or haunch melts into the body it hangs from; everything else is set apart
        const bool limbOnBody = p.group == gBody && (q.group == gLegNF || q.group == gLegNH || q.group == gArmN);
        if (q.mat != mNone && q.layer < kStamp && q.mat != mShadow && q.group != p.group && q.layer > p.layer + 4 && !limbOnBody) {
          p.shade = kContour;
          break;
        }
      }
    }

  // the ramps: made once per palette (the conversions cost more than the drawing)
  struct RampCache {
    RigPalette pal;
    bool made = false;
    std::array<Ramp, mCount> ramps;
  };
  thread_local RampCache cache;
  auto same = [](const RigPalette& x, const RigPalette& y) {
    return x.body == y.body && x.belly == y.belly && x.mark == y.mark && x.eyes == y.eyes && x.extra == y.extra;
  };
  if (!cache.made || !same(cache.pal, pal)) {
    std::array<Ramp, mCount>& ramps = cache.ramps;
    const Color white{1, 1, 1, 1};
    ramps[mBody] = makeRamp(pal.body);
    ramps[mBelly] = makeRamp(pal.belly, 0.8F);
    ramps[mMark] = makeRamp(pal.mark);
    ramps[mExtra] = makeRamp(pal.extra);
    const Oklch bo = toOklch(pal.body);
    ramps[mInner] = makeRamp(fromOklch({std::clamp(bo.L * 0.4F + 0.5F, 0.6F, 0.82F), 0.09F, 12}), 0.7F);
    ramps[mBlush] = makeRamp(fromOklch({0.74F, 0.12F, 10}));
    ramps[mTongue] = makeRamp(fromOklch({0.66F, 0.15F, 8}));
    ramps[mMouth] = makeRamp(fromOklch({0.28F, 0.07F, 15}));
    ramps[mEye] = makeRamp(pal.eyes);
    const Oklch eo = toOklch(pal.eyes);
    ramps[mPupil] = makeRamp(fromOklch({std::min(eo.L, 0.22F), std::min(eo.C, 0.05F), eo.h}));
    ramps[mShine] = makeRamp(white);
    ramps[mWhite] = makeRamp(white);
    ramps[mNose] = makeRamp(fromOklch({std::min(0.32F, bo.L * 0.45F), 0.04F, bo.h}));
    // a glow: the eye colour, bright, never shaded
    Oklch gl = eo;
    gl.L = std::max(0.78F, eo.L), gl.C = std::max(0.12F, eo.C);
    const std::uint32_t glow = pack(fromOklch(gl));
    Ramp glowR;
    glowR.shade = {glow, glow, glow, pack(fromOklch({0.96F, gl.C * 0.4F, gl.h}))};
    glowR.contour = glow;
    glowR.outline = pack(fromOklch({0.35F, gl.C * 0.6F, gl.h}));
    ramps[mGlow] = glowR;
    const std::uint32_t shadow = pack(fromOklch({0.16F, 0.02F, 285}));
    Ramp shadowR;
    shadowR.shade = {shadow, shadow, shadow, shadow};
    shadowR.contour = shadowR.outline = shadow;
    ramps[mShadow] = shadowR;


    cache.pal = pal, cache.made = true;
  }
  const std::array<Ramp, mCount>& ramps = cache.ramps;

  out.fill(0);
  auto write = [&](int x, int y, std::uint32_t c) {
    const int ox = flip ? N - 1 - x : x;
    std::uint8_t* o = &out[(y * N + ox) * 4];
    o[0] = c & 0xFF, o[1] = c >> 8 & 0xFF, o[2] = c >> 16 & 0xFF, o[3] = 0xFF;
  };
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) {
      const Px& p = g.at(x, y);
      if (p.mat != mNone) {
        const Ramp& r = ramps[p.mat];
        if (g_guide) {
          if (p.mat != mShadow) write(x, y, guideColour(p));
          continue;
        }
        write(x, y, p.shade == kContour ? r.contour : r.shade[std::min<int>(p.shade, 3)]);
        continue;
      }
      // the outline: in the darkest tone of the frontmost neighbour (none round a shadow)
      const Px* best = nullptr;
      for (const auto& d : nb) {
        const int qx = x + d[0], qy = y + d[1];
        if (qx < 0 || qy < 0 || qx >= N || qy >= N) continue;
        const Px& q = g.at(qx, qy);
        if (q.mat != mNone && q.mat != mShadow && (!best || q.layer > best->layer)) best = &q;
      }
      if (best && g_guide) write(x, y, 0x5a5550);
      else if (best) write(x, y, ramps[best->mat == mShine || best->mat == mWhite ? std::uint8_t(best->layer >= kStamp ? mPupil : mExtra) : best->mat].outline);
    }
}

}  // namespace undershell
