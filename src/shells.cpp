// SPDX-License-Identifier: GPL-3.0-or-later
#include "shells.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <toml++/toml.hpp>

namespace undershell {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string env(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return v && *v ? v : expandHome(fallback);
}
std::string stateDir() { return env("XDG_STATE_HOME", "~/.local/state"); }
std::string cacheDir() { return env("XDG_CACHE_HOME", "~/.cache"); }
std::string configDir() { return env("XDG_CONFIG_HOME", "~/.config"); }

std::string base(const std::string& p) {
  const auto s = p.rfind('/');
  return s == std::string::npos ? p : p.substr(s + 1);
}

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
  return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

bool hasArg(const Argv& a, const std::string& want) { return std::ranges::find(a, want) != a.end(); }
bool argContains(const Argv& a, const std::string& part) {
  return std::ranges::any_of(a, [&](const std::string& s) { return s.find(part) != std::string::npos; });
}
bool isQuickshell(const Argv& a) { return !a.empty() && (base(a[0]) == "qs" || base(a[0]) == "quickshell"); }

float luma(Color c) { return 0.2126F * c.r + 0.7152F * c.g + 0.0722F * c.b; }

bool hexColor(std::string v, Color& out) {
  if (!v.empty() && v[0] != '#') v = "#" + v;
  if (v.size() != 7 || !std::all_of(v.begin() + 1, v.end(), [](unsigned char c) { return std::isxdigit(c); })) return false;
  out = Color::fromHex(v);
  return true;
}

// a JSON object of role → "#rrggbb" (or "rrggbb"), roles turned to snake_case
std::map<std::string, Color> rolesOf(const json& obj) {
  std::map<std::string, Color> out;
  if (!obj.is_object()) return out;
  for (auto& [k, v] : obj.items()) {
    Color c;
    if (v.is_string() && hexColor(v.get<std::string>(), c)) out[snakeRole(k)] = c;
  }
  return out;
}

void darkFrom(const std::map<std::string, Color>& pal, bool& dark) {
  for (const char* k : {"surface", "background"})
    if (auto it = pal.find(k); it != pal.end()) {
      dark = luma(it->second) < 0.5F;
      return;
    }
}

json parse(const std::string& text) {
  try {
    return json::parse(text);
  } catch (...) {
    return {};
  }
}

std::string firstExisting(std::initializer_list<std::string> paths) {
  for (const auto& p : paths)
    if (fs::exists(p)) return p;
  return {};
}

// the newest awww/swww cache directory (they are versioned: ~/.cache/awww/0.12.1/)
std::string awwwCacheDir() {
  std::string best;
  fs::file_time_type bestTime{};
  std::error_code ec;
  for (const char* tool : {"awww", "swww"})
    for (auto& e : fs::directory_iterator(cacheDir() + "/" + tool, ec)) {
      if (!e.is_directory(ec)) continue;
      const auto t = e.last_write_time(ec);
      if (best.empty() || t > bestTime) {
        best = e.path().string();
        bestTime = t;
      }
    }
  return best;
}

}  // namespace

std::string snakeRole(std::string k) {
  if (k.starts_with("over") && k.size() > 4 && std::isupper(static_cast<unsigned char>(k[4]))) k = "on" + k.substr(4);
  std::string out;
  for (char c : k) {
    if (std::isupper(static_cast<unsigned char>(c))) {
      if (!out.empty()) out += '_';
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else {
      out += c;
    }
  }
  return out;
}

int fillFrom(const std::string& tool, const std::string& m) {
  std::string mode = m;
  std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) { return std::tolower(c); });
  if (const auto c = mode.find(':'); c != std::string::npos) mode.resize(c);  // awww's "crop:center"
  if (mode.starts_with("fit")) return 2;  // wpaperd's fit-border-color, COSMIC's Fit((r, g, b))
  if (mode == "stretch") return 3;
  if (mode.starts_with("tile") || mode == "repeat") return 4;
  if (mode == "no" || mode == "pad") return 0;
  if (mode == "center" || mode == "centre") return tool == "wpaperd" ? 1 : 0;  // wpaperd's "center" crops
  if (mode == "contain") return 2;  // hyprpaper
  if (mode == "fill") return tool == "hyprpaper" ? 3 : 1;  // hyprpaper's fill stretches; elsewhere it crops
  return 1;  // crop, cover, zoom, scrolling, default
}

