// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"
#include "gl.hpp"
#include "motion.hpp"
#include "noctalia.hpp"

#include <array>
#include <vector>

namespace undershell {

// Ryoku's desktop spectrum (VisualizerView + SpectrumField + Motion).
class Visualizer {
public:
  void configure(const VisualizerConfig& cfg, const NoctaliaState& noct);
  // Advances motion. raw = analyser bands (0..1), empty when silent.
  void tick(double dt, const std::vector<float>& raw, double energy);
  // Draws into the current framebuffer (logical size w x h). outputW/H: the
  // monitor, used by the frame look and for scale-dependent sizes.
  void draw(float w, float h, float outputW, float outputH);

  [[nodiscard]] bool animating(double energy) const { return m_motion.animating(energy); }
  [[nodiscard]] bool visible() const { return m_motion.fade() > 0.002; }
  [[nodiscard]] int fps() const { return m_cfg.fps; }
  [[nodiscard]] bool fullscreen() const { return m_cfg.style == "frame"; }
  void rest() { m_motion.rest(); }
  [[nodiscard]] Color accent() const { return m_ramp[4]; }

private:
  VisualizerConfig m_cfg;
  Motion m_motion;
  std::array<Color, 8> m_ramp{};
  int m_styleIndex = 0;
  Program m_prog;
  std::vector<float> m_drawLevels, m_drawPeaks;
};

}  // namespace undershell
