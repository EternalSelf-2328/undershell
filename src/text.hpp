// SPDX-License-Identifier: GPL-3.0-or-later
// Text for every widget: Pango lays out and rasterises a string once into an
// alpha texture; drawing it is one tinted quad. Textures are cached by
// (text, style, scale) and evicted when unused, so a clock that changes one
// label per second uploads one small texture per second and nothing else.
#pragma once

#include "common.hpp"
#include "gl.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace undershell {

struct TextStyle {
  std::string family = "Sans";
  float size = 16;           // logical px
  int weight = 400;          // 100..900 (CSS scale)
  float letterSpacing = 0;   // logical px between glyphs
  bool italic = false;
  float stroke = 0;          // > 0: hollow text, outline this wide (logical px)
  float maxWidth = 0;        // > 0: wrap (maxLines > 1) or ellipsize at this width
  int maxLines = 1;
  int align = 0;             // 0 left, 1 centre, 2 right (within maxWidth)
  std::string variations;    // variable-font axes, e.g. "wght=700,wdth=120,ROND=100"
};

struct TextImage {
  GLuint texture = 0;
  int pxW = 0, pxH = 0;      // texture size, device px
  float scale = 1;           // device px per logical px it was rendered at
  float w = 0, h = 0;        // logical box (Pango logical extents)
  float baseline = 0;        // logical px from the top of the box
  float inkLeft = 0, inkTop = 0, inkW = 0, inkH = 0;  // painted area within the box
  // the texture quad relative to the logical box's top-left, logical px
  float quadX = 0, quadY = 0, quadW = 0, quadH = 0;
};

class TextRenderer {
public:
  TextRenderer() = default;
  ~TextRenderer();
  TextRenderer(const TextRenderer&) = delete;
  TextRenderer& operator=(const TextRenderer&) = delete;

  // Registers undershell's bundled fonts with fontconfig (call before any text).
  static void registerBundledFonts();
  static std::string fontsDir();
  // Picks up fonts installed since startup (~/.local/share/fonts, …):
  // reloads fontconfig when its directories changed. True when it did.
  static bool refreshFonts();

  // Rasterises (or returns the cached) text. Needs a current GL context.
  const TextImage& get(const std::string& text, const TextStyle& style, float scale);
  // Logical size only, no GL work.
  static void measure(const std::string& text, const TextStyle& style, float& w, float& h, float& baseline);
  // What the text really paints, relative to its logical box's top-left
  // (decorative fonts often reach past their metrics), plus that box's size;
  // hollow text includes its outline. Cached, no GL needed.
  struct Ink {
    float x = 0, y = 0, w = 0, h = 0;  // painted area
    float boxW = 0, boxH = 0;          // logical box
  };
  static Ink measureInk(const std::string& text, const TextStyle& style);
  // The glyphs' outlines as closed polylines, relative to the logical box's
  // top-left (curves flattened to within `tolerance` px). No GL needed.
  using Contour = std::vector<std::pair<float, float>>;
  static std::vector<Contour> outline(const std::string& text, const TextStyle& style, float tolerance = 0.4F);
  // A sheet of cells, `perRow` across, each `cellW` x `cellH` device px, with
  // one glyph (a UTF-8 string) in each: centred across, all on one baseline
  // (`style.size` in device px). Alpha, one byte a pixel, rows packed.
  static std::vector<std::uint8_t> glyphSheet(const std::vector<std::string>& glyphs, const TextStyle& style, int cellW, int cellH,
                                              int perRow);

  // Draws at (x, y) = top-left of the logical box, in surface logical px.
  void draw(const TextImage& img, float x, float y, Color color, float surfaceW, float surfaceH, float opacity = 1);
  // Full control: scale the quad about the box centre (sx, sy), turn it about
  // that centre (`angle`, radians, clockwise on screen) and optionally blur
  // the glyph alpha (a soft halo), `blur` in logical px.
  void drawEx(const TextImage& img, float x, float y, Color color, float surfaceW, float surfaceH, float sx, float sy,
              float blur, float opacity = 1, float angle = 0);

  // Evicts textures unused for `maxAgeSec`. Call once in a while.
  void collect(double maxAgeSec = 30);
  void releaseGl();
  [[nodiscard]] size_t cached() const { return m_cache.size(); }

private:
  struct Entry {
    TextImage img;
    double lastUse = 0;
  };
  std::unordered_map<std::string, Entry> m_cache;
  Program m_prog;
};

}  // namespace undershell