// caelestia cli utils/scheme.py → $XDG_STATE_HOME/caelestia/scheme.json:
// {"mode": "dark", "colours": {"primary": "a6c8ff", "onPrimary": …}}
std::map<std::string, Color> caelestiaScheme(const std::string& text, bool& dark) {
  const json j = parse(text);
  if (!j.is_object()) return {};
  auto pal = rolesOf(j.value("colours", json::object()));
  dark = j.value("mode", std::string("dark")) != "light";
  return pal;
}

// DMS matugen/templates/dank.json → ~/.cache/DankMaterialShell/dms-colors.json:
// {"mode": "dark", "colors": {"dark": {"primary": "#…"}, "light": {…}}}
std::map<std::string, Color> dmsColors(const std::string& text, bool& dark) {
  const json j = parse(text);
  if (!j.is_object() || !j.contains("colors")) return {};
  const std::string mode = j.value("mode", std::string("dark"));
  dark = mode != "light";
  return rolesOf(j["colors"].value(mode, json::object()));
}

// end-4 ii ~/.local/state/quickshell/user/generated/colors.json and Ambxst
// ~/.cache/ambxst/colors.json: a flat object of roles
std::map<std::string, Color> flatPalette(const std::string& text, bool& dark) {
  auto pal = rolesOf(parse(text));
  darkFrom(pal, dark);
  return pal;
}

// pywal ~/.cache/wal/colors.json: a terminal palette, not Material roles; the
// usual reading of its sixteen colours
std::map<std::string, Color> pywalPalette(const std::string& text, std::string& wallpaper, bool& dark) {
  const json j = parse(text);
  if (!j.is_object() || !j.contains("colors")) return {};
  wallpaper = j.value("wallpaper", std::string());
  const json& c = j["colors"];
  const json& s = j.value("special", json::object());
  std::map<std::string, Color> pal;
  auto put = [&](const char* role, const json& from, const char* key) {
    Color col;
    if (from.contains(key) && from[key].is_string() && hexColor(from[key].get<std::string>(), col)) pal[role] = col;
  };
  put("surface", s, "background");
  put("on_surface", s, "foreground");
  put("primary", c, "color4");
  put("secondary", c, "color5");
  put("tertiary", c, "color6");
  put("error", c, "color1");
  put("outline", c, "color8");
  put("surface_variant", c, "color0");
  darkFrom(pal, dark);
  return pal;
}

// DMS Common/SessionData.qml → ~/.local/state/DankMaterialShell/session.json
// (wallpaperPath, perMonitorWallpaper + monitorWallpapers, perModeWallpaper
// + …Light/…Dark, monitorWallpaperFillModes); the global fill in
// ~/.config/DankMaterialShell/settings.json wallpaperFillMode ("Fill")
bool dmsSession(const std::string& session, const std::string& settings, bool dark, ShellState& out) {
  const json s = parse(session);
  if (!s.is_object()) return false;
  const bool perMode = s.value("perModeWallpaper", false);
  const char* suffix = dark ? "Dark" : "Light";
  auto image = [](const json& v) {
    const std::string p = v.is_string() ? v.get<std::string>() : std::string();
    return p.starts_with('#') ? std::string() : expandHome(p);  // "#1e1e2e": a plain colour
  };
  out.fallback = image(s.value(perMode ? std::string("wallpaperPath") + suffix : std::string("wallpaperPath"), json()));
  if (out.fallback.empty()) out.fallback = image(s.value("wallpaperPath", json()));
  if (s.value("perMonitorWallpaper", false)) {
    const json mons = s.value(perMode ? std::string("monitorWallpapers") + suffix : std::string("monitorWallpapers"), json::object());
    for (auto& [name, v] : mons.items())
      if (const std::string p = image(v); !p.empty()) out.byOutput[name] = p;
  }
  const json st = parse(settings);
  out.fillMode = fillFrom("dms", st.is_object() ? st.value("wallpaperFillMode", std::string("Fill")) : std::string("Fill"));
  return !out.fallback.empty() || !out.byOutput.empty();
}

// Ambxst modules/widgets/dashboard/wallpapers/Wallpaper.qml →
// ~/.cache/ambxst/wallpapers.json {"currentWall", "perScreenWallpapers"}
bool ambxstWallpapers(const std::string& text, ShellState& out) {
  const json j = parse(text);
  if (!j.is_object()) return false;
  out.fallback = expandHome(j.value("currentWall", std::string()));
  const json screens = j.value("perScreenWallpapers", json::object());
  for (auto& [name, v] : screens.items())
    if (v.is_string() && !v.get<std::string>().empty()) out.byOutput[name] = expandHome(v.get<std::string>());
  out.fillMode = 1;
  return !out.fallback.empty() || !out.byOutput.empty();
}

