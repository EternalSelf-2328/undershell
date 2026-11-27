// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.hpp"

#include "common.hpp"
#include "geom.hpp"
#include "perf.hpp"

#include <glib.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <cstring>
#include <regex>
#include <set>
#include <span>
#include <sstream>
#include <toml++/toml.hpp>

namespace undershell {

namespace fs = std::filesystem;

static const char* kDefaultConfig = R"(# undershell: desktop widgets under any shell (looks ported from Ryoku).
# Edit this file and save: changes apply live.
# Toggle the on-screen editor (drag to move, bottom-right corner to resize,
# right-click to finish) with:   undershell msg edit

[general]
grid = 16                 # snap step for the editor, px
noise_reduction = 0.45    # analyser smoothing (Ryoku's cava used 45)
monstercat = false        # spread peaks to neighbours (cava's monstercat)
idle = "show"             # visualizers with nothing playing: show | hide | demo
language = "system"       # editor language: system (follows the locale) | en | es
profiles = true           # remember a widget layout for each wallpaper
performance = "auto"      # auto (adapts to the measured load) | eco | balanced | high | ultra
on_battery = "lower"      # on battery: lower (one step down) | eco | keep
# wallpaper = "~/Pictures/wall.jpg"   # only without Noctalia or KDE Plasma: the wallpaper
# wallpaper_fill = "crop"             # (for depth and per-wallpaper layouts) and how it is fitted
# depth_threshold = 30      # depth plane of widgets with depth_level 0 (higher is nearer)
# depth_feather = 8         # softness of the depth cut

# [screensaver]             # after `idle` seconds without input, until the next one
# idle = 300                # seconds; 0 (the default): off
# background = "black"      # black | wallpaper (darkened by dim = 0.6)
# while_playing = false     # also while music or a video plays
# [screensaver.look]        # any visualizer keys; style = "none" for no look
# style = "pixels"
# pixels_text = "@TIME@"
# [screensaver.clock]       # any clock keys; face = "digital" (or another) to show one

[[widget]]
id = "visualizer"
type = "visualizer"
x = 460                   # x, y, width, height: logical px of a screen of size `space`;
y = 760                   # on any other screen the widget keeps its place on the wallpaper
width = 1000
height = 280
space = [1920, 1080]
# bars split dots segments wave ribbon curtain line frame radial orb spiral halo vortex fire muzzle electric sfx speedlines rain ripples neon fireworks terminal tv pond aura stars pixels life lava globe
style = "bars"
bars = 64
thickness = 0.58
reflection = 0.1
segments = 10
grow = "up"               # up down center left right
shape = "rounded"         # rounded square
color_mode = "theme"      # theme | gradient | custom
color = "primary"         # palette role or #rrggbb
color2 = "tertiary"
colors_from = "theme"     # theme | cover (the roles above follow the playing album)
gain = 1.0
smoothing = 0.5
peaks = false
mirror = false
idle_wave = false         # a breathing line while silent (costs a little GPU)
spin = 0.0                # polar looks: degrees per second
glow = 0.6
fps = 60
opacity = 1.0
depth = true              # pass behind what stands in front in the wallpaper
)";

std::string Config::defaultPath() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  std::string base = xdg && *xdg ? xdg : expandHome("~/.config");
  return base + "/undershell/config.toml";
}

template <typename T>
static T get(const toml::table& t, std::string_view key, T def) {
  if (auto v = t[key].value<T>()) return *v;
  return def;
}

