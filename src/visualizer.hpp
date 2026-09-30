// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gl.hpp"
#include "motion.hpp"
#include "widget.hpp"

#include <array>
#include <cmath>
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
    return m_motion.animating(ctx.audio.energy) || m_breath > 0.003 || std::abs(m_breathVel) > 0.003 || m_hit > 0 || m_pump > 0.003 ||
           (m_styleIndex == 12 && m_haloAurora > 0) || (m_styleIndex == 13 && m_vSpeed > 0) || m_waveAges[0] >= 0 || m_waveAges[1] >= 0 || m_waveAges[2] >= 0 ||
           m_waveAges[3] >= 0;
  }
  [[nodiscard]] bool visible() const override {
    return m_styleIndex >= 12 || m_motion.fade() > 0.002;  // the halo and the vortex rest visible
  }
  [[nodiscard]] int fps() const override { return m_cfg.fps; }
  [[nodiscard]] bool fullscreen() const override { return m_cfg.style == "frame"; }
  [[nodiscard]] bool usesAudio() const override { return true; }
  [[nodiscard]] Color accent() const override { return m_ramp[4]; }
  void rest() override { m_motion.rest(); }
  [[nodiscard]] const VisualizerConfig& config() const { return m_cfg; }
  // the halo's inner signals, for tuning tools
  struct HaloTrace {
    double energy, bass, flux, threshold, breath, hit, tone;
    bool kick;
    double pump;
  };
  [[nodiscard]] const HaloTrace& haloTrace() const { return m_trace; }

private:
  void configureHalo(const WidgetConfig& cfg, const NoctaliaState& noct);
  void drawRing(const DrawContext& ctx);
  void drawVortex(const DrawContext& ctx);
  void tickRing(double dt, const std::vector<float>* raw);

  VisualizerConfig m_cfg;
  HaloTrace m_trace{};
  // the halo: one ring of light
  Program m_ringProg, m_vortexProg;
  // the vortex: shape, and its motion integrated from the halo's signals
  int64_t m_vArms = 3;
  double m_vTwist = 3.4, m_vReach = 1.9, m_vSpeed = 0.35, m_vTurb = 0.45, m_vPhase = 0, m_vFlow = 0, m_vBurst = 0;
  bool m_vClockwise = false;
  std::string m_vMode = "inward";
  double m_breath = 0, m_breathVel = 0, m_hit = 0, m_tone = 0.3, m_prevBass = 0, m_fluxMean = 0, m_fluxVar = 0, m_sinceBeat = 1,
         m_ringTime = 0, m_pump = 0, m_energySlow = 0, m_loud = 0.5, m_liveTime = 0;
  double m_bassFloor = 0, m_bassPeak = 0, m_energyFloor = 0, m_energyPeak = 0;  // recent ranges
  std::array<double, 4> m_waveAges{-1, -1, -1, -1}, m_waveGain{0, 0, 0, 0};
  bool m_haloWaves = true;
  double m_haloWidth = 0.012, m_haloSpread = 0.09, m_haloInner = 0.7, m_haloBloom = 0.8;
  double m_haloBreathe = 0.6, m_haloHits = 0.7, m_haloAurora = 0, m_haloPulse = 0.7;
  Color m_haloA, m_haloB;
  Motion m_motion;
  std::array<Color, 8> m_ramp{};
  int m_styleIndex = 0;
  Program m_prog;
  std::vector<float> m_drawLevels, m_drawPeaks;
};

}  // namespace undershell
