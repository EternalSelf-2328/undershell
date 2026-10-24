// SPDX-License-Identifier: GPL-3.0-or-later
// What undershell reads from Noctalia: the effective config (`noctalia
// config export`) for wallpapers, fill mode and the wallpaper_depth plugin's
// settings, and the theme palette (`noctalia theme <wallpaper> --scheme ...`),
// so the widgets retint and mask exactly like Noctalia's own desktop widgets.
// Without Noctalia (or inside a Plasma session) the same state comes from KDE
// Plasma (plasma.hpp); a wallpaper named in [general] overrides either.
#pragma once

#include "common.hpp"
#include "gnome.hpp"
#include "plasma.hpp"
#include "shells.hpp"

#include <map>
#include <set>
#include <optional>
#include <string>

namespace undershell {

struct NoctaliaState {
  std::string source = "none";  // where it came from: noctalia | plasma | none (+ "manual" wallpaper)
  bool manualWallpaper = false;
  std::set<std::string> sceneOutputs;  // outputs where a scene stands for the wallpaper (its still in wallpaperByOutput)
  std::map<std::string, std::string> wallpaperByOutput;
  std::string defaultWallpaper;
  int fillMode = 1;  // center crop fit stretch repeat span (Noctalia's enum order)
  // the wallpaper_depth plugin's threshold and feather (0-100), if it has
  // settings: undershell's [general] depth_threshold / depth_feather start
  // from them
  std::optional<double> pluginThreshold, pluginFeather;
  std::string themeSource, themeScheme, themeMode, builtin, communityPalette;
  std::map<std::string, Color> palette;

  [[nodiscard]] std::string wallpaperFor(const std::string& output) const;
  [[nodiscard]] Color color(const std::string& roleOrHex) const { return color(roleOrHex, Color::fromHex("#e2342a")); }
  [[nodiscard]] Color color(const std::string& roleOrHex, Color fallback) const;
};

class Noctalia {
public:
  // Re-reads the effective config and, when the theme inputs changed, the
  // palette. Returns true if anything the widgets use changed.
  bool refresh();
  [[nodiscard]] const NoctaliaState& state() const { return m_state; }
  // true once the source has been read (with Noctalia: an export)
  [[nodiscard]] bool ready() const { return m_ready; }
  // a wallpaper from [general] (empty: none); true when it changed
  bool setManual(const std::string& wallpaper, const std::string& fill);
  // outputs where undershell draws a scene: its still stands for their
  // wallpaper; true when it changed
  bool setSceneStills(std::map<std::string, std::string> stills);
  // each screen's connector and pixel size (picks images out of Plasma's
  // wallpaper packages); true when they changed
  bool setScreens(std::map<std::string, std::pair<int, int>> screens);
  // which source refresh() reads now ("shell": `shell` says which one)
  static std::string pickSource(ShellState* shell = nullptr);
  // directories to watch for the current source (beyond Noctalia's and
  // Plasma's own), and whether it must be asked again every few seconds (a
  // daemon with no file, or a shell that may start later)
  [[nodiscard]] const std::vector<std::string>& watchDirs() const { return m_watchDirs; }
  [[nodiscard]] bool polls() const { return m_lastSource == "shell" || m_lastSource == "none"; }

  static std::string settingsPath();
  static std::string configDir();

private:
  bool refreshNoctalia(NoctaliaState& s);
  bool refreshPlasma(NoctaliaState& s);
  bool refreshGnome(NoctaliaState& s);
  bool refreshShell(NoctaliaState& s, ShellState shell);
  void refreshPalette(NoctaliaState& s);
  void compose();
  NoctaliaState m_base;   // what the source says
  std::map<std::string, std::string> m_stills;  // output -> a scene's still
  NoctaliaState m_state;  // with the manual wallpaper over it
  std::string m_paletteKey;
  std::string m_lastExport;
  std::string m_lastSource;
  std::string m_shellKey;
  std::vector<std::string> m_watchDirs;
  std::string m_manual, m_manualFill;
  std::map<std::string, std::pair<int, int>> m_screens;
  Plasma m_plasma;
  Gnome m_gnome;
  bool m_ready = false;
  bool m_warned = false;
};

int fillModeIndex(const std::string& s);

}  // namespace undershell
