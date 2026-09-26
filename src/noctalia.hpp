// SPDX-License-Identifier: GPL-3.0-or-later
// What undershell reads from Noctalia: the effective config (`noctalia
// config export`) for wallpapers, fill mode and the wallpaper_depth plugin's
// settings, and the theme palette (`noctalia theme <wallpaper> --scheme ...`),
// so the widgets retint and mask exactly like Noctalia's own desktop widgets.
#pragma once

#include "common.hpp"

#include <map>
#include <string>

namespace undershell {

struct NoctaliaState {
  std::map<std::string, std::string> wallpaperByOutput;
  std::string defaultWallpaper;
  int fillMode = 1;  // center crop fit stretch repeat span (Noctalia's enum order)
  bool depthPluginEnabled = false;
  double depthThreshold = 0.30;
  double depthFeather = 0.08;
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

  static std::string settingsPath();
  static std::string configDir();

private:
  void refreshPalette();
  NoctaliaState m_state;
  std::string m_paletteKey;
  std::string m_lastExport;
};

}  // namespace undershell
