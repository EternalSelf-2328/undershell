// SPDX-License-Identifier: GPL-3.0-or-later
// Ryoku's desktop clock (modules/desktop/clock): twelve faces and three date
// strips. Each face lays itself out in Ryoku's own pixel units as a small
// display list; the widget fits that design into its box.
#pragma once

#include "canvas.hpp"
#include "widget.hpp"

#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

namespace undershell {

struct ClockConfig {
  std::string face = "digital";  // digital minimal analog flip rings bighour metal goodnight grand column outline banner
  std::string date = "inline";   // none inline badge stacked
  bool clock24 = true;
  bool seconds = false;
  std::string accent = "primary";  // primary secondary tertiary brand mono custom
  std::string accentColor = "#e2342a";
  std::string ink = "on_surface";
  std::string language = "system";  // system en es
  bool weather = true;
  bool fahrenheit = false;
  double opacity = 1.0;

  static ClockConfig fromTable(const toml::table& t);
};

class ClockWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void configure(const ClockConfig& cfg, const NoctaliaState& noct);
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] bool animating(const TickContext& ctx) const override;
  [[nodiscard]] double nextWakeup(double now) const override;
  [[nodiscard]] int fps() const override { return m_cfg.face == "flip" ? 30 : 60; }
  [[nodiscard]] Color accent() const override { return m_accent; }

  // tests pin the wall clock so goldens are stable
  static void setFixedTime(std::time_t t) { s_fixedTime = t; }
  static const std::vector<std::string>& faces();

  // display list item, in design units (public for the face builders)
  struct Item {
    enum Kind { Text, Rect, Circle, Segment, Arc } kind = Text;
    std::string text;
    TextStyle style;
    float x = 0, y = 0, w = 0, h = 0, r = 0, x2 = 0, y2 = 0, a0 = 0, a1 = 0;
    Color color, stroke;
    float strokeW = 0, opacity = 1, sy = 1, blur = 0;
    bool roundCap = true;
  };
  struct Scene {
    std::vector<Item> items;
    float w = 0, h = 0;
  };
  Canvas::Size measure(const std::string& text, const TextStyle& st);

private:
  Scene build(std::time_t now);
  std::time_t currentTime() const;

  ClockConfig m_cfg;
  toml::table m_opts;  // the widget's table: element overrides of editable structures
  NoctaliaState m_noct;
  Color m_ink, m_accent;
  Canvas m_canvas;
  std::unordered_map<std::string, Canvas::Size> m_measures;
  // flip card animation
  struct Card {
    std::string shown, target;
    double start = -1;  // animation start (steady seconds), -1 = idle
  };
  std::vector<Card> m_cards;
  double m_now = 0;
  static inline std::time_t s_fixedTime = 0;
  friend struct FaceBuilder;
};

}  // namespace undershell
