// SPDX-License-Identifier: GPL-3.0-or-later
// Performance profiles: how much undershell may spend on frames, particles
// and effects. A fixed profile (eco … ultra) is what the person chose; "auto"
// starts high and a governor steps down when the measured GPU/CPU time of the
// widgets goes over budget (and back up when there is room). On battery the
// ceiling drops one step. Looks read the effective knobs with perfKnobs().
#pragma once

#include <optional>
#include <string>

namespace undershell {

enum class PerfLevel { Eco = 0, Balanced = 1, High = 2, Ultra = 3 };

struct PerfKnobs {
  int fpsCap = 0;            // frames per second at most; 0: as fast as the widget asks (the monitor)
  float renderScale = 1;     // buffer resolution of costly widgets (1: full)
  float particles = 1;       // multiplier of particle counts and emission
  int simHz = 60;            // particle simulation steps per second (interpolated in between)
  int quality = 2;           // effect quality 0..3 (blur taps, parallax layers…)
  float trails = 1;          // length of particle trails
  bool bloom = true;
  int msaa = 0;
};

[[nodiscard]] const PerfKnobs& knobsFor(PerfLevel level);
[[nodiscard]] const char* perfLevelName(PerfLevel level);
// "eco", "balanced", "high", "ultra"; nullopt for anything else ("auto" included)
[[nodiscard]] std::optional<PerfLevel> parsePerfLevel(const std::string& s);

// The knobs in effect right now (set by the App once per frame loop).
[[nodiscard]] const PerfKnobs& perfKnobs();
void setPerfKnobs(const PerfKnobs& k);

// Steps the level by measured load. Pure logic (no clock, no GL) so it can be
// tested: feed it one sample per second.
class PerfGovernor {
public:
  // GPU and render-CPU time spent per second of wall time (0..1).
  static constexpr double kHigh = 0.20;  // over this for 2 s: one step down
  static constexpr double kLow = 0.07;   // under this for 6 s: one step up
  static constexpr int kDownAfter = 2, kUpAfter = 6;
  static constexpr double kHoldAfterDown = 30;  // seconds before stepping up again

  // how many times the cost of `level` the next level up is (frame rate ×
  // pixels of the costly widgets): a step up must still fit under kHigh,
  // or the level would bounce between two steps
  [[nodiscard]] static double upCost(PerfLevel level);

  void reset(PerfLevel start, PerfLevel floor, PerfLevel ceiling);
  void setCeiling(PerfLevel ceiling);
  // one second's load; `active` false when nothing was drawn (no evidence:
  // the level stays). Returns the level after the sample.
  PerfLevel sample(double now, double gpuBusy, double cpuBusy, bool active);
  [[nodiscard]] PerfLevel level() const { return m_level; }

private:
  PerfLevel m_level = PerfLevel::High, m_floor = PerfLevel::Eco, m_ceiling = PerfLevel::High;
  int m_over = 0, m_under = 0;
  double m_lastDown = -1e9;
};

// Running on battery: some power supply is a battery and no mains adapter is
// online (reads /sys/class/power_supply; false on desktops and when unknown).
[[nodiscard]] bool onBattery(const std::string& sysDir = "/sys/class/power_supply");

}  // namespace undershell
