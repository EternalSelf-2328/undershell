// Performance profiles: names, knobs, the governor's steps and the battery check.
#include "check.hpp"
#include "config.hpp"
#include "perf.hpp"

#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace undershell;
namespace fs = std::filesystem;

static void put(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << s << "\n";
}

int main() {
  CHECK(parsePerfLevel("eco") == PerfLevel::Eco);
  CHECK(parsePerfLevel("ultra") == PerfLevel::Ultra);
  CHECK(!parsePerfLevel("auto"));
  CHECK(!parsePerfLevel("fast"));
  for (int i = 0; i < 4; ++i) CHECK(parsePerfLevel(perfLevelName(static_cast<PerfLevel>(i))) == static_cast<PerfLevel>(i));
  // every step up costs more or equal, never less
  for (int i = 0; i < 3; ++i) {
    const PerfKnobs &a = knobsFor(static_cast<PerfLevel>(i)), &b = knobsFor(static_cast<PerfLevel>(i + 1));
    CHECK(a.particles < b.particles);
    CHECK(a.renderScale <= b.renderScale);
    CHECK(a.quality < b.quality);
    CHECK(a.fpsCap != 0 || b.fpsCap == 0);  // once uncapped, uncapped above
  }
  CHECK(knobsFor(PerfLevel::Eco).fpsCap == 30);

  // the governor: two seconds over budget step down, a quiet spell steps up
  PerfGovernor g;
  g.reset(PerfLevel::High, PerfLevel::Eco, PerfLevel::High);
  double t = 0;
  CHECK(g.sample(t += 1, 0.5, 0, true) == PerfLevel::High);  // one second is not enough
  CHECK(g.sample(t += 1, 0.5, 0, true) == PerfLevel::Balanced);
  CHECK(g.sample(t += 1, 0.5, 0, true) == PerfLevel::Balanced);
  CHECK(g.sample(t += 1, 0, 0.3, true) == PerfLevel::Eco);  // CPU counts too
  for (int i = 0; i < 10; ++i) g.sample(t += 1, 0.5, 0.5, true);
  CHECK(g.level() == PerfLevel::Eco);  // the floor holds
  // quiet, but within the hold after the last step down: stays
  for (int i = 0; i < 10; ++i) g.sample(t += 1, 0.01, 0.01, true);
  CHECK(g.level() == PerfLevel::Eco);
  t += 30;
  // quiet, but the next level costs ~4.5× (60 fps and more pixels): 5 % would
  // land over budget there, so it stays
  CHECK(PerfGovernor::upCost(PerfLevel::Eco) > 4 && PerfGovernor::upCost(PerfLevel::Eco) < 5);
  for (int i = 0; i < 10; ++i) g.sample(t += 1, 0.05, 0.01, true);
  CHECK(g.level() == PerfLevel::Eco);
  for (int i = 0; i < 6; ++i) g.sample(t += 1, 0.01, 0.01, true);
  CHECK(g.level() == PerfLevel::Balanced);
  // nothing drawn is no evidence either way
  for (int i = 0; i < 20; ++i) g.sample(t += 1, 0, 0, false);
  CHECK(g.level() == PerfLevel::Balanced);
  // a load between the marks keeps the level
  for (int i = 0; i < 20; ++i) g.sample(t += 1, 0.12, 0.1, true);
  CHECK(g.level() == PerfLevel::Balanced);
  // the ceiling (battery) pulls it down at once and caps it
  g.setCeiling(PerfLevel::Eco);
  CHECK(g.level() == PerfLevel::Eco);
  for (int i = 0; i < 60; ++i) g.sample(t += 1, 0.0, 0.0, true);
  CHECK(g.level() == PerfLevel::Eco);

  // the battery: a laptop unplugged, plugged, and a desktop
  const fs::path dir = fs::temp_directory_path() / ("undershell-perf-" + std::to_string(getpid()));
  put(dir / "BAT0/type", "Battery");
  put(dir / "BAT0/present", "1");
  put(dir / "ADP1/type", "Mains");
  put(dir / "ADP1/online", "0");
  CHECK(onBattery(dir.string()));
  put(dir / "ADP1/online", "1");
  CHECK(!onBattery(dir.string()));
  fs::remove_all(dir / "BAT0");
  CHECK(!onBattery(dir.string()));
  CHECK(!onBattery((dir / "missing").string()));
  fs::remove_all(dir);

  // [general] performance / on_battery: unknown values fall back
  const fs::path cfgPath = fs::temp_directory_path() / ("undershell-perf-" + std::to_string(getpid()) + ".toml");
  std::ofstream(cfgPath) << "[general]\nperformance = \"eco\"\non_battery = \"keep\"\n";
  Config c = Config::load(cfgPath.string());
  CHECK(c.performance == "eco" && c.onBattery == "keep");
  std::ofstream(cfgPath) << "[general]\nperformance = \"turbo\"\non_battery = \"nope\"\n";
  c = Config::load(cfgPath.string());
  CHECK(c.performance == "auto" && c.onBattery == "lower");
  fs::remove(cfgPath);
  return TEST_RESULT();
}
