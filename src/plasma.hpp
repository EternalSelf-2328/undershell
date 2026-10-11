// SPDX-License-Identifier: GPL-3.0-or-later
// What undershell reads from KDE Plasma when Noctalia is not there: the
// wallpaper of each screen's desktop (plasma-org.kde.plasma.desktop-appletsrc,
// screens named in plasmashellrc, the current activity in
// kactivitymanagerdrc), its fill mode, and a palette from the colour scheme
// and accent in kdeglobals.
#pragma once

#include "common.hpp"

#include <map>
#include <string>

namespace undershell {

// One KConfig file: group ("Containments][1][Wallpaper…" without the outer
// brackets) → key → value, with the escapes undone.
using KConfig = std::map<std::string, std::map<std::string, std::string>>;
KConfig parseKConfig(const std::string& text);

struct PlasmaWallpapers {
  std::map<std::string, std::string> byOutput;  // connector → image path
  std::string fallback;                         // a desktop whose screen is not named
  int fillMode = 1;                             // undershell's order: center crop fit stretch repeat
  bool slideshow = false;                       // a slideshow: Plasma keeps the current image to itself
};

// Plasma's screen numbers (a desktop's lastScreen) → connectors: Plasma 6
// numbers the screens by KWin's output priority (kwinoutputconfig.json, the
// setup of the screens connected now); Plasma 5 listed them in plasmashellrc.
std::map<std::string, std::string> plasmaScreenNumbers(const std::string& kwinOutputs, const std::string& plasmashellrc,
                                                       const std::map<std::string, std::pair<int, int>>& screens);
// From the texts of the files; `screens` gives each connector's pixel size,
// to pick an image out of a wallpaper package; `dark` its dark variant.
PlasmaWallpapers plasmaWallpapers(const std::string& appletsrc, const std::map<std::string, std::string>& screenNumbers,
                                  const std::string& activities, const std::map<std::string, std::pair<int, int>>& screens,
                                  bool dark);
// The image of a wallpaper package (a folder with contents/images/WxH.ext)
// closest to a w×h screen; a plain file comes back as it is.
std::string plasmaPackageImage(const std::string& path, int w, int h, bool dark);
// Palette roles from kdeglobals (over the colour scheme it names). Empty when
// there is no colour information at all.
std::map<std::string, Color> plasmaPalette(const std::string& kdeglobals, const std::string& schemeText, bool& dark);

class Plasma {
public:
  static std::string configDir();  // ~/.config
  static bool present();           // a Plasma desktop config to read
  static bool session();           // running inside Plasma (XDG_CURRENT_DESKTOP)
  // Re-reads the files; true when anything changed.
  bool refresh(const std::map<std::string, std::pair<int, int>>& screens);
  [[nodiscard]] const PlasmaWallpapers& wallpapers() const { return m_walls; }
  [[nodiscard]] const std::map<std::string, Color>& palette() const { return m_palette; }
  [[nodiscard]] bool dark() const { return m_dark; }
  // the files whose change means "read again"
  static bool watches(const std::string& name);

private:
  PlasmaWallpapers m_walls;
  std::map<std::string, Color> m_palette;
  bool m_dark = true;
  std::string m_key;
};

}  // namespace undershell