Config Config::load(const std::string& path) {
  Config cfg;
  cfg.path = path;
  if (!fs::exists(path)) {
    fs::create_directories(fs::path(path).parent_path());
    writeFileAtomic(path, kDefaultConfig);
    US_INFO("wrote default config {}", path);
  }
  toml::table root;
  try {
    root = toml::parse_file(path);
  } catch (const toml::parse_error& e) {
    US_ERROR("config {}: {} (line {})", path, e.description(), e.source().begin.line);
    throw;
  }
  if (auto* g = root["general"].as_table()) {
    cfg.gridSize = static_cast<int>(get<int64_t>(*g, "grid", 16));
    cfg.profiles = get<bool>(*g, "profiles", true);
    cfg.noiseReduction = get<double>(*g, "noise_reduction", 0.45);
    cfg.monstercat = get<bool>(*g, "monstercat", false);
    cfg.idle = get<std::string>(*g, "idle", "show");
    if (cfg.idle != "hide" && cfg.idle != "demo") cfg.idle = "show";
    cfg.language = get<std::string>(*g, "language", "system");
    if (cfg.language != "en" && cfg.language != "es") cfg.language = "system";
    cfg.wallpaper = get<std::string>(*g, "wallpaper", "");
    cfg.wallpaperFill = get<std::string>(*g, "wallpaper_fill", "");
    cfg.depthModel = get<bool>(*g, "depth_model", true);
    cfg.depthThreshold = get<double>(*g, "depth_threshold", -1.0);
    cfg.depthFeather = get<double>(*g, "depth_feather", -1.0);
    cfg.performance = get<std::string>(*g, "performance", "auto");
    if (cfg.performance != "auto" && !parsePerfLevel(cfg.performance)) cfg.performance = "auto";
    cfg.onBattery = get<std::string>(*g, "on_battery", "lower");
    if (auto* dirs = (*g)["wallpaper_dirs"].as_array())
      for (auto& d : *dirs)
        if (auto s = d.value<std::string>()) cfg.wallpaperDirs.push_back(*s);
    if (cfg.onBattery != "eco" && cfg.onBattery != "keep") cfg.onBattery = "lower";
  }
  cfg.sceneRules = readSceneRules(root["scene_rule"].as_array());
  if (auto* s = root["screensaver"].as_table()) {
    ScreensaverConfig& v = cfg.screensaver;
    v.idle = std::clamp(get<double>(*s, "idle", 0.0), 0.0, 86400.0);
    if (v.idle > 0 && v.idle < 10) v.idle = 10;  // a typo should not start it under one's hands
    v.background = get<std::string>(*s, "background", "black") == "wallpaper" ? "wallpaper" : "black";
    v.dim = std::clamp(get<double>(*s, "dim", 0.6), 0.0, 1.0);
    v.whilePlaying = get<bool>(*s, "while_playing", false);
    v.output = get<std::string>(*s, "output", "");
    if (auto* t = (*s)["look"].as_table()) v.look = *t;
    if (auto* t = (*s)["clock"].as_table()) v.clock = *t;
  }
  if (auto* arr = root["widget"].as_array()) {
    int n = 0;
    for (auto& node : *arr) {
      auto* t = node.as_table();
      if (!t) continue;
      WidgetConfig w;
      w.id = get<std::string>(*t, "id", std::format("widget{}", n++));
      w.type = get<std::string>(*t, "type", "visualizer");
      w.output = get<std::string>(*t, "output", "");
      w.x = static_cast<int>(get<int64_t>(*t, "x", 0));
      w.y = static_cast<int>(get<int64_t>(*t, "y", 0));
      w.width = static_cast<int>(std::max<int64_t>(24, get<int64_t>(*t, "width", 800)));
      w.height = static_cast<int>(std::max<int64_t>(24, get<int64_t>(*t, "height", 240)));
      if (const toml::array* a = (*t)["space"].as_array(); a && a->size() == 2) {
        w.spaceW = std::max(0.0, (*a)[0].value_or(0.0));
        w.spaceH = std::max(0.0, (*a)[1].value_or(0.0));
      }
      w.enabled = get<bool>(*t, "enabled", true);
      w.depth = get<bool>(*t, "depth", true);
      w.depthLevel = std::clamp(get<double>(*t, "depth_level", 0.0), 0.0, 100.0);
      w.tiltX = std::clamp(get<double>(*t, "tilt_x", 0.0), -70.0, 70.0);
      w.tiltY = std::clamp(get<double>(*t, "tilt_y", 0.0), -70.0, 70.0);
      w.skewX = std::clamp(get<double>(*t, "skew", 0.0), -60.0, 60.0);
      w.perspective = std::clamp(get<double>(*t, "perspective", 2.5), 1.2, 20.0);
      if (auto i = (*t)["tilt_x"].value<int64_t>()) w.tiltX = std::clamp(static_cast<double>(*i), -70.0, 70.0);
      if (auto i = (*t)["tilt_y"].value<int64_t>()) w.tiltY = std::clamp(static_cast<double>(*i), -70.0, 70.0);
      if (auto i = (*t)["skew"].value<int64_t>()) w.skewX = std::clamp(static_cast<double>(*i), -60.0, 60.0);
      if (auto r = (*t)["rotation"].value<double>()) {
        double d = std::fmod(*r, 360.0);
        w.rotation = d <= -180.0 ? d + 360.0 : (d > 180.0 ? d - 360.0 : d);
      }
      // warp: none | tilt | pin | mesh; older blocks have no key and are
      // read as before (pin_corners, else a tilt or skew that is set).
      // (not "shape": that is the visualizer's bar shape)
      std::string shape = get<std::string>(*t, "warp", "");
      if (shape != "none" && shape != "tilt" && shape != "pin" && shape != "mesh")
        shape = get<bool>(*t, "pin_corners", false)                                                   ? "pin"
                : (std::abs(w.tiltX) > 0.01 || std::abs(w.tiltY) > 0.01 || std::abs(w.skewX) > 0.01) ? "tilt"
                                                                                                      : "none";
      if (shape != "tilt") w.tiltX = w.tiltY = w.skewX = 0;  // kept in the file, not applied
      if (shape == "pin") {
        if (const toml::array* a = (*t)["pin"].as_array(); a && a->size() == 8) {
          for (size_t k = 0; k < 8; ++k) w.pin[k] = (*a)[k].value_or(0.0);
        } else {  // turned on without points: start from where the box is drawn
          double qx[4], qy[4];
          widgetCorners(w, qx, qy);
          for (int k = 0; k < 4; ++k) w.pin[2 * k] = qx[k], w.pin[2 * k + 1] = qy[k];
        }
        w.pinned = true;
      }
      if (auto g = (*t)["mesh_grid"].value<int64_t>()) w.meshN = static_cast<int>(*g);
      else if (auto gs = (*t)["mesh_grid"].value<std::string>()) w.meshN = std::atoi(gs->c_str());
      w.meshN = std::clamp(w.meshN, 3, WidgetConfig::kMeshMax);
      w.meshSmooth = get<std::string>(*t, "mesh_between", "smooth") != "straight";
      if (shape == "mesh") {
        const size_t want = static_cast<size_t>(2 * w.meshN * w.meshN);
        if (const toml::array* a = (*t)["mesh"].as_array(); a && a->size() == want) {
          for (size_t k = 0; k < want; ++k) w.mesh[k] = (*a)[k].value_or(0.0);
        } else {  // no points yet: a flat grid where the box is drawn
          double qx[4], qy[4];
          widgetCorners(w, qx, qy);
          meshShape(qx, qy, w.meshN, "flat", 0, w.mesh);
        }
        w.meshed = true;
      }
      w.layer = static_cast<int>(std::clamp<int64_t>(get<int64_t>(*t, "layer", 0), -999, 999));
      w.name = get<std::string>(*t, "name", "");
      w.group = get<std::string>(*t, "group", "");
      w.hidden = get<bool>(*t, "hidden", false);
      w.locked = get<bool>(*t, "locked", false);
      if (auto i = (*t)["depth_level"].value<int64_t>()) w.depthLevel = std::clamp(static_cast<double>(*i), 0.0, 100.0);
      w.options = *t;
      cfg.widgets.push_back(std::move(w));
    }
  }
  return cfg;
}

