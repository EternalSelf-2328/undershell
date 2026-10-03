// SPDX-License-Identifier: GPL-3.0-or-later
// Layouts follow the wallpaper, not the screen. A widget block remembers the
// output size it was laid out on (`space = [w, h]`; legacy blocks were all
// made at 1920x1080). On an output of another size its box goes where the
// same part of the wallpaper now is, through the wallpaper's fill mode (crop
// on a smaller screen shows the image smaller, so the widget shrinks with it
// and stays on the rock or the portal it was put on).
#pragma once

#include "config.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace undershell {

constexpr double kLegacySpaceW = 1920, kLegacySpaceH = 1080;

// image px -> output px: X = ox + u * sx (Noctalia's calculateWallpaperUV,
// fill modes center crop fit stretch repeat span)
struct WallpaperFit {
  double sx = 1, sy = 1, ox = 0, oy = 0;
};
inline WallpaperFit wallpaperFit(double W, double H, double iw, double ih, int fill) {
  WallpaperFit f;
  if (W <= 0 || H <= 0 || iw <= 0 || ih <= 0) return f;
  if (fill == 1 || fill == 5) f.sx = f.sy = std::max(W / iw, H / ih);  // crop (span on one output)
  else if (fill == 2) f.sx = f.sy = std::min(W / iw, H / ih);          // fit
  else if (fill == 3) f.sx = W / iw, f.sy = H / ih;                    // stretch
  else f.sx = f.sy = 1;                                                // center, repeat
  if (fill != 4) {  // repeat tiles from the top-left corner
    f.ox = (W - iw * f.sx) / 2;
    f.oy = (H - ih * f.sy) / 2;
  }
  return f;
}

// per axis affine: x' = ax * x + bx
struct SpaceMap {
  double ax = 1, bx = 0, ay = 1, by = 0;
  [[nodiscard]] bool identity() const {
    return std::abs(ax - 1) < 1e-9 && std::abs(ay - 1) < 1e-9 && std::abs(bx) < 1e-9 && std::abs(by) < 1e-9;
  }
  [[nodiscard]] SpaceMap inverse() const { return {1 / ax, -bx / ax, 1 / ay, -by / ay}; }
};

// from an output of fromW x fromH to one of toW x toH, both showing an image
// of iw x ih (unknown, e.g. a video: the first screen's own size, so the
// layout scales like a picture of it would)
inline SpaceMap spaceMap(double fromW, double fromH, double toW, double toH, double iw, double ih, int fill) {
  if (fromW <= 0 || fromH <= 0 || toW <= 0 || toH <= 0) return {};
  if (std::abs(fromW - toW) < 0.5 && std::abs(fromH - toH) < 0.5) return {};
  if (iw <= 0 || ih <= 0) iw = fromW, ih = fromH;
  const WallpaperFit a = wallpaperFit(fromW, fromH, iw, ih, fill), b = wallpaperFit(toW, toH, iw, ih, fill);
  SpaceMap m;
  m.ax = b.sx / a.sx;
  m.bx = b.ox - a.ox * m.ax;
  m.ay = b.sy / a.sy;
  m.by = b.oy - a.oy * m.ay;
  return m;
}

inline double blockSpaceW(const WidgetConfig& c) { return c.spaceW > 0 ? c.spaceW : kLegacySpaceW; }
inline double blockSpaceH(const WidgetConfig& c) { return c.spaceH > 0 ? c.spaceH : kLegacySpaceH; }

// the box and corner pins through `m` (rotation, tilt and the rest are angles
// and stay); the edges are mapped, so neighbours stay flush
inline void mapWidget(WidgetConfig& c, const SpaceMap& m) {
  if (m.identity()) return;
  const double x0 = m.ax * c.x + m.bx, x1 = m.ax * (c.x + c.width) + m.bx;
  const double y0 = m.ay * c.y + m.by, y1 = m.ay * (c.y + c.height) + m.by;
  c.x = static_cast<int>(std::lround(x0));
  c.y = static_cast<int>(std::lround(y0));
  c.width = std::max(24, static_cast<int>(std::lround(x1 - x0)));
  c.height = std::max(24, static_cast<int>(std::lround(y1 - y0)));
  for (int i = 0; i < 4; ++i) {
    c.pin[2 * i] = m.ax * c.pin[2 * i] + m.bx;
    c.pin[2 * i + 1] = m.ay * c.pin[2 * i + 1] + m.by;
  }
}

inline std::string pinText(const double* p) {
  std::string text = "[";
  for (int i = 0; i < 8; ++i) text += std::format("{}{:.1f}", i ? ", " : "", p[i]);
  return text + "]";
}

}  // namespace undershell
