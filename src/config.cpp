// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.hpp"

#include "common.hpp"

#include <algorithm>
#include <cmath>
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
profiles = true           # remember a widget layout for each wallpaper

[[widget]]
id = "visualizer"
type = "visualizer"
output = "HDMI-A-1"
x = 460
y = 760
width = 1000
height = 280
# bars split dots segments wave ribbon curtain line frame radial orb spiral
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
      w.enabled = get<bool>(*t, "enabled", true);
      w.depth = get<bool>(*t, "depth", true);
      w.depthLevel = std::clamp(get<double>(*t, "depth_level", 0.0), 0.0, 100.0);
      if (auto i = (*t)["depth_level"].value<int64_t>()) w.depthLevel = std::clamp(static_cast<double>(*i), 0.0, 100.0);
      if (auto r = (*t)["rotation"].value<double>()) {
        double d = std::fmod(*r, 360.0);
        w.rotation = d <= -180.0 ? d + 360.0 : (d > 180.0 ? d - 360.0 : d);
      }
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
  const std::regex re("^(\\s*" + key + R"(\s*=\s*)("(?:[^"\\]|\\.)*"|[^#\s]*)(\s*#.*)?\s*$)");
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
                                 int w, int h, const std::string& look) {
  std::string b = std::format("[[widget]]\nid = \"{}\"\ntype = \"{}\"\n", id, type);
  if (!output.empty()) b += std::format("output = \"{}\"\n", output);
  b += std::format("x = {}\ny = {}\nwidth = {}\nheight = {}\n", x, y, w, h);
  if (type == "clock") {
    b += std::format("face = \"{}\"            # digital minimal analog flip rings bighour metal goodnight grand column outline banner\n",
                     look.empty() ? "digital" : look);
    b += "date = \"inline\"          # none inline badge stacked\n"
         "clock_24h = true\nseconds = false\n"
         "accent = \"primary\"        # primary secondary tertiary brand mono custom\n"
         "accent_color = \"#e2342a\"\nink = \"on_surface\"\n"
         "language = \"system\"      # system en es\nweather = true            # metal face\nfahrenheit = false\n";
  } else if (type == "visualizer") {
    b += std::format("style = \"{}\"             # bars split dots segments wave ribbon curtain line frame radial orb spiral\n",
                     look.empty() ? "bars" : look);
  } else if (type == "now_playing") {
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