namespace {
using Lines = std::vector<std::string>;

Lines configLines(const std::string& text) {
  std::istringstream in(text);
  Lines lines;
  for (std::string line; std::getline(in, line);) lines.push_back(std::move(line));
  return lines;
}

std::string configText(std::span<const std::string> lines) {
  std::string text;
  for (const auto& line : lines) text += line + "\n";
  return text;
}

std::optional<toml::table> parseConfig(const std::string& text) {
  try {
    return toml::parse(text);
  } catch (const toml::parse_error&) {
    return {};
  }
}

bool writeConfig(const std::string& path, const Lines& lines) {
  const std::string text = configText(lines);
  return parseConfig(text) && writeFileAtomic(path, text);
}

size_t sourceLine(toml::source_position p) { return p.line - 1; }

size_t sourceByte(const std::string& line, toml::source_position p) {
  return static_cast<size_t>(g_utf8_offset_to_pointer(line.c_str(), p.column - 1) - line.c_str());
}

bool isHeader(const Lines& lines, size_t index) {
  const size_t first = lines[index].find_first_not_of(" \t");
  return first != std::string::npos && lines[index][first] == '[';
}

// TOML source regions keep bracket-prefixed lines inside strings or arrays
// from being confused with section headers.
size_t assignmentsEnd(const Lines& lines, const toml::table& table) {
  size_t end = table.source().begin.line;
  for (const auto& [key, value] : table) {
    if (const auto* child = value.as_table(); child && !child->is_inline()) {
      if (!isHeader(lines, sourceLine(child->source().begin)))
        end = std::max(end, assignmentsEnd(lines, *child));
    } else if (const auto* array = value.as_array(); array && !array->empty() &&
               (*array)[0].as_table() && !(*array)[0].as_table()->is_inline()) {
      continue;  // a separate array-of-tables header
    } else {
      end = std::max(end, static_cast<size_t>(value.source().end.line));
    }
  }
  return end;
}

size_t sectionEnd(const Lines& lines, const toml::table& table) {
  size_t end = assignmentsEnd(lines, table);
  while (end < lines.size() && !isHeader(lines, end)) ++end;
  return end;
}

const toml::table* findWidget(const toml::table& document, const std::string& id) {
  const auto* widgets = document["widget"].as_array();
  if (!widgets) return nullptr;
  for (const auto& node : *widgets)
    if (const auto* widget = node.as_table(); widget && !widget->is_inline() &&
        (*widget)["id"].value<std::string>() == id)
      return widget;
  return nullptr;
}

bool findBlock(const Lines& lines, const std::string& id, size_t& start, size_t& end) {
  const auto document = parseConfig(configText(lines));
  const auto* widget = document ? findWidget(*document, id) : nullptr;
  if (!widget) return false;
  start = sourceLine(widget->source().begin);
  end = sectionEnd(lines, *widget);
  return true;
}

void replaceValue(Lines& lines, toml::source_region where, const std::string& value, size_t base = 0) {
  const size_t first = base + sourceLine(where.begin), last = base + sourceLine(where.end);
  const size_t firstByte = sourceByte(lines[first], where.begin), lastByte = sourceByte(lines[last], where.end);
  lines[first] = lines[first].substr(0, firstByte) + value + lines[last].substr(lastByte);
  lines.erase(lines.begin() + first + 1, lines.begin() + last + 1);
}

bool setLine(Lines& lines, size_t start, size_t& end, const std::string& key, const std::string& value) {
  const auto section = parseConfig(configText(std::span(lines).subspan(start, end - start)));
  if (!section) return false;
  const auto* table = (*section)["widget"][0].as_table();
  if (!table) table = (*section)["general"].as_table();
  if (!table) return false;

  if (const auto* current = table->get(key)) {
    const auto where = current->source();
    const size_t extra = where.end.line - where.begin.line;
    if (value.empty()) {
      const size_t first = start + sourceLine(where.begin);
      lines.erase(lines.begin() + first, lines.begin() + first + extra + 1);
      end -= extra + 1;
    } else {
      replaceValue(lines, where, value, start);
      end -= extra;
    }
  } else if (!value.empty()) {
    size_t at = end;
    while (at > start + 1 && lines[at - 1].find_first_not_of(" \t") == std::string::npos) --at;
    lines.insert(lines.begin() + at, key + " = " + value);
    ++end;
  }
  return true;
}
}  // namespace

