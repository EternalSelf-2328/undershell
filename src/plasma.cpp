// SPDX-License-Identifier: GPL-3.0-or-later
#include "plasma.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <glib.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <sstream>

namespace undershell {

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r"), e = s.find_last_not_of(" \t\r");
  return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

// KConfig's value escapes: \s \t \n \r \\ and \xNN
std::string unescape(const std::string& v) {
  std::string out;
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] != '\\' || i + 1 >= v.size()) {
      out += v[i];
      continue;
    }
    const char c = v[++i];
    if (c == 's') out += ' ';
    else if (c == 't') out += '\t';
    else if (c == 'n') out += '\n';
    else if (c == 'r') out += '\r';
    else if (c == 'x' && i + 2 < v.size() && std::isxdigit(static_cast<unsigned char>(v[i + 1])) &&
             std::isxdigit(static_cast<unsigned char>(v[i + 2]))) {
      out += static_cast<char>(std::stoi(v.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else out += c;
  }
  return out;
}

// "r,g,b" (KDE's colours), or #rrggbb
bool kdeColor(const std::string& v, Color& out) {
  if (v.starts_with('#')) {
    out = Color::fromHex(v);
    return true;
  }
  int r = 0, g = 0, b = 0;
  if (std::sscanf(v.c_str(), "%d,%d,%d", &r, &g, &b) != 3) return false;
  out = Color{std::clamp(r, 0, 255) / 255.0F, std::clamp(g, 0, 255) / 255.0F, std::clamp(b, 0, 255) / 255.0F, 1};
  return true;
}

float luma(Color c) { return 0.2126F * c.r + 0.7152F * c.g + 0.0722F * c.b; }

Color mix(Color a, Color b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1}; }

// Plasma's image wallpaper keeps Qt's Image.fillMode: Stretch 0, Fit 1,
// Crop 2, Tile 3, TileVertically 4, TileHorizontally 5, Pad 6 (centred)
int fillFromQt(const std::string& v) {
  if (v.empty()) return 1;  // Plasma's default: scaled and cropped
  switch (std::atoi(v.c_str())) {
    case 0: return 3;
    case 1: return 2;
    case 3:
    case 4:
    case 5: return 4;
    case 6: return 0;
    default: return 1;
  }
}

std::string pathFromValue(const std::string& v) {
  std::string p = trim(v);
  if (p.starts_with("file:")) {
    if (gchar* f = g_filename_from_uri(p.c_str(), nullptr, nullptr); f) {
      p = f;
      g_free(f);
    } else {
      p = p.substr(5);
      while (p.size() > 1 && p[0] == '/' && p[1] == '/') p.erase(0, 1);
    }
  }
  return expandHome(p);
}

std::string plasmaDefaultWallpaper() {
  for (const char* p : {"/usr/share/wallpapers/Next", "/usr/local/share/wallpapers/Next"})
    if (fs::is_directory(p)) return p;
  return {};
}

}  // namespace

KConfig parseKConfig(const std::string& text) {
  KConfig out;
  std::string group;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    if (line[0] == '[') {
      // "[a][b][c]" → "a][b][c"; a trailing [$i] marks the group immutable
      const auto end = line.rfind(']');
      group = end == std::string::npos ? line.substr(1) : line.substr(1, end - 1);
      if (group.ends_with("][$i")) group.resize(group.size() - 4);
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(line.substr(0, eq));
    // "Key[$e]" (expand) and "Key[de]" (a translation): the plain key only
    if (const auto br = key.find('['); br != std::string::npos) {
      const std::string tag = key.substr(br);
      key.resize(br);
      if (tag.find('$') == std::string::npos) continue;
    }
    out[group][key] = unescape(trim(line.substr(eq + 1)));
  }
  return out;
}

std::string plasmaPackageImage(const std::string& path, int w, int h, bool dark) {
  std::error_code ec;
  if (path.empty() || !fs::is_directory(path, ec)) return path;
  fs::path dir = fs::path(path) / "contents" / (dark ? "images_dark" : "images");
  if (!fs::is_directory(dir, ec)) dir = fs::path(path) / "contents" / "images";
  if (w <= 0 || h <= 0) {
    w = 1920;
    h = 1080;
  }
  // the shape first, then the size: a 16:9 screen takes the 16:9 image even
  // if a portrait one is nearer in pixels
  static const std::regex kSize(R"((\d+)x(\d+)\.\w+)");
  std::string best;
  double bestScore = 1e18;
  for (auto& e : fs::directory_iterator(dir, ec)) {
    std::smatch m;
    const std::string name = e.path().filename().string();
    if (!std::regex_match(name, m, kSize)) continue;
    const double iw = std::stod(m[1].str()), ih = std::stod(m[2].str());
    if (iw <= 0 || ih <= 0) continue;
    const double aspect = std::abs(std::log((iw / ih) / (static_cast<double>(w) / h)));
    double size = std::log((iw * ih) / (static_cast<double>(w) * h));
    if (size < 0) size *= -3;  // smaller than the screen is worse than larger
    const double score = aspect * 10 + size;
    if (score < bestScore) {
      bestScore = score;
      best = e.path().string();
    }
  }
  return best;
}

