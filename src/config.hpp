// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <toml++/toml.hpp>
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

  // reads the visualizer keys of a [[widget]] table (missing keys keep defaults)
  static VisualizerConfig fromTable(const toml::table& t);
};

// The keys every widget shares; type-specific keys stay in `options` and are
// read by the widget itself (so adding a widget type never touches this file).
struct WidgetConfig {
  std::string id;
  std::string type = "visualizer";
  std::string output;  // connector name; empty = first output
  int x = 0, y = 0, width = 800, height = 240;  // logical px on the output
  bool enabled = true;
  bool depth = true;   // pass behind the wallpaper_depth foreground
  double rotation = 0;  // degrees, clockwise, about the box centre
  toml::table options;  // the whole [[widget]] table
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
  // Every edit finds its widget by id, so ids must be unique: renames repeats
  // ("visualizer" twice -> "visualizer", "visualizer-2") in place. Returns
  // how many it renamed.
  static int uniquifyIds(const std::string& path);
  // A block's text with its id and position replaced (duplicating a widget).
  static std::string retargetBlock(const std::string& block, const std::string& id, int x, int y);
  // Sets `key = value` (value already TOML-formatted) inside widget `id`'s
  // block, adding the line if missing. Comments and layout are preserved.
  static bool setKey(const std::string& path, const std::string& id, const std::string& key, const std::string& tomlValue);
  // The full text of widget `id`'s [[widget]] block (with its header), or "".
  static std::string blockText(const std::string& path, const std::string& id);
  // Removes widget `id`'s block; returns its text through `removed`.
  static bool removeBlock(const std::string& path, const std::string& id, std::string* removed = nullptr);
  // Appends a block (as returned by blockText / defaultBlock).
  static bool appendBlock(const std::string& path, const std::string& block);
  // A commented starter block for a widget type.
  static std::string defaultBlock(const std::string& type, const std::string& id, const std::string& output, int x, int y,
                                  int w, int h, const std::string& look);
  // TOML text of a value (strings quoted), for undo records
  static std::string tomlText(const toml::node& n);
};

}  // namespace undershell
