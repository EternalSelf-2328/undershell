// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <toml++/toml.hpp>

namespace undershell {

struct LayerPose {
  float x = 0, y = 0, brightness = 1, opacity = 1;
};

class LayerMotion {
public:
  void configure(const toml::table& options);
  void tick(double now, bool editing);
  [[nodiscard]] bool enabled() const { return m_mode != Mode::None; }
  [[nodiscard]] double nextWakeup() const { return m_next; }
  [[nodiscard]] LayerPose pose() const { return m_pose; }

  // For a possibly overnight local time interval. Times are minutes.
  [[nodiscard]] static double nightAmount(double minute, double from, double until, double fade);

private:
  enum class Mode { None, Loop, Night };
  Mode m_mode = Mode::None;
  LayerPose m_target, m_pose;
  double m_start = -1, m_next = 1e18, m_period = 12, m_preview = -1;
  float m_startOpacity = 1;
  double m_from = 1080, m_until = 360, m_fade = 12;
};

}  // namespace undershell
