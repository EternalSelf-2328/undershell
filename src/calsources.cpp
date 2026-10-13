// SPDX-License-Identifier: GPL-3.0-or-later
// Calendar sources (see calsources.hpp).
#include "calsources.hpp"

#include "common.hpp"

#include <glib.h>
#include <glob.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>

#ifdef US_HAVE_SQLITE
#include <sqlite3.h>
#endif

namespace fs = std::filesystem;

namespace undershell {

namespace {
std::string env(const char* name, const char* fallback) {
  const char* v = std::getenv(name);
  return v && *v ? std::string(v) : expandHome(fallback);
}

std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  return s.substr(i);
}

std::string hexColor(std::string c) {
  c = trim(c);
  if (c.size() >= 7 && c[0] == '#' && std::all_of(c.begin() + 1, c.begin() + 7, [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)); }))
    return c.substr(0, 7);
  return {};
}

// a folder's own colour (vdirsyncer keeps one in a file named "color")
std::string folderColor(const std::string& dir) { return hexColor(readFile(dir + "/color")); }

// the files a source stands for
std::vector<std::string> filesOf(const CalSource& s) {
  std::vector<std::string> out;
  std::error_code ec;
  if (!s.folder) {
    out.push_back(s.path);
    return out;
  }
  for (const auto& e : fs::directory_iterator(s.path, ec))
    if (e.is_regular_file(ec) && e.path().extension() == ".ics") out.push_back(e.path().string());
  std::sort(out.begin(), out.end());
  return out;
}

#ifdef US_HAVE_SQLITE
// every VEVENT Evolution keeps for an online calendar (read-only)
std::string evolutionCache(const std::string& db) {
  std::string out;
  sqlite3* h = nullptr;
  const std::string uri = "file:" + db + "?mode=ro";
  if (sqlite3_open_v2(uri.c_str(), &h, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
    if (h) sqlite3_close(h);
    return out;
  }
  sqlite3_busy_timeout(h, 200);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(h, "SELECT ECacheOBJ FROM ECacheObjects", -1, &st, nullptr) == SQLITE_OK) {
    while (sqlite3_step(st) == SQLITE_ROW && out.size() < (64u << 20)) {
      const auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
      if (t) out += std::string(t) + "\r\n";
    }
  }
  sqlite3_finalize(st);
  sqlite3_close(h);
  return out;
}
#endif
}  // namespace

std::vector<CalSource> icsSources(const std::string& list) {
  std::vector<CalSource> out;
  size_t at = 0;
  while (at <= list.size()) {
    const size_t semi = list.find(';', at);
    const std::string p = trim(list.substr(at, semi == std::string::npos ? std::string::npos : semi - at));
    at = semi == std::string::npos ? list.size() + 1 : semi + 1;
    if (p.empty()) continue;
    CalSource s;
    s.path = expandHome(p);
    std::error_code ec;
    s.folder = fs::is_directory(s.path, ec);
    if (s.folder) s.color = folderColor(s.path);
    out.push_back(s);
  }
  return out;
}

// khal's config is configobj: [calendars] then a [[name]] for each, with
// `path` (a vdir, or a glob with `type = discover`) and maybe `color`
std::vector<CalSource> khalSources() {
  std::vector<CalSource> out;
  std::ifstream f(env("XDG_CONFIG_HOME", "~/.config") + "/khal/config");
  if (!f) return out;
  bool inCalendars = false;
  struct Cal {
    std::string path, type, color;
  };
  std::vector<Cal> cals;
  for (std::string line; std::getline(f, line);) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    if (t.rfind("[[", 0) == 0) {
      if (inCalendars) cals.emplace_back();
      continue;
    }
    if (t[0] == '[') {
      inCalendars = t == "[calendars]";
      continue;
    }
    const size_t eq = t.find('=');
    if (!inCalendars || cals.empty() || eq == std::string::npos) continue;
    const std::string k = trim(t.substr(0, eq));
    std::string v = trim(t.substr(eq + 1));
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) v = v.substr(1, v.size() - 2);
    if (k == "path") cals.back().path = v;
    else if (k == "type") cals.back().type = v;
    else if (k == "color") cals.back().color = hexColor(v);
  }
  for (const auto& c : cals) {
    if (c.path.empty()) continue;
    std::string p = expandHome(c.path);
    if (p.rfind("$HOME", 0) == 0) p = expandHome("~") + p.substr(5);
    std::vector<std::string> dirs;
    if (c.type == "discover") {
      glob_t g{};
      if (glob(p.c_str(), GLOB_NOSORT, nullptr, &g) == 0)
        for (size_t i = 0; i < g.gl_pathc; ++i) dirs.emplace_back(g.gl_pathv[i]);
      globfree(&g);
    } else {
      dirs.push_back(p);
    }
    for (const auto& d : dirs) {
      std::error_code ec;
      if (!fs::is_directory(d, ec)) continue;
      CalSource s;
      s.path = d;
      s.folder = true;
      s.color = c.color.empty() ? folderColor(d) : c.color;
      out.push_back(s);
    }
  }
  return out;
}

