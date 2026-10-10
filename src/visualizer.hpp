// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "electric.hpp"
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
    if (m_hidden && m_hideFade < 0.003) return false;
    if (m_hidden || m_hideFade < 0.997) return true;  // fading
    return m_motion.animating(ctx.audio.energy) || m_breath > 0.003 || std::abs(m_breathVel) > 0.003 || m_hit > 0 || m_pump > 0.003 ||
           (m_styleIndex == 12 && m_haloAurora > 0) || (m_styleIndex == 13 && m_vSpeed > 0) || m_styleIndex == 14 ||
           (m_styleIndex == 15 && muzzleShows()) || (m_styleIndex == 16 && electricMoves()) || m_waveAges[0] >= 0 || m_waveAges[1] >= 0 || m_waveAges[2] >= 0 ||
           m_waveAges[3] >= 0;
  }
  [[nodiscard]] bool visible() const override {
    if (m_hidden && m_hideFade < 0.003) return false;  // faded out: nothing to draw, no frames spent
    if (m_styleIndex == 15) return muzzleShows();      // between shots there is nothing to draw
    if (m_styleIndex == 16) return m_eForm != "bolts" || boltsAlive();  // bolts: nothing between strikes
    return m_styleIndex >= 12 || m_motion.fade() > 0.002;  // halo, vortex and fire rest visible
  }
  [[nodiscard]] int fps() const override { return m_cfg.fps; }
  // the arc and the plasma still crackle in silence, a few times a second
  [[nodiscard]] double nextWakeup(double now) const override {
    return m_styleIndex == 16 && m_eForm != "bolts" && !m_hidden ? now + 0.09 : 1e18;
  }
  [[nodiscard]] bool fullscreen() const override { return m_cfg.style == "frame"; }
  [[nodiscard]] bool usesAudio() const override { return true; }
  [[nodiscard]] Color accent() const override { return m_ramp[4]; }
  void rest() override { m_motion.rest(); }
  void setHidden(bool hidden) override { m_hidden = hidden; }
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
  void drawFire(const DrawContext& ctx);
  void tickRing(double dt, const std::vector<float>* raw, const AudioFrame& audio);

  VisualizerConfig m_cfg;
  HaloTrace m_trace{};
  bool m_hidden = false;
  double m_hideFade = 1;  // 1 shown .. 0 hidden
  [[nodiscard]] float opacityNow() const { return static_cast<float>(m_cfg.opacity * m_hideFade * m_hideFade * (3 - 2 * m_hideFade)); }
  // the halo: one ring of light
  Program m_ringProg, m_vortexProg;
  // the vortex: shape, and its motion integrated from the halo's signals
  int64_t m_vArms = 3;
  double m_vTwist = 3.4, m_vReach = 1.9, m_vSpeed = 0.35, m_vTurb = 0.45, m_vPhase = 0, m_vFlow = 0, m_vBurst = 0;
  bool m_vClockwise = false;
  std::string m_vMode = "inward";
  double m_vRing = 1.0;
  // fire
  Program m_fireProg;
  std::string m_fShape = "bonfire";
  double m_fHeight = 0.6, m_fTurb = 0.55, m_fSpectrum = 0.5, m_fSparks = 0.6, m_fSpeed = 1.0;
  double m_fTime = 0, m_fSparkTime = 0, m_fFlare = 0;
  bool m_fTheme = false;
  // the muzzle flash: a shot per kick, up to four alive at once
  Program m_muzzleProg;
  std::array<double, 4> m_shotAge{-1, -1, -1, -1}, m_shotSeed{}, m_shotGain{};
  double m_shotCounter = 0;
  bool m_mManga = false, m_mTheme = false, m_mPreview = false;  // preview: one shot frozen in time
  double m_mLength = 0.85, m_mSpikes = 4, m_mSparks = 0.6, m_mSmoke = 0.5;
  // each shot's shape, drawn once when it is fired (the shader only draws
  // it): 3 forward tongues then 6 side spikes (cos, sin, length as a share
  // of the reach, width), 14 sparks (direction, speed, life; life < 0: none)
  // and 4 smoke puffs (along as a share of the reach, across, swell)
  std::array<float, 4 * 9 * 4> m_lobes{};
  std::array<float, 4 * 14 * 4> m_sparks{};
  std::array<float, 4 * 4 * 4> m_puffs{};
  void shoot(double gain, double age);
  void shapeShot(size_t slot);
  // electricity: an arc across the box, bolts striking on the kicks, or a
  // plasma globe; its paths are built each frame from seeds that change
  // faster the louder it is
  BoltRenderer m_bolts;
  std::vector<BoltSeg> m_boltSegs;
  std::string m_eForm = "arc";  // arc bolts plasma
  bool m_eManga = false, m_eTheme = false, m_eLive = false;
  double m_eAmount = 0.5, m_eBranches = 0.5, m_eGlow = 0.6, m_eWidth = 1.0;
  double m_ePreview = -1;  // >= 0: frozen this long after a kick (previews, tests)
  double m_eTime = 0, m_eFlash = 0, m_eEnergy = 0;
  uint32_t m_eCounter = 1;
  BoltRandom m_eRnd{20261009};
  std::array<uint32_t, 4> m_arcSeed{};
  std::array<double, 4> m_arcAt{};
  std::array<double, 4> m_boltAge{-1, -1, -1, -1}, m_boltGain{};
  std::array<uint32_t, 4> m_boltSeed{};
  std::array<float, 4> m_boltX0{}, m_boltX1{}, m_boltY1{};
  static constexpr size_t kFilaments = 12;
  std::array<double, kFilaments> m_filAngle{}, m_filSpeed{}, m_filAt{};
  std::array<uint32_t, kFilaments> m_filSeed{};
  [[nodiscard]] bool boltsAlive() const {
    return m_boltAge[0] >= 0 || m_boltAge[1] >= 0 || m_boltAge[2] >= 0 || m_boltAge[3] >= 0;
  }
  [[nodiscard]] bool electricMoves() const { return m_eLive || m_eFlash > 0.01 || boltsAlive(); }
  uint32_t nextSeed() { return (m_eCounter++) * 2654435761U + 12345U; }
  void electricTick(double dt, bool kicked, double strength);
  void strike(double gain, double age);
  void drawElectric(const DrawContext& ctx);
  // a shot alive (its smoke too), or the realistic ember glowing with the bass
  [[nodiscard]] bool muzzleShows() const {
    return m_shotAge[0] >= 0 || m_shotAge[1] >= 0 || m_shotAge[2] >= 0 || m_shotAge[3] >= 0 || (!m_mManga && m_pump * m_haloPulse > 0.003);
  }
  void drawMuzzle(const DrawContext& ctx);
  double m_breath = 0, m_breathVel = 0, m_hit = 0, m_tone = 0.3, m_prevBass = 0, m_fluxMean = 0, m_fluxVar = 0, m_sinceBeat = 1,
         m_ringTime = 0, m_pump = 0, m_energySlow = 0, m_loud = 0.5, m_liveTime = 0;
  double m_bassFloor = 0, m_bassPeak = 0, m_energyFloor = 0, m_energyPeak = 0;  // recent ranges
  uint64_t m_lastKicks = 0;  // the analyser's kick counter, as last seen
  std::array<double, 4> m_waveAges{-1, -1, -1, -1}, m_waveGain{0, 0, 0, 0};
  bool m_haloWaves = true;
  double m_haloWidth = 0.012, m_haloSpread = 0.09, m_haloInner = 0.7, m_haloBloom = 0.8;
  std::string m_haloForm = "circle";
  std::vector<float> m_haloFormR = std::vector<float>(128, 1.0F);
  double m_haloBreathe = 0.6, m_haloHits = 0.7, m_haloAurora = 0, m_haloPulse = 0.7;
  Color m_haloA, m_haloB;
  Motion m_motion;
  std::array<Color, 8> m_ramp{};
  int m_styleIndex = 0;
  Program m_prog;
  std::vector<float> m_drawLevels, m_drawPeaks;
};

}  // namespace undershell
