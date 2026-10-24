// SPDX-License-Identifier: GPL-3.0-or-later
#include "perf.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace undershell {

namespace fs = std::filesystem;

namespace {

// eco: the HD 520 class at 30 fps; ultra: room to spare on a desktop GPU
const PerfKnobs kKnobs[4] = {
    {.fpsCap = 30, .renderScale = 0.5F, .particles = 0.4F, .simHz = 30, .quality = 0, .trails = 0.5F, .bloom = false, .msaa = 0},
    {.fpsCap = 60, .renderScale = 0.75F, .particles = 0.7F, .simHz = 30, .quality = 1, .trails = 0.75F, .bloom = true, .msaa = 0},
    {.fpsCap = 0, .renderScale = 1.0F, .particles = 1.0F, .simHz = 60, .quality = 2, .trails = 1.0F, .bloom = true, .msaa = 0},
    {.fpsCap = 0, .renderScale = 1.0F, .particles = 1.5F, .simHz = 60, .quality = 3, .trails = 1.0F, .bloom = true, .msaa = 4},
};

PerfKnobs g_knobs = kKnobs[2];

PerfLevel step(PerfLevel l, int by) { return static_cast<PerfLevel>(std::clamp(static_cast<int>(l) + by, 0, 3)); }

std::string readLine(const fs::path& p) {
  std::ifstream f(p);
  std::string s;
  std::getline(f, s);
  return s;
}

}  // namespace

const PerfKnobs& knobsFor(PerfLevel level) { return kKnobs[static_cast<int>(level)]; }

const char* perfLevelName(PerfLevel level) {
  switch (level) {
    case PerfLevel::Eco: return "eco";
    case PerfLevel::Balanced: return "balanced";
    case PerfLevel::High: return "high";
    case PerfLevel::Ultra: return "ultra";
  }
  return "high";
}

std::optional<PerfLevel> parsePerfLevel(const std::string& s) {
  if (s == "eco") return PerfLevel::Eco;
  if (s == "balanced") return PerfLevel::Balanced;
  if (s == "high") return PerfLevel::High;
  if (s == "ultra") return PerfLevel::Ultra;
  return std::nullopt;
}

const PerfKnobs& perfKnobs() { return g_knobs; }
void setPerfKnobs(const PerfKnobs& k) { g_knobs = k; }

double PerfGovernor::upCost(PerfLevel level) {
  if (level == PerfLevel::Ultra) return 1;
  const PerfKnobs &a = knobsFor(level), &b = knobsFor(step(level, +1));
  const double fa = a.fpsCap > 0 ? a.fpsCap : 60, fb = b.fpsCap > 0 ? b.fpsCap : 60;
  const double px = (b.renderScale / a.renderScale) * (b.renderScale / a.renderScale);
  return std::max(1.0, fb / fa * px);
}

void PerfGovernor::reset(PerfLevel start, PerfLevel floor, PerfLevel ceiling) {
  m_floor = floor;
  m_ceiling = std::max(floor, ceiling);
  m_level = std::clamp(start, m_floor, m_ceiling);
  m_over = m_under = 0;
  m_lastDown = -1e9;
}

void PerfGovernor::setCeiling(PerfLevel ceiling) {
  m_ceiling = std::max(m_floor, ceiling);
  m_level = std::min(m_level, m_ceiling);
}

PerfLevel PerfGovernor::sample(double now, double gpuBusy, double cpuBusy, bool active) {
  if (!active) {
    m_over = m_under = 0;
    return m_level;
  }
  const double load = std::max(gpuBusy, cpuBusy);
  m_over = load > kHigh ? m_over + 1 : 0;
  m_under = load < kLow ? m_under + 1 : 0;
  if (m_over >= kDownAfter && m_level > m_floor) {
    m_level = step(m_level, -1);
    m_over = m_under = 0;
    m_lastDown = now;
  } else if (m_under >= kUpAfter && m_level < m_ceiling && now - m_lastDown >= kHoldAfterDown &&
             load * upCost(m_level) < kHigh * 0.8) {
    m_level = step(m_level, +1);
    m_over = m_under = 0;
  }
  return m_level;
}

bool onBattery(const std::string& sysDir) {
  std::error_code ec;
  bool battery = false, mains = false;
  for (const auto& e : fs::directory_iterator(sysDir, ec)) {
    const std::string type = readLine(e.path() / "type");
    if (type == "Battery") {
      if (readLine(e.path() / "present") != "0") battery = true;
    } else if (type == "Mains" || type == "USB") {
      if (readLine(e.path() / "online") == "1") mains = true;
    }
  }
  return battery && !mains;
}

}  // namespace undershell
