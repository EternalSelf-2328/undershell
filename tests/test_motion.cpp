// Ryoku's motion: resampling and attack/decay easing.
#include "audio.hpp"
#include "check.hpp"
#include "motion.hpp"

#include <cmath>
#include <vector>

using namespace undershell;

int main() {
  std::vector<float> out;
  Motion::resample({1, 1, 0, 0}, 2, out);
  CHECK(out.size() == 2 && out[0] == 1.0F && out[1] == 0.0F);
  Motion::resample({0, 1}, 3, out);
  CHECK(out.size() == 3 && out[1] > 0.49F && out[1] < 0.51F);
  Motion::resample({}, 5, out);
  CHECK(out.size() == 5 && out[4] == 0.0F);

  CHECK(Motion::srcIndex(0, 8, true) == 4);
  CHECK(Motion::srcIndex(4, 8, true) == 0);
  CHECK(Motion::srcIndex(3, 8, false) == 3);

  Motion m;
  m.configure(16);
  std::vector<float> loud(16, 0.8F);
  for (int i = 0; i < 30; ++i) m.tick(1.0 / 60, loud, 0.5);
  CHECK(m.activity > 0.9);
  CHECK(m.levels[0] > 0.6F);           // attack reaches the target quickly
  CHECK(m.fade() > 0.9);
  const float peak = m.levels[0];
  for (int i = 0; i < 6; ++i) m.tick(1.0 / 60, {}, 0.0);
  CHECK(m.levels[0] < peak);           // decays on silence
  CHECK(m.levels[0] > peak * 0.2F);    // ...but gently
  for (int i = 0; i < 400; ++i) m.tick(1.0 / 60, {}, 0.0);
  CHECK(m.fade() < 0.01);              // releases fully without the idle wave
  CHECK(!m.animating(0.0));

  // the kick detector: a 55 Hz bass all along, kicks (a 155 -> 45 Hz drop)
  // every half second from 2 s; each found within 20 ms, nothing else
  {
    const int sr = 48000;
    std::vector<float> pcm(static_cast<size_t>(sr * 10));
    std::vector<double> kicks;
    for (double b = 2.0; b < 9.5; b += 0.5) kicks.push_back(b);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<float>(0.25 * std::sin(2 * M_PI * 55 * i / sr));
    for (double t0 : kicks) {
      double ph = 0;
      for (int k = 0; k < sr * 0.35; ++k) {
        const double t = k / double(sr);
        ph += 2 * M_PI * (45 + 110 * std::exp(-t / 0.03)) / sr;
        pcm[static_cast<size_t>(t0 * sr) + k] += static_cast<float>(0.8 * std::sin(ph) * std::exp(-t / 0.12) * std::min(1.0, t / 0.002));
      }
    }
    Audio a;
    a.setSampleRate(sr);
    std::vector<double> found;
    uint64_t seen = 0;
    for (size_t pos = 0; pos + 800 <= pcm.size(); pos += 800) {
      a.feed(pcm.data() + pos, 800, true);
      if (a.kickCount() != seen) {
        seen = a.kickCount();
        found.push_back((pos + 800) / double(sr) - a.kickAge());
      }
    }
    int caught = 0, extra = 0;
    for (double f : found) {
      bool near = false;
      for (double t : kicks) near = near || (f >= t && f <= t + 0.02);
      if (near) ++caught;
      else if (f > 1.0) ++extra;  // the first second: the bass itself starting
    }
    CHECK(caught == static_cast<int>(kicks.size()));
    CHECK(extra == 0);
    CHECK(a.kickStrength() > 0.3F && a.kickStrength() <= 1.0F);
  }
  return TEST_RESULT();
}