bool Config::setGeneral(const std::string& path, const std::string& key, const std::string& tomlValue) {
  std::string text;
  if (!readFileChecked(path, text)) return false;
  const auto document = parseConfig(text);
  if (!document) return false;
  auto lines = configLines(text);
  const auto* general = (*document)["general"].as_table();
  size_t start, end;
  if (general && isHeader(lines, sourceLine(general->source().begin))) {
    start = sourceLine(general->source().begin);
    end = sectionEnd(lines, *general);
  } else {
    if (document->contains("general")) return false;
    start = lines.size();
    lines.emplace_back("[general]");
    end = lines.size();
  }
  return setLine(lines, start, end, key, tomlValue) && writeConfig(path, lines);
}

static bool editBlock(const std::string& path, const std::string& id,
                      const std::vector<std::pair<std::string, std::string>>& kv) {
  std::string text;
  if (!readFileChecked(path, text)) return false;
  auto lines = configLines(text);
  size_t start = 0, end = 0;
  if (!findBlock(lines, id, start, end)) return false;
  for (const auto& [key, value] : kv)
    if (!setLine(lines, start, end, key, value)) return false;
  return writeConfig(path, lines);
}

VisualizerConfig VisualizerConfig::fromTable(const toml::table& t) {
  VisualizerConfig v;
  v.style = get<std::string>(t, "style", v.style);
  v.bars = static_cast<int>(std::clamp<int64_t>(get<int64_t>(t, "bars", v.bars), 4, 128));
  v.thickness = std::clamp(get<double>(t, "thickness", v.thickness), 0.05, 1.0);
  v.reflection = std::clamp(get<double>(t, "reflection", v.reflection), 0.0, 0.6);
  v.segments = static_cast<int>(std::clamp<int64_t>(get<int64_t>(t, "segments", v.segments), 3, 24));
  v.grow = get<std::string>(t, "grow", v.grow);
  v.shape = get<std::string>(t, "shape", v.shape);
  v.colorMode = get<std::string>(t, "color_mode", v.colorMode);
  v.color = get<std::string>(t, "color", v.color);
  v.color2 = get<std::string>(t, "color2", v.color2);
  v.gain = std::clamp(get<double>(t, "gain", v.gain), 0.1, 4.0);
  v.smoothing = std::clamp(get<double>(t, "smoothing", v.smoothing), 0.0, 1.0);
  v.peaks = get<bool>(t, "peaks", v.peaks);
  v.mirror = get<bool>(t, "mirror", v.mirror);
  v.idleWave = get<bool>(t, "idle_wave", v.idleWave);
  v.spin = get<double>(t, "spin", v.spin);
  v.glow = std::clamp(get<double>(t, "glow", v.glow), 0.0, 1.0);
  v.fps = static_cast<int>(std::clamp<int64_t>(get<int64_t>(t, "fps", v.fps), 5, 240));
  v.opacity = std::clamp(get<double>(t, "opacity", v.opacity), 0.0, 1.0);
  return v;
}

// a block runs from its [[widget]] header up to the next header, minus the
// blank lines that separate it from that header
static void trimBlockEnd(const std::vector<std::string>& lines, size_t start, size_t& end) {
  while (end > start + 1 && lines[end - 1].find_first_not_of(" \t") == std::string::npos) --end;
}

std::string Config::blockText(const std::string& path, const std::string& id) {
  auto lines = configLines(readFile(path));
  size_t start = 0, end = 0;
  if (!findBlock(lines, id, start, end)) return {};
  trimBlockEnd(lines, start, end);
  std::string out;
  for (size_t i = start; i < end; ++i) out += lines[i] + "\n";
  return out;
}

