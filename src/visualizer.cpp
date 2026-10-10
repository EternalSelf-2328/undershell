// SPDX-License-Identifier: GPL-3.0-or-later
// Port of Ryoku's ryoku/ui/SpectrumField.qml: the look's uniforms derived
// from the config, the widget box and the eased levels.
#include "visualizer.hpp"

#include "m3shapes.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "ring_shader.inc"
#include "vortex_shader.inc"
#include "fire_shader.inc"
#include "muzzle_shader.inc"
#include "spectrum_shader.inc"

namespace undershell {

static const char* kStyles[] = {"bars", "split", "dots", "segments", "wave", "ribbon",
                                "curtain", "line", "frame", "radial", "orb", "spiral", "halo", "vortex", "fire", "muzzle",
                                "electric", "sfx", "speedlines", "rain", "ripples", "neon", "fireworks"};

void Visualizer::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  configure(VisualizerConfig::fromTable(cfg.options), noct);
  configureHalo(cfg, noct);
}

void Visualizer::configure(const VisualizerConfig& cfg, const NoctaliaState& noct) {
  m_cfg = cfg;
  m_styleIndex = 0;
  for (int i = 0; i < static_cast<int>(std::size(kStyles)); ++i)
    if (cfg.style == kStyles[i]) m_styleIndex = i;
  if (cfg.style != m_lookStyle) {  // a new look starts fresh; the same one keeps its state
    m_look = makeLook(cfg.style);
    m_lookStyle = cfg.style;
  }
  const bool polar = m_styleIndex >= 9 && m_styleIndex <= 13;  // radial, orb, spiral, halo, vortex
  m_motion.gain = cfg.gain;
  m_motion.smoothing = cfg.smoothing;
  m_motion.mirror = cfg.mirror && !polar && m_styleIndex != 8;
  m_motion.idleWave = cfg.idleWave;
  m_motion.wantPeaks = cfg.peaks && (m_styleIndex == 0 || m_styleIndex == 3 || m_styleIndex == 8);
  m_motion.spin = polar ? cfg.spin : 0.0;
  m_motion.configure(cfg.bars);

  // Colour, like Ryoku's VisualizerView ramp: a two-stop gradient, or one base
  // colour walked gently from bass (darker) to treble (lighter).
  const Color white{1, 1, 1, 1}, black{0, 0, 0, 1};
  if (cfg.colorMode == "gradient") {
    Color a = noct.color(cfg.color), b = noct.color(cfg.color2);
    for (int k = 0; k < 8; ++k) m_ramp[k] = a.mix(b, k / 7.0F);
  } else {
    Color base = cfg.colorMode == "custom" ? noct.color(cfg.color) : noct.color("primary");
    for (int k = 0; k < 8; ++k) {
      float f = (k / 7.0F - 0.5F) * 0.36F;
      m_ramp[k] = f >= 0 ? base.mix(white, f * 0.55F) : base.mix(black, -f * 0.55F);
    }
  }
}

void Visualizer::configureHalo(const WidgetConfig& cfg, const NoctaliaState& noct) {
  const toml::table& t = cfg.options;
  m_haloWaves = t["halo_waves"].value_or(true);
  m_haloWidth = std::clamp(t["halo_width"].value_or(0.012), 0.003, 0.15);
  m_haloSpread = std::clamp(t["halo_spread"].value_or(0.09), 0.01, 0.4);
  m_haloInner = std::clamp(t["halo_inner"].value_or(0.7), 0.1, 1.4);
  m_haloBloom = std::clamp(t["halo_bloom"].value_or(0.8), 0.0, 2.0);
  m_haloBreathe = std::clamp(t["halo_breathe"].value_or(0.6), 0.0, 1.0);
  m_haloHits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
  m_haloAurora = std::clamp(t["halo_aurora"].value_or(0.0), 0.0, 1.0);
  m_haloPulse = std::clamp(t["halo_pulse"].value_or(0.7), 0.0, 1.0);
  m_haloForm = t["halo_form"].value_or(std::string("circle"));
  m_haloFormR = m3ShapeRadii(m_haloForm, 128);
  m_vArms = std::clamp(t["vortex_arms"].value_or(int64_t{3}), int64_t{1}, int64_t{12});
  m_vTwist = std::clamp(t["vortex_twist"].value_or(3.4), 0.0, 8.0);
  m_vReach = std::clamp(t["vortex_reach"].value_or(1.9), 1.1, 6.0);
  m_vSpeed = std::clamp(t["vortex_speed"].value_or(0.35), 0.0, 3.0);
  m_vTurb = std::clamp(t["vortex_turbulence"].value_or(0.45), 0.0, 1.0);
  m_vClockwise = t["vortex_clockwise"].value_or(false);
  m_vMode = t["vortex_mode"].value_or(std::string("inward"));
  m_vRing = std::clamp(t["vortex_ring"].value_or(1.0), 0.0, 1.0);
  m_fShape = t["fire_shape"].value_or(std::string("bonfire"));
  m_fHeight = std::clamp(t["fire_height"].value_or(0.6), 0.1, 1.0);
  m_fTurb = std::clamp(t["fire_turbulence"].value_or(0.55), 0.0, 1.0);
  m_fSpectrum = std::clamp(t["fire_spectrum"].value_or(0.5), 0.0, 1.0);
  m_fSparks = std::clamp(t["fire_sparks"].value_or(0.6), 0.0, 1.0);
  m_fSpeed = std::clamp(t["fire_speed"].value_or(1.0), 0.1, 3.0);
  m_fTheme = t["fire_colors"].value_or(std::string("fire")) == "theme";
  m_mManga = t["muzzle_style"].value_or(std::string("flash")) == "manga";
  m_mTheme = t["muzzle_colors"].value_or(std::string("fire")) == "theme";
  m_mLength = std::clamp(t["muzzle_length"].value_or(0.85), 0.3, 1.0);
  m_mSpikes = std::clamp(t["muzzle_spikes"].value_or(4.0), 0.0, 6.0);
  m_mSparks = std::clamp(t["muzzle_sparks"].value_or(0.6), 0.0, 1.0);
  m_mSmoke = std::clamp(t["muzzle_smoke"].value_or(0.5), 0.0, 1.0);
  // a shot frozen at an age (seconds), for previews and tests
  m_mPreview = false;
  if (auto age = t["muzzle_preview"].value<double>()) {
    m_mPreview = true;
    m_shotAge = {*age, -1, -1, -1};
    m_shotSeed[0] = 7;
    m_shotGain[0] = 1;
  }
  for (size_t i = 0; i < m_shotAge.size(); ++i)  // the spark count may have changed
    if (m_shotAge[i] >= 0) shapeShot(i);
  m_sfx.configure(t, noct);
  if (m_look) m_look->configure(t, noct);
  m_eForm = t["electric_form"].value_or(std::string("arc"));
  if (m_eForm != "bolts" && m_eForm != "plasma") m_eForm = "arc";
  m_eManga = t["electric_style"].value_or(std::string("flash")) == "manga";
  m_eTheme = t["electric_colors"].value_or(std::string("blue")) == "theme";
  m_eAmount = std::clamp(t["electric_amount"].value_or(0.5), 0.0, 1.0);
  m_eBranches = std::clamp(t["electric_branches"].value_or(0.5), 0.0, 1.0);
  m_eGlow = std::clamp(t["electric_glow"].value_or(0.6), 0.0, 1.0);
  m_eWidth = std::clamp(t["electric_width"].value_or(1.0), 0.3, 3.0);
  m_ePreview = t["electric_preview"].value_or(-1.0);
  {
    // the globe's filaments: spread round it (the golden angle), each drifting
    // its own way at its own pace
    BoltRandom r(7);
    for (size_t i = 0; i < kFilaments; ++i) {
      m_filAngle[i] = static_cast<double>(i) * 2.39996 + r.range(-0.3F, 0.3F);
      m_filSpeed[i] = (r.next() < 0.5F ? -1 : 1) * r.range(0.25F, 0.8F);
      m_filSeed[i] = 101 + static_cast<uint32_t>(i) * 7919;
    }
    for (size_t k = 0; k < m_arcSeed.size(); ++k) m_arcSeed[k] = 3 + static_cast<uint32_t>(k) * 104729;
    if (m_ePreview >= 0) {  // one strike, frozen
      m_boltAge = {m_ePreview, -1, -1, -1};
      m_boltGain[0] = 1;
      m_boltSeed[0] = 17;
      m_boltX0[0] = 0.42F;
      m_boltX1[0] = 0.58F;
      m_boltY1[0] = 0.95F;
    }
  }
  if (m_cfg.colorMode == "theme") {
    m_haloA = noct.color("primary");
    m_haloB = noct.color("secondary");
  } else {
    m_haloA = noct.color(m_cfg.color);
    m_haloB = noct.color(m_cfg.colorMode == "gradient" ? m_cfg.color2 : m_cfg.color);
  }
}

