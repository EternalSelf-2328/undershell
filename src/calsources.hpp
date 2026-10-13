// SPDX-License-Identifier: GPL-3.0-or-later
// Where calendar events come from, all read-only and all on this computer:
// - .ics files, or folders of them (a "vdir", as vdirsyncer keeps them);
// - khal's calendars, from its config (~/.config/khal/config: their vdirs);
// - Evolution's, which GNOME Calendar shares: the local ones (calendar.ics)
//   and the copies it keeps of online ones (cache.db), the hidden or
//   disabled ones left out.
// Nothing is fetched from the network here: online calendars arrive through
// whatever syncs them (vdirsyncer, Evolution).
#pragma once

#include "ics.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace undershell {

struct CalSource {
  std::string path;   // a file, or a folder of .ics files
  bool folder = false;
  bool sqlite = false;  // Evolution's cache.db
  std::string color;    // "#rrggbb", or "" for the widget's accent
};

std::vector<CalSource> icsSources(const std::string& list);  // paths separated by ';'
std::vector<CalSource> khalSources();
std::vector<CalSource> evolutionSources();

// every event of those sources in [from, to), sorted
std::vector<CalEvent> loadEvents(const std::vector<CalSource>& sources, int64_t from, int64_t to);
// what the sources are now (names, sizes, times): it changes when they do
std::string sourcesStamp(const std::vector<CalSource>& sources);

}  // namespace undershell