bool Config::removeBlock(const std::string& path, const std::string& id, std::string* removed) {
  auto lines = configLines(readFile(path));
  size_t start = 0, end = 0;
  if (!findBlock(lines, id, start, end)) return false;
  size_t cut = end;
  trimBlockEnd(lines, start, cut);
  if (removed) {
    removed->clear();
    for (size_t i = start; i < cut; ++i) *removed += lines[i] + "\n";
  }
  // drop the block and the blank lines before it
  size_t from = start;
  while (from > 0 && lines[from - 1].find_first_not_of(" \t") == std::string::npos) --from;
  lines.erase(lines.begin() + static_cast<long>(from), lines.begin() + static_cast<long>(cut));
  std::string out;
  for (auto& l : lines) out += l + "\n";
  return writeFileAtomic(path, out);
}

int Config::uniquifyIds(const std::string& path) {
  const std::string text = readFile(path);
  const auto document = parseConfig(text);
  const auto* widgets = document ? (*document)["widget"].as_array() : nullptr;
  if (!widgets) return 0;
  std::set<std::string> taken, seen;
  std::vector<const toml::value<std::string>*> ids;
  for (const auto& node : *widgets) {
    const auto* widget = node.as_table();
    const auto* id = widget ? (*widget)["id"].as_string() : nullptr;
    if (!id) continue;
    ids.push_back(id);
    taken.insert(id->get());
  }
  std::vector<std::pair<toml::source_region, std::string>> edits;
  for (const auto* node : ids) {
    if (seen.insert(node->get()).second) continue;
    std::string id;
    for (int n = 2;; ++n) {
      id = node->get() + "-" + std::to_string(n);
      if (taken.insert(id).second) break;
    }
    edits.emplace_back(node->source(), tomlText(toml::value(id)));
  }
  if (edits.empty()) return 0;
  auto lines = configLines(text);
  // IDs can be inline or span multiple lines. Apply edits back to front.
  std::ranges::sort(edits, [](const auto& a, const auto& b) { return b.first.begin < a.first.begin; });
  for (const auto& [where, value] : edits) replaceValue(lines, where, value);
  return writeConfig(path, lines) ? static_cast<int>(edits.size()) : 0;
}

// A saved layout preserves each widget's text, including multiline values.
static std::vector<std::pair<size_t, size_t>> widgetRanges(const Lines& lines, const std::string& text) {
  std::vector<std::pair<size_t, size_t>> ranges;
  const auto document = parseConfig(text);
  const auto* widgets = document ? (*document)["widget"].as_array() : nullptr;
  if (!widgets) return ranges;
  for (const auto& node : *widgets) {
    const auto* widget = node.as_table();
    if (!widget || widget->is_inline()) continue;
    const size_t start = sourceLine(widget->source().begin);
    size_t end = sectionEnd(lines, *widget);
    trimBlockEnd(lines, start, end);
    ranges.emplace_back(start, end);
  }
  return ranges;
}

static void splitWidgets(const std::string& text, std::string& rest, std::string& blocks) {
  const auto lines = configLines(text);
  const std::span<const std::string> all(lines);
  size_t previous = 0;
  rest.clear();
  blocks.clear();
  for (const auto& [start, end] : widgetRanges(lines, text)) {
    rest += configText(all.subspan(previous, start - previous));
    if (!blocks.empty() && start != previous) blocks += '\n';
    blocks += configText(all.subspan(start, end - start));
    previous = end;
  }
  rest += configText(all.subspan(previous));
}

std::string Config::widgetBlocks(const std::string& text) {
  std::string rest, blocks;
  splitWidgets(text, rest, blocks);
  return blocks;
}

std::string Config::moveToOutput(const std::string& blocks, const std::string& output) {
  auto lines = configLines(blocks);
  const auto ranges = widgetRanges(lines, blocks);
  const auto value = tomlText(toml::value(output));
  for (auto it = ranges.rbegin(); it != ranges.rend(); ++it) {
    auto [start, end] = *it;
    if (!setLine(lines, start, end, "output", value)) return blocks;
  }
  return configText(lines);
}

std::vector<std::string> Config::splitBlocks(const std::string& blocks) {
  const auto lines = configLines(blocks);
  std::vector<std::string> result;
  for (const auto& [start, end] : widgetRanges(lines, blocks))
    result.push_back(configText(std::span(lines).subspan(start, end - start)));
  return result;
}