// Keeps a signal's recent range (floor .. peak) and maps it to 0..1, so a
// quiet song and a loud one both use the full movement: the peak jumps up and
// sinks over `span` seconds; the floor drops at once and rises slowly.
static double normalise(double x, double& floor, double& peak, double dt, double span) {
  peak = std::max(x, peak * std::exp(-dt / span));
  floor = x < floor ? x : floor + (x - floor) * (1 - std::exp(-dt / (span * 0.5)));
  const double range = std::max(peak - floor, 0.05);  // silence is not stretched into noise
  return std::clamp((x - floor) / range, 0.0, 1.0);
}

// The halo's motion, from the analyser's bands, every signal normalised to
// the song's own recent range:
//   pump  - the bass envelope (fast up, slower down): the light pulses with the groove
//   breath - the energy relative to its recent level, on a damped spring: the size
//            swells with the phrasing
//   kicks - the analyser's own kick detector, which reads the samples (the bands
//           come from an 85 ms window and are smoothed, so kicks from them land
//           ~100 ms late); the demo spectrum has none, so there it falls back to
//           bass flux over mean + 1.6 sd of itself: a flash, and a wave
//   tone  - the spectral centroid: the colour drifts toward the secondary
void Visualizer::tickRing(double dt, const std::vector<float>* raw, const AudioFrame& audio) {
  m_ringTime += dt;
  const bool live = raw && !raw->empty() && m_motion.fade() > 0.01;
  const size_t n = live ? raw->size() : 0;
  double energy = 0, bass = 0, centroid = 0, sum = 0;
  const size_t nb = std::max<size_t>(3, n / 6);  // the lowest sixth: kick drum, 808s and bass
  for (size_t i = 0; i < n; ++i) {
    const double v = std::clamp(static_cast<double>((*raw)[i]) * m_cfg.gain, 0.0, 1.5);
    energy += v;
    if (i < nb) bass += v * (1.0 - 0.5 * static_cast<double>(i) / static_cast<double>(nb));  // lowest weigh most
    centroid += v * static_cast<double>(i) / static_cast<double>(std::max<size_t>(1, n - 1));
    sum += v;
  }
  if (n) {
    energy /= static_cast<double>(n);
    bass /= static_cast<double>(nb) * 0.75;
  }
  const double bassN = live ? normalise(bass, m_bassFloor, m_bassPeak, dt, 6.0) : 0.0;
  const double energyN = live ? normalise(energy, m_energyFloor, m_energyPeak, dt, 8.0) : 0.0;
  auto follow = [dt](double& env, double target, double up, double down) {
    env += (target - env) * (1 - std::exp(-dt / (target > env ? up : down)));
  };
  // how loud the song really is (normalising alone would make a ballad
  // jump like a club track): calm music keeps calmer reactions
  follow(m_loud, std::clamp(m_bassPeak / 0.55, 0.0, 1.0), 1.5, 4.0);
  // warm-up: the first second and a half of sound (a new song, after a
  // pause) eases in while the ranges settle, instead of jumping
  m_liveTime = live ? m_liveTime + dt : 0.0;
  const double warm = std::clamp(m_liveTime / 1.5, 0.0, 1.0);
  const double loud = (0.4 + 0.6 * m_loud) * warm * warm;
  // the pump: the groove in the light, on a soft curve so sustained bass
  // does not pin it at the top
  const double pumpTarget = bassN * bassN * (3 - 2 * bassN) * 0.85 * loud;
  follow(m_pump, pumpTarget, 0.025, 0.16 + 0.25 * m_cfg.smoothing);
  // the breath: a critically damped spring, no overshoot
  follow(m_energySlow, energyN, 0.6, 0.6);
  const double target = std::clamp(0.55 * energyN + 0.45 * m_energySlow, 0.0, 1.0) * m_haloBreathe * warm;
  const double omega = 7.0 + 6.0 * (1 - m_cfg.smoothing);
  m_breathVel += (omega * omega * (target - m_breath) - 2 * omega * m_breathVel) * dt;
  m_breath = std::clamp(m_breath + m_breathVel * dt, 0.0, 1.2);
  // kicks: the normalised bass rising faster than it usually does
  const double flux = std::max(0.0, bassN - m_prevBass);
  m_prevBass = bassN;
  const double k = 1 - std::exp(-dt / 0.8);
  m_fluxMean += (flux - m_fluxMean) * k;
  m_fluxVar += ((flux - m_fluxMean) * (flux - m_fluxMean) - m_fluxVar) * k;
  const double threshold = m_fluxMean + 1.6 * std::sqrt(m_fluxVar) + 0.03;
  m_sinceBeat += dt;
  m_trace = {energyN, bassN, flux, threshold, m_breath, m_hit, m_tone, false};
  const bool kicked = audio.onsets ? audio.kicks != m_lastKicks : flux > threshold && bassN > 0.35 && m_sinceBeat > 0.15;
  m_lastKicks = audio.kicks;
  if (live && warm > 0.6 && kicked) {
    m_trace.kick = true;
    m_sinceBeat = 0;
    const double strength = audio.onsets ? audio.kickStrength
                                         : std::clamp((flux - threshold) / std::max(threshold, 0.02) * 0.5 + 0.5, 0.3, 1.0);
    m_hit = std::max(m_hit, strength * m_haloHits * loud);
    if (m_haloWaves && m_haloHits > 0 && strength * loud > 0.55) {  // only the strong hits send a wave
      size_t slot = 0;
      for (size_t i = 1; i < m_waveAges.size(); ++i)
        if (m_waveAges[i] < 0 || (m_waveAges[slot] >= 0 && m_waveAges[i] > m_waveAges[slot])) slot = i;
      m_waveAges[slot] = 0;
      m_waveGain[slot] = strength;
    }
  }
  m_hit *= std::exp(-dt / 0.22);  // the flash dies in about a fifth of a second
  if (m_hit < 0.002) m_hit = 0;
  if (!live) {
    m_pump *= std::exp(-dt / 0.4);
    m_energySlow *= std::exp(-dt / 0.8);
  }
  // the tone: slow, so colour drifts rather than flickers
  const double tone = sum > 0.02 ? std::clamp((centroid / sum - 0.12) / 0.3, 0.0, 1.0) : m_tone;
  m_tone += (tone - m_tone) * (1 - std::exp(-dt / 1.6));
  for (double& a : m_waveAges)
    if (a >= 0) {
      a += dt / 0.9;
      if (a >= 1) a = -1;
    }
  m_trace.pump = m_pump;
  // the vortex turns faster with the music; a kick gives a burst that brakes
  if (m_trace.kick) m_vBurst = std::min(1.5, m_vBurst + 0.8 * m_haloHits);
  m_vBurst *= std::exp(-dt / 0.6);
  const double spin = m_vSpeed * (0.35 + 0.65 * m_breath + 0.5 * m_pump + m_vBurst);
  m_vPhase = std::fmod(m_vPhase + dt * spin, 2 * std::numbers::pi * 1000);
  m_vFlow = std::fmod(m_vFlow + dt * (1.2 + 2.5 * m_pump + 2.0 * m_vBurst) * (0.3 + m_vSpeed), 2 * std::numbers::pi * 1000);
  // fire: a kick throws a flare that falls back; flames rise faster with energy
  if (m_trace.kick) m_fFlare = std::min(1.0, m_fFlare + 0.7 * m_haloHits);
  m_fFlare *= std::exp(-dt / 0.45);
  m_fTime = std::fmod(m_fTime + dt * m_fSpeed * (0.9 + 0.8 * m_breath + 0.6 * m_pump), 1000.0);
  m_fSparkTime = std::fmod(m_fSparkTime + dt * m_fSpeed * (0.8 + 0.6 * m_breath + 0.8 * m_fFlare), 1000.0);
  // the muzzle flash: every kick is a shot, as hard as the kick
  if (m_styleIndex == 15 && !m_mPreview) {
    for (double& a : m_shotAge)
      if (a >= 0) a = a + dt > 1.6 ? -1 : a + dt;  // the smoke is gone at 1.6 s
    // the shot starts as old as the kick already is (a frame at most)
    if (m_trace.kick)
      shoot(std::clamp(0.45 + 0.55 * m_hit / std::max(0.05, m_haloHits), 0.45, 1.0), audio.onsets ? std::min(audio.kickAge, 0.05) : 0.0);
  }
  if (m_look) {
    Beat b;
    b.dt = dt;
    b.kick = m_trace.kick;
    b.strength = std::clamp(m_hit / std::max(0.05, m_haloHits * loud), 0.3, 1.0);
    b.energy = std::clamp(0.7 * m_breath + 0.6 * m_pump, 0.0, 1.0);
    b.live = live;
    m_look->tick(b);
  }
  if (m_styleIndex == 17) m_sfx.tick(dt, m_trace.kick, std::clamp(m_hit / std::max(0.05, m_haloHits * loud), 0.3, 1.0));
  if (m_styleIndex == 16) {
    m_eLive = live;
    electricTick(dt, m_trace.kick, std::clamp(m_hit / std::max(0.05, m_haloHits), 0.3, 1.0));
  }
}

