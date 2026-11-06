// SPDX-License-Identifier: GPL-3.0-or-later
// The pet widget (see pet.cpp).
#pragma once

#include "canvas.hpp"
#include "widget.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace undershell {

class PetWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] bool animating(const TickContext& ctx) const override;
  [[nodiscard]] double nextWakeup(double now) const override;
  [[nodiscard]] int fps() const override { return 30; }
  [[nodiscard]] bool usesAudio() const override { return m_dances; }
  [[nodiscard]] bool wantsWindows() const override { return m_climb; }
  [[nodiscard]] bool overlay() const override { return true; }
  [[nodiscard]] bool offset(float& dx, float& dy) const override;
  [[nodiscard]] Color accent() const override { return m_body; }

  enum class Mode { Idle, Sit, Walk, Jump, Dance, Sleep };
  // tests: where it stands and what it does
  [[nodiscard]] double x() const { return m_x; }
  [[nodiscard]] double y() const { return m_y; }
  [[nodiscard]] Mode mode() const { return m_mode; }

private:
  [[nodiscard]] double rnd();
  [[nodiscard]] double support(double x, double y) const;
  void pick(Mode m, double secs);
  void stepBy(const TickContext& ctx, double dt, bool kick);

  Color m_body, m_sprout;
  double m_speed = 1, m_opacity = 1;
  bool m_climb = true, m_sleeps = true, m_dances = true;
  std::string m_preview;  // a fixed pose for previews and tests: walk, dance, sleep

  Mode m_mode = Mode::Idle;
  double m_timer = 1.5, m_time = 0, m_silence = 0, m_blink = 2, m_squash = 0;
  double m_x = 0, m_y = 0, m_vx = 0, m_vy = 0, m_facing = 1;
  double m_homeX = 0, m_homeY = 0, m_ground = 0, m_minX = 0, m_maxX = 1e9, m_boxW = 0, m_boxH = 0;
  bool m_placed = false;
  uint64_t m_kicks = 0;
  uint64_t m_rng = 0x853C49E6748FEA9BULL;
  std::vector<Rect> m_windows;
  Canvas m_canvas;
};

}  // namespace undershell
