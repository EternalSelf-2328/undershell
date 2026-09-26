// Media feed helpers: LRC parsing and Ryoku's album accent.
#include "check.hpp"
#include "media.hpp"

using namespace undershell;

int main() {
  auto lines = parseLrc("[00:31.84] Se que estas pensando\n[00:12.00]first\n[01:02.5][01:40.25] chorus \nnot a lyric\n[00:40.00]");
  CHECK(lines.size() == 5);
  CHECK(lines[0].ms == 12000 && lines[0].text == "first");  // sorted
  CHECK(lines[1].ms == 31840 && lines[1].text == "Se que estas pensando");
  CHECK(lines[2].ms == 40000 && lines[2].text.empty());      // instrumental gap keeps its slot
  CHECK(lines[3].ms == 62500 && lines[3].text == "chorus");  // trimmed, repeated stamps
  CHECK(lines[4].ms == 100250);

  // a muddy image with a vivid red patch: the accent is the red, lifted
  std::vector<uint8_t> img(64 * 64 * 4);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) {
      uint8_t* p = &img[(y * 64 + x) * 4];
      const bool red = x < 24 && y < 24;
      p[0] = red ? 200 : 60;
      p[1] = red ? 30 : 58;
      p[2] = red ? 40 : 55;
      p[3] = 255;
    }
  Color a;
  CHECK(accentOfPixels(img.data(), 64, 64, a));
  CHECK(a.r > a.g * 2 && a.r > a.b * 2);
  const float l = (std::max({a.r, a.g, a.b}) + std::min({a.r, a.g, a.b})) / 2;
  CHECK(l >= 0.51F && l <= 0.69F);  // lightness lifted into Ryoku's band

  // a grey image has nothing worth taking
  std::fill(img.begin(), img.end(), 128);
  CHECK(!accentOfPixels(img.data(), 64, 64, a));

  MediaState s;
  s.playing = true;
  s.positionUs = 10'000'000;
  s.positionAt = 100;
  s.lengthUs = 12'000'000;
  CHECK(std::abs(s.positionSec(101.5) - 11.5) < 1e-6);   // extrapolated while playing
  CHECK(std::abs(s.positionSec(200) - 12.0) < 1e-6);     // clamped to the length
  s.playing = false;
  CHECK(std::abs(s.positionSec(200) - 10.0) < 1e-6);     // frozen when paused
  return TEST_RESULT();
}
