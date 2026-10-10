// SPDX-License-Identifier: GPL-3.0-or-later
// Strokes: line segments built on the CPU and drawn as narrow strips around
// each one, so the GPU only shades the pixels near a stroke and a big box costs
// no more than a small one. Three ways to draw them:
//   Light - a white-hot core in a glow (lightning, neon, fireworks); the glow
//           goes on a coarser copy of each run of segments, since it is soft
//   Ink   - white with a black outline that thins with the stroke (manga)
//   Flat  - one colour, its alpha the stroke's intensity (rain, speed lines)
// Segments may taper (a width at each end) and a zero-length one is a dot.
#pragma once

#include "gl.hpp"

#include <vector>

namespace undershell {

struct Stroke {
  float x0, y0, x1, y1;
  float width;            // at (x0, y0), logical px
  float intensity;        // 0..1+: brightness (Light), thickness (Ink), alpha (Flat)
  float widthEnd = -1;    // at (x1, y1); < 0: the same as `width`
};

class StrokeRenderer {
public:
  enum class Mode { Light, Ink, Flat };
  struct Look {
    Mode mode = Mode::Light;
    float glow = 14;      // Light: the glow's reach, logical px
    float ink = 2.2F;     // Ink: the outline around the white
    float core[3] = {1, 1, 1};            // Light: the core; Flat: the colour
    float halo[3] = {0.45F, 0.62F, 1.0F}; // Light: the glow
    float opacity = 1;
  };
  // Draws `strokes` on the current framebuffer of `w` x `h` logical px.
  void draw(const std::vector<Stroke>& strokes, float w, float h, const Look& look);
  void release() { m_prog.destroy(); }

private:
  void pass(const std::vector<Stroke>& strokes, float reach, int mode, const Look& look);
  Program m_prog;
  std::vector<float> m_verts;
  std::vector<Stroke> m_coarse;
};

}  // namespace undershell
