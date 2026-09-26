// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace undershell {

struct VisualizerConfig {
  std::string style = "bars";  // bars split dots segments wave ribbon curtain line frame radial orb spiral
  int bars = 64;
  double thickness = 0.58;
  double reflection = 0.1;
  int segments = 10;
  std::string grow = "up";       // up down center left right
  std::string shape = "rounded";  // rounded square
  std::string colorMode = "theme";  // theme gradient custom
  std::string color = "primary";    // palette role or #hex
  std::string color2 = "tertiary";
  double gain = 1.0;
  double smoothing = 0.5;
  bool peaks = false;
  bool mirror = false;
  bool idleWave = false;
  double spin = 0.0;   // degrees / s, polar looks
  double glow = 0.6;
  int fps = 60;
  double opacity = 1.0;
  bool depth = true;   // honour wallpaper_depth masks
};

struct WidgetConfig {
  std::string id;
  std::string type = "visualizer";
  std::string output;  // connector name; empty = first output
  int x = 0, y = 0, width = 800, height = 240;  // logical px on the output
  bool enabled = true;
  VisualizerConfig viz;
};

struct Config {
  std::string path;
  std::vector<WidgetConfig> widgets;
  int gridSize = 16;
  // audio analysis
  double noiseReduction = 0.45;  // Ryoku's cava noise_reduction = 45
  bool monstercat = false;

  static std::string defaultPath();
  static Config load(const std::string& path);
  // Rewrites only the x/y/width/height lines of widget `id`, keeping the
  // rest of the file (comments, ordering) as the user wrote it.
  static bool saveGeometry(const std::string& path, const WidgetConfig& w);
  // Sets `key = value` (value already TOML-formatted) inside widget `id`'s
  // block, adding the line if missing. Comments and layout are preserved.
  static bool setKey(const std::string& path, const std::string& id, const std::string& key, const std::string& tomlValue);
};

}  // namespace undershell
