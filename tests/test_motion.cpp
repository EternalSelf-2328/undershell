// Ryoku's motion: resampling and attack/decay easing.
#include "check.hpp"
#include "motion.hpp"

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
  return TEST_RESULT();
}
