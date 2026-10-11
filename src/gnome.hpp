// SPDX-License-Identifier: GPL-3.0-or-later
// What undershell reads from GNOME: the wallpaper (org.gnome.desktop.background
// picture-uri, picture-uri-dark under prefer-dark, picture-options) and a
// palette from the accent colour (org.gnome.desktop.interface accent-color,
// GNOME 47+) and the colour scheme, over libadwaita's own colours. Changes
// come as dconf's Notify signal on the session bus.
#pragma once

#include "common.hpp"

#include <map>
#include <string>

struct sd_bus;
struct sd_bus_slot;

namespace undershell {

struct GnomeSettings {
  std::string wallpaper;  // the image path ("" for none, a colour, or an XML slideshow)
  int fillMode = 1;       // center crop fit stretch repeat span
  bool dark = false;
  std::map<std::string, Color> palette;
};

// From `gsettings get` of picture-uri, picture-uri-dark, picture-options,
// color-scheme and accent-color, one value per line ('quoted').
GnomeSettings parseGnomeSettings(const std::string& lines);

class Gnome {
public:
  static bool session();  // XDG_CURRENT_DESKTOP names GNOME
  // Re-reads the settings (one gsettings run); true when anything changed.
  bool refresh();
  [[nodiscard]] const GnomeSettings& settings() const { return m_settings; }

private:
  GnomeSettings m_settings;
  std::string m_key;
};

// dconf's Notify for /org/gnome/desktop/: poll fd(), call dispatch(); changed()
// says whether one came since it was last asked.
class DconfWatch {
public:
  ~DconfWatch();
  bool start();
  [[nodiscard]] int fd() const;
  void dispatch();
  bool changed() {
    const bool c = m_changed;
    m_changed = false;
    return c;
  }
  void mark() { m_changed = true; }

private:
  sd_bus* m_bus = nullptr;
  sd_bus_slot* m_slot = nullptr;
  bool m_changed = false;
};

}  // namespace undershell
