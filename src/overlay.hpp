// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.hpp"
#include "gl.hpp"

#include <vector>

namespace undershell {

struct MaskParams {
  GLuint texture = 0;
  float surfaceW = 0, surfaceH = 0;   // logical px
  float offsetX = 0, offsetY = 0;     // surface position on the output
  float outputW = 0, outputH = 0;
  float imageW = 0, imageH = 0;
  int fillMode = 1;
};

// Erases widget pixels under the wallpaper_depth foreground (DestinationOut),
// with Noctalia's wallpaper sampling maths (MIT, (c) 2026 noctalia-dev).
class MaskPass {
public:
  void draw(const MaskParams& p);

private:
  Program m_prog;
};

struct EditRect {
  float x, y, w, h;
};

// The editor canvas: drawn on a fullscreen surface above everything while
// editing. A faint grid, and for each widget a translucent plate, rounded
// outline and a resize grip in its bottom-right corner.
class OverlayPass {
public:
  static constexpr int kMaxRects = 16;
  void draw(float w, float h, const std::vector<EditRect>& rects, int hover, int active, Color accent, float grid);

private:
  Program m_prog;
};

}  // namespace undershell
