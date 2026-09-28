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

  [[nodiscard]] bool animating(const TickContext& ctx) const override {
    return m_motion.animating(ctx.audio.energy) || m_bassEnv > 0.003 || m_waveAges[0] >= 0 || m_waveAges[1] >= 0 ||
           m_waveAges[2] >= 0 || m_waveAges[3] >= 0;
  }
  [[nodiscard]] bool visible() const override {
    return (m_styleIndex == 12 && m_haloShape == "ring") || m_motion.fade() > 0.002;  // the ring rests visible
  }
  [[nodiscard]] int fps() const override { return m_cfg.fps; }
  [[nodiscard]] bool fullscreen() const override { return m_cfg.style == "frame"; }
  [[nodiscard]] bool usesAudio() const override { return true; }
  [[nodiscard]] Color accent() const override { return m_ramp[4]; }
  void rest() override { m_motion.rest(); }
  [[nodiscard]] const VisualizerConfig& config() const { return m_cfg; }

private:
  void configureHalo(const WidgetConfig& cfg, const NoctaliaState& noct);
  void drawHalo(const DrawContext& ctx);
  void drawRing(const DrawContext& ctx);
  void tickRing(double dt);

  VisualizerConfig m_cfg;
  // halo (Noctalia's fancy visualizer, outer part only)
  Program m_haloProg, m_ringProg;
  std::string m_haloShape = "ring";
  // the ring's beat: bass envelope, a slow mean for onsets, shock waves
  double m_bassEnv = 0, m_bassMean = 0, m_energyEnv = 0, m_sinceBeat = 1;
  std::array<double, 4> m_waveAges{-1, -1, -1, -1};
  bool m_haloWaves = true;
  double m_haloWidth = 0.02, m_haloSpread = 0.07;
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
