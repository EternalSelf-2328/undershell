// SPDX-License-Identifier: GPL-3.0-or-later
// A calendar: the month as a grid, the week as a strip, or today as a
// tear-off page, with today marked in the accent colour. On a glass or
// solid plate, or on nothing. It draws once and wakes again at midnight.
#pragma once

#include "canvas.hpp"
#include "widget.hpp"

#include <string>

namespace undershell {

struct CalendarConfig {
  std::string layout = "month";     // month week day
  std::string firstDay = "monday";  // monday sunday
  std::string plate = "glass";      // glass solid none
  std::string accent = "primary", ink = "on_surface";
  std::string font = "Space Grotesk";
  bool weekends = true;      // weekends tinted
  bool weekNumbers = false;  // month: ISO week numbers down the side
  std::string language = "system";  // system en es
  double opacity = 1;
  std::string date;  // "YYYY-MM-DD": shown instead of today (pictures, tests)

  static CalendarConfig fromTable(const toml::table& t);
};

class CalendarWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void tick(const TickContext& ctx) override;
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

  CalendarConfig m_cfg;
  Color m_accent, m_ink, m_surface, m_onAccent;
  bool m_es = false;
  Canvas m_canvas;
  Day m_fixed;
};

}  // namespace undershell