// end-4 ~/.config/illogical-impulse/config.json .background.wallpaperPath
std::string iiWallpaper(const std::string& text) {
  const json j = parse(text);
  if (!j.is_object() || !j.contains("background") || !j["background"].is_object()) return {};
  return expandHome(j["background"].value("wallpaperPath", std::string()));
}

// HyprPanel ~/.config/hyprpanel/config.json, flat dotted keys (nested also
// read): "wallpaper.enable", "wallpaper.image"
std::string hyprpanelWallpaper(const std::string& text) {
  const json j = parse(text);
  if (!j.is_object()) return {};
  auto get = [&](const char* group, const char* key) -> json {
    const std::string flat = std::string(group) + "." + key;
    if (j.contains(flat)) return j[flat];
    if (j.contains(group) && j[group].is_object() && j[group].contains(key)) return j[group][key];
    return {};
  };
  const json on = get("wallpaper", "enable"), image = get("wallpaper", "image");
  if (!on.is_boolean() || !on.get<bool>() || !image.is_string()) return {};
  return expandHome(image.get<std::string>());
}

// cosmic-bg: ~/.local/state/cosmic/com.system76.CosmicBackground/v1/wallpapers
// [("eDP-1", Path("/img.jpg")), …]; scaling_mode from the config's `all` entry
bool cosmicWallpapers(const std::string& state, const std::string& allEntry, ShellState& out) {
  static const std::regex kPair(R"re(\(\s*"([^"]+)"\s*,\s*Path\(\s*"((?:[^"\\]|\\.)*)"\s*\)\s*\))re");
  for (std::sregex_iterator it(state.begin(), state.end(), kPair), end; it != end; ++it) {
    std::string p = (*it)[2].str();
    for (size_t i = 0; (i = p.find("\\\"", i)) != std::string::npos;) p.replace(i, 2, "\"");
    out.byOutput[(*it)[1].str()] = p;
    if (out.fallback.empty()) out.fallback = p;
  }
  std::smatch m;
  static const std::regex kScale(R"(scaling_mode\s*:\s*(\w+))");
  out.fillMode = std::regex_search(allEntry, m, kScale) ? fillFrom("cosmic", m[1].str()) : 1;
  return !out.byOutput.empty();
}

