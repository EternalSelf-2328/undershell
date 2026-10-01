// SPDX-License-Identifier: GPL-3.0-or-later
// A widget's placement on its output once `rotation` and perspective are
// applied: the widget keeps its own box (x, y, width, height); it is skewed,
// leaned back in 3D (tilt), seen in perspective, then turned about the box's
// centre. Its surface is the bounding rectangle of the result. A plane seen
// in perspective is a homography of the box, so output <-> widget mapping
// stays exact (clicks land where they should).
#pragma once

#include "config.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace undershell {

struct Box {
  int x = 0, y = 0, w = 0, h = 0;
};

inline bool warped(const WidgetConfig& c) {
  return std::abs(c.tiltX) > 0.01 || std::abs(c.tiltY) > 0.01 || std::abs(c.skewX) > 0.01;
}
inline bool turnedOnly(const WidgetConfig& c) { return std::abs(c.rotation) > 0.01; }
// turned or warped: drawn off-screen and laid on its surface
inline bool rotated(const WidgetConfig& c) { return turnedOnly(c) || warped(c); }

// a 3x3 projective map, row-major
struct Homography {
  double m[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
  void apply(double x, double y, double& ox, double& oy) const {
    const double w = m[6] * x + m[7] * y + m[8];
    ox = (m[0] * x + m[1] * y + m[2]) / w;
    oy = (m[3] * x + m[4] * y + m[5]) / w;
  }
  [[nodiscard]] Homography inverse() const {
    const double* a = m;
    Homography r;
    r.m[0] = a[4] * a[8] - a[5] * a[7];
    r.m[1] = a[2] * a[7] - a[1] * a[8];
    r.m[2] = a[1] * a[5] - a[2] * a[4];
    r.m[3] = a[5] * a[6] - a[3] * a[8];
    r.m[4] = a[0] * a[8] - a[2] * a[6];
    r.m[5] = a[2] * a[3] - a[0] * a[5];
    r.m[6] = a[3] * a[7] - a[4] * a[6];
    r.m[7] = a[1] * a[6] - a[0] * a[7];
    r.m[8] = a[0] * a[4] - a[1] * a[3];
    return r;
  }
};

// the unit square (0,0) (1,0) (1,1) (0,1) onto a quad (Heckbert)
inline Homography squareToQuad(const double qx[4], const double qy[4]) {
  const double sx = qx[0] - qx[1] + qx[2] - qx[3], sy = qy[0] - qy[1] + qy[2] - qy[3];
  Homography h;
  if (std::abs(sx) < 1e-12 && std::abs(sy) < 1e-12) {  // affine
    h.m[0] = qx[1] - qx[0], h.m[1] = qx[2] - qx[1], h.m[2] = qx[0];
    h.m[3] = qy[1] - qy[0], h.m[4] = qy[2] - qy[1], h.m[5] = qy[0];
    h.m[6] = 0, h.m[7] = 0, h.m[8] = 1;
    return h;
  }
  const double dx1 = qx[1] - qx[2], dx2 = qx[3] - qx[2], dy1 = qy[1] - qy[2], dy2 = qy[3] - qy[2];
  const double den = dx1 * dy2 - dx2 * dy1;
  const double g = (sx * dy2 - dx2 * sy) / den, hh = (dx1 * sy - sx * dy1) / den;
  h.m[0] = qx[1] - qx[0] + g * qx[1], h.m[1] = qx[3] - qx[0] + hh * qx[3], h.m[2] = qx[0];
  h.m[3] = qy[1] - qy[0] + g * qy[1], h.m[4] = qy[3] - qy[0] + hh * qy[3], h.m[5] = qy[0];
  h.m[6] = g, h.m[7] = hh, h.m[8] = 1;
  return h;
}

// The box's four corners on the output (top-left, top-right, bottom-right,
// bottom-left), after skew, tilt, perspective and rotation.
inline void widgetCorners(const WidgetConfig& c, double qx[4], double qy[4]) {
  const double w = c.width, h = c.height, cx = c.x + w / 2.0, cy = c.y + h / 2.0;
  const double d2r = std::numbers::pi / 180.0;
  const double tx = c.tiltX * d2r, ty = c.tiltY * d2r, sk = std::tan(c.skewX * d2r), rz = c.rotation * d2r;
  const double f = c.perspective * std::max(w, h);
  const double lx[4] = {0, w, w, 0}, ly[4] = {0, 0, h, h};
  for (int i = 0; i < 4; ++i) {
    double X = lx[i] - w / 2.0, Y = ly[i] - h / 2.0;
    X -= sk * Y;  // top (Y < 0) moves right for a positive skew
    // lean back about the horizontal axis (top away), then the vertical one (right away)
    const double Y1 = Y * std::cos(tx), Z1 = -Y * std::sin(tx);
    const double X2 = X * std::cos(ty) - Z1 * std::sin(ty), Z2 = X * std::sin(ty) + Z1 * std::cos(ty);
    const double s = f / std::max(f + Z2, f * 0.05);  // never behind the camera
    const double px = X2 * s, py = Y1 * s;
    qx[i] = cx + px * std::cos(rz) - py * std::sin(rz);
    qy[i] = cy + px * std::sin(rz) + py * std::cos(rz);
  }
}

// widget px -> output, for a warped widget
inline Homography widgetHomography(const WidgetConfig& c) {
  double qx[4], qy[4];
  widgetCorners(c, qx, qy);
  Homography h = squareToQuad(qx, qy);
  // from widget px instead of the unit square
  for (int r = 0; r < 3; ++r) {
    h.m[r * 3 + 0] /= c.width;
    h.m[r * 3 + 1] /= c.height;
  }
  return h;
}

// degrees into (-180, 180]
inline double normalizeDegrees(double d) {
  d = std::fmod(d, 360.0);
  if (d <= -180.0) d += 360.0;
  if (d > 180.0) d -= 360.0;
  return d;
}

// The bounding rectangle of the turned box (snapping, labels). `pad` widens
// it on every side, room for the antialiased edge on the surface.
inline Box visualBox(const WidgetConfig& c, int pad = 0) {
  if (!rotated(c)) return {c.x - pad, c.y - pad, c.width + 2 * pad, c.height + 2 * pad};
  if (warped(c)) {
    double qx[4], qy[4];
    widgetCorners(c, qx, qy);
    double x0 = qx[0], x1 = qx[0], y0 = qy[0], y1 = qy[0];
    for (int i = 1; i < 4; ++i) x0 = std::min(x0, qx[i]), x1 = std::max(x1, qx[i]), y0 = std::min(y0, qy[i]), y1 = std::max(y1, qy[i]);
    const int bx = static_cast<int>(std::floor(x0)) - pad, by = static_cast<int>(std::floor(y0)) - pad;
    return {bx, by, static_cast<int>(std::ceil(x1)) + pad - bx, static_cast<int>(std::ceil(y1)) + pad - by};
  }
  const double r = c.rotation * std::numbers::pi / 180.0;
  const double cs = std::abs(std::cos(r)), sn = std::abs(std::sin(r));
  const double bw = c.width * cs + c.height * sn + 2 * pad, bh = c.width * sn + c.height * cs + 2 * pad;
  const double cx = c.x + c.width / 2.0, cy = c.y + c.height / 2.0;
  // (cos 90° is not exactly 0: a hair of slack keeps a quarter turn exact)
  const int w = static_cast<int>(std::ceil(bw - 1e-6)), h = static_cast<int>(std::ceil(bh - 1e-6));
  return {static_cast<int>(std::lround(cx - w / 2.0)), static_cast<int>(std::lround(cy - h / 2.0)), w, h};
}

// the widget's surface: one extra pixel around a turned box for its soft edge
inline Box surfaceBox(const WidgetConfig& c) { return visualBox(c, rotated(c) ? 1 : 0); }

// output point -> the widget's own (untransformed) coordinates
inline void toLocal(const WidgetConfig& c, double ox, double oy, double& lx, double& ly) {
  if (warped(c)) {
    widgetHomography(c).inverse().apply(ox, oy, lx, ly);
    return;
  }
  const double r = -c.rotation * std::numbers::pi / 180.0;
  const double dx = ox - (c.x + c.width / 2.0), dy = oy - (c.y + c.height / 2.0);
  lx = dx * std::cos(r) - dy * std::sin(r) + c.width / 2.0;
  ly = dx * std::sin(r) + dy * std::cos(r) + c.height / 2.0;
}

// the widget's own coordinates -> output point
inline void toOutput(const WidgetConfig& c, double lx, double ly, double& ox, double& oy) {
  if (warped(c)) {
    widgetHomography(c).apply(lx, ly, ox, oy);
    return;
  }
  const double r = c.rotation * std::numbers::pi / 180.0;
  const double dx = lx - c.width / 2.0, dy = ly - c.height / 2.0;
  ox = dx * std::cos(r) - dy * std::sin(r) + c.x + c.width / 2.0;
  oy = dx * std::sin(r) + dy * std::cos(r) + c.y + c.height / 2.0;
}

// surface px (of surfaceBox) -> texture uv (0..1), for the warp blit
inline Homography surfaceToUv(const WidgetConfig& c) {
  double qx[4], qy[4];
  widgetCorners(c, qx, qy);
  const Box b = surfaceBox(c);  // the same box the surface is made of
  for (int i = 0; i < 4; ++i) qx[i] -= b.x, qy[i] -= b.y;
  return squareToQuad(qx, qy).inverse();
}

inline bool insideWidget(const WidgetConfig& c, double ox, double oy) {
  double lx = 0, ly = 0;
  toLocal(c, ox, oy, lx, ly);
  return lx >= 0 && ly >= 0 && lx < c.width && ly < c.height;
}

}  // namespace undershell
