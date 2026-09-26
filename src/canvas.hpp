// SPDX-License-Identifier: GPL-3.0-or-later
// A small immediate-mode 2D canvas for widgets that draw shapes and text
// (clocks, cards): antialiased rounded rects, circles, segments and arcs as
// signed-distance quads, plus text through TextRenderer. Widgets lay out in
// their own "design" units; setTransform() fits that design into the surface,
// and text is rasterised at the final pixel size so it stays crisp.
#pragma once

#include "common.hpp"
#include "gl.hpp"
#include "text.hpp"

#include <string>

namespace undershell {

class Canvas {
public:
  void begin(float surfaceW, float surfaceH, int pixelScale, TextRenderer* text);
  void setTransform(float scale, float offsetX, float offsetY);
  [[nodiscard]] float scale() const { return m_scale; }

  void roundRect(float x, float y, float w, float h, float radius, Color fill, float strokeW = 0, Color stroke = {});
  void circle(float cx, float cy, float r, Color fill, float strokeW = 0, Color stroke = {});
  void segment(float x1, float y1, float x2, float y2, float width, Color color, bool roundCap = true);
  // angles in radians, 0 = 12 o'clock, clockwise
  void arc(float cx, float cy, float r, float width, float a0, float a1, Color color, bool roundCap = true);
  void triangle(float x1, float y1, float x2, float y2, float x3, float y3, Color color);
  // a sine stroke from x0 to x1 around baseline y: amplitude, wavelength, phase (px)
  void wave(float x0, float x1, float y, float amplitude, float wavelength, float phase, float thickness, Color color);
  // an RGBA texture drawn "cover"-fitted into a rounded rect; blur in design px
  void image(GLuint texture, int texW, int texH, float x, float y, float w, float h, float radius, float opacity = 1,
             float blur = 0);
  // clip subsequent drawing to a rect (design units); clip() with no args ends it
  void clip(float x, float y, float w, float h);
  void clip();

  // Text in design units. (x, y) = top-left of the logical box. sy squashes
  // vertically about the box centre (flip cards); blur > 0 draws a soft halo.
  struct Size {
    float w = 0, h = 0, baseline = 0;
  };
  static Size measure(const std::string& text, const TextStyle& style);
  void text(const std::string& text, const TextStyle& style, float x, float y, Color color, float opacity = 1,
            float sy = 1, float blur = 0);

private:
  void shape(int kind, float x0, float y0, float x1, float y1, const float* params, Color fill, float strokeW, Color stroke);
  float m_w = 0, m_h = 0, m_scale = 1, m_ox = 0, m_oy = 0;
  int m_pixelScale = 1;
  TextRenderer* m_text = nullptr;
  Program m_prog;
  Program m_image;
};

}  // namespace undershell