// awww common/src/cache.rs: NUL-separated `namespace, resize[:gravity],
// filter, path` per entry (older swww: fewer fields, the path last)
std::pair<std::string, std::string> awwwCacheEntry(const std::string& data) {
  std::vector<std::string> f;
  std::string cur;
  for (char c : data) {
    if (c == '\0') {
      f.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) f.push_back(cur);
  while (!f.empty() && f.back().empty()) f.pop_back();
  if (f.empty()) return {};
  if (f.size() >= 4) {
    // the default namespace ("") if it is there, else the first entry
    for (size_t i = 0; i + 3 < f.size(); i += 4)
      if (f[i].empty()) return {f[i + 3], f[i + 1]};
    return {f[3], f[1]};
  }
  return {f.back(), f.size() >= 2 ? f[f.size() - 2] : std::string("crop")};
}

// `hyprctl hyprpaper listactive`: "DP-1: /path" (0.8) or "DP-1 = /path" (0.7)
bool hyprpaperActive(const std::string& text, ShellState& out) {
  std::istringstream in(text);
  std::string line;
  static const std::regex kLine(R"(^\s*([^:=\s][^:=]*?)\s*(?::|=)\s*(/.+?)\s*$)");
  while (std::getline(in, line)) {
    std::smatch m;
    if (!std::regex_match(line, m, kLine)) continue;
    const std::string mon = m[1].str(), path = m[2].str();
    if (mon.empty() || mon == "*") out.fallback = path;
    else out.byOutput[mon] = path;
  }
  return !out.byOutput.empty() || !out.fallback.empty();
}

// ~/.config/hypr/hyprpaper.conf: `wallpaper { monitor = …  path = …
// fit_mode = … }` (0.8) or `wallpaper = DP-1,contain:/path` (0.7)
bool hyprpaperConfig(const std::string& text, ShellState& out) {
  std::istringstream in(text);
  std::string line, mon, path, fit;
  bool block = false;
  auto flush = [&]() {
    if (path.empty()) return;
    const std::string p = expandHome(path);
    std::error_code ec;
    if (!fs::is_directory(p, ec)) {  // a folder rotates: its current image is not in the file
      if (mon.empty() || mon == "*") out.fallback = p;
      else out.byOutput[mon] = p;
    }
    if (!fit.empty()) out.fillMode = fillFrom("hyprpaper", fit);
  };
  while (std::getline(in, line)) {
    if (const auto h = line.find('#'); h != std::string::npos) line.resize(h);
    line = trim(line);
    if (line.empty()) continue;
    if (!block && line.starts_with("wallpaper") && line.find('{') != std::string::npos) {
      block = true;
      mon = path = fit = "";
      continue;
    }
    if (block && line.starts_with('}')) {
      flush();
      block = false;
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
    if (block) {
      if (k == "monitor") mon = v;
      else if (k == "path") path = v;
      else if (k == "fit_mode") fit = v;
    } else if (k == "wallpaper") {
      const auto comma = v.find(',');
      if (comma == std::string::npos) continue;
      mon = trim(v.substr(0, comma));
      path = trim(v.substr(comma + 1));
      fit = "cover";
      for (const char* pre : {"contain:", "tile:", "cover:", "fill:"})
        if (path.starts_with(pre)) {
          fit = std::string(pre).substr(0, std::string(pre).size() - 1);
          path = path.substr(fit.size() + 1);
        }
      flush();
    }
  }
  return !out.byOutput.empty() || !out.fallback.empty();
}

// swaybg main.c: options before the first -o apply to every output, each
// `-o NAME` starts that output's -i/-m; no -m means stretch
bool swaybgArgs(const Argv& a, ShellState& out) {
  std::string output = "*", image, mode;
  auto flush = [&]() {
    if (image.empty()) return;
    const bool first = out.byOutput.empty() && out.fallback.empty();
    if (output == "*") out.fallback = expandHome(image);
    else out.byOutput[output] = expandHome(image);
    if (first) out.fillMode = fillFrom("swaybg", mode.empty() ? "stretch" : mode);  // the first image's mode
  };
  for (size_t i = 1; i < a.size(); ++i) {
    const std::string& s = a[i];
    auto next = [&]() { return i + 1 < a.size() ? a[++i] : std::string(); };
    if (s == "-o" || s == "--output" || s.starts_with("--output=")) {
      flush();
      output = s.starts_with("--output=") ? s.substr(9) : next();
      image.clear();
      mode.clear();
    } else if (s == "-i" || s == "--image") {
      image = next();
    } else if (s.starts_with("--image=")) {
      image = s.substr(8);
    } else if (s == "-m" || s == "--mode") {
      mode = next();
    } else if (s.starts_with("--mode=")) {
      mode = s.substr(7);
    }
  }
  flush();
  return !out.byOutput.empty() || !out.fallback.empty();
}

// mpvpaper [options] <output> <path>: the last two words
bool mpvpaperArgs(const Argv& a, ShellState& out) {
  std::vector<std::string> words;
  for (size_t i = 1; i < a.size(); ++i) {
    if (a[i].starts_with('-')) {
      // options that take a value
      if ((a[i] == "-o" || a[i] == "--mpv-options" || a[i] == "-l" || a[i] == "--layer" || a[i] == "-n" || a[i] == "--slideshow") &&
          i + 1 < a.size())
        ++i;
      continue;
    }
    words.push_back(a[i]);
  }
  if (words.size() < 2) return false;
  const std::string output = words[words.size() - 2], path = expandHome(words.back());
  std::string up = output;
  std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) { return std::toupper(c); });
  if (output == "*" || up == "ALL") out.fallback = path;
  else out.byOutput[output] = path;
  return true;
}

// waypaper config.py: [Settings] (or [State] in ~/.local/state/waypaper/state.ini
// with use_xdg_state) monitors/wallpaper as index-aligned continuation lines
bool waypaperIni(const std::string& text, ShellState& out) {
  std::map<std::string, std::vector<std::string>> kv;
  std::istringstream in(text);
  std::string line, key;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') {
      if (!line.empty() && line[0] == '[') key.clear();
      continue;
    }
    if ((line[0] == ' ' || line[0] == '\t') && !key.empty()) {  // a continuation
      if (const std::string v = trim(line); !v.empty()) kv[key].push_back(v);
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    key = trim(line.substr(0, eq));
    kv[key] = {};
    if (const std::string v = trim(line.substr(eq + 1)); !v.empty()) kv[key].push_back(v);
  }
  const auto& mons = kv["monitors"];
  const auto& walls = kv["wallpaper"];
  for (size_t i = 0; i < walls.size(); ++i) {
    const std::string mon = i < mons.size() ? mons[i] : std::string("All");
    if (mon == "All" || mon == "all") out.fallback = expandHome(walls[i]);
    else out.byOutput[mon] = expandHome(walls[i]);
  }
  if (!kv["fill"].empty()) out.fillMode = fillFrom("waypaper", kv["fill"][0]);
  return !out.byOutput.empty() || !out.fallback.empty();
}