// Electricity's time: the louder, the faster its paths are drawn anew (an arc
// re-forms 7 to 28 times a second); a kick flashes it, re-forms everything at
// once and, as bolts, strikes.
void Visualizer::electricTick(double dt, bool kicked, double strength) {
  if (m_ePreview >= 0) {
    m_eLive = true;
    m_eEnergy = 0.6;
    m_eFlash = std::exp(-m_ePreview / 0.18);
    return;
  }
  m_eTime += dt;
  m_eEnergy = std::clamp(0.7 * m_breath + 0.6 * m_pump, 0.0, 1.0);
  const double flashGain = std::clamp(m_haloHits / 0.7, 0.0, 1.45);
  if (kicked) m_eFlash = std::max(m_eFlash, strength * flashGain);
  m_eFlash *= std::exp(-dt / 0.18);
  if (m_eFlash < 0.003) m_eFlash = 0;
  const double interval = m_eLive ? 0.14 - 0.105 * m_eEnergy : 0.12;
  for (size_t k = 0; k < m_arcSeed.size(); ++k)
    if (kicked || m_eTime >= m_arcAt[k]) {
      m_arcSeed[k] = nextSeed();
      m_arcAt[k] = m_eTime + interval * m_eRnd.range(0.7F, 1.3F);
    }
  for (size_t i = 0; i < kFilaments; ++i) {
    m_filAngle[i] += dt * m_filSpeed[i] * (0.4 + 1.2 * m_eEnergy + 2.0 * m_eFlash);
    if (kicked || m_eTime >= m_filAt[i]) {
      m_filSeed[i] = nextSeed();
      m_filAt[i] = m_eTime + (m_eLive ? 0.09 - 0.04 * m_eEnergy : 0.12) * m_eRnd.range(0.7F, 1.3F);
    }
  }
  for (double& a : m_boltAge)
    if (a >= 0) a = a + dt > 0.6 ? -1 : a + dt;
  if (kicked && m_eForm == "bolts") {
    strike(strength, 0);
    if (m_eAmount > 0.6 && strength > 0.7) strike(strength * 0.7, 0);  // a hard kick, a second fork of the sky
  }
}

