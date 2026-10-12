// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's colour picker, the parts that need no window: HSV and hex
// conversions, the wallpaper's dominant colours, the eyedropper's copy of the
// wallpaper and the list of recently used colours.
#pragma once

#include "common.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace undershell {

struct Hsv {
  double h = 0, s = 0, v = 0;  // h in [0, 360), s and v in [0, 1]
};

Hsv toHsv(const Color& c);
Color fromHsv(const Hsv& c);
// "#rrggbb" (alpha dropped: the looks draw their colours opaque)
std::string toHex(const Color& c);
// "#rgb", "#rrggbb", with or without '#', any case
bool parseHex(const std::string& text, Color& out);

// A wallpaper decoded for the picker: an RGBA copy for the eyedropper (longest
// side up to `maxSide`) and its dominant colours.
struct WallpaperSample {
  std::string path;
  int w = 0, h = 0;
  std::vector<std::uint8_t> rgba;
  std::vector<Color> dominant;
};
// false when the file cannot be decoded (a video, a missing file)
bool sampleWallpaper(const std::string& path, int maxSide, WallpaperSample& out);

// A widget's colour as the editor's accent: lifted toward white until it reads
// on the editor's dark panels (a widget can be any colour, black included).
Color readableAccent(Color c);

// Up to `n` colours that cover most of the image, the most common first, no
// two alike (k-means on a coarse copy, seeded farthest-first: deterministic).
std::vector<Color> dominantColors(const std::uint8_t* rgba, int w, int h, int n);

// Recently used colours, newest first (hex strings), kept in
// $XDG_STATE_HOME/undershell/recent-colors.
std::string recentColorsPath();
std::vector<std::string> loadRecentColors();
void pushRecentColor(std::vector<std::string>& list, const std::string& hex, size_t keep = 8);
bool saveRecentColors(const std::vector<std::string>& list);

}  // namespace undershell
