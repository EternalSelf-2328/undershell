// SPDX-License-Identifier: GPL-3.0-or-later
// The weather where you say you are (Open-Meteo, see weatherfeed.hpp): now
// (an icon, the temperature, the sky in words), a card with the next hours,
// the week, or the hours alone. Drawn with the canvas, icons included; it
// redraws when a new report lands (every half hour) and on the hour.
#pragma once

#include "canvas.hpp"
#include "weatherfeed.hpp"
#include "widget.hpp"

#include <string>

namespace undershell {

struct WeatherConfig {
  std::string location;          // "Quito, Ecuador" or "-0.18, -78.47"; empty: a hint to set it
  std::string layout = "now";    // now card week hours
  std::string units = "metric";  // metric imperial
  std::string plate = "glass";   // glass solid none
  std::string accent = "primary", ink = "on_surface";
  std::string font = "Space Grotesk";
  std::string icons = "color";   // color mono
  std::string language = "system";
  bool place = true;             // show the place's name
  double opacity = 1;

  static WeatherConfig fromTable(const toml::table& t);
};

class WeatherWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] double nextWakeup(double now) const override;
  [[nodiscard]] bool wantsWeather() const override { return true; }
  [[nodiscard]] Color accent() const override { return m_accent; }

  // the picture for a sky (cx, cy: its middle; r: about half its size)
  static void drawSky(Canvas& c, Sky sky, bool day, float cx, float cy, float r, bool color, Color ink, Color accent, float op);

private:
  void drawNow(Canvas& c, float x, float y, float w, float h, bool details);
  void drawHours(Canvas& c, float x, float y, float w, float h, int count);
  void drawWeek(Canvas& c, float x, float y, float w, float h);
  void drawMessage(Canvas& c, float w, float h, const std::string& text);

  WeatherConfig m_cfg;
  WeatherReport m_report;
  Color m_accent, m_ink, m_surface;
  bool m_es = false;
  Canvas m_canvas;
};

}  // namespace undershell
