// SPDX-License-Identifier: GPL-3.0-or-later
// Pure editor geometry (unit-tested in tests/test_editor.cpp).
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace undershell {

// Finds the closest (edge, target) pair within `threshold`. On success sets
// `delta` (to add to the edges) and `guide` (the target line) and returns true.
inline bool snapAxis(const std::vector<double>& edges, const std::vector<double>& targets, double threshold,
                     double& delta, double& guide) {
  double bestAbs = threshold + 1;
  for (double e : edges)
    for (double t : targets)
      if (std::abs(t - e) < bestAbs) {
        bestAbs = std::abs(t - e);
        delta = t - e;
        guide = t;
      }
  return bestAbs <= threshold;
}

// Keeps a box on an output: it may hang off an edge, but a quarter of it (at
// least 48 px) stays visible; sizes are bounded to [48|32, 2x output].
inline void clampBox(int& x, int& y, int& w, int& h, int outW, int outH) {
  w = std::clamp(w, 48, std::max(48, outW * 2));
  h = std::clamp(h, 32, std::max(32, outH * 2));
  const int keepX = std::min(w, std::max(48, w / 4));
  const int keepY = std::min(h, std::max(48, h / 4));
  x = std::clamp(x, keepX - w, outW - keepX);
  y = std::clamp(y, keepY - h, outH - keepY);
}

inline int snapToGrid(int v, int grid) {
  grid = std::max(1, grid);
  return static_cast<int>(std::lround(static_cast<double>(v) / grid)) * grid;
}

}  // namespace undershell