std::map<std::string, std::string> plasmaScreenNumbers(const std::string& kwinOutputs, const std::string& plasmashellrc,
                                                       const std::map<std::string, std::pair<int, int>>& screens) {
  std::map<std::string, std::string> out;
  try {
    const auto j = nlohmann::json::parse(kwinOutputs);
    std::vector<std::string> names;  // outputIndex → connector
    const nlohmann::json* setups = nullptr;
    for (auto& part : j) {
      if (part.value("name", "") == "outputs")
        for (auto& o : part["data"]) names.push_back(o.value("connectorName", ""));
      if (part.value("name", "") == "setups") setups = &part["data"];
    }
    // the setup of exactly the screens there are now, else the last one that
    // has them all, else the first
    const nlohmann::json* pick = nullptr;
    int pickScore = -1;
    if (setups)
      for (auto& setup : *setups) {
        std::set<std::string> on;
        for (auto& o : setup["outputs"]) {
          const size_t i = o.value("outputIndex", 0);
          if (o.value("enabled", true) && i < names.size()) on.insert(names[i]);
        }
        bool all = true;
        for (auto& [n, sz] : screens) all = all && on.contains(n);
        const int score = all ? (on.size() == screens.size() ? 2 : 1) : 0;
        if (score >= pickScore) {
          pickScore = score;
          pick = &setup;
        }
        if (score == 2) break;
      }
    if (pick) {
      std::vector<std::pair<int, std::string>> order;
      for (auto& o : (*pick)["outputs"]) {
        const size_t i = o.value("outputIndex", 0);
        if (o.value("enabled", true) && i < names.size()) order.emplace_back(o.value("priority", 0), names[i]);
      }
      std::stable_sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first < b.first; });
      for (size_t i = 0; i < order.size(); ++i) out[std::to_string(i)] = order[i].second;
    }
  } catch (...) {
  }
  if (out.empty()) {
    const KConfig shell = parseKConfig(plasmashellrc);
    if (auto g = shell.find("ScreenConnectors"); g != shell.end()) out = g->second;
  }
  return out;
}

PlasmaWallpapers plasmaWallpapers(const std::string& appletsrc, const std::map<std::string, std::string>& connectors,
                                  const std::string& activities, const std::map<std::string, std::pair<int, int>>& screens,
                                  bool dark) {
  PlasmaWallpapers out;
  const KConfig applets = parseKConfig(appletsrc);
  const KConfig acts = parseKConfig(activities);
  std::string current;
  if (auto g = acts.find("main"); g != acts.end())
    if (auto k = g->second.find("currentActivity"); k != g->second.end()) current = k->second;

  bool fillSet = false;
  static const std::regex kContainment(R"(Containments\]\[(\d+))");
  for (const auto& [group, keys] : applets) {
    std::smatch m;
    if (!std::regex_match(group, m, kContainment)) continue;
    auto get = [&](const char* k) {
      auto it = keys.find(k);
      return it == keys.end() ? std::string() : it->second;
    };
    // a desktop (not a panel) of this activity
    if (get("plugin") == "org.kde.panel" || (!get("formfactor").empty() && get("formfactor") != "0")) continue;
    if (!current.empty() && !get("activityId").empty() && get("activityId") != current) continue;
    const std::string plugin = get("wallpaperplugin");
    if (plugin.empty()) continue;
    const std::string base = group + "][Wallpaper][" + plugin + "][General";
    const auto wg = applets.find(base);
    auto wget = [&](const char* k) {
      if (wg == applets.end()) return std::string();
      auto it = wg->second.find(k);
      return it == wg->second.end() ? std::string() : it->second;
    };
    std::string output;
    if (auto c = connectors.find(get("lastScreen")); c != connectors.end()) output = c->second;
    else if (screens.size() == 1 && (get("lastScreen") == "0" || get("lastScreen").empty())) output = screens.begin()->first;
    int sw = 0, sh = 0;
    if (auto s = screens.find(output); s != screens.end()) std::tie(sw, sh) = s->second;
    std::string image;
    if (plugin == "org.kde.image") {
      const std::string v = wget("Image");
      image = plasmaPackageImage(v.empty() ? plasmaDefaultWallpaper() : pathFromValue(v), sw, sh, dark);
    } else if (plugin == "org.kde.slideshow") {
      out.slideshow = true;
    } else {
      continue;  // a plain colour, the picture of the day, …: no image to read
    }
    if (!fillSet && (plugin == "org.kde.image" || plugin == "org.kde.slideshow")) {
      out.fillMode = fillFromQt(wget("FillMode"));
      fillSet = true;
    }
    if (image.empty()) continue;
    if (output.empty()) {
      if (out.fallback.empty()) out.fallback = image;
    } else {
      out.byOutput[output] = image;
      if (out.fallback.empty() || get("lastScreen") == "0") out.fallback = image;
    }
  }
  return out;
}