// Evolution keeps a calendar per source uid; its settings in
// ~/.config/evolution/sources/<uid>.source say whether it is on and shown,
// and its colour
std::vector<CalSource> evolutionSources() {
  std::vector<CalSource> out;
  const std::string sources = env("XDG_CONFIG_HOME", "~/.config") + "/evolution/sources/";
  auto settings = [&](const std::string& uid, std::string& color) {
    GKeyFile* k = g_key_file_new();
    bool on = true;
    if (g_key_file_load_from_file(k, (sources + uid + ".source").c_str(), G_KEY_FILE_NONE, nullptr)) {
      auto flag = [&](const char* group, const char* key) {
        GError* e = nullptr;
        const gboolean v = g_key_file_get_boolean(k, group, key, &e);
        if (e) {
          g_error_free(e);
          return true;  // not said: on
        }
        return v != FALSE;
      };
      on = flag("Data Source", "Enabled") && flag("Calendar", "Selected");
      if (gchar* c = g_key_file_get_string(k, "Calendar", "Color", nullptr)) {
        color = hexColor(c);
        g_free(c);
      }
    }
    g_key_file_free(k);
    return on;
  };
  std::error_code ec;
  // local calendars: ~/.local/share/evolution/calendar/<uid>/calendar.ics ("system" is the personal one)
  for (const auto& e : fs::directory_iterator(env("XDG_DATA_HOME", "~/.local/share") + "/evolution/calendar", ec)) {
    const std::string uid = e.path().filename().string();
    const std::string file = e.path().string() + "/calendar.ics";
    std::string color;
    if (!fs::is_regular_file(file, ec) || !settings(uid == "system" ? "system-calendar" : uid, color)) continue;
    out.push_back({file, false, false, color});
  }
#ifdef US_HAVE_SQLITE
  // online ones, as Evolution keeps them: ~/.cache/evolution/calendar/<uid>/cache.db
  for (const auto& e : fs::directory_iterator(env("XDG_CACHE_HOME", "~/.cache") + "/evolution/calendar", ec)) {
    const std::string uid = e.path().filename().string(), db = e.path().string() + "/cache.db";
    std::string color;
    if (!fs::is_regular_file(db, ec) || !settings(uid, color)) continue;
    out.push_back({db, false, true, color});
  }
#endif
  return out;
}

std::vector<CalEvent> loadEvents(const std::vector<CalSource>& sources, int64_t from, int64_t to) {
  std::vector<CalEvent> out;
  for (const auto& s : sources) {
    std::string text;
    if (s.sqlite) {
#ifdef US_HAVE_SQLITE
      text = evolutionCache(s.path);
#endif
    } else {
      for (const auto& f : filesOf(s)) {
        std::error_code ec;
        if (fs::file_size(f, ec) > (32u << 20) || ec) continue;  // not a calendar anyone keeps
        text += readFile(f);
        text += "\r\n";
      }
    }
    auto ev = parseIcs(text, from, to, s.color);
    out.insert(out.end(), std::make_move_iterator(ev.begin()), std::make_move_iterator(ev.end()));
  }
  std::sort(out.begin(), out.end(), [](const CalEvent& a, const CalEvent& b) { return a.start < b.start; });
  return out;
}

std::string sourcesStamp(const std::vector<CalSource>& sources) {
  std::string out;
  for (const auto& s : sources) {
    std::vector<std::string> files = filesOf(s);
    if (s.sqlite) files.push_back(s.path + "-wal");
    for (const auto& f : files) {
      std::error_code ec;
      const auto t = fs::last_write_time(f, ec).time_since_epoch().count();
      out += std::format("{}\x1f{}\x1f{}\n", f, ec ? 0 : fs::file_size(f, ec), ec ? 0 : t);
    }
  }
  return out;
}

}  // namespace undershell
