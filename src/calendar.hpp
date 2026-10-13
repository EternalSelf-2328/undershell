// SPDX-License-Identifier: GPL-3.0-or-later
// A calendar: the month as a grid, the week as a strip, or today as a
// tear-off page, with today marked in the accent colour. On a glass or
// solid plate, or on nothing. It draws once and wakes again at midnight.
#pragma once

#include "calsources.hpp"
#include "canvas.hpp"
#include "widget.hpp"

#include <string>

namespace undershell {

struct CalendarConfig {
  std::string layout = "month";     // month week day agenda
  std::string firstDay = "monday";  // monday sunday
  std::string plate = "glass";      // glass solid none
  std::string accent = "primary", ink = "on_surface";
  std::string font = "Space Grotesk";
  bool weekends = true;      // weekends tinted
  bool weekNumbers = false;  // month: ISO week numbers down the side
  std::string language = "system";  // system en es
  double opacity = 1;
  std::string date;  // "YYYY-MM-DD": shown instead of today (pictures, tests)
  // events, read from this computer only (see calsources.hpp)
  std::string ics;        // .ics files or folders, separated by ';'
  bool khal = false;      // khal's calendars
  bool evolution = false; // Evolution's / GNOME Calendar's
  int agendaDays = 7;     // how far the agenda looks ahead

  static CalendarConfig fromTable(const toml::table& t);
};

class CalendarWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void tick(const TickContext& ctx) override;
  [[nodiscard]] std::vector<std::string> watchedFiles() const override;
  void filesChanged() override { m_stamp.clear(); }
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] double nextWakeup(double now) const override;
  [[nodiscard]] Color accent() const override { return m_accent; }

private:
  struct Day {
    int y = 0, m = 0, d = 0;
  };
  [[nodiscard]] Day today() const;
  void drawMonth(Canvas& c, float w, float h, const Day& t);
  void drawWeek(Canvas& c, float w, float h, const Day& t);
  void drawPage(Canvas& c, float w, float h, const Day& t);
  void drawAgenda(Canvas& c, float w, float h, const Day& t);
  void loadEvents(const Day& t);
  // the events that touch a day (local time)
  [[nodiscard]] std::vector<const CalEvent*> eventsOn(int y, int m, int d) const;
  void dots(Canvas& c, int y, int m, int d, float cx, float cy, float r, Color over) const;

  CalendarConfig m_cfg;
  Color m_accent, m_ink, m_surface, m_onAccent;
  bool m_es = false;
  Canvas m_canvas;
  Day m_fixed;
  std::vector<CalSource> m_sources;
  std::vector<CalEvent> m_events;
  std::string m_stamp, m_window;  // what the events were read from, and for which days
};

}  // namespace undershell
