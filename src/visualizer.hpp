// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gl.hpp"
#include "motion.hpp"
#include "widget.hpp"

#include <array>
#include <vector>

namespace undershell {

// Ryoku's desktop spectrum (VisualizerView + SpectrumField + Motion).
class Visualizer final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void configure(const VisualizerConfig& cfg, const NoctaliaState& noct);
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;

  [[nodiscard]] bool animating(const TickContext& ctx) const override { return m_motion.animating(ctx.audio.energy); }
  [[nodiscard]] bool visible() const override { return m_motion.fade() > 0.002; }
  [[nodiscard]] int fps() const override { return m_cfg.fps; }
  [[nodiscard]] bool fullscreen() const override { return m_cfg.style == "frame"; }
  [[nodiscard]] bool usesAudio() const override { return true; }
  [[nodiscard]] Color accent() const override { return m_ramp[4]; }
  void rest() override { m_motion.rest(); }
  [[nodiscard]] const VisualizerConfig& config() const { return m_cfg; }

private:
  void configureHalo(const WidgetConfig& cfg, const NoctaliaState& noct);
  void drawHalo(const DrawContext& ctx);

  VisualizerConfig m_cfg;
  // halo (Noctalia's fancy visualizer, outer part only)
  Program m_haloProg;
  std::string m_haloShape = "bars";
  bool m_haloRing = false;
  double m_haloInner = 0.7, m_haloBloom = 0.5;
  Color m_haloA, m_haloB;
  Motion m_motion;
  std::array<Color, 8> m_ramp{};
  int m_styleIndex = 0;
  Program m_prog;
  std::vector<float> m_drawLevels, m_drawPeaks;
};

}  // namespace undershell
