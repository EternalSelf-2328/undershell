// Particles: the JSON format and what it refuses, the simulation (births,
// deaths, bounds, the level's counts, the music), the person's folder, the
// inspector rows and (with GL) drawing over the wallpaper.
#include "check.hpp"
#include "noctalia.hpp"
#include "offscreen.hpp"
#include "particles.hpp"
#include "perf.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <unistd.h>

using namespace undershell;
namespace fs = std::filesystem;

static bool hasRow(const std::vector<PropSpec>& rows, const std::string& key) {
  for (const auto& r : rows)
    if (r.key == key) return true;
  return false;
}

// every particle drawn this instant: x, y (1080-high units) of each
static std::vector<std::pair<float, float>> where(const ParticleSystem& s, const MusicLevels& m = {}) {
  std::vector<float> buf;
  const size_t n = s.fill(buf, 16.0F / 9, m);
  std::vector<std::pair<float, float>> out;
  for (size_t i = 0; i < n; ++i) out.emplace_back(buf[i * 12], buf[i * 12 + 1]);
  return out;
}

int main() {
  const fs::path dir = fs::temp_directory_path() / ("undershell-particles-" + std::to_string(getpid()));
  fs::create_directories(dir / "undershell/particles");
  setenv("XDG_CONFIG_HOME", dir.c_str(), 1);  // never the person's own folders
  setenv("XDG_DATA_HOME", dir.c_str(), 1);
  setPerfKnobs(knobsFor(PerfLevel::High));

  // the built-in systems: all well formed
  ParticleLibrary& lib = particleLibrary();
  CHECK(lib.all().size() == 8);
  std::set<std::string> ids;
  for (const auto& d : lib.all()) {
    if (!d.error.empty()) std::fprintf(stderr, "%s: %s\n", d.id.c_str(), d.error.c_str());
    CHECK(d.error.empty() && ids.insert(d.id).second && d.nameEs != d.id);
  }
  CHECK(lib.find("sparks") && lib.find("sparks")->additive && lib.find("sparks")->react > 0);

  // what the format refuses
  CHECK(!parseParticles("x", "{ nope").error.empty());
  CHECK(!parseParticles("x", R"({"sprite": "cube"})").error.empty());
  CHECK(!parseParticles("x", R"({"operators": [{"type": "teleport"}]})").error.empty());
  CHECK(!parseParticles("Bad", "{}").error.empty());
  const ParticleDef plain = parseParticles("x", R"({"max": 99999, "init": {"alpha": [0.2, 7]}})");
  CHECK(plain.error.empty() && plain.max == 5000 && plain.alpha[1] == 1);  // clamped

  // snow: already falling when it starts, inside the screen, under its cap
  ParticleSettings s;
  ParticleSystem snow(*lib.find("snow"), s, 7);
  const auto p0 = where(snow);
  CHECK(p0.size() > 100 && snow.count() <= 700);
  bool inside = true;
  for (const auto& [x, y] : p0) inside = inside && x > -0.2F * 1920 && x < 1.2F * 1920 && y > -0.25F * 1080 && y < 1.25F * 1080;
  CHECK(inside);
  // it falls: the mean height goes down the screen
  auto meanY = [](const std::vector<std::pair<float, float>>& v) {
    double m = 0;
    for (const auto& p : v) m += p.second;
    return v.empty() ? 0.0 : m / static_cast<double>(v.size());
  };
  // (a fresh system: a prewarmed one is already as low on average as it gets)
  const ParticleDef drop = parseParticles("drop", R"({"max": 400, "rate": 400, "prewarm": 0,
      "emitter": {"origin": [0.5, 0.0], "size": [1, 0.02]}, "init": {"lifetime": 5, "speed": 500, "direction": 90}})");
  ParticleSystem rain(drop, s, 3);
  rain.advance(0.1, 16.0F / 9, {});
  const double y0 = meanY(where(rain));
  for (int i = 0; i < 6; ++i) rain.advance(1.0 / 60, 16.0F / 9, {});
  CHECK(meanY(where(rain)) > y0);
  // amount and the level scale how many there can be
  ParticleSettings half = s;
  half.amount = 0.5F;
  ParticleSystem fewer(*lib.find("snow"), half, 7);
  CHECK(fewer.count() <= 350 && fewer.count() < snow.count());
  setPerfKnobs(knobsFor(PerfLevel::Eco));
  ParticleSystem eco(*lib.find("snow"), s, 7);
  CHECK(eco.count() <= static_cast<size_t>(700 * knobsFor(PerfLevel::Eco).particles) + 1);
  setPerfKnobs(knobsFor(PerfLevel::High));
  // short lives end: nothing new is born, everything is gone after its lifetime
  const ParticleDef brief = parseParticles("brief", R"({"max": 50, "rate": 0, "prewarm": 0, "init": {"lifetime": 0.2}})");
  ParticleSystem none(brief, s, 1);
  for (int i = 0; i < 30; ++i) none.advance(1.0 / 60, 16.0F / 9, {});
  CHECK(none.count() == 0);
  // the beat drives the sparks (with "with the music" on)
  ParticleSettings loud = s;
  loud.react = 1;
  ParticleSystem quiet(*lib.find("sparks"), loud, 5), beat(*lib.find("sparks"), loud, 5);
  MusicLevels kick;
  kick.v = {0.8F, 0.5F, 0.3F, 1.0F};
  kick.live = true;
  for (int i = 0; i < 30; ++i) {
    quiet.advance(1.0 / 60, 16.0F / 9, {});
    beat.advance(1.0 / 60, 16.0F / 9, kick);
  }
  CHECK(beat.count() > quiet.count() * 3);

  // the person's folder: a new system and a broken one
  std::ofstream(dir / "undershell/particles/hearts.json")
      << R"({"name": {"en": "Hearts", "es": "Corazones"}, "sprite": "petal", "rate": 5, "max": 30})";
  std::ofstream(dir / "undershell/particles/oops.json") << "{ broken";
  CHECK(lib.refresh());
  CHECK(lib.find("hearts") && lib.find("hearts")->nameEs == "Corazones" && !lib.find("hearts")->file.empty());
  CHECK(lib.find("oops") && !lib.find("oops")->error.empty());
  CHECK(!lib.refresh());

  // rows: a switch each; the options of those on
  toml::table opts;
  opts.insert_or_assign("pt_snow", true);
  const auto rows = particleRows(opts, false);
  CHECK(rows[0].kind == PropSpec::Header && hasRow(rows, "pt_rain") && hasRow(rows, "pt_snow_amount") && hasRow(rows, "pt_snow_behind"));
  CHECK(!hasRow(rows, "pt_rain_amount") && !hasRow(rows, "pt_snow_split") && !hasRow(rows, "pt_snow_paint"));
  for (const auto& r : rows)
    if (r.key == "pt_oops") CHECK(r.labelEn.find("(error)") != std::string::npos);
  opts.insert_or_assign("pt_snow_mask", "paint2");
  CHECK(hasRow(particleRows(opts, false), "pt_snow_paint"));

  // with GL: over the wallpaper, and gone when off
  Headless gl;
  if (gl.ok()) {
    Image wall;
    wall.w = 160, wall.h = 90;
    wall.rgba.assign(160 * 90 * 4, 255);
    for (size_t i = 0; i < wall.rgba.size(); i += 4) wall.rgba[i] = 20, wall.rgba[i + 1] = 30, wall.rgba[i + 2] = 60;
    const std::string png = (dir / "wall.png").string();
    CHECK(writePng(wall, png, false));
    const NoctaliaState pal = testPalette();
    toml::table o;
    o.insert_or_assign("style", "custom");
    const Image base = renderWidget("filter", o, 320, 180, pal, 1000, 1, 3.0, nullptr, png);
    for (const auto& d : builtinParticles()) {
      toml::table one = o;
      one.insert_or_assign(particleKey(d.id), true);
      one.insert_or_assign(particleKey(d.id, "amount"), 3.0);
      const Image img = renderWidget("filter", one, 320, 180, pal, 1000, 30, 3.0, nullptr, png);
      const double diff = imageDiff(img, base);
      if (!(diff > 0.005)) std::fprintf(stderr, "%s changed the picture by %.3f\n", d.id.c_str(), diff);
      CHECK(diff > 0.005);  // dust and embers are small: a little
    }
    // only below the line: the top as it was
    toml::table below = o;
    below.insert_or_assign("pt_snow", true);
    below.insert_or_assign("pt_snow_amount", 3.0);
    below.insert_or_assign("pt_snow_mask", "below");
    below.insert_or_assign("pt_snow_split", 0.5);
    const Image half = renderWidget("filter", below, 320, 180, pal, 1000, 30, 3.0, nullptr, png);
    double up = 0, down = 0;
    for (int y = 0; y < 180; ++y)
      for (int x = 0; x < 320; ++x)
        for (int k = 0; k < 3; ++k) {
          const size_t i = (static_cast<size_t>(y) * 320 + x) * 4 + k;
          const int d = std::abs(half.rgba[i] - base.rgba[i]);
          if (y < 80) up += d;
          if (y > 100) down += d;
        }
    CHECK(up < 1 && down > 100);
  } else {
    std::fputs("no surfaceless EGL: GL part skipped\n", stderr);
  }
  fs::remove_all(dir);
  return TEST_RESULT();
}