std::string Config::replaceWidgetBlocks(const std::string& text, const std::string& blocks) {
  std::string rest, old;
  splitWidgets(text, rest, old);
  // collapse the blank runs the removed blocks leave behind
  std::string out;
  int blanks = 0;
  std::istringstream in(rest);
  for (std::string l; std::getline(in, l);) {
    const bool blank = l.find_first_not_of(" \t") == std::string::npos;
    blanks = blank ? blanks + 1 : 0;
    if (blanks <= 1) out += l + "\n";
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
  if (blocks.empty()) return out + "\n";
  return out + "\n\n" + blocks + (blocks.ends_with("\n") ? "" : "\n");
}

namespace {
std::string oneLine(std::string s) {
  for (char& c : s)
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  while (!s.empty() && s.back() == ' ') s.pop_back();
  while (!s.empty() && s.front() == ' ') s.erase(s.begin());
  return s;
}

bool validSaveId(const std::string& id) {
  if (id.empty() || id.size() > 64) return false;
  for (char c : id)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') return false;  // never a path
  return true;
}

std::string headerValue(const std::string& text, const char* key) {
  std::smatch m;
  const std::regex re(std::string("(^|\n)# ") + key + ": ([^\n]*)");
  return std::regex_search(text, m, re) ? m[2].str() : std::string();
}
}  // namespace

std::vector<SavedLayout> Config::listSaves(const std::string& dir) {
  std::vector<SavedLayout> out;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
    if (e.path().extension() != ".toml" || !validSaveId(e.path().stem().string())) continue;
    SavedLayout s;
    s.id = e.path().stem().string();
    const std::string text = readFile(e.path().string());
    s.name = headerValue(text, "name");
    s.created = headerValue(text, "created");
    s.wallpaper = headerValue(text, "wallpaper");
    s.blocks = widgetBlocks(text);
    if (s.name.empty()) s.name = s.id;
    try {
      s.widgets = load(e.path().string()).widgets;
    } catch (...) {
    }
    out.push_back(std::move(s));
  }
  std::sort(out.begin(), out.end(), [](const SavedLayout& a, const SavedLayout& b) { return a.id > b.id; });
  return out;
}

std::string Config::writeSave(const std::string& dir, const std::string& name, const std::string& wallpaper,
                              const std::string& blocks, const std::string& id) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::string use = id, keepName = oneLine(name), created;
  const std::time_t now = std::time(nullptr);
  char stamp[32], when[32];
  std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
  std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", std::localtime(&now));
  if (use.empty()) {
    use = stamp;
    for (int n = 2; std::filesystem::exists(dir + "/" + use + ".toml", ec); ++n) use = std::string(stamp) + "-" + std::to_string(n);
  } else {
    if (!validSaveId(use)) return {};
    const std::string old = readFile(dir + "/" + use + ".toml");
    if (keepName.empty()) keepName = headerValue(old, "name");
  }
  if (keepName.empty()) keepName = use;
  const std::string text = std::format("# undershell saved layout\n# name: {}\n# created: {}\n# wallpaper: {}\n\n{}", keepName,
                                       when, oneLine(wallpaper), blocks);
  return writeFileAtomic(dir + "/" + use + ".toml", text) ? use : std::string();
}

bool Config::renameSave(const std::string& dir, const std::string& id, const std::string& name) {
  if (!validSaveId(id) || oneLine(name).empty()) return false;
  const std::string path = dir + "/" + id + ".toml";
  std::string text = readFile(path);
  if (text.empty()) return false;
  const std::regex re("(^|\n)# name: [^\n]*");
  text = std::regex_replace(text, re, "$1# name: " + std::regex_replace(oneLine(name), std::regex(R"(\$)"), "$$$$"),
                            std::regex_constants::format_first_only);
  return writeFileAtomic(path, text);
}

bool Config::deleteSave(const std::string& dir, const std::string& id) {
  if (!validSaveId(id)) return false;
  std::error_code ec;
  return std::filesystem::remove(dir + "/" + id + ".toml", ec);
}

int Config::switchProfile(const std::string& path, const std::string& dir, const std::string& fromKey,
                          const std::string& fromLabel, const std::string& toKey) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::string text;
  if (!readFileChecked(path, text)) return -1;
  const std::string saved = std::format("# undershell layout for {}\n# (restored whenever this wallpaper is shown)\n\n{}",
                                        fromLabel.empty() ? fromKey : fromLabel, widgetBlocks(text));
  if (!writeFileAtomic(dir + "/" + fromKey + ".toml", saved)) return -1;
  const std::string next = dir + "/" + toKey + ".toml";
  if (!std::filesystem::exists(next, ec)) return 0;
  std::string nextText;
  if (!readFileChecked(next, nextText)) return -1;
  return writeFileAtomic(path, replaceWidgetBlocks(text, widgetBlocks(nextText))) ? 1 : -1;
}

std::string Config::retargetBlock(const std::string& block, const std::string& id, int x, int y) {
  auto lines = configLines(block);
  size_t end = lines.size();
  if (!setLine(lines, 0, end, "id", tomlText(toml::value(id))) ||
      !setLine(lines, 0, end, "x", std::to_string(x)) ||
      !setLine(lines, 0, end, "y", std::to_string(y)))
    return block;
  return configText(lines);
}

std::string Config::setBlockKey(const std::string& block, const std::string& key, const std::string& tomlValue) {
  auto lines = configLines(block);
  size_t end = lines.size();
  return setLine(lines, 0, end, key, tomlValue) ? configText(lines) : block;
}

std::string Config::blockId(const std::string& block) {
  const auto document = parseConfig(block);
  return document ? (*document)["widget"][0]["id"].value_or(std::string()) : std::string();
}

bool Config::appendBlock(const std::string& path, const std::string& block) {
  std::string text;
  if (!readFileChecked(path, text)) return false;
  while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
  return writeFileAtomic(path, text + "\n\n" + block + (block.ends_with("\n") ? "" : "\n"));
}