void Visualizer::strike(double gain, double age) {
  size_t slot = 0;  // a free slot, else the oldest bolt
  for (size_t i = 0; i < m_boltAge.size(); ++i) {
    if (m_boltAge[i] < 0) {
      slot = i;
      break;
    }
    if (m_boltAge[i] > m_boltAge[slot]) slot = i;
  }
  m_boltAge[slot] = age;
  m_boltGain[slot] = gain;
  m_boltSeed[slot] = nextSeed();
  m_boltX0[slot] = m_eRnd.range(0.12F, 0.88F);
  m_boltX1[slot] = std::clamp(m_boltX0[slot] + m_eRnd.range(-0.3F, 0.3F), 0.05F, 0.95F);
  m_boltY1[slot] = m_eRnd.range(0.7F, 1.0F);
}

void Visualizer::shoot(double gain, double age) {
  size_t slot = 0;  // a free slot, else the oldest shot
  for (size_t i = 0; i < m_shotAge.size(); ++i) {
    if (m_shotAge[i] < 0) {
      slot = i;
      break;
    }
    if (m_shotAge[i] > m_shotAge[slot]) slot = i;
  }
  m_shotAge[slot] = age;
  m_shotGain[slot] = gain;
  m_shotCounter = std::fmod(m_shotCounter + 1, 997.0);
  m_shotSeed[slot] = m_shotCounter * 1.618 + 0.37;  // every shot its own shape
  shapeShot(slot);
}

// a number in 0..1 from a shot's seed and a salt (lowbias32)
static float shotRandom(double seed, uint32_t salt) {
  uint32_t x = static_cast<uint32_t>(std::llround(seed * 1000.0)) ^ (salt * 0x9E3779B9U);
  x ^= x >> 16;
  x *= 0x7FEB352DU;
  x ^= x >> 15;
  x *= 0x846CA68BU;
  x ^= x >> 16;
  return static_cast<float>(x >> 8) / 16777216.0F;
}

void Visualizer::shapeShot(size_t slot) {
  const double sd = m_shotSeed[slot];
  auto R = [sd](uint32_t salt) { return shotRandom(sd, salt); };
  auto mix = [](float a, float b, float t) { return a + (b - a) * t; };
  auto put = [](auto& arr, size_t at, float x, float y, float z, float w) {
    arr[at * 4] = x;
    arr[at * 4 + 1] = y;
    arr[at * 4 + 2] = z;
    arr[at * 4 + 3] = w;
  };
  // forward: three tongues fanned a little, the middle one longest
  const float tilt = (R(5) - 0.5F) * 0.06F;
  for (int t = 0; t < 3; ++t) {
    const float ang = static_cast<float>(t - 1) * mix(0.1F, 0.24F, R(1 + t)) + tilt;
    const float len = t == 1 ? mix(0.8F, 1.0F, R(0)) : mix(0.45F, 0.75F, R(10 + t));
    put(m_lobes, slot * 9 + t, std::cos(ang), std::sin(ang), len, t == 1 ? 0.13F : 0.1F);
  }
  // sideways: short, sharp spikes, alternating sides
  for (int k = 0; k < 6; ++k) {
    const float ang = (k % 2 == 0 ? 1.0F : -1.0F) * mix(0.75F, 1.45F, R(20 + k));
    put(m_lobes, slot * 9 + 3 + k, std::cos(ang), std::sin(ang), mix(0.14F, 0.32F, R(30 + k)), 0.065F);
  }
  // sparks: a fan of streaks (a share of them, by muzzle_sparks)
  for (int j = 0; j < 14; ++j) {
    const float ang = (R(40 + j) - 0.5F) * 1.3F;
    const bool on = R(60 + j) <= m_mSparks;
    put(m_sparks, slot * 14 + j, std::cos(ang), std::sin(ang), mix(1.2F, 3.6F, R(80 + j)), on ? 0.3F + 0.35F * R(100 + j) : -1.0F);
  }
  // smoke: puffs spread along the shot's path
  for (int k = 0; k < 4; ++k)
    put(m_puffs, slot * 4 + k, mix(0.08F, 0.6F, (static_cast<float>(k) + R(120 + k)) / 4.0F), (R(130 + k) - 0.5F) * 0.18F,
        mix(0.3F, 0.5F, R(140 + k)), 0.0F);
}

