// SPDX-License-Identifier: GPL-3.0-or-later
// A geometric shape, to build compositions out of layers: rectangles,
// ellipses, triangles, polygons, stars, hearts, rings and lines, plus the
// Material 3 shapes. Filled with a colour or a gradient, outlined, with a
// glow and a soft shadow, all from one signed-distance shader: a still shape
// is drawn once and costs nothing after that. It can turn slowly or pulse
// with the music.
#pragma once

#include "gl.hpp"
#include "widget.hpp"

#include <string>
#include <vector>

namespace undershell {

struct ShapeConfig {
  // rect ellipse triangle polygon star heart ring line, or a Material 3 shape (m3shapes)
  std::string shape = "rect";
  int sides = 5;          // polygon sides, star points
  double sharpness = 0.5;  // star: how deep its points are cut (0: a polygon)
  double corner = 0.15;   // rounding of corners (rect, triangle, polygon, star, line ends)
  double thickness = 0.25;  // ring band and line width, a share of the shape's size
  double arc = 360;       // ring: how much of it is drawn, degrees
  std::string fill = "solid";  // solid gradient radial none
  std::string color = "primary", color2 = "tertiary";
  double angle = 90;  // gradient direction, degrees (0: left to right, 90: top to bottom)
  double stroke = 0;  // outline width, px (inside the shape)
  std::string strokeColor = "on_surface";
  double glow = 0, shadow = 0;  // 0..1
  std::string react = "none";   // none pulse glow: the music's kicks
  double spin = 0;              // degrees per second
  double opacity = 1;

  static ShapeConfig fromTable(const toml::table& t);
};

class ShapeWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void configure(const ShapeConfig& cfg, const NoctaliaState& noct);
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] bool animating(const TickContext& ctx) const override;
  [[nodiscard]] bool usesAudio() const override { return m_cfg.react != "none"; }
  [[nodiscard]] int fps() const override { return 60; }
  [[nodiscard]] Color accent() const override { return m_c1; }
  static const std::vector<std::string>& shapes();

private:
  ShapeConfig m_cfg;
  Color m_c1, m_c2, m_strokeC;
  std::vector<float> m_radii;  // a Material shape's outline (64 angles)
  Program m_prog;
  double m_now = 0, m_spinAt = 0;
  double m_beat = 0;  // the last kick, fading
  uint64_t m_kicks = 0;
};

}  // namespace undershell
