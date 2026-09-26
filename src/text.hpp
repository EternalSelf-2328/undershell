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

namespace undershell {

struct TextStyle {
  std::string family = "Sans";
  float size = 16;           // logical px
  int weight = 400;          // 100..900 (CSS scale)
  float letterSpacing = 0;   // logical px between glyphs
  bool italic = false;
};

struct TextImage {
  GLuint texture = 0;
  int pxW = 0, pxH = 0;      // texture size, device px
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

  // Rasterises (or returns the cached) text. Needs a current GL context.
  const TextImage& get(const std::string& text, const TextStyle& style, int scale);
  // Logical size only, no GL work.
  static void measure(const std::string& text, const TextStyle& style, float& w, float& h, float& baseline);

  // Draws at (x, y) = top-left of the logical box, in surface logical px.
  void draw(const TextImage& img, float x, float y, Color color, float surfaceW, float surfaceH, float opacity = 1);

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
