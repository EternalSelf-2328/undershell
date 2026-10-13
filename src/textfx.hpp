// SPDX-License-Identifier: GPL-3.0-or-later
// Text with effects, shared by the text and lyrics widgets. Pango rasterises
// the text once (whole, or one grapheme at a time when the letters move on
// their own) into mipmapped alpha textures; one shader then builds every
// effect from that alpha: a fill (a colour, a gradient across the whole text
// or a moving rainbow), an outline (the alpha grown by sampling round each
// pixel), a 3D extrusion (the alpha stepped back along a direction), a glow
// and a soft shadow (read from smaller mipmaps), and a red/cyan split for
// glitches. Two passes -- everything behind, then the fills -- so one
// letter's outline never covers the next one's face.
#pragma once

#include "canvas.hpp"
#include "common.hpp"
#include "gl.hpp"
#include "text.hpp"
#include "widget.hpp"

#include <string>
#include <vector>

namespace undershell {

struct TextFxStyle {
  std::string font = "Inter Display";
  int weight = 800;
  bool italic = false;
  double spacing = 0;  // letter spacing, a share of the size
  int align = 1;       // 0 left, 1 centre, 2 right
  int fill = 0;        // 0 solid, 1 gradient, 2 rainbow, 3 none (hollow: only the outline)
  Color c1, c2;
  double angle = 90;   // gradient direction, degrees (0: left to right, 90: top to bottom)
  // the effects' sizes are shares of the font size, so they follow the box
  double outline = 0;  // 0..1
  Color outlineC;
  double shadow = 0;   // 0..1
  Color shadowC;
  double glow = 0;     // 0..1
  Color glowC;
  double extrude = 0;  // 0..1: 3D depth
  Color extrudeC;
  double extrudeAngle = 135;  // where the depth goes, degrees (0: right, 90: down)
};

// How one unit (the whole text, or a letter) is moved this frame.
struct TextFxMotion {
  float dx = 0, dy = 0;  // logical px
  float scale = 1, turn = 0;  // turn: radians, clockwise
  float alpha = 1, glow = 1;  // multipliers
  float chroma = 0;           // red/cyan split, logical px
  bool shown = true;
};

class TextFx {
public:
  TextFx();
  ~TextFx();
  TextFx(const TextFx&) = delete;
  TextFx& operator=(const TextFx&) = delete;

  // Lays `text` out in a w x h box (logical px) at device `scale`. With
  // `size` = 0 the text grows to fill the box (its painted ink, with room for
  // the effects and `slack` -- a share of the size the motion needs);
  // otherwise it is set at that size and wrapped to the box's width.
  // `perLetter` splits it into graphemes that can move on their own.
  // Cheap when nothing changed.
  void layout(const std::string& text, const TextFxStyle& st, float w, float h, float scale, float slack, bool perLetter,
              float size = 0, int maxLines = 3);
  void setStyle(const TextFxStyle& st) { m_st = st; }  // colours and effects only (no new layout)

  struct Unit {
    std::string text;
    float x = 0, y = 0;  // the logical box's top-left, logical px in the widget
    float w = 0, h = 0;
    int line = 0;
    bool blank = false;
  };
  [[nodiscard]] const std::vector<Unit>& units() const { return m_units; }
  [[nodiscard]] float size() const { return m_size; }
  [[nodiscard]] Rect inkBox() const { return m_ink; }  // the painted text, logical px in the widget
  [[nodiscard]] bool empty() const { return m_units.empty(); }
  // the size that fits `text` in w x h with this style (what layout(…, 0) would pick)
  static float fitSize(const std::string& text, const TextFxStyle& st, float w, float h, float slack);

  // Draws every shown unit; `motion` is per unit (missing ones stay put).
  // `hue` turns a rainbow fill; `opacity` fades it all.
  void draw(const DrawContext& ctx, const std::vector<TextFxMotion>& motion, float hue, float opacity);
  // A solid bar (a typewriter's caret), logical px.
  void bar(const DrawContext& ctx, float x, float y, float w, float h, Color c);

  void collect() { m_text.collect(20); }
  void releaseGl();

private:
  [[nodiscard]] TextStyle textStyle(float size) const;
  static float spill(const TextFxStyle& st);  // what the effects need round the ink, a share of the size

  TextFxStyle m_st;
  TextRenderer m_text;  // its own: mipmapped textures
  Canvas m_canvas;
  Program m_prog;
  std::vector<Unit> m_units;
  TextStyle m_wholeStyle, m_glyphStyle;  // the whole text as laid out, and one grapheme
  float m_size = 0, m_scale = 1, m_pad = 0;
  Rect m_ink;
  std::string m_key;  // what the layout was made for
};

}  // namespace undershell
