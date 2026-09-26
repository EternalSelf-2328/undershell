// SPDX-License-Identifier: GPL-3.0-or-later
// The spectrum's motion, kept apart from its drawing: a port of Ryoku's
// modules/visualizer/Motion.qml + ryoku/ui/lib/spectrum.js. Analyser bands
// become eased levels, falling peaks, an activity signal and a spin.
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace undershell {

class Motion {
public:
  int bands = 64;
  double gain = 1.0, smoothing = 0.5, spin = 0.0;
  bool mirror = false, idleWave = false, wantPeaks = false;

  std::vector<float> levels, peaks;
  double activity = 0, idlePhase = 0, spinDeg = 0, maxLevel = 0;

  void configure(int nBands) {
    bands = std::clamp(nBands, 4, 128);
    if (static_cast<int>(levels.size()) != bands) {
      levels.assign(static_cast<size_t>(bands), 0.0F);
      peaks.assign(static_cast<size_t>(bands), 0.0F);
    }
  }

  static int srcIndex(int i, int n, bool mirror) {
    if (!mirror) return i;
    int c = n / 2;
    return std::clamp(std::abs(i - c), 0, n - 1);
  }

  // Fold src into exactly n buckets (average when shrinking, lerp when growing).
  static void resample(const std::vector<float>& src, int n, std::vector<float>& out) {
    out.assign(static_cast<size_t>(n), 0.0F);
    const int m = static_cast<int>(src.size());
    if (m == 0) return;
    if (m >= n) {
      for (int i = 0; i < n; ++i) {
        int a = i * m / n, b = std::max(a + 1, (i + 1) * m / n);
        float s = 0;
        for (int j = a; j < b; ++j) s += src[static_cast<size_t>(j)];
        out[static_cast<size_t>(i)] = s / static_cast<float>(b - a);
      }
    } else {
      for (int k = 0; k < n; ++k) {
        float t = n == 1 ? 0 : static_cast<float>(k) * (m - 1) / (n - 1);
        int lo = static_cast<int>(t), hi = std::min(m - 1, lo + 1);
        float f = t - lo;
        out[static_cast<size_t>(k)] = src[static_cast<size_t>(lo)] * (1 - f) + src[static_cast<size_t>(hi)] * f;
      }
    }
  }

  [[nodiscard]] bool sounding(double energy) const { return energy > 0.04 || activity > 0.02; }
  [[nodiscard]] bool animating(double energy) const {
    return sounding(energy) || idleWave || maxLevel > 0.004 || spin > 0;
  }
  // with the idle wave off, the picture releases with the activity signal
  [[nodiscard]] double fade() const {
    return idleWave ? 1.0 : std::clamp((activity - 0.02) / 0.25, 0.0, 1.0);
  }

  // raw: analyser bands normalised to 0..1 (may be empty when idle)
  void tick(double dt, const std::vector<float>& raw, double energy) {
    const double goal = energy > 0.04 ? 1.0 : 0.0;
    const double tau = goal > activity ? 0.05 : 1.1;
    activity += (goal - activity) * (1.0 - std::exp(-dt / tau));
    if (idleWave) idlePhase += dt * (std::numbers::pi * 2 / 6);
    if (spin > 0) spinDeg = std::fmod(spinDeg + dt * spin, 360.0);

    const int n = bands;
    if (!raw.empty() && static_cast<int>(raw.size()) != n) {
      resample(raw, n, m_src);
    } else {
      m_src = raw;
    }
    const double idleAmt = idleWave ? (1.0 - activity) : 0.0;
    const double kUp = 1.0 - std::exp(-dt / (0.035 + 0.02 * smoothing));
    const double kDown = 1.0 - std::exp(-dt / (0.06 + 0.20 * smoothing));
    double mx = 0;
    for (int i = 0; i < n; ++i) {
      const int s = srcIndex(i, n, mirror);
      const float v = s < static_cast<int>(m_src.size()) ? m_src[static_cast<size_t>(s)] : 0.0F;
      double target = v > 0 ? activity * std::min(1.0, std::pow(v, 0.72) * gain) : 0.0;
      if (idleAmt > 0) target += idleAmt * (0.012 + 0.02 * (0.5 + 0.5 * std::sin(s * 0.4 + idlePhase)));
      float& cur = levels[static_cast<size_t>(i)];
      cur = static_cast<float>(cur + (target - cur) * (target > cur ? kUp : kDown));
      mx = std::max(mx, static_cast<double>(cur));
    }
    maxLevel = mx;
    if (wantPeaks) {
      const float fall = static_cast<float>(dt * 0.5);
      for (int p = 0; p < n; ++p) {
        float pc = peaks[static_cast<size_t>(p)] - fall;
        float o = levels[static_cast<size_t>(p)];
        peaks[static_cast<size_t>(p)] = o > pc ? o : std::max(0.0F, pc);
      }
    }
  }

  void rest() {
    maxLevel = 0;
    std::fill(levels.begin(), levels.end(), 0.0F);
    std::fill(peaks.begin(), peaks.end(), 0.0F);
  }

private:
  std::vector<float> m_src;
};

}  // namespace undershell
