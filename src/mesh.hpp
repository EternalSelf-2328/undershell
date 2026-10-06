// SPDX-License-Identifier: GPL-3.0-or-later
// The mesh warp: a widget's box bent through a grid of meshN x meshN points
// placed on the output (row by row from the top-left). Smooth, the surface is
// a Catmull-Rom patch that passes through every point (a cylinder, a flag, a
// bulging rock); straight, each cell between four points is bilinear, so the
// box folds along the grid lines (a box's corner, an open book).
#pragma once

#include "config.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace undershell {

inline int meshCorner(int n, int k) {  // top-left, top-right, bottom-right, bottom-left
  return k == 0 ? 0 : k == 1 ? n - 1 : k == 2 ? n * n - 1 : n * (n - 1);
}

// control point (column i, row j); outside the grid it is mirrored through
// the edge (P(-1) = 2 P(0) - P(1)), so the patch continues past its border
inline void meshCtrl(const WidgetConfig& c, int i, int j, double& x, double& y) {
  const int n = c.meshN;
  auto at = [&](int a, int b, double& px, double& py) {
    px = c.mesh[2 * (b * n + a)];
    py = c.mesh[2 * (b * n + a) + 1];
  };
  const int ci = std::clamp(i, 0, n - 1), cj = std::clamp(j, 0, n - 1);
  if (ci == i && cj == j) {
    at(i, j, x, y);
    return;
  }
  double ax = 0, ay = 0, bx = 0, by = 0;
  at(ci, cj, ax, ay);
  at(ci + (i < 0 ? 1 : i > n - 1 ? -1 : 0), cj + (j < 0 ? 1 : j > n - 1 ? -1 : 0), bx, by);
  x = 2 * ax - bx;
  y = 2 * ay - by;
}

inline double catmullRom(double p0, double p1, double p2, double p3, double t) {
  return 0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (3 * p1 - p0 - 3 * p2 + p3) * t * t * t);
}

// the surface at (u, v) of the box (0..1 each; a little outside extrapolates)
inline void meshEval(const WidgetConfig& c, double u, double v, double& x, double& y) {
  const int n = c.meshN;
  const double fu = u * (n - 1), fv = v * (n - 1);
  const int i = std::clamp(static_cast<int>(std::floor(fu)), 0, n - 2);
  const int j = std::clamp(static_cast<int>(std::floor(fv)), 0, n - 2);
  const double s = fu - i, t = fv - j;
  if (!c.meshSmooth) {
    double x00, y00, x10, y10, x01, y01, x11, y11;
    meshCtrl(c, i, j, x00, y00);
    meshCtrl(c, i + 1, j, x10, y10);
    meshCtrl(c, i, j + 1, x01, y01);
    meshCtrl(c, i + 1, j + 1, x11, y11);
    x = (1 - t) * ((1 - s) * x00 + s * x10) + t * ((1 - s) * x01 + s * x11);
    y = (1 - t) * ((1 - s) * y00 + s * y10) + t * ((1 - s) * y01 + s * y11);
    return;
  }
  double rx[4], ry[4];
  for (int k = 0; k < 4; ++k) {
    double px[4], py[4];
    for (int m = 0; m < 4; ++m) meshCtrl(c, i - 1 + m, j - 1 + k, px[m], py[m]);
    rx[k] = catmullRom(px[0], px[1], px[2], px[3], s);
    ry[k] = catmullRom(py[0], py[1], py[2], py[3], s);
  }
  x = catmullRom(rx[0], rx[1], rx[2], rx[3], t);
  y = catmullRom(ry[0], ry[1], ry[2], ry[3], t);
}

// the surface sampled on a (steps+1)^2 lattice, row-major, from -du..1+du
// across and -dv..1+dv down (the margin is room for an antialiased edge)
struct MeshGrid {
  int steps = 0;
  std::vector<double> x, y, u, v;
};
inline int meshSteps(const WidgetConfig& c) { return 8 * (c.meshN - 1); }
inline MeshGrid meshGrid(const WidgetConfig& c, double du = 0, double dv = 0) {
  MeshGrid g;
  g.steps = meshSteps(c);
  const int m = g.steps + 1;
  g.x.resize(static_cast<size_t>(m) * m);
  g.y.resize(g.x.size());
  g.u.resize(g.x.size());
  g.v.resize(g.x.size());
  for (int b = 0; b < m; ++b)
    for (int a = 0; a < m; ++a) {
      const size_t k = static_cast<size_t>(b) * m + a;
      g.u[k] = -du + (1 + 2 * du) * a / g.steps;
      g.v[k] = -dv + (1 + 2 * dv) * b / g.steps;
      meshEval(c, g.u[k], g.v[k], g.x[k], g.y[k]);
    }
  return g;
}

// output point -> (u, v) on the surface, or false outside it
inline bool meshInverse(const WidgetConfig& c, double ox, double oy, double& u, double& v) {
  const MeshGrid g = meshGrid(c);
  const int m = g.steps + 1;
  auto tri = [&](size_t a, size_t b, size_t d) {
    const double x0 = g.x[a], y0 = g.y[a];
    const double e1x = g.x[b] - x0, e1y = g.y[b] - y0, e2x = g.x[d] - x0, e2y = g.y[d] - y0;
    const double den = e1x * e2y - e2x * e1y;
    if (std::abs(den) < 1e-12) return false;
    const double px = ox - x0, py = oy - y0;
    const double l1 = (px * e2y - e2x * py) / den, l2 = (e1x * py - px * e1y) / den;
    if (l1 < -1e-9 || l2 < -1e-9 || l1 + l2 > 1 + 1e-9) return false;
    u = g.u[a] + l1 * (g.u[b] - g.u[a]) + l2 * (g.u[d] - g.u[a]);
    v = g.v[a] + l1 * (g.v[b] - g.v[a]) + l2 * (g.v[d] - g.v[a]);
    return true;
  };
  for (int b = 0; b < g.steps; ++b)
    for (int a = 0; a < g.steps; ++a) {
      const size_t p00 = static_cast<size_t>(b) * m + a, p10 = p00 + 1, p01 = p00 + m, p11 = p01 + 1;
      if (tri(p00, p10, p01) || tri(p11, p01, p10)) return true;
    }
  return false;
}

// the box size that keeps the widget crisp at its shape: the mean length of
// its top and bottom edges, of its left and right ones
inline void meshFitSize(const WidgetConfig& c, int& w, int& h) {
  const MeshGrid g = meshGrid(c);
  const int m = g.steps + 1;
  auto len = [&](size_t start, size_t stride) {
    double L = 0;
    for (int k = 0; k < g.steps; ++k) {
      const size_t a = start + k * stride, b = a + stride;
      L += std::hypot(g.x[b] - g.x[a], g.y[b] - g.y[a]);
    }
    return L;
  };
  const size_t last = static_cast<size_t>(m) * (m - 1);
  w = std::max(48, static_cast<int>(std::lround((len(0, 1) + len(last, 1)) / 2)));
  h = std::max(32, static_cast<int>(std::lround((len(0, m) + len(m - 1, m)) / 2)));
}

}  // namespace undershell