void Visualizer::drawElectric(const DrawContext& ctx) {
  const float w = ctx.w, h = ctx.h;
  const float energy = static_cast<float>(m_eEnergy), flash = static_cast<float>(std::min(1.0, m_eFlash));
  const float ui = std::clamp(std::min(w, h) / 200.0F, 0.7F, 2.2F);  // a big box, a bigger bolt
  const float live = m_eLive ? 1.0F : 0.0F;
  // ink wants a bolder stroke than light, which has its glow around it
  const float core = (1.3F + 1.1F * energy * live + 1.6F * flash) * static_cast<float>(m_eWidth) * ui * (m_eManga ? 1.8F : 1.0F);
  const float glow = (6 + 12 * static_cast<float>(m_eGlow)) * ui * (0.8F + 0.4F * energy * live + 0.5F * flash);
  auto depthFor = [](float len) { return std::clamp(static_cast<int>(std::lround(std::log2(std::max(16.0F, len) / 7))), 3, 7); };
  m_boltSegs.clear();

  if (m_eForm == "bolts") {
    // each strike: the leader comes down in ~30 ms, then the return strokes
    // flicker -- the first, a second at ~90 ms, a faint third at ~200
    for (size_t i = 0; i < m_boltAge.size(); ++i) {
      const double a = m_boltAge[i];
      if (a < 0) continue;
      const double I = m_boltGain[i] * (std::exp(-a / 0.07) + (a > 0.09 ? 0.75 * std::exp(-(a - 0.09) / 0.05) : 0) +
                                        (a > 0.2 ? 0.4 * std::exp(-(a - 0.2) / 0.06) : 0));
      if (I < 0.02) continue;
      BoltShape b;
      const float x0 = m_boltX0[i] * w, x1 = m_boltX1[i] * w, y1 = m_boltY1[i] * h;
      const float len = std::hypot(x1 - x0, y1);
      b.rough = 0.16F;
      b.branches = static_cast<float>(m_eBranches) * 0.9F;
      b.width = (1.6F + 1.6F * static_cast<float>(m_boltGain[i])) * static_cast<float>(m_eWidth) * ui;
      b.intensity = static_cast<float>(std::min(1.3, I));
      b.depth = depthFor(len);
      b.reveal = static_cast<float>(std::clamp(a / 0.03, 0.0, 1.0));
      b.width *= m_eManga ? 1.6F : 1.0F;
      b.boundX0 = 2, b.boundY0 = 0, b.boundX1 = w - 2, b.boundY1 = h - 2;
      lightning(m_boltSegs, x0, -2, x1, y1, b, m_boltSeed[i]);
    }
  } else if (m_eForm == "plasma") {
    const float cx = w / 2, cy = h / 2, R = std::max(8.0F, std::min(w, h) / 2 - glow * 0.7F), r0 = R * 0.1F;
    // the glass, faint (or an inked circle), and the electrode at its heart
    const int ringN = 72;
    for (int k = 0; k < ringN; ++k) {
      const float a0 = 6.2831853F * k / ringN, a1 = 6.2831853F * (k + 1) / ringN;
      m_boltSegs.push_back({cx + R * std::cos(a0), cy + R * std::sin(a0), cx + R * std::cos(a1), cy + R * std::sin(a1),
                            m_eManga ? 1.4F * ui : 1.0F * ui, m_eManga ? 1.0F : 0.10F});
    }
    m_boltSegs.push_back({cx, cy, cx, cy, r0 * 2, std::min(1.2F, 0.75F + 0.5F * flash)});
    const int n = std::min<int>(kFilaments, 3 + static_cast<int>(std::lround(m_eAmount * 6)) + (flash > 0.3F ? 3 : 0));
    for (int i = 0; i < n; ++i) {
      const float ang = static_cast<float>(m_filAngle[static_cast<size_t>(i)]);
      const float dx = std::cos(ang), dy = std::sin(ang);
      BoltRandom flick(m_filSeed[static_cast<size_t>(i)]);
      BoltShape b;
      b.rough = 0.11F + 0.08F * energy * live + 0.06F * flash;
      b.branches = static_cast<float>(m_eBranches) * (0.35F + 0.5F * flash);
      b.width = core * 0.7F;
      b.intensity = (m_eLive ? 0.4F + 0.6F * energy : 0.3F) * flick.range(0.75F, 1.0F) + 0.5F * flash;
      b.depth = depthFor(R);
      b.boundCx = cx, b.boundCy = cy, b.boundR = R * 0.97F;
      const float ex = cx + dx * R * 0.985F, ey = cy + dy * R * 0.985F;
      lightning(m_boltSegs, cx + dx * r0, cy + dy * r0, ex, ey, b, m_filSeed[static_cast<size_t>(i)]);
      m_boltSegs.push_back({ex, ey, ex, ey, b.width * 2.2F, b.intensity});  // where it touches the glass
    }
  } else {
    // the arc: end to end across the box, the zigzag kept inside it
    const float mx = w * 0.04F, len = w - 2 * mx;
    const float rough = std::min(0.10F + 0.10F * energy * live + 0.12F * flash, h * 0.42F / std::max(1.0F, 2 * len));
    const int strands = 1 + static_cast<int>(std::lround(m_eAmount * 2)) + (flash > 0.3F ? 1 : 0);
    for (int k = 0; k < std::min<int>(strands, static_cast<int>(m_arcSeed.size())); ++k) {
      BoltShape b;
      b.rough = rough * (k == 0 ? 1.0F : 1.25F);
      b.branches = static_cast<float>(m_eBranches) * (0.25F + 0.3F * energy * live + 0.7F * flash);
      b.width = core * (k == 0 ? 1.0F : 0.6F);
      b.intensity = (k == 0 ? 1.0F : 0.55F) * (m_eLive ? 0.45F + 0.55F * energy : 0.3F) + 0.6F * flash;
      b.depth = depthFor(len);
      const float edge = std::min(h * 0.08F, glow * 0.5F);  // its glow too should mostly stay in
      b.boundX0 = edge, b.boundY0 = edge, b.boundX1 = w - edge, b.boundY1 = h - edge;
      lightning(m_boltSegs, mx, h / 2, w - mx, h / 2, b, m_arcSeed[static_cast<size_t>(k)]);
    }
    // the two ends glow where the charge leaves and lands
    const float endI = std::min(1.2F, (m_eLive ? 0.5F + 0.5F * energy : 0.35F) + 0.5F * flash);
    m_boltSegs.push_back({mx, h / 2, mx, h / 2, core * 2.4F, endI});
    m_boltSegs.push_back({w - mx, h / 2, w - mx, h / 2, core * 2.4F, endI});
  }

  StrokeRenderer::Look look;
  look.mode = m_eManga ? StrokeRenderer::Mode::Ink : StrokeRenderer::Mode::Light;
  look.glow = glow;
  look.ink = 1.6F * ui;
  look.opacity = opacityNow();
  if (m_eTheme) {
    const Color c = m_haloA.mix(Color{1, 1, 1, 1}, 0.8F);
    look.core[0] = c.r, look.core[1] = c.g, look.core[2] = c.b;
    look.halo[0] = m_haloA.r, look.halo[1] = m_haloA.g, look.halo[2] = m_haloA.b;
  } else {
    look.core[0] = 0.95F, look.core[1] = 0.97F, look.core[2] = 1.0F;
    look.halo[0] = 0.42F, look.halo[1] = 0.62F, look.halo[2] = 1.0F;
  }
  m_bolts.draw(m_boltSegs, w, h, look);
}

