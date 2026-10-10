// SPDX-License-Identifier: GPL-3.0-or-later
// A character screen: a grid of cells drawn from a sheet of glyphs in one
// pass (JetBrains Mono, katakana and the rest from the system), then shown
// through a CRT in a second: phosphor colour, glow, scanlines, flicker, the
// tube's curve and its dark glass. A cell holds a glyph, or a bar filled from
// its bottom or its left (spectrum and meter bars, gapless across rows).
#pragma once

#include "common.hpp"
#include "gl.hpp"

#include <string_view>
#include <unordered_map>
#include <vector>

namespace undershell {

class TermScreen {
public:
  // Lays the grid out for a box of `w` x `h` logical px at `scale` device px
  // each, the text `textScale` times its usual size.
  void layout(float w, float h, float scale, float textScale);
  [[nodiscard]] int cols() const { return m_cols; }
  [[nodiscard]] int rows() const { return m_rows; }

  void clear();
  void put(int col, int row, char32_t ch, float bright);
  // UTF-8 text from (col, row), clipped at the right edge; the columns it took
  int text(int col, int row, std::string_view utf8, float bright);
  void bar(int col, int row, float fill, float bright);   // filled from the bottom, 0..1
  void hbar(int col, int row, float fill, float bright);  // filled from the left, 0..1

  struct Crt {
    float phosphor[3] = {0.35F, 1.0F, 0.5F};
    float glow = 0.6F, scan = 0.5F, flicker = 0.2F, curve = 0.3F, background = 0.85F, opacity = 1;
    float noise = 0;  // this frame's flicker, 0..1
  };
  // Draws the screen on the current framebuffer (needs GL).
  void draw(const Crt& crt);
  void release();

  static std::u32string decode(std::string_view utf8);

private:
  struct Cell {
    char32_t ch = 0;
    float bright = 0;
    uint8_t kind = 0;  // 0 glyph, 1 bar from the bottom, 2 bar from the left
    float fill = 0;
  };
  void buildSheet();
  int glyphIndex(char32_t c) const;

  int m_cols = 0, m_rows = 0;
  float m_cellW = 8, m_cellH = 16, m_scale = 1, m_em = 12;  // logical px
  int m_cw = 8, m_ch = 16;                                   // device px
  std::vector<Cell> m_cells;
  // the glyph sheet
  std::vector<char32_t> m_charset;
  std::unordered_map<char32_t, int> m_index;
  GLuint m_sheet = 0;
  int m_sheetEm = 0, m_perRow = 32, m_sheetW = 0, m_sheetH = 0;
  // the grid's own picture, mipmapped for the glow
  GLuint m_fbo = 0, m_tex = 0;
  int m_texW = 0, m_texH = 0;
  Program m_gridProg, m_crtProg;
  std::vector<float> m_verts;
};

}  // namespace undershell
