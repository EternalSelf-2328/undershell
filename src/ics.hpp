// SPDX-License-Identifier: GPL-3.0-or-later
// Calendar events from iCalendar (RFC 5545) text: VEVENTs with their start,
// end, title, place, all-day or timed, in any time zone (TZID via GLib's
// zone database, UTC, or floating local time), and repeating ones expanded
// over a window (RRULE: DAILY, WEEKLY with BYDAY, MONTHLY with BYMONTHDAY
// or BYDAY like 2MO / -1FR, YEARLY; INTERVAL, COUNT, UNTIL; EXDATE; a
// RECURRENCE-ID moving one occurrence). Cancelled events are left out.
// Read-only: nothing here writes a calendar.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace undershell {

struct CalEvent {
  int64_t start = 0, end = 0;  // unix seconds (an all-day event: local midnights)
  bool allDay = false;
  std::string title, place;
  std::string color;           // "#rrggbb" when the calendar has one
  std::string uid;
};

// The events of one iCalendar text that touch [from, to) (unix seconds),
// sorted by start. `color` is given to those that do not carry their own.
std::vector<CalEvent> parseIcs(const std::string& text, int64_t from, int64_t to, const std::string& color = {});

}  // namespace undershell
