// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from Sung (github.com/yappologistic/Sung) src/m3shape.cpp, MIT
// licence, (c) yappologistic; Qt removed, kept to the shapes and their radii.
#include "m3shapes.hpp"

#include <algorithm>
#include <cmath>

namespace undershell {

namespace {
constexpr double kPi = 3.14159265358979323846;
// graphics-shapes' own tolerance for a length that is nothing (Utils.kt).
constexpr double kDistanceEpsilon = 1e-4;

struct Point {
  double x = 0, y = 0;
  Point operator+(Point o) const { return {x + o.x, y + o.y}; }
  Point operator-(Point o) const { return {x - o.x, y - o.y}; }
  Point operator*(double k) const { return {x * k, y * k}; }
  double dot(Point o) const { return x * o.x + y * o.y; }
  double cross(Point o) const { return x * o.y - y * o.x; }
  double length() const { return std::hypot(x, y); }
  Point direction() const { return *this * (1 / length()); }
  Point rotate90() const { return {-y, x}; }
};

struct Cubic {
  Point a0, c0, c1, a1;
  bool line = false;
  Point at(double t) const {
    const double m = 1 - t;
    return a0 * (m * m * m) + c0 * (3 * m * m * t) + c1 * (3 * m * t * t) + a1 * (t * t * t);
  }
  bool zeroLength() const {
    return std::abs(a0.x - a1.x) < kDistanceEpsilon && std::abs(a0.y - a1.y) < kDistanceEpsilon;
  }
};

Cubic straightLine(Point from, Point to) {
  return {from, from + (to - from) * (1.0 / 3), from + (to - from) * (2.0 / 3), to, true};
}

// Cubic.circularArc: one cubic for the whole arc, its handles 4/3 tan(θ/4)
// of the radius long.
Cubic circularArc(Point centre, Point from, Point to) {
  const Point p0d = (from - centre).direction(), p1d = (to - centre).direction();
  const Point r0 = p0d.rotate90(), r1 = p1d.rotate90();
  const bool clockwise = r0.dot(to - centre) >= 0;
  const double cosa = p0d.dot(p1d);
  if (cosa > 0.999)
    return straightLine(from, to);
  const double k = (from - centre).length() * 4 / 3 *
                   (std::sqrt(2 * (1 - cosa)) - std::sqrt(1 - cosa * cosa)) / (1 - cosa) *
                   (clockwise ? 1 : -1);
  return {from, from + r0 * k, to - r1 * k, to};
}

struct Vertex {
  Point at;
  double rounding = 0;
};

// RoundedPolygon's constructor, for corners without smoothing: no shape in
// this library asks for any, so the flanking curves smoothing adds are
// always of no length and are left out. Each corner is cut back along both
// sides far enough for a circle of its rounding radius to meet them, and a
// side too short for both of its corners' cuts shares itself out between
// them in proportion, shrinking the radius to match.
std::vector<Cubic> rounded(const std::vector<Vertex> &vertices) {
  const int n = int(vertices.size());
  struct Corner {
    Point d1, d2;
    double radius = 0, cut = 0;
  };
  std::vector<Corner> corners(n);
  for (int i = 0; i < n; ++i) {
    const Point p0 = vertices[(i + n - 1) % n].at, p1 = vertices[i].at, p2 = vertices[(i + 1) % n].at;
    const Point v01 = p0 - p1, v21 = p2 - p1;
    if (v01.length() <= 0 || v21.length() <= 0)
      continue;
    auto &c = corners[i];
    c.d1 = v01.direction();
    c.d2 = v21.direction();
    c.radius = vertices[i].rounding;
    const double cosAngle = c.d1.dot(c.d2), sinAngle = std::sqrt(1 - cosAngle * cosAngle);
    c.cut = sinAngle > 1e-3 ? c.radius * (cosAngle + 1) / sinAngle : 0;
  }
  std::vector<double> sideRatio(n);
  for (int i = 0; i < n; ++i) {
    const double expected = corners[i].cut + corners[(i + 1) % n].cut;
    const double side = (vertices[i].at - vertices[(i + 1) % n].at).length();
    sideRatio[i] = expected > side ? side / expected : 1;
  }
  std::vector<Cubic> arcs(n);
  std::vector<bool> point(n, true);
  for (int i = 0; i < n; ++i) {
    const auto &c = corners[i];
    const Point p1 = vertices[i].at;
    const double allowed = std::min(c.cut * sideRatio[(i + n - 1) % n], c.cut * sideRatio[i]);
    if (c.cut < kDistanceEpsilon || allowed < kDistanceEpsilon || c.radius < kDistanceEpsilon) {
      arcs[i] = straightLine(p1, p1);
      continue;
    }
    const double cut = std::min(allowed, c.cut);
    const double radius = c.radius * cut / c.cut;
    const double centreDistance = std::sqrt(radius * radius + cut * cut);
    const Point centre = p1 + ((c.d1 + c.d2) * 0.5).direction() * centreDistance;
    arcs[i] = circularArc(centre, p1 + c.d1 * cut, p1 + c.d2 * cut);
    point[i] = false;
  }
  std::vector<Cubic> cubics;
  for (int i = 0; i < n; ++i) {
    if (!point[i] && !arcs[i].zeroLength())
      cubics.push_back(arcs[i]);
    const Cubic edge = straightLine(arcs[i].a1, arcs[(i + 1) % n].a0);
    if (!edge.zeroLength())
      cubics.push_back(edge);
  }
  return cubics;
}

Point rotated(Point p, double degrees, Point about = {}) {
  const double a = degrees * kPi / 180, c = std::cos(a), s = std::sin(a);
  const Point o = p - about;
  return Point{o.x * c - o.y * s, o.x * s + o.y * c} + about;
}

// RoundedPolygon(numVertices, radius, rounding): the first vertex at three
// o'clock and the rest evenly round.
std::vector<Vertex> regular(int count, double radius, double rounding) {
  std::vector<Vertex> out;
  for (int i = 0; i < count; ++i)
    out.push_back({{radius * std::cos(2 * kPi * i / count), radius * std::sin(2 * kPi * i / count)}, rounding});
  return out;
}

// RoundedPolygon.circle: a polygon whose corners are rounded by the radius of
// the circle it stands for, so the arcs meet in one.
std::vector<Vertex> circle(int count) { return regular(count, 1 / std::cos(kPi / count), 1); }

// RoundedPolygon.star: outer and inner vertices alternating, every corner
// rounded alike.
std::vector<Vertex> star(int count, double inner, double rounding) {
  std::vector<Vertex> out;
  for (int i = 0; i < count; ++i) {
    out.push_back({{std::cos(kPi / count * 2 * i), std::sin(kPi / count * 2 * i)}, rounding});
    out.push_back({{inner * std::cos(kPi / count * (2 * i + 1)), inner * std::sin(kPi / count * (2 * i + 1))}, rounding});
  }
  return out;
}

// MaterialShapes.customPolygon and doRepeat: a few points about (0.5, 0.5)
// repeated round the centre, every other repeat mirrored when asked.
std::vector<Vertex> custom(const std::vector<Vertex> &points, int reps, bool mirroring = false) {
  const Point centre{0.5, 0.5};
  std::vector<Vertex> out;
  const int np = int(points.size());
  if (mirroring) {
    std::vector<double> angles, distances;
    for (const auto &p : points) {
      const Point o = p.at - centre;
      angles.push_back(std::atan2(o.y, o.x) * 180 / kPi);
      distances.push_back(o.length());
    }
    const int actual = reps * 2;
    const double section = 360.0 / actual;
    for (int it = 0; it < actual; ++it)
      for (int index = 0; index < np; ++index) {
        const int i = it % 2 == 0 ? index : np - 1 - index;
        if (i > 0 || it % 2 == 0) {
          const double a = (section * it + (it % 2 == 0 ? angles[i] : section - angles[i] + 2 * angles[0])) * kPi / 180;
          out.push_back({Point{std::cos(a), std::sin(a)} * distances[i] + centre, points[i].rounding});
        }
      }
  } else {
    for (int it = 0; it < np * reps; ++it)
      out.push_back({rotated(points[it % np].at, (it / np) * 360.0 / reps, centre), points[it % np].rounding});
  }
  return out;
}

using Transform = Point (*)(Point);

struct Definition {
  const char *name;
  std::vector<Vertex> (*vertices)();
  Transform transform = nullptr;
};

// MaterialShapes.kt, companion object, each shape's construction as written
// there. The oval and the cookies are turned after rounding, as there.
const Definition kDefinitions[] = {
    {"circle", [] { return circle(10); }},
    {"square", [] { return std::vector<Vertex>{{{0.5, 0.5}, 0.3}, {{-0.5, 0.5}, 0.3}, {{-0.5, -0.5}, 0.3}, {{0.5, -0.5}, 0.3}}; }},
    {"oval", [] { return circle(8); }, [](Point p) { return rotated({p.x, p.y * 0.64}, -45); }},
    {"pill", [] { return custom({{{0.961, 0.039}, 0.426}, {{1.001, 0.428}, 0}, {{1.000, 0.609}, 1.0}}, 2, true); }},
    {"pentagon", [] { return custom({{{0.500, -0.009}, 0.172}, {{1.030, 0.365}, 0.164}, {{0.828, 0.970}, 0.169}}, 1, true); }},
    {"sunny", [] { return star(8, 0.8, 0.15); }},
    {"verySunny", [] { return custom({{{0.500, 1.080}, 0.085}, {{0.358, 0.843}, 0.085}}, 8); }},
    {"cookie4Sided", [] { return custom({{{1.237, 1.236}, 0.258}, {{0.500, 0.918}, 0.233}}, 4); }},
    {"cookie6Sided", [] { return custom({{{0.723, 0.884}, 0.394}, {{0.500, 1.099}, 0.398}}, 6); }},
    {"cookie7Sided", [] { return star(7, 0.75, 0.5); }, [](Point p) { return rotated(p, -90); }},
    {"cookie9Sided", [] { return star(9, 0.8, 0.5); }, [](Point p) { return rotated(p, -90); }},
    {"cookie12Sided", [] { return star(12, 0.8, 0.5); }, [](Point p) { return rotated(p, -90); }},
    {"clover4Leaf", [] { return custom({{{0.500, 0.074}, 0}, {{0.725, -0.099}, 0.476}}, 4, true); }},
    {"clover8Leaf", [] { return custom({{{0.500, 0.036}, 0}, {{0.758, -0.101}, 0.209}}, 8); }},
    {"burst", [] { return custom({{{0.500, -0.006}, 0.006}, {{0.592, 0.158}, 0.006}}, 12); }},
    {"softBurst", [] { return custom({{{0.193, 0.277}, 0.053}, {{0.176, 0.055}, 0.053}}, 10); }},
    {"flower", [] { return custom({{{0.370, 0.187}, 0}, {{0.416, 0.049}, 0.381}, {{0.479, 0.001}, 0.095}}, 8, true); }},
    {"puffyDiamond", [] { return custom({{{0.870, 0.130}, 0.146}, {{0.818, 0.357}, 0}, {{1.000, 0.332}, 0.853}}, 4, true); }},
};
constexpr int kShapeCount = int(sizeof(kDefinitions) / sizeof(kDefinitions[0]));

// Each curve is followed closely enough that the chord never strays more than
// a thousandth of the corner's radius from the arc it stands for.
constexpr int kStepsPerCurve = 32;

struct Outline {
  // The outline in the box's own units, centre at the origin and the longer
  // side running from -1 to 1, as RoundedPolygon.normalized and toShape
  // leave it.
  std::vector<Point> points;
  // Each point's angle about the centre, unwrapped so it only increases and
  // ends one full turn past where it starts.
  std::vector<double> angles;
};

Outline build(const Definition &definition) {
  auto cubics = rounded(definition.vertices());
  if (definition.transform)
    for (auto &c : cubics)
      c = {definition.transform(c.a0), definition.transform(c.c0), definition.transform(c.c1),
           definition.transform(c.a1), c.line};
  // RoundedPolygon.calculateBounds defaults to the approximate bounds, those
  // of the anchors and the handles, and normalized() and toShape both size
  // and centre the shape by them. A handle stands proud of its arc, so a
  // Material circle is drawn a little inside its box, and so is this one.
  double left = 1e9, top = 1e9, right = -1e9, bottom = -1e9;
  for (const auto &c : cubics)
    for (const Point p : {c.a0, c.c0, c.c1, c.a1}) {
      left = std::min(left, p.x);
      right = std::max(right, p.x);
      top = std::min(top, p.y);
      bottom = std::max(bottom, p.y);
    }
  const Point centre{(left + right) / 2, (top + bottom) / 2};
  const double scale = 2 / std::max(right - left, bottom - top);
  Outline out;
  for (const auto &c : cubics) {
    const int steps = c.line ? 1 : kStepsPerCurve;
    for (int k = 0; k < steps; ++k)
      out.points.push_back((c.at(double(k) / steps) - centre) * scale);
  }
  // Wind the outline so its angle increases, then unwrap it.
  double area = 0;
  for (size_t i = 0; i < out.points.size(); ++i)
    area += out.points[i].cross(out.points[(i + 1) % out.points.size()]);
  if (area < 0)
    std::reverse(out.points.begin(), out.points.end());
  double previous = 0;
  for (size_t i = 0; i < out.points.size(); ++i) {
    double a = std::atan2(out.points[i].y, out.points[i].x);
    if (i > 0)
      while (a < previous - kPi)
        a += 2 * kPi;
    out.angles.push_back(a);
    previous = a;
  }
  return out;
}

const std::vector<Outline> &outlines() {
  // Built once, together, on first use; a function-local static is safe to
  // reach from the render thread too.
  static const std::vector<Outline> all = [] {
    std::vector<Outline> built;
    for (const auto &definition : kDefinitions)
      built.push_back(build(definition));
    return built;
  }();
  return all;
}

// The radius where a ray from the centre at `angle` leaves the outline. The
// shapes are star-shaped about their centre, so the outline's angle rises
// monotonically and the one edge the ray crosses is found by bisection.
double radiusAt(const Outline &outline, double angle) {
  const auto &a = outline.angles;
  const int n = int(a.size());
  double t = std::fmod(angle - a[0], 2 * kPi);
  if (t < 0)
    t += 2 * kPi;
  t += a[0];
  const int upper = int(std::upper_bound(a.begin(), a.end(), t) - a.begin());
  const Point p = outline.points[(upper - 1 + n) % n], q = outline.points[upper % n];
  const Point u{std::cos(angle), std::sin(angle)}, e = q - p;
  const double denominator = u.cross(e);
  if (std::abs(denominator) < 1e-12)
    return std::min(p.length(), q.length());
  return p.cross(e) / denominator;
}

}  // namespace

const std::vector<std::string>& m3ShapeNames() {
  static const std::vector<std::string> names = [] {
    std::vector<std::string> n;
    for (const auto& d : kDefinitions) n.emplace_back(d.name);
    return n;
  }();
  return names;
}

std::vector<float> m3ShapeRadii(const std::string& name, int count) {
  count = std::clamp(count, 8, 1024);
  int index = -1;
  for (int i = 0; i < kShapeCount; ++i)
    if (name == kDefinitions[i].name) index = i;
  std::vector<float> radii(static_cast<size_t>(count), 1.0F);
  if (index < 0) return radii;
  for (int i = 0; i < count; ++i) radii[static_cast<size_t>(i)] = static_cast<float>(radiusAt(outlines()[index], i * 2 * kPi / count));
  return radii;
}

}  // namespace undershell
