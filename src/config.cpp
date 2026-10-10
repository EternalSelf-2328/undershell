// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.hpp"

#include "common.hpp"
#include "geom.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <regex>
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

[[widget]]
id = "visualizer"
type = "visualizer"
x = 460                   # x, y, width, height: logical px of a screen of size `space`;
y = 760                   # on any other screen the widget keeps its place on the wallpaper
width = 1000
height = 280
space = [1920, 1080]
# bars split dots segments wave ribbon curtain line frame radial orb spiral halo vortex fire muzzle electric
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
gain = 1.0
smoothing = 0.5
peaks = false
mirror = false
idle_wave = false         # a breathing line while silent (costs a little GPU)
spin = 0.0                # polar looks: degrees per second
glow = 0.6
fps = 60
opacity = 1.0
depth = true              # pass behind the wallpaper_depth foreground
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
      w.layer = static_cast<int>(std::clamp<int64_t>(get<int64_t>(*t, "layer", 0), -10, 10));
      if (auto i = (*t)["depth_level"].value<int64_t>()) w.depthLevel = std::clamp(static_cast<double>(*i), 0.0, 100.0);
      w.options = *t;
      cfg.widgets.push_back(std::move(w));
    }
  }
  return cfg;
}

// locate [[widget]] block of `id`: [start, end) line range
static bool findBlock(const std::vector<std::string>& lines, const std::string& id, size_t& start, size_t& end) {
  static const std::regex kHeader(R"(^\s*\[)");
  static const std::regex kWidget(R"(^\s*\[\[\s*widget\s*\]\]\s*(#.*)?$)");
  const std::regex kId("^\\s*id\\s*=\\s*\"" + std::regex_replace(id, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\$&)") + "\"");
  for (size_t i = 0; i < lines.size(); ++i) {
    if (!std::regex_search(lines[i], kWidget)) continue;
    size_t j = i + 1;
    while (j < lines.size() && !std::regex_search(lines[j], kHeader)) ++j;
    for (size_t k = i + 1; k < j; ++k) {
      if (std::regex_search(lines[k], kId)) {
        start = i;
        end = j;
        return true;
      }
    }
  }
  return false;
}

