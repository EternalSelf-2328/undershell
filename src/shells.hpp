// SPDX-License-Identifier: GPL-3.0-or-later
// The wallpaper (and, where there is one, the palette) from the shells and
// wallpaper tools people run instead of Noctalia: caelestia, DankMaterialShell,
// end-4's illogical-impulse, Ambxst, HyprPanel, COSMIC, and the daemons mpvpaper,
// awww/swww, hyprpaper, wpaperd, swaybg, with waypaper's settings as a last
// resort; a pywal palette for the wallpaper is used when the tool has none.
// The first one running is read. Each format comes from the project's own
// source (paths and keys noted beside the parser).
#pragma once

#include "common.hpp"

#include <map>
#include <string>
#include <vector>

namespace undershell {

struct ShellState {
  std::string name;                              // caelestia, dms, ii, ambxst, hyprpanel, cosmic, mpvpaper, awww, …
  std::map<std::string, std::string> byOutput;   // connector → image
  std::string fallback;                          // every other output
  int fillMode = 1;                              // center crop fit stretch repeat
  std::map<std::string, Color> palette;          // Material roles, snake_case; empty: none
  bool dark = true;
  std::vector<std::string> watch;                // directories whose changes mean "read again"
};

using Screens = std::map<std::string, std::pair<int, int>>;
using Argv = std::vector<std::string>;

// The command lines of this user's processes.
std::vector<Argv> userProcesses();
// The first shell or tool of `procs` (or the session) with a wallpaper; false if none.
bool readShell(const std::vector<Argv>& procs, ShellState& out);

// ── parsers, one per format (exposed for tests) ──
// Material roles in snake_case from camelCase ("onSurfaceVariant"); Ambxst's
// "over" is "on"
std::string snakeRole(std::string camel);
std::map<std::string, Color> caelestiaScheme(const std::string& json, bool& dark);
std::map<std::string, Color> dmsColors(const std::string& json, bool& dark);
std::map<std::string, Color> flatPalette(const std::string& json, bool& dark);  // end-4 ii, Ambxst
std::map<std::string, Color> pywalPalette(const std::string& json, std::string& wallpaper, bool& dark);
bool dmsSession(const std::string& session, const std::string& settings, bool dark, ShellState& out);
bool ambxstWallpapers(const std::string& json, ShellState& out);
std::string iiWallpaper(const std::string& configJson);
std::string hyprpanelWallpaper(const std::string& configJson);
bool cosmicWallpapers(const std::string& state, const std::string& allEntry, ShellState& out);
// awww/swww's cache file of one output: (image, resize mode)
std::pair<std::string, std::string> awwwCacheEntry(const std::string& data);
bool hyprpaperActive(const std::string& listactive, ShellState& out);
bool hyprpaperConfig(const std::string& conf, ShellState& out);
bool swaybgArgs(const Argv& argv, ShellState& out);
bool mpvpaperArgs(const Argv& argv, ShellState& out);
bool waypaperIni(const std::string& ini, ShellState& out);
// each tool's names for its fill modes → undershell's index
int fillFrom(const std::string& tool, const std::string& mode);

}  // namespace undershell
