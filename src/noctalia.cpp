// SPDX-License-Identifier: GPL-3.0-or-later
#include "noctalia.hpp"

#include <filesystem>
#include <glib.h>
#include <regex>
#include <toml++/toml.hpp>

namespace undershell {

namespace fs = std::filesystem;

std::string Noctalia::settingsPath() {
  const char* st = std::getenv("NOCTALIA_STATE_HOME");
  if (st && *st) return std::string(st) + "/settings.toml";
  const char* xdg = std::getenv("XDG_STATE_HOME");
  return std::string(xdg && *xdg ? xdg : expandHome("~/.local/state")) + "/noctalia/settings.toml";
}

std::string Noctalia::configDir() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  return std::string(xdg && *xdg ? xdg : expandHome("~/.config")) + "/noctalia";
}

std::string NoctaliaState::wallpaperFor(const std::string& output) const {
  auto it = wallpaperByOutput.find(output);
  if (it != wallpaperByOutput.end() && !it->second.empty()) return it->second;
  return defaultWallpaper;
}

Color NoctaliaState::color(const std::string& roleOrHex, Color fallback) const {
  if (!roleOrHex.empty() && roleOrHex[0] == '#') return Color::fromHex(roleOrHex, fallback);
  auto it = palette.find(roleOrHex);
  return it != palette.end() ? it->second : fallback;
}

int fillModeIndex(const std::string& s) {
  static const char* kModes[] = {"center", "crop", "fit", "stretch", "repeat", "span"};
  for (int i = 0; i < 6; ++i)
    if (s == kModes[i]) return i;
  return 1;
}

bool Noctalia::refreshNoctalia(NoctaliaState& out) {
  std::string exported = runCommand("noctalia config export", 8);
  if (exported.empty()) {
    if (!m_warned) US_WARN("`noctalia config export` returned nothing; retrying (is Noctalia installed?)");
    m_warned = true;
    return false;
  }
  if (m_warned) US_INFO("Noctalia config available");
  m_warned = false;
  if (exported == m_lastExport && m_lastSource == "noctalia") return false;
  m_lastExport = exported;

  toml::table root;
  try {
    root = toml::parse(exported);
  } catch (const toml::parse_error& e) {
    US_WARN("could not parse Noctalia's exported config: {}", e.description());
    return false;
  }

  NoctaliaState s;
  s.source = "noctalia";
  s.palette = out.palette;
  if (auto* wp = root["wallpaper"].as_table()) {
    s.defaultWallpaper = expandHome((*wp)["default"]["path"].value_or(std::string{}));
    s.fillMode = fillModeIndex((*wp)["fill_mode"].value_or(std::string("crop")));
    if (auto* mons = (*wp)["monitors"].as_table()) {
      for (auto& [name, node] : *mons) {
        if (auto* t = node.as_table()) {
          s.wallpaperByOutput[std::string(name.str())] = expandHome((*t)["path"].value_or(std::string{}));
        }
      }
    }
  }
  if (auto* en = root["plugins"]["enabled"].as_array()) {
    for (auto& n : *en)
      if (n.value_or(std::string{}) == "noctalia/wallpaper_depth") s.depthPluginEnabled = true;
  }
  if (auto* d = root["plugin_settings"]["noctalia/wallpaper_depth"].as_table()) {
    s.depthThreshold = (*d)["threshold"].value_or(30.0) / 100.0;
    s.depthFeather = (*d)["feather"].value_or(8.0) / 100.0;
    if (auto i = (*d)["threshold"].value<int64_t>()) s.depthThreshold = static_cast<double>(*i) / 100.0;
    if (auto i = (*d)["feather"].value<int64_t>()) s.depthFeather = static_cast<double>(*i) / 100.0;
  }
  if (auto* th = root["theme"].as_table()) {
    s.themeSource = (*th)["source"].value_or(std::string("builtin"));
    s.themeScheme = (*th)["wallpaper_scheme"].value_or(std::string("m3-content"));
    s.themeMode = (*th)["mode"].value_or(std::string("dark"));
    s.builtin = (*th)["builtin"].value_or(std::string("Noctalia"));
    s.communityPalette = (*th)["community_palette"].value_or(std::string{});
  }
  if (m_lastSource != "noctalia") m_paletteKey.clear();
  refreshPalette(s);
  out = std::move(s);
  return true;
}

// KDE Plasma: its wallpapers and fill mode, and the palette of its colour
// scheme. There is no depth plugin there (depth comes from undershell's own
// model, depthgen.hpp).
bool Noctalia::refreshPlasma(NoctaliaState& out) {
  if (!m_plasma.refresh(m_screens) && m_lastSource == "plasma") return false;
  NoctaliaState s;
  s.source = "plasma";
  const PlasmaWallpapers& w = m_plasma.wallpapers();
  s.wallpaperByOutput = w.byOutput;
  s.defaultWallpaper = w.fallback;
  s.fillMode = w.fillMode;
  s.themeMode = m_plasma.dark() ? "dark" : "light";
  m_paletteKey.clear();
  refreshPalette(s);
  for (auto& [role, c] : m_plasma.palette()) s.palette[role] = c;
  if (w.slideshow && w.byOutput.empty() && w.fallback.empty())
    US_INFO("plasma: the wallpaper is a slideshow; its current image is not known, so there is no depth");
  US_INFO("plasma: wallpaper {} ({} screen(s)), fill {}, palette {}", fs::path(s.defaultWallpaper).filename().string(),
          s.wallpaperByOutput.size(), s.fillMode, m_plasma.palette().empty() ? "default" : "from the colour scheme");
  out = std::move(s);
  return true;
}

