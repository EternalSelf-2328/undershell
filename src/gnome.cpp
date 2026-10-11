// SPDX-License-Identifier: GPL-3.0-or-later
#include "gnome.hpp"

#include <glib.h>
#include <systemd/sd-bus.h>

#include <algorithm>
#include <sstream>
#include <vector>

namespace undershell {

namespace {

std::string unquote(std::string v) {
  while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
  if (v.size() >= 2 && v.front() == '\'' && v.back() == '\'') v = v.substr(1, v.size() - 2);
  return v;
}

Color mix(Color a, Color b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1}; }

// libadwaita's accent colours (adw-accent-color.c)
Color accentHex(const std::string& name) {
  static const std::map<std::string, const char*> kAccents = {
      {"blue", "#3584e4"}, {"teal", "#2190a4"},   {"green", "#3a944a"}, {"yellow", "#c88800"}, {"orange", "#ed5b00"},
      {"red", "#e62d42"},  {"pink", "#d56199"},   {"purple", "#9141ac"}, {"slate", "#6f8396"}};
  auto it = kAccents.find(name);
  return Color::fromHex(it != kAccents.end() ? it->second : "#3584e4");
}

// picture-options: zoom (crop) is GNOME's default
int fillOf(const std::string& o) {
  if (o == "centered") return 0;
  if (o == "scaled") return 2;
  if (o == "stretched") return 3;
  if (o == "wallpaper") return 4;
  if (o == "spanned") return 5;
  return 1;
}

int onNotify(sd_bus_message*, void* ud, sd_bus_error*) {
  static_cast<DconfWatch*>(ud)->mark();
  return 0;
}

}  // namespace

GnomeSettings parseGnomeSettings(const std::string& lines) {
  std::vector<std::string> v;
  std::istringstream in(lines);
  std::string line;
  while (std::getline(in, line)) v.push_back(unquote(line));
  v.resize(5);
  GnomeSettings s;
  s.dark = v[3] == "prefer-dark";
  const std::string options = v[2].empty() ? "zoom" : v[2];
  // GNOME takes the dark picture only under prefer-dark (background.js)
  const std::string uri = s.dark && !v[1].empty() ? v[1] : v[0];
  if (options != "none" && !uri.empty()) {
    if (gchar* p = g_filename_from_uri(uri.c_str(), nullptr, nullptr); p) {
      s.wallpaper = p;
      g_free(p);
    } else if (uri[0] == '/') {
      s.wallpaper = uri;
    }
    // an XML slideshow animates between pictures: no single image
    if (s.wallpaper.ends_with(".xml")) s.wallpaper.clear();
  }
  s.fillMode = fillOf(options);
  const Color accent = accentHex(v[4].empty() ? "blue" : v[4]);
  // libadwaita's window colours, the accent over them
  const Color surface = Color::fromHex(s.dark ? "#222226" : "#fafafb");
  const Color text = Color::fromHex(s.dark ? "#ffffff" : "#2e2e33");
  s.palette = {{"primary", accent},
               {"on_primary", Color::fromHex("#ffffff")},
               {"secondary", mix(accent, text, 0.4F)},
               {"tertiary", mix(accent, text, 0.7F)},
               {"surface", surface},
               {"on_surface", text},
               {"surface_variant", Color::fromHex(s.dark ? "#2e2e32" : "#ebebed")},
               {"surface_container", Color::fromHex(s.dark ? "#2e2e32" : "#ffffff")},
               {"outline", Color::fromHex(s.dark ? "#9a9a9e" : "#77767b")},
               {"on_surface_variant", Color::fromHex(s.dark ? "#c0bfc4" : "#5e5c64")},
               {"error", Color::fromHex(s.dark ? "#ff7b63" : "#e01b24")}};
  return s;
}

bool Gnome::session() {
  const char* d = std::getenv("XDG_CURRENT_DESKTOP");
  if (!d) return false;
  std::string s = d;
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
  return s.find("GNOME") != std::string::npos;
}

bool Gnome::refresh() {
  // one run; accent-color is GNOME 47+ (an older one answers on stderr)
  const std::string out = runCommand(
      "gsettings get org.gnome.desktop.background picture-uri; gsettings get org.gnome.desktop.background picture-uri-dark; "
      "gsettings get org.gnome.desktop.background picture-options; gsettings get org.gnome.desktop.interface color-scheme; "
      "gsettings get org.gnome.desktop.interface accent-color 2>/dev/null || echo \"'blue'\"",
      5);
  if (out == m_key) return false;
  m_key = out;
  m_settings = parseGnomeSettings(out);
  return true;
}

DconfWatch::~DconfWatch() {
  if (m_slot) sd_bus_slot_unref(m_slot);
  if (m_bus) sd_bus_flush_close_unref(m_bus);
}

bool DconfWatch::start() {
  if (m_bus) return true;
  if (sd_bus_open_user(&m_bus) < 0) {
    m_bus = nullptr;
    return false;
  }
  // the match dconf itself uses for the desktop's keys
  if (sd_bus_add_match(m_bus, &m_slot,
                       "type='signal',interface='ca.desrt.dconf.Writer',path='/ca/desrt/dconf/Writer/user',arg0path='/org/gnome/desktop/'",
                       onNotify, this) < 0) {
    sd_bus_flush_close_unref(m_bus);
    m_bus = nullptr;
    return false;
  }
  return true;
}

int DconfWatch::fd() const { return m_bus ? sd_bus_get_fd(m_bus) : -1; }

void DconfWatch::dispatch() {
  if (!m_bus) return;
  while (sd_bus_process(m_bus, nullptr) > 0) {
  }
}

}  // namespace undershell