void Visualizer::drawMuzzle(const DrawContext& ctx) {
  if (!m_muzzleProg.valid()) m_muzzleProg.create(kQuadVertexShader, kMuzzleFrag, "muzzle");
  glUseProgram(m_muzzleProg.id());
  auto U = [&](const char* nm) { return m_muzzleProg.uniform(nm); };
  float age[4], seed[4], gain[4];
  for (size_t i = 0; i < 4; ++i) {
    age[i] = static_cast<float>(m_shotAge[i]);
    seed[i] = static_cast<float>(m_shotSeed[i]);
    gain[i] = static_cast<float>(m_shotGain[i]);
  }
  glUniform2f(U("res"), ctx.w, ctx.h);
  glUniform1fv(U("u_age"), 4, age);
  glUniform1fv(U("u_seed"), 4, seed);
  glUniform1fv(U("u_gain"), 4, gain);
  glUniform4fv(U("u_lobe"), 4 * 9, m_lobes.data());
  glUniform4fv(U("u_spark"), 4 * 14, m_sparks.data());
  glUniform4fv(U("u_puff"), 4 * 4, m_puffs.data());
  glUniform1f(U("u_pump"), static_cast<float>(m_pump * m_haloPulse));
  glUniform1f(U("u_manga"), m_mManga ? 1.0F : 0.0F);
  glUniform1f(U("u_theme"), m_mTheme ? 1.0F : 0.0F);
  glUniform3f(U("u_c1"), m_haloA.r, m_haloA.g, m_haloA.b);
  glUniform3f(U("u_c2"), m_haloB.r, m_haloB.g, m_haloB.b);
  glUniform1f(U("u_length"), static_cast<float>(m_mLength));
  glUniform1f(U("u_spikes"), static_cast<float>(m_mSpikes));
  glUniform1f(U("u_sparks"), static_cast<float>(m_mSparks));
  glUniform1f(U("u_smoke"), static_cast<float>(m_mSmoke));
  glUniform1f(U("u_opacity"), opacityNow());
  glDisable(GL_BLEND);
  drawUnitQuad();
}

void Visualizer::drawVortex(const DrawContext& ctx) {
  if (!m_vortexProg.valid()) m_vortexProg.create(kQuadVertexShader, kVortexFrag, "vortex");
  glUseProgram(m_vortexProg.id());
  auto U = [&](const char* nm) { return m_vortexProg.uniform(nm); };
  const float half = std::min(ctx.w, ctx.h) * 0.5F;
  glUniform2f(U("res"), ctx.w, ctx.h);
  glUniform3f(U("u_primary"), m_haloA.r, m_haloA.g, m_haloA.b);
  glUniform3f(U("u_secondary"), m_haloB.r, m_haloB.g, m_haloB.b);
  glUniform1f(U("u_radius"), static_cast<float>(m_haloInner / 2 + 0.2));  // the same ring as the halo
  glUniform1f(U("u_width"), static_cast<float>(m_haloWidth));
  glUniform1f(U("u_reach"), static_cast<float>(m_vReach));
  glUniform1f(U("u_arms"), static_cast<float>(m_vArms));
  glUniform1f(U("u_twist"), static_cast<float>(m_vTwist));
  glUniform1f(U("u_turb"), static_cast<float>(m_vTurb));
  glUniform1f(U("u_phase"), static_cast<float>(m_vPhase));
  glUniform1f(U("u_flow"), static_cast<float>(m_vFlow));
  glUniform1f(U("u_dir"), m_vClockwise ? -1.0F : 1.0F);
  // inward: outside the ring, drawn in; outward: outside, pouring out;
  // inside: a tunnel inside the ring, sinking to the centre; inside_out: welling up from it
  const bool inside = m_vMode == "inside" || m_vMode == "inside_out";
  const bool towardCentre = m_vMode == "inward" || m_vMode == "inside";
  glUniform1f(U("u_inside"), inside ? 1.0F : 0.0F);
  glUniform1f(U("u_flowSign"), towardCentre ? 1.0F : -1.0F);
  glUniform1f(U("u_intensity"), static_cast<float>(m_haloBloom));
  glUniform1f(U("u_ring"), static_cast<float>(m_vRing));
  glUniform1f(U("u_breath"), static_cast<float>(m_breath));
  glUniform1f(U("u_pump"), static_cast<float>(m_pump * m_haloPulse));
  glUniform1f(U("u_hit"), static_cast<float>(m_hit));
  glUniform1f(U("u_tone"), static_cast<float>(m_tone));
  float ages[4], gains[4];
  for (size_t i = 0; i < 4; ++i) {
    ages[i] = static_cast<float>(m_waveAges[i]);
    gains[i] = static_cast<float>(m_waveGain[i]);
  }
  glUniform1fv(U("u_waves"), 4, ages);
  glUniform1fv(U("u_waveGain"), 4, gains);
  glUniform1f(U("u_opacity"), opacityNow());
  glUniform1f(U("u_px"), 1.0F / std::max(1.0F, half * ctx.scale));
  glDisable(GL_BLEND);
  drawUnitQuad();
}