std::string Config::tomlText(const toml::node& n) {
  if (const auto* str = n.as_string()) {
    // always a basic "..." string (toml++ would pick '...' literals)
    std::string out = "\"";
    for (unsigned char c : str->get()) {
      if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
      else if (c == '\n') out += "\\n";
      else if (c == '\r') out += "\\r";
      else if (c == '\t') out += "\\t";
      else if (c < 0x20 || c == 0x7F) out += std::format("\\u{:04X}", static_cast<unsigned>(c));
      else out += static_cast<char>(c);
    }
    return out + "\"";
  }
  std::ostringstream ss;
  n.visit([&](auto&& v) { ss << v; });
  return ss.str();
}

std::string Config::defaultBlock(const std::string& type, const std::string& id, const std::string& output, int x, int y,
                                 int w, int h, const std::string& look, int spaceW, int spaceH) {
  std::string b = std::format("[[widget]]\nid = \"{}\"\ntype = \"{}\"\n", id, type);
  if (!output.empty()) b += std::format("output = \"{}\"\n", output);
  b += std::format("x = {}\ny = {}\nwidth = {}\nheight = {}\n", x, y, w, h);
  if (spaceW > 0 && spaceH > 0)  // the output it was laid out on: other sizes follow the wallpaper
    b += std::format("space = [{}, {}]\n", spaceW, spaceH);
  if (type == "clock") {
    b += std::format("face = \"{}\"            # digital minimal analog flip rings bighour metal goodnight grand column outline banner\n",
                     look.empty() ? "digital" : look);
    b += "date = \"inline\"          # none inline badge stacked\n"
         "clock_24h = true\nseconds = false\n"
         "accent = \"primary\"        # auto primary secondary tertiary brand mono custom\n"
         "accent_color = \"#e2342a\"\nink = \"on_surface\"\n"
         "language = \"system\"      # system en es\nweather = true            # metal face\nfahrenheit = false\n";
  } else if (type == "visualizer") {
    b += std::format("style = \"{}\"             # bars split dots segments wave ribbon curtain line frame radial orb spiral halo vortex fire muzzle electric sfx speedlines rain ripples neon fireworks terminal tv pond aura stars pixels life lava globe\n",
                     look.empty() ? "bars" : look);
  } else if (type == "diorama") {
    b += std::format("style = \"{}\"             # aquarium terrarium rainy_room\n", look.empty() ? "aquarium" : look);
  } else if (type == "instrument") {
    b += std::format("style = \"{}\"             # scope meters spectrogram\n", look.empty() ? "scope" : look);
  } else if (type == "scene") {
    b += "scene = \"\"                 # a Wallpaper Engine wallpaper: its Workshop id or folder (scenes and videos)\n"
         "depth = false\n"
         "layer = -999               # under every other widget\n";
  } else if (type == "filter") {
    b += std::format("style = \"{}\"             # warm cool noir vintage dream night vivid faded duotone custom\n", look.empty() ? "warm" : look);
    b += "depth = false              # true: only the background, behind the wallpaper's subject\n"
         "layer = -999               # under every other widget\n"
         "# animated effects: fx_reflection fx_waves fx_ripple fx_flow fx_sway fx_scroll fx_parallax fx_droplets fx_godrays fx_shine fx_twinkle\n"
         "# fx_pulse fx_aurora fx_mist fx_snowcap fx_grain fx_vhs fx_chroma = true (options: docs/OPTIONS.md, your own: docs/EFFECTS.md)\n"
         "# particles: pt_snow pt_rain pt_petals pt_fireflies pt_dust pt_embers pt_bubbles pt_sparks = true (docs/PARTICLES.md)\n";
  } else if (type == "weather") {
    b += std::format("layout = \"{}\"           # now card week hours\n", look.empty() ? "now" : look);
    b += "location = \"\"            # your city (\"Quito, Ecuador\") or \"lat, lon\": only Open-Meteo sees it\n"
         "units = \"metric\"         # metric imperial\n";
  } else if (type == "pet") {
    b += std::format("species = \"{}\"          # companion; see docs/PETS.md\n", look.empty() ? "sprout" : look);
    b += "color = \"primary\"         # Sprout's body; sprout = its leaf\n"
         "variant = \"\"               # its colouring (each species has its own; \"\" = the natural one)\n"
         "paint = \"variant\"         # variant theme wallpaper custom (then body_color belly_color mark_color eye_color detail_color)\n"
         "climb = true               # hop onto window tops (Umbriel says where they are)\n"
         "notify = \"title\"          # off react app title full: what it tells you of a notification\n"
         "interact = true            # a click pats it, a drag carries it, a double click makes it jump\n"
         "depth = false\n";
  } else if (type == "activity") {
    b += std::format("layout = \"{}\"            # grid weeks\n", look.empty() ? "grid" : look);
    b += "folders = [\"~\"]          # where git repositories are looked for (three levels down)\n"
         "author = \"\"              # \"\": your git email; \"all\": anyone; or a name or email\n";
  } else if (type == "system") {
    b += std::format("layout = \"{}\"           # rings bars graph text\n", look.empty() ? "rings" : look);
    b += "show_cpu = true            # also show_ram show_gpu show_temp show_net show_disk show_battery\n"
         "interval = 1.0             # seconds between readings of /proc and /sys\n";
  } else if (type == "notes") {
    b += "plate = \"paper\"           # paper glass none\n"
         "file = \"\"                # a Markdown file; empty: ~/.local/share/undershell/notes/<id>.md\n";
  } else if (type == "calendar") {
    b += std::format("layout = \"{}\"           # month week day agenda\n", look.empty() ? "month" : look);
    b += "plate = \"glass\"           # glass solid none\n";
  } else if (type == "lyrics") {
    b += std::format("layout = \"{}\"           # lines line karaoke\n", look.empty() ? "lines" : look);
    b += "style = \"plain\"            # plain neon retro comic outline sticker chrome glitch rainbow typewriter\n";
  } else if (type == "text") {
    b += std::format("style = \"{}\"            # plain neon retro comic outline sticker chrome glitch rainbow typewriter\n",
                     look.empty() ? "plain" : look);
    b += "text = \"undershell\"       # {title} {artist} {album} {time} {date} {day} are filled in\n";
  } else if (type == "image") {
    b += "file = \"\"                # a picture, a GIF or a video (the inspector's Choose button)\n"
         "fit = \"fit\"               # fit fill stretch\n";
  } else if (type == "shape") {
    b += std::format("shape = \"{}\"           # see Shapes in docs/OPTIONS.md\n",
                     look.empty() ? "rect" : look);
    b += "fill = \"solid\"            # solid gradient radial none\n"
         "color = \"primary\"\ncolor2 = \"tertiary\"\n";
  } else if (type == "now_playing") {
    b += std::format("layout = \"{}\"           # sheet vinyl tile poster strip portrait cyber\n", look.empty() ? "sheet" : look);
    b += "plate = \"cover\"           # cover glass none\n"
         "show_lyrics = true          # synced lyrics from LRCLIB\n"
         "viz = \"bars\"              # bars wave (when there are no lyrics)\n"
         "accent_source = \"album\"   # album theme\n"
         "music_app = \"spotify\"     # opened by the corner button\n"
         "ink = \"on_surface\"\nfps = 30\n";
  }
  return b;
}