std::string Noctalia::pickSource() {
  // inside Plasma, Plasma draws the wallpaper whatever else is installed
  if (Plasma::session()) return "plasma";
  if (gchar* p = g_find_program_in_path("noctalia"); p) {
    g_free(p);
    return "noctalia";
  }
  return Plasma::present() ? "plasma" : "none";
}

bool Noctalia::refresh() {
  const std::string source = pickSource();
  bool changed = false;
  if (source == "noctalia") {
    changed = refreshNoctalia(m_base);
    m_ready = !m_lastExport.empty();
  } else if (source == "plasma") {
    changed = refreshPlasma(m_base);
    m_ready = true;
  } else {
    changed = m_lastSource != "none";
    if (changed) {
      NoctaliaState s;
      m_paletteKey.clear();
      refreshPalette(s);
      m_base = std::move(s);
      if (m_manual.empty()) US_INFO("no Noctalia or Plasma here: name the wallpaper in [general] wallpaper for depth and per-wallpaper layouts");
    }
    m_ready = true;
  }
  if (m_ready) m_lastSource = source;
  if (!changed) return false;
  compose();
  return true;
}

void Noctalia::compose() {
  NoctaliaState s = m_base;
  if (!m_manual.empty()) {
    s.manualWallpaper = true;
    s.defaultWallpaper = m_manual;
    s.wallpaperByOutput.clear();
    if (!m_manualFill.empty()) s.fillMode = fillModeIndex(m_manualFill);
  }
  m_state = std::move(s);
}

bool Noctalia::setManual(const std::string& wallpaper, const std::string& fill) {
  const std::string w = wallpaper.empty() ? std::string() : expandHome(wallpaper);
  if (w == m_manual && fill == m_manualFill) return false;
  m_manual = w;
  m_manualFill = fill;
  compose();
  return true;
}

bool Noctalia::setScreens(std::map<std::string, std::pair<int, int>> screens) {
  if (screens == m_screens) return false;
  m_screens = std::move(screens);
  return true;
}



// Palette roles as Noctalia renders them. Wallpaper themes are regenerated
// with Noctalia's own generator; everything else falls back to the gtk4
// template output (accent) plus Ryoku's paper-and-ink defaults.
void Noctalia::refreshPalette(NoctaliaState& s) {
  const bool dark = s.themeMode != "light";
  std::string key = s.themeSource + "|" + s.themeScheme + "|" + s.themeMode + "|" + s.defaultWallpaper + "|" +
                    s.builtin + "|" + s.communityPalette;
  if (key == m_paletteKey && !s.palette.empty()) return;
  m_paletteKey = key;

  std::map<std::string, Color> pal = {
      {"primary", Color::fromHex("#e2342a")},   {"secondary", Color::fromHex("#c9b89a")},
      {"tertiary", Color::fromHex("#8fb0c4")},  {"surface", Color::fromHex("#0b0b0c")},
      {"on_surface", Color::fromHex("#f4f1ea")}, {"surface_variant", Color::fromHex("#1a1a1c")},
      {"outline", Color::fromHex("#5a5650")},   {"error", Color::fromHex("#ff6b6b")},
  };
  std::string css = readFile(expandHome("~/.config/gtk-4.0/noctalia.css"));
  static const std::regex kDef(R"(@define-color\s+([\w_]+)\s+(#[0-9a-fA-F]{6}))");
  for (std::sregex_iterator it(css.begin(), css.end(), kDef), end; it != end; ++it) {
    std::string n = (*it)[1];
    Color c = Color::fromHex((*it)[2].str());
    if (n == "accent_color") pal["primary"] = c;
    if (n == "window_bg_color") pal["surface"] = c;
    if (n == "window_fg_color") pal["on_surface"] = c;
    if (n == "card_bg_color") pal["surface_variant"] = c;
  }

  std::string json;
  if (s.themeSource == "wallpaper" && !s.defaultWallpaper.empty()) {
    gchar* quoted = g_shell_quote(s.defaultWallpaper.c_str());
    gchar* scheme = g_shell_quote(s.themeScheme.c_str());
    json = runCommand(std::format("noctalia theme {} --scheme {} {}", quoted, scheme, dark ? "--dark" : "--light"), 20);
    g_free(quoted);
    g_free(scheme);
  } else if (s.themeSource == "community" && !s.communityPalette.empty()) {
    json = readFile(expandHome("~/.local/state/noctalia/community-palettes/" + s.communityPalette + ".json"));
  }
  static const std::regex kPair(R"re("([a-z_]+)"\s*:\s*"(#[0-9a-fA-F]{6})")re");
  int found = 0;
  for (std::sregex_iterator it(json.begin(), json.end(), kPair), end; it != end; ++it) {
    pal[(*it)[1].str()] = Color::fromHex((*it)[2].str());
    ++found;
  }
  // community palette files use the v4 "mPrimary" naming
  static const std::regex kLegacy(R"re("m([A-Z][A-Za-z]+)"\s*:\s*"(#[0-9a-fA-F]{6})")re");
  for (std::sregex_iterator it(json.begin(), json.end(), kLegacy), end; it != end; ++it) {
    std::string camel = (*it)[1];
    std::string snake;
    for (char c : camel) {
      if (std::isupper(static_cast<unsigned char>(c)) && !snake.empty()) snake += '_';
      snake += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    pal[snake] = Color::fromHex((*it)[2].str());
    ++found;
  }
  US_INFO("palette: source={} roles={} primary=({:.2f},{:.2f},{:.2f})", s.themeSource.empty() ? s.source : s.themeSource, found, pal["primary"].r,
          pal["primary"].g, pal["primary"].b);
  s.palette = std::move(pal);
}

}  // namespace undershell