void Visualizer::drawFire(const DrawContext& ctx) {
  if (!m_fireProg.valid()) m_fireProg.create(kQuadVertexShader, kFireFrag, "fire");
  Motion::resample(m_motion.levels, 32, m_drawLevels);
  glUseProgram(m_fireProg.id());
  auto U = [&](const char* nm) { return m_fireProg.uniform(nm); };
  glUniform2f(U("res"), ctx.w, ctx.h);
  glUniform1fv(U("u_lv"), 32, m_drawLevels.data());
  glUniform3f(U("u_c1"), m_haloA.r, m_haloA.g, m_haloA.b);
  glUniform3f(U("u_c2"), m_haloB.r, m_haloB.g, m_haloB.b);
  glUniform1f(U("u_theme"), m_fTheme ? 1.0F : 0.0F);
  glUniform1f(U("u_shape"), m_fShape == "wall" ? 1.0F : 0.0F);
  glUniform1f(U("u_height"), static_cast<float>(m_fHeight));
  glUniform1f(U("u_turb"), static_cast<float>(m_fTurb));
  glUniform1f(U("u_spectrum"), static_cast<float>(m_fSpectrum));
  glUniform1f(U("u_sparks"), static_cast<float>(m_fSparks));
  glUniform1f(U("u_time"), static_cast<float>(m_fTime));
  glUniform1f(U("u_sparkTime"), static_cast<float>(m_fSparkTime));
  glUniform1f(U("u_breath"), static_cast<float>(m_breath));
  glUniform1f(U("u_pump"), static_cast<float>(m_pump * m_haloPulse));
  glUniform1f(U("u_hit"), static_cast<float>(m_hit));
  glUniform1f(U("u_flare"), static_cast<float>(m_fFlare));
  glUniform1f(U("u_opacity"), opacityNow());
  glDisable(GL_BLEND);
  drawUnitQuad();
}

void Visualizer::drawRing(const DrawContext& ctx) {
  if (!m_ringProg.valid()) m_ringProg.create(kQuadVertexShader, kRingFrag, "ring");
  glUseProgram(m_ringProg.id());
  auto U = [&](const char* nm) { return m_ringProg.uniform(nm); };
  const float half = std::min(ctx.w, ctx.h) * 0.5F;
  glUniform2f(U("res"), ctx.w, ctx.h);
  glUniform3f(U("u_primary"), m_haloA.r, m_haloA.g, m_haloA.b);
  glUniform3f(U("u_secondary"), m_haloB.r, m_haloB.g, m_haloB.b);
  glUniform1f(U("u_radius"), static_cast<float>(m_haloInner / 2 + 0.2));
  glUniform1f(U("u_width"), static_cast<float>(m_haloWidth));
  glUniform1f(U("u_spread"), static_cast<float>(m_haloSpread));
  glUniform1f(U("u_intensity"), static_cast<float>(m_haloBloom));
  glUniform1f(U("u_breath"), static_cast<float>(m_breath));
  glUniform1f(U("u_hit"), static_cast<float>(m_hit));
  glUniform1f(U("u_pump"), static_cast<float>(m_pump * m_haloPulse));
  glUniform1f(U("u_tone"), static_cast<float>(m_tone));
  glUniform1f(U("u_aurora"), static_cast<float>(m_haloAurora));
  glUniform1fv(U("u_shapeR"), 128, m_haloFormR.data());
  glUniform1f(U("u_shaped"), m_haloForm != "circle" && m_haloForm != "none" ? 1.0F : 0.0F);
  glUniform1f(U("u_time"), static_cast<float>(std::fmod(m_ringTime, 3600.0)));
  float ages[4], gains[4];
  for (size_t i = 0; i < 4; ++i) {
    ages[i] = static_cast<float>(m_waveAges[i]);
    gains[i] = static_cast<float>(m_waveGain[i]);
  }
  glUniform1fv(U("u_waves"), 4, ages);
  glUniform1fv(U("u_waveGain"), 4, gains);
  glUniform1f(U("u_opacity"), opacityNow());
  glUniform1f(U("u_px"), 1.0F / std::max(1.0F, half * ctx.scale));
  glDisable(GL_BLEND);
  drawUnitQuad();
}

void Visualizer::tick(const TickContext& ctx) {
  // hiding with nothing playing: a smooth fade either way
  m_hideFade += ((m_hidden ? 0.0 : 1.0) - m_hideFade) * (1 - std::exp(-ctx.dt / (m_hidden ? 0.18 : 0.15)));
  if (std::abs(m_hideFade - (m_hidden ? 0.0 : 1.0)) < 0.002) m_hideFade = m_hidden ? 0.0 : 1.0;
  static const std::vector<float> kEmpty;
  m_motion.tick(ctx.dt, (ctx.audio.silent || !ctx.audio.bands) ? kEmpty : *ctx.audio.bands, ctx.audio.energy);
  if (m_styleIndex >= 12) tickRing(ctx.dt, (ctx.audio.silent || !ctx.audio.bands) ? nullptr : ctx.audio.bands, ctx.audio);
}