bool Config::saveGeometry(const std::string& path, const WidgetConfig& w) {
  return editBlock(path, w.id, {{"x", std::to_string(w.x)}, {"y", std::to_string(w.y)},
                                {"width", std::to_string(w.width)}, {"height", std::to_string(w.height)}});
}

bool Config::setKey(const std::string& path, const std::string& id, const std::string& key, const std::string& tomlValue) {
  return editBlock(path, id, {{key, tomlValue}});
}

}  // namespace undershell

namespace undershell {

std::vector<WidgetConfig> screensaverWidgets(const ScreensaverConfig& s, int w, int h) {
  std::vector<WidgetConfig> out;
  auto base = [&](const char* id, const char* type) {
    WidgetConfig c;
    c.id = id;
    c.type = type;
    c.x = 0, c.y = 0, c.width = w, c.height = h;
    c.spaceW = w, c.spaceH = h;
    c.depth = false;
    return c;
  };
  if (s.background == "wallpaper") {  // the wallpaper, darker and with a vignette
    WidgetConfig c = base("screensaver-wallpaper", "filter");
    c.options.insert_or_assign("style", "custom");
    c.options.insert_or_assign("brightness", -0.6 * s.dim);
    c.options.insert_or_assign("vignette", 0.5 * s.dim);
    c.options.insert_or_assign("depth", false);
    out.push_back(std::move(c));
  }
  if (s.look["style"].value_or(std::string("pixels")) != "none") {
    WidgetConfig c = base("screensaver-look", "visualizer");
    c.options = s.look;
    if (!c.options.contains("style")) {  // the omarchy.org-like default: the time in pixels
      c.options.insert_or_assign("style", "pixels");
      if (!c.options.contains("pixels_text")) c.options.insert_or_assign("pixels_text", "@TIME@");
    }
    c.options.insert_or_assign("depth", false);
    c.options.insert_or_assign("idle", "show");  // a silent screensaver still moves
    if (!c.options.contains("fps")) c.options.insert_or_assign("fps", 30);  // half the cost of 60, and a screensaver needs no more
    out.push_back(std::move(c));
  }
  if (const std::string face = s.clock["face"].value_or(std::string("none")); face != "none") {
    WidgetConfig c = base("screensaver-clock", "clock");
    c.width = std::max(200, w * 2 / 5), c.height = std::max(90, h / 5);
    c.x = (w - c.width) / 2, c.y = (h - c.height) / 2;
    c.options = s.clock;
    c.options.insert_or_assign("depth", false);
    out.push_back(std::move(c));
  }
  return out;
}

}  // namespace undershell
