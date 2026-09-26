// SPDX-License-Identifier: GPL-3.0-or-later
// A widget's placement on its output once `rotation` is applied: the widget
// keeps its own box (x, y, width, height) and turns about that box's centre;
// its surface is the rotated box's bounding rectangle.
#pragma once

#include "config.hpp"

#include <cmath>
#include <numbers>

namespace undershell {

struct Box {
  int x = 0, y = 0, w = 0, h = 0;
};

inline bool rotated(const WidgetConfig& c) { return std::abs(c.rotation) > 0.01; }

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

// output point -> the widget's own (unrotated) coordinates
inline void toLocal(const WidgetConfig& c, double ox, double oy, double& lx, double& ly) {
  const double r = -c.rotation * std::numbers::pi / 180.0;
  const double dx = ox - (c.x + c.width / 2.0), dy = oy - (c.y + c.height / 2.0);
  lx = dx * std::cos(r) - dy * std::sin(r) + c.width / 2.0;
  ly = dx * std::sin(r) + dy * std::cos(r) + c.height / 2.0;
}

// the widget's own coordinates -> output point
inline void toOutput(const WidgetConfig& c, double lx, double ly, double& ox, double& oy) {
  const double r = c.rotation * std::numbers::pi / 180.0;
  const double dx = lx - c.width / 2.0, dy = ly - c.height / 2.0;
  ox = dx * std::cos(r) - dy * std::sin(r) + c.x + c.width / 2.0;
  oy = dx * std::sin(r) + dy * std::cos(r) + c.y + c.height / 2.0;
}

inline bool insideWidget(const WidgetConfig& c, double ox, double oy) {
  double lx = 0, ly = 0;
  toLocal(c, ox, oy, lx, ly);
  return lx >= 0 && ly >= 0 && lx < c.width && ly < c.height;
}

}  // namespace undershell
