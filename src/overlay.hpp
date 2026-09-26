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
  float angle = 0;  // degrees, about the centre
};

// Draws a widget rendered off-screen (premultiplied texture of its own box)
// turned about `centre` on the current surface, with a soft 1 px edge.
class RotatedBlit {
public:
  void draw(GLuint texture, float surfaceW, float surfaceH, float centreX, float centreY, float boxW, float boxH,
            float degrees);

private:
  Program m_prog;
};

// The editor canvas: drawn on a fullscreen surface above everything while
// editing. A faint grid, and for each widget a translucent plate, rounded
// outline and a resize grip in its bottom-right corner.
class OverlayPass {
public:
  static constexpr int kMaxRects = 16;
  static constexpr int kMaxGuides = 4;
  // `handle`: the rect that shows a rotation knob above its top edge (-1: none)
  void draw(float w, float h, const std::vector<EditRect>& rects, int hover, int active, int selected, Color accent,
            float grid, const std::vector<float>& vguides, const std::vector<float>& hguides, int handle = -1);
  static constexpr float kHandleGap = 30;  // knob centre above the top edge
  static constexpr float kHandleR = 7;

  // a rounded, filled rectangle (label plates); straight-alpha colour
  void drawPill(float x, float y, float w, float h, float radius, Color color, float surfaceW, float surfaceH);

private:
  Program m_prog;
  Program m_pill;
};

}  // namespace undershell