std::map<std::string, Color> plasmaPalette(const std::string& kdeglobals, const std::string& schemeText, bool& dark) {
  KConfig cfg = parseKConfig(schemeText);
  for (auto& [g, keys] : parseKConfig(kdeglobals))
    for (auto& [k, v] : keys) cfg[g][k] = v;
  auto color = [&](const char* group, const char* key, Color& out) {
    auto g = cfg.find(group);
    if (g == cfg.end()) return false;
    auto k = g->second.find(key);
    return k != g->second.end() && kdeColor(k->second, out);
  };
  Color window, text;
  if (!color("Colors:Window", "BackgroundNormal", window) || !color("Colors:Window", "ForegroundNormal", text)) return {};
  std::map<std::string, Color> pal;
  Color c;
  Color accent = Color::fromHex("#3daee9");  // Breeze's highlight
  if (!color("General", "AccentColor", accent)) color("Colors:Selection", "BackgroundNormal", accent);
  pal["primary"] = accent;
  if (color("Colors:Selection", "ForegroundNormal", c)) pal["on_primary"] = c;
  pal["surface"] = window;
  pal["on_surface"] = text;
  pal["surface_variant"] = color("Colors:View", "BackgroundAlternate", c) || color("Colors:Button", "BackgroundNormal", c) ? c : mix(window, text, 0.08F);
  if (color("Colors:View", "BackgroundNormal", c)) pal["surface_container"] = c;
  if (color("Colors:Window", "ForegroundInactive", c)) {
    pal["outline"] = c;
    pal["on_surface_variant"] = c;
  }
  // the scheme has no second and third accent: the accent softened towards
  // the text, and the link colour
  pal["secondary"] = mix(accent, text, 0.4F);
  pal["tertiary"] = color("Colors:View", "ForegroundLink", c) ? c : mix(accent, text, 0.7F);
  if (color("Colors:View", "ForegroundNegative", c)) pal["error"] = c;
  dark = luma(window) < 0.5F;
  return pal;
}

std::string Plasma::configDir() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  return xdg && *xdg ? xdg : expandHome("~/.config");
}

bool Plasma::present() { return fs::exists(configDir() + "/plasma-org.kde.plasma.desktop-appletsrc"); }

bool Plasma::session() {
  const char* d = std::getenv("XDG_CURRENT_DESKTOP");
  if (!d) return false;
  std::string s = d;
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return std::tolower(ch); });
  return s.find("kde") != std::string::npos || s.find("plasma") != std::string::npos;
}

bool Plasma::watches(const std::string& name) {
  return name == "plasma-org.kde.plasma.desktop-appletsrc" || name == "kdeglobals" || name == "plasmashellrc" ||
         name == "kactivitymanagerdrc" || name == "kwinoutputconfig.json";
}

bool Plasma::refresh(const std::map<std::string, std::pair<int, int>>& screens) {
  const std::string dir = configDir();
  const std::string applets = readFile(dir + "/plasma-org.kde.plasma.desktop-appletsrc");
  const std::string shell = readFile(dir + "/plasmashellrc");
  const std::string acts = readFile(dir + "/kactivitymanagerdrc");
  const std::string globals = readFile(dir + "/kdeglobals");
  const std::string kwin = readFile(dir + "/kwinoutputconfig.json");
  // the scheme kdeglobals names (Plasma copies its colours in, but not always
  // all of them), else Breeze
  std::string scheme = "BreezeLight";
  if (auto g = parseKConfig(globals); g.contains("General") && g["General"].contains("ColorScheme")) scheme = g["General"]["ColorScheme"];
  std::string schemeText;
  for (const std::string& d : {expandHome("~/.local/share/color-schemes"), std::string("/usr/share/color-schemes")})
    if (schemeText.empty()) schemeText = readFile(d + "/" + scheme + ".colors");
  std::string sizes;
  for (auto& [n, s] : screens) sizes += std::format("{}={}x{};", n, s.first, s.second);
  std::string key = applets + '\x1f' + shell + '\x1f' + acts + '\x1f' + globals + '\x1f' + schemeText + '\x1f' + kwin + '\x1f' + sizes;
  if (key == m_key) return false;
  m_key = std::move(key);
  bool dark = true;
  m_palette = plasmaPalette(globals, schemeText, dark);
  m_dark = m_palette.empty() ? true : dark;
  m_walls = plasmaWallpapers(applets, plasmaScreenNumbers(kwin, shell, screens), acts, screens, m_dark);
  return true;
}

}  // namespace undershell