void Visualizer::draw(const DrawContext& ctx) {
  if (m_styleIndex == 12) {
    drawRing(ctx);
    return;
  }
  if (m_styleIndex == 13) {
    drawVortex(ctx);
    return;
  }
  if (m_styleIndex == 14) {
    drawFire(ctx);
    return;
  }
  if (m_styleIndex == 15) {
    drawMuzzle(ctx);
    return;
  }
  if (m_styleIndex == 16) {
    drawElectric(ctx);
    return;
  }
  if (m_styleIndex == 17) {
    m_sfx.draw(ctx, opacityNow());
    return;
  }
  if (m_look) {
    m_look->draw(ctx, opacityNow());
    return;
  }
  const float w = ctx.w, h = ctx.h, outputW = ctx.outputW, outputH = ctx.outputH;
  if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kSpectrumFrag, "spectrum");

  const bool polar = m_styleIndex >= 9;
  const bool frame = m_styleIndex == 8;
  const std::string& grow = m_cfg.grow;
  const bool vertical = grow == "left" || grow == "right";
  const bool centred = (grow == "center" && m_cfg.style != "curtain") || m_cfg.style == "split";
  const int bands = m_motion.bands;
  const float ui = std::clamp(std::min(outputW, outputH) / 900.0F, 0.75F, 2.5F);
  const float glow = static_cast<float>(m_cfg.glow);
  const float thickness = static_cast<float>(m_cfg.thickness);

  // The box is the surface minus room for the bloom to fall off in (Ryoku's
  // pass = box + margin). Margin depends on the band width, which depends on
  // the box: one refinement step settles it.
  float margin = 2 * ui;
  float bw = w, bh = h;
  float r0 = 0, rMax = 0, shapePx = 2, glowPx = 1.5F, maxLen = 2, reflectPx = 0;
  int drawBands = bands;
  const float framePerim = 2 * (outputW + outputH);
  for (int pass = 0; pass < 2; ++pass) {
    bw = frame ? w : std::max(24.0F, w - 2 * margin);
    bh = frame ? h : std::max(24.0F, h - 2 * margin);
    const float acrossFull = vertical ? bw : bh;
    const float alongFull = vertical ? bh : bw;
    reflectPx = (grow == "up" && !polar && !centred && !frame) ? std::round(acrossFull * static_cast<float>(m_cfg.reflection)) : 0;
    maxLen = frame ? std::max(6.0F, std::min(outputW, outputH) * 0.06F) : std::max(2.0F, std::round(acrossFull - reflectPx));
    const float radius = std::max(6.0F, std::min(bw, bh) / 2);
    r0 = radius * (m_cfg.style == "orb" ? 0.62F : 0.38F);
    rMax = radius - r0;
    drawBands = polar ? std::max(10, std::min(bands, static_cast<int>(std::floor(2 * std::numbers::pi_v<float> * r0 / (14 * ui)))))
                      : bands;
    const float slotPx = (frame ? framePerim : alongFull) / static_cast<float>(drawBands);
    shapePx = frame ? std::max(2.0F, framePerim / (drawBands * 3.0F) * thickness)
                    : (polar ? std::max(3.0F, 2 * std::numbers::pi_v<float> * r0 / drawBands * thickness)
                             : std::max(2.0F, slotPx * thickness));
    glowPx = std::max(1.5F, std::min(shapePx * 0.9F, 12 * ui)) * (0.35F + 0.65F * std::clamp(glow, 0.0F, 1.0F));
    margin = std::ceil(glow > 0 ? glowPx * 3.5F + 2 : 2 * ui);
  }
  const float pad = frame ? 0 : margin;

  // levels: a folded ring averages its groups first
  const std::vector<float>* lv = &m_motion.levels;
  const std::vector<float>* pk = &m_motion.peaks;
  if (drawBands < bands) {
    Motion::resample(m_motion.levels, drawBands, m_drawLevels);
    lv = &m_drawLevels;
    if (m_motion.wantPeaks) {
      Motion::resample(m_motion.peaks, drawBands, m_drawPeaks);
      pk = &m_drawPeaks;
    }
  }

  const float fade = static_cast<float>(m_motion.fade());
  glUseProgram(m_prog.id());
  auto U = [&](const char* n) { return m_prog.uniform(n); };
  glUniform1fv(U("u_lv"), static_cast<GLsizei>(std::min<size_t>(128, lv->size())), lv->data());
  if (m_motion.wantPeaks) glUniform1fv(U("u_pk"), static_cast<GLsizei>(std::min<size_t>(128, pk->size())), pk->data());
  const char* cn[] = {"c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7"};
  for (int i = 0; i < 8; ++i) glUniform4f(U(cn[i]), m_ramp[i].r, m_ramp[i].g, m_ramp[i].b, 1.0F);
  glUniform2f(U("res"), w, h);
  glUniform2f(U("origin"), pad + bw / 2, pad + bh / 2);
  glUniform1f(U("style"), static_cast<float>(m_styleIndex));
  const float posMode = frame ? 0.0F
                              : (grow == "down" ? 1.0F : grow == "center" ? 2.0F : grow == "left" ? 3.0F : grow == "right" ? 4.0F : 0.0F);
  glUniform1f(U("posMode"), posMode);
  glUniform1f(U("pad"), pad);
  glUniform1f(U("bands"), static_cast<float>(drawBands));
  glUniform1f(U("maxLen"), maxLen);
  glUniform1f(U("minLen"), std::max(1.5F, 2 * ui) * (fade > 0.9F ? 1.0F : fade));
  glUniform1f(U("thickness"), std::clamp(thickness, 0.05F, 1.0F));
  glUniform1f(U("shapeW"), shapePx);
  glUniform1f(U("capR"), m_cfg.shape == "rounded" ? shapePx * 0.5F : std::min(2 * ui, shapePx * 0.2F));
  const float segN = static_cast<float>(std::clamp(m_cfg.segments, 3, 24));
  glUniform1f(U("segN"), segN);
  glUniform1f(U("segGap"), std::max(1.5F * ui, maxLen / segN * 0.26F));
  glUniform1f(U("gapPx"), std::max(2 * ui, maxLen * 0.06F));
  glUniform1f(U("glowAmt"), std::clamp(glow, 0.0F, 1.0F));
  glUniform1f(U("glowPx"), glowPx);
  glUniform1f(U("reflectPx"), reflectPx);
  glUniform1f(U("peakOn"), m_motion.wantPeaks ? 1.0F : 0.0F);
  glUniform1f(U("r0"), r0);
  glUniform1f(U("rMax"), rMax);
  glUniform1f(U("spinRad"), static_cast<float>(m_motion.spinDeg * std::numbers::pi / 180.0));
  glUniform1f(U("energy"), static_cast<float>(std::clamp(m_motion.maxLevel, 0.0, 1.0)));
  glUniform1f(U("fade"), fade);
  glUniform1f(U("aa"), 0.85F);
  glUniform1f(U("u_opacity"), opacityNow());
  glDisable(GL_BLEND);
  drawUnitQuad();
}

}  // namespace undershell
