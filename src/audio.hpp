// SPDX-License-Identifier: GPL-3.0-or-later
// In-process playback spectrum: a passive PipeWire capture of the default
// sink's monitor, a 4096-point FFT and cava-style band processing (gravity,
// noise reduction, optional monstercat). Adapted from Noctalia's
// src/pipewire/pipewire_spectrum.cpp (MIT, (c) 2026 noctalia-dev).
#pragma once

#include <array>
#include <chrono>
#include <complex>
#include <cstdint>
#include <vector>

struct pw_loop;
struct pw_context;
struct pw_core;
struct pw_stream;

namespace undershell {

class Audio {
public:
  Audio();
  ~Audio();
  Audio(const Audio&) = delete;
  Audio& operator=(const Audio&) = delete;

  bool start();              // connect to PipeWire and open the capture stream
  void stop();
  [[nodiscard]] int fd() const;  // PipeWire loop fd for poll()
  void dispatch();           // call when fd() is readable

  void setBandCount(int n);
  void setNoiseReduction(float nr) { m_noiseReduction = nr; }
  void setMonstercat(bool on) { m_monstercat = on; }

  // Runs one analysis frame if one is due. Returns true when values changed.
  bool tick();
  // ms until the next analysis frame, or -1 when idle (nothing to do).
  [[nodiscard]] int pollTimeoutMs() const;

  [[nodiscard]] const std::vector<float>& values() const { return m_values; }
  [[nodiscard]] bool idle() const { return m_idle; }
  [[nodiscard]] float energy() const;
  // kick onsets straight from the samples, not the smoothed bands: a counter
  // that steps once per kick, how hard the latest was (0..1) and how long
  // before the newest sample it hit
  [[nodiscard]] uint64_t kickCount() const { return m_kicks; }
  [[nodiscard]] float kickStrength() const { return m_kickStrength; }
  [[nodiscard]] double kickAge() const { return static_cast<double>(m_samplesFed - m_kickSample) / m_sampleRate; }

  // called from the stream's process callback (same thread as dispatch())
  void feed(const float* mono, int count, bool nonZero);
  // offline analysis (tools, tests): one frame from what was fed, no clock
  void analyseFrame() { processFrame(); }
  void setSampleRate(int rate);

  struct Stream;

private:
  static constexpr int kFftSize = 4096;
  static constexpr int kFrameRateHz = 60;

  void computeBins();
  void processFrame();
  void clear();
  void setupKick();
  void onsetHop(float hopSum);

  struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    float run(float x) {
      const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      x2 = x1;
      x1 = x;
      y2 = y1;
      y1 = y;
      return y;
    }
  };

  pw_loop* m_loop = nullptr;
  pw_context* m_context = nullptr;
  pw_core* m_core = nullptr;
  Stream* m_stream = nullptr;

  int m_bandCount = 64;
  int m_sampleRate = 48000;
  float m_noiseReduction = 0.45F;
  bool m_monstercat = false;
  float m_sensitivity = 0.01F;
  bool m_sensInit = true;

  std::vector<float> m_ring;
  int m_ringPos = 0;
  bool m_ringFull = false;
  bool m_samplesReceived = false;
  bool m_idle = true;
  int m_idleFrames = 0;
  std::vector<float> m_window;
  std::vector<float> m_bins;
  std::vector<std::complex<float>> m_fft;
  std::vector<float> m_bands, m_prev, m_peak, m_fall, m_mem, m_values;
  std::chrono::steady_clock::time_point m_nextFrameAt{};

  // the kick detector (feed()): the low band in 2 ms hops
  std::array<Biquad, 2> m_low{};
  int m_hopLen = 96, m_hopFill = 0;
  float m_hopAcc = 0;
  std::array<float, 6> m_hopSums{};  // the 12 ms window the level is read over
  std::array<float, 24> m_powers{};  // the latest hops' window power, linear
  size_t m_hop = 0;
  float m_levelPeak = -100, m_riseMean = 0, m_riseVar = 0;
  int64_t m_samplesFed = 0, m_kickSample = -(int64_t{1} << 40);
  uint64_t m_kicks = 0;
  float m_kickStrength = 0;
};

}  // namespace undershell