static void setLine(std::vector<std::string>& lines, size_t start, size_t& end, const std::string& key,
                    const std::string& value) {
  // a value: a string, an array (corner pins), or a bare token
  const std::regex re("^(\\s*" + key + R"(\s*=\s*)("(?:[^"\\]|\\.)*"|\[[^\]]*\]|[^#\s]*)(\s*#.*)?\s*$)");
  for (size_t k = start + 1; k < end; ++k) {
    std::smatch m;
    if (std::regex_match(lines[k], m, re)) {
      lines[k] = m[1].str() + value + (m[3].matched ? m[3].str() : std::string());
      return;
    }
  }
  // append after the last non-empty line of the block
  size_t at = end;
  while (at > start + 1 && lines[at - 1].find_first_not_of(" \t") == std::string::npos) --at;
  lines.insert(lines.begin() + static_cast<long>(at), key + " = " + value);
  ++end;
}

bool Config::setGeneral(const std::string& path, const std::string& key, const std::string& tomlValue) {
  std::string text = readFile(path);
  std::istringstream in(text);
  std::vector<std::string> lines;
  for (std::string l; std::getline(in, l);) lines.push_back(l);
  size_t start = lines.size(), end = lines.size();
  for (size_t k = 0; k < lines.size(); ++k) {
    const auto first = lines[k].find_first_not_of(" \t");
    if (first == std::string::npos || lines[k][first] != '[') continue;
    if (start < lines.size()) {  // the next table ends [general]
      end = k;
      break;
    }
    if (lines[k].compare(first, 9, "[general]") == 0) start = k;
  }
  if (start == lines.size()) {  // no [general] yet: one at the top
    lines.insert(lines.begin(), {"[general]", ""});
    start = 0;
    end = 2;
  }
  setLine(lines, start, end, key, tomlValue);
  std::string out;
  for (auto& l : lines) out += l + "\n";
  return writeFileAtomic(path, out);
}

static bool editBlock(const std::string& path, const std::string& id,
                      const std::vector<std::pair<std::string, std::string>>& kv) {
  std::string text = readFile(path);
  if (text.empty()) return false;
  std::istringstream in(text);
  std::vector<std::string> lines;
  for (std::string l; std::getline(in, l);) lines.push_back(l);
  size_t start = 0, end = 0;
  if (!findBlock(lines, id, start, end)) return false;
  for (auto& [k, v] : kv) setLine(lines, start, end, k, v);
  std::string out;
  for (auto& l : lines) out += l + "\n";
  return writeFileAtomic(path, out);
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

static std::vector<std::string> fileLines(const std::string& path) {
  std::istringstream in(readFile(path));
  std::vector<std::string> lines;
  for (std::string l; std::getline(in, l);) lines.push_back(l);
  return lines;
}

// a block runs from its [[widget]] header up to the next header, minus the
// blank lines that separate it from that header
static void trimBlockEnd(const std::vector<std::string>& lines, size_t start, size_t& end) {
  while (end > start + 1 && lines[end - 1].find_first_not_of(" \t") == std::string::npos) --end;
}

std::string Config::blockText(const std::string& path, const std::string& id) {
  auto lines = fileLines(path);
  size_t start = 0, end = 0;
  if (!findBlock(lines, id, start, end)) return {};
  trimBlockEnd(lines, start, end);
  std::string out;
  for (size_t i = start; i < end; ++i) out += lines[i] + "\n";
  return out;
}

bool Config::removeBlock(const std::string& path, const std::string& id, std::string* removed) {
  auto lines = fileLines(path);
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
  auto lines = fileLines(path);
  static const std::regex kHeader(R"(^\s*\[)");
  static const std::regex kWidget(R"(^\s*\[\[\s*widget\s*\]\]\s*(#.*)?$)");
  static const std::regex kId(R"re(^(\s*id\s*=\s*)"([^"]*)"(.*)$)re");
  // every id line, in order
  std::vector<size_t> at;
  std::vector<std::string> ids;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (!std::regex_search(lines[i], kWidget)) continue;
    for (size_t k = i + 1; k < lines.size() && !std::regex_search(lines[k], kHeader); ++k) {
      std::smatch m;
      if (std::regex_match(lines[k], m, kId)) {
        at.push_back(k);
        ids.push_back(m[2].str());
        break;
      }
    }
  }
  int renamed = 0;
  std::vector<std::string> seen;
  auto taken = [&](const std::string& id) {
    return std::find(seen.begin(), seen.end(), id) != seen.end() || std::find(ids.begin(), ids.end(), id) != ids.end();
  };
  for (size_t n = 0; n < ids.size(); ++n) {
    if (std::find(seen.begin(), seen.end(), ids[n]) == seen.end()) {
      seen.push_back(ids[n]);
      continue;
    }
    std::string id;
    for (int k = 2;; ++k) {
      id = ids[n] + "-" + std::to_string(k);
      if (!taken(id)) break;
    }
    std::smatch m;
    std::regex_match(lines[at[n]], m, kId);
    lines[at[n]] = m[1].str() + "\"" + id + "\"" + m[3].str();
    seen.push_back(id);
    ++renamed;
  }
  if (renamed == 0) return 0;
  std::string out;
  for (auto& l : lines) out += l + "\n";
  return writeFileAtomic(path, out) ? renamed : 0;
}

// splits a config into its [[widget]] blocks and the rest; a block runs from
// its header to the next header, its trailing blank lines left to the rest
static void splitWidgets(const std::string& text, std::string& rest, std::string& blocks) {
  static const std::regex kHeader(R"(^\s*\[)");
  static const std::regex kWidget(R"(^\s*\[\[\s*widget\s*\]\]\s*(#.*)?$)");
  std::istringstream in(text);
  std::vector<std::string> lines;
  for (std::string l; std::getline(in, l);) lines.push_back(l);
  std::vector<bool> inWidget(lines.size(), false);
  for (size_t i = 0; i < lines.size(); ++i) {
    if (!std::regex_search(lines[i], kWidget)) continue;
    size_t j = i + 1;
    while (j < lines.size() && !std::regex_search(lines[j], kHeader)) ++j;
    size_t end = j;
    trimBlockEnd(lines, i, end);
    for (size_t k = i; k < end; ++k) inWidget[k] = true;
    i = j - 1;
  }
  rest.clear();
  blocks.clear();
  for (size_t i = 0; i < lines.size(); ++i) {
    if (!inWidget[i]) {
      rest += lines[i] + "\n";
      continue;
    }
    if (i > 0 && !inWidget[i - 1] && !blocks.empty()) blocks += "\n";  // one blank line between blocks
    blocks += lines[i] + "\n";
  }
}

std::string Config::widgetBlocks(const std::string& text) {
  std::string rest, blocks;
  splitWidgets(text, rest, blocks);
  return blocks;
}

std::string Config::moveToOutput(const std::string& blocks, const std::string& output) {
  std::istringstream in(blocks);
  std::vector<std::string> lines;
  for (std::string l; std::getline(in, l);) lines.push_back(l);
  auto header = [&](size_t k) {
    const auto first = lines[k].find_first_not_of(" \t");
    return first != std::string::npos && lines[k][first] == '[';
  };
  for (size_t start = 0; start < lines.size(); ++start) {
    if (lines[start].find("[[widget]]") != 0) continue;
    size_t end = start + 1;
    while (end < lines.size() && !header(end)) ++end;
    setLine(lines, start, end, "output", "\"" + output + "\"");
    start = end - 1;
  }
  std::string out;
  for (auto& l : lines) out += l + "\n";
  return out;
}

std::vector<std::string> Config::splitBlocks(const std::string& blocks) {
  std::istringstream in(blocks);
  std::vector<std::string> out;
  for (std::string l; std::getline(in, l);) {
    const auto first = l.find_first_not_of(" \t");
    if (l.find("[[widget]]") == 0) out.emplace_back();
    else if (first != std::string::npos && l[first] == '[') continue;  // not ours (widgetBlocks has none)
    if (!out.empty()) out.back() += l + "\n";
  }
  for (auto& b : out)
    while (b.size() > 1 && b.ends_with("\n\n")) b.pop_back();
  return out;
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
  const std::string text = readFile(path);
  const std::string saved = std::format("# undershell layout for {}\n# (restored whenever this wallpaper is shown)\n\n{}",
                                        fromLabel.empty() ? fromKey : fromLabel, widgetBlocks(text));
  if (!writeFileAtomic(dir + "/" + fromKey + ".toml", saved)) return -1;
  const std::string next = dir + "/" + toKey + ".toml";
  if (!std::filesystem::exists(next, ec)) return 0;
  return writeFileAtomic(path, replaceWidgetBlocks(text, widgetBlocks(readFile(next)))) ? 1 : -1;
}

std::string Config::retargetBlock(const std::string& block, const std::string& id, int x, int y) {
  std::istringstream in(block);
  std::vector<std::string> lines;
  for (std::string l; std::getline(in, l);) lines.push_back(l);
  size_t end = lines.size();
  setLine(lines, 0, end, "id", "\"" + id + "\"");
  setLine(lines, 0, end, "x", std::to_string(x));
  setLine(lines, 0, end, "y", std::to_string(y));
  std::string out;
  for (auto& l : lines) out += l + "\n";
  return out;
}

bool Config::appendBlock(const std::string& path, const std::string& block) {
  std::string text = readFile(path);
  while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
  return writeFileAtomic(path, text + "\n\n" + block + (block.ends_with("\n") ? "" : "\n"));
}

std::string Config::tomlText(const toml::node& n) {
  if (const auto* str = n.as_string()) {
    // always a basic "..." string (toml++ would pick '...' literals)
    std::string out = "\"";
    for (char c : str->get()) {
      if (c == '"' || c == '\\') out += '\\';
      out += c;
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
         "accent = \"primary\"        # primary secondary tertiary brand mono custom\n"
         "accent_color = \"#e2342a\"\nink = \"on_surface\"\n"
         "language = \"system\"      # system en es\nweather = true            # metal face\nfahrenheit = false\n";
  } else if (type == "visualizer") {
    b += std::format("style = \"{}\"             # bars split dots segments wave ribbon curtain line frame radial orb spiral halo vortex fire muzzle electric\n",
                     look.empty() ? "bars" : look);
  } else if (type == "now_playing") {
    b += std::format("layout = \"{}\"           # sheet vinyl tile poster strip portrait\n", look.empty() ? "sheet" : look);
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