std::vector<Argv> userProcesses() {
  std::vector<Argv> out;
  const uid_t me = getuid();
  DIR* d = opendir("/proc");
  if (!d) return out;
  while (dirent* e = readdir(d)) {
    if (!std::isdigit(static_cast<unsigned char>(e->d_name[0]))) continue;
    const std::string dir = std::string("/proc/") + e->d_name;
    struct stat sb{};
    if (stat(dir.c_str(), &sb) != 0 || sb.st_uid != me) continue;
    const std::string raw = readFile(dir + "/cmdline");
    if (raw.empty()) continue;
    Argv a;
    std::string cur;
    for (char c : raw) {
      if (c == '\0') {
        a.push_back(cur);
        cur.clear();
      } else {
        cur += c;
      }
    }
    if (!cur.empty()) a.push_back(cur);
    if (!a.empty()) out.push_back(std::move(a));
  }
  closedir(d);
  return out;
}

bool readShell(const std::vector<Argv>& procs, ShellState& out) {
  auto running = [&](auto pred) { return std::ranges::any_of(procs, pred); };
  auto named = [](std::initializer_list<const char*> names) {
    return [names](const Argv& a) {
      return !a.empty() && std::ranges::any_of(names, [&](const char* n) { return base(a[0]) == n; });
    };
  };
  const std::string st = stateDir(), cache = cacheDir(), cfg = configDir();
  auto done = [&](const char* name, std::vector<std::string> watch) {
    out.name = name;
    out.watch = std::move(watch);
    return !out.byOutput.empty() || !out.fallback.empty();
  };
  out = ShellState{};

  // ── shells (they draw their own wallpaper and make a palette for it) ──
  if (running([](const Argv& a) { return isQuickshell(a) && hasArg(a, "caelestia"); })) {
    out.fallback = expandHome(trim(readFile(st + "/caelestia/wallpaper/path.txt")));
    out.palette = caelestiaScheme(readFile(st + "/caelestia/scheme.json"), out.dark);
    out.fillMode = 1;
    if (done("caelestia", {st + "/caelestia", st + "/caelestia/wallpaper"})) return true;
  }
  if (running([](const Argv& a) { return (isQuickshell(a) && argContains(a, "/dms")) || (!a.empty() && base(a[0]) == "dms" && hasArg(a, "run")); })) {
    out.palette = dmsColors(readFile(cache + "/DankMaterialShell/dms-colors.json"), out.dark);
    dmsSession(readFile(st + "/DankMaterialShell/session.json"), readFile(cfg + "/DankMaterialShell/settings.json"), out.dark, out);
    if (done("dms", {st + "/DankMaterialShell", cache + "/DankMaterialShell", cfg + "/DankMaterialShell"})) return true;
  }
  if (running([](const Argv& a) { return isQuickshell(a) && hasArg(a, "ii"); })) {
    out.fallback = iiWallpaper(readFile(cfg + "/illogical-impulse/config.json"));
    out.palette = flatPalette(readFile(st + "/quickshell/user/generated/colors.json"), out.dark);
    out.fillMode = 1;
    if (done("ii", {cfg + "/illogical-impulse", st + "/quickshell/user/generated"})) return true;
  }
  if (running([](const Argv& a) { return (!a.empty() && base(a[0]) == "ambxst") || (isQuickshell(a) && argContains(a, "mbxst")); })) {
    ambxstWallpapers(readFile(cache + "/ambxst/wallpapers.json"), out);
    out.palette = flatPalette(readFile(cache + "/ambxst/colors.json"), out.dark);
    if (done("ambxst", {cache + "/ambxst"})) return true;
  }
  // the hyprpanel launcher, or gjs running its bundle
  if (running([](const Argv& a) { return !a.empty() && (base(a[0]) == "hyprpanel" || (base(a[0]) == "gjs" && argContains(a, "hyprpanel"))); })) {
    out.fallback = hyprpanelWallpaper(readFile(cfg + "/hyprpanel/config.json"));
    out.fillMode = 1;  // it hands the picture to swww with its default (crop)
    if (done("hyprpanel", {cfg + "/hyprpanel"})) return true;
  }
  const char* desk = std::getenv("XDG_CURRENT_DESKTOP");
  if ((desk && std::string(desk).find("COSMIC") != std::string::npos) || running(named({"cosmic-bg"}))) {
    const std::string conf = firstExisting({cfg + "/cosmic/com.system76.CosmicBackground/v1/all",
                                            "/usr/share/cosmic/com.system76.CosmicBackground/v1/all"});
    cosmicWallpapers(readFile(st + "/cosmic/com.system76.CosmicBackground/v1/wallpapers"), conf.empty() ? std::string() : readFile(conf), out);
    if (done("cosmic", {st + "/cosmic/com.system76.CosmicBackground/v1", cfg + "/cosmic/com.system76.CosmicBackground/v1"})) return true;
  }
  out = ShellState{};

  // ── wallpaper daemons ──
  for (const auto& a : procs)
    if (!a.empty() && base(a[0]) == "mpvpaper") mpvpaperArgs(a, out);
  bool found = done("mpvpaper", {});
  if (!found && running(named({"awww-daemon", "swww-daemon"}))) {
    const std::string dir = awwwCacheDir();
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
      if (!e.is_regular_file(ec)) continue;
      const auto [image, resize] = awwwCacheEntry(readFile(e.path().string()));
      if (image.empty()) continue;
      out.byOutput[e.path().filename().string()] = image;
      out.fillMode = fillFrom("awww", resize);
    }
    found = done(dir.find("/swww/") != std::string::npos ? "swww" : "awww", {dir});
  }
  if (!found && running(named({"hyprpaper"}))) {
    if (!hyprpaperActive(runCommand("hyprctl hyprpaper listactive 2>/dev/null", 2), out)) {
      out = ShellState{};
      hyprpaperConfig(readFile(cfg + "/hypr/hyprpaper.conf"), out);
    } else {
      ShellState conf;  // the fit mode is only in the file
      hyprpaperConfig(readFile(cfg + "/hypr/hyprpaper.conf"), conf);
      out.fillMode = conf.fillMode;
    }
    found = done("hyprpaper", {cfg + "/hypr"});
  }
  if (!found && running(named({"wpaperd"}))) {
    // the daemon keeps a link to each output's current image
    const std::string links = st + "/wpaperd/wallpapers";
    std::error_code ec;
    for (auto& e : fs::directory_iterator(links, ec)) {
      const fs::path target = fs::read_symlink(e.path(), ec);
      if (!ec && !target.empty()) out.byOutput[e.path().filename().string()] = target.string();
    }
    try {
      const toml::table t = toml::parse_file(cfg + "/wpaperd/config.toml");
      std::string mode = t["default"]["mode"].value_or(t["any"]["mode"].value_or(std::string("center")));
      out.fillMode = fillFrom("wpaperd", mode);
    } catch (...) {
      out.fillMode = 1;
    }
    found = done("wpaperd", {links});
  }
  if (!found && running(named({"swaybg"}))) {
    for (const auto& a : procs)
      if (!a.empty() && base(a[0]) == "swaybg") swaybgArgs(a, out);
    found = done("swaybg", {});
  }
  if (!found) {
    // waypaper is a front end, not a daemon: its settings say what it last set
    const std::string ini = readFile(cfg + "/waypaper/config.ini");
    const std::string state = ini.find("use_xdg_state = True") != std::string::npos ? readFile(st + "/waypaper/state.ini") : std::string();
    if (!ini.empty() && waypaperIni(state.empty() ? ini : state, out)) {
      if (!state.empty()) {
        ShellState fill;
        waypaperIni(ini, fill);
        out.fillMode = fill.fillMode;
      }
      found = done("waypaper", {cfg + "/waypaper", st + "/waypaper"});
    }
  }
  if (!found) return false;
  // these tools make no palette: pywal's, when it was made from this wallpaper
  const std::string walDir = env("PYWAL_CACHE_DIR", cache + "/wal");
  std::string walWall;
  bool dark = true;
  auto pal = pywalPalette(readFile(walDir + "/colors.json"), walWall, dark);
  walWall = expandHome(walWall);
  bool matches = !walWall.empty() && walWall == out.fallback;
  for (auto& [o, p] : out.byOutput) matches = matches || p == walWall;
  if (matches) {
    out.palette = std::move(pal);
    out.dark = dark;
  }
  out.watch.push_back(walDir);
  return true;
}

}  // namespace undershell
