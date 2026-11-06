// SPDX-License-Identifier: GPL-3.0-or-later
// The system monitor: what it reads from /proc and /sys (against a made-up
// tree), and how a reading is drawn.
#include "check.hpp"
#include "noctalia.hpp"
#include "offscreen.hpp"
#include "activity.hpp"
#include "pet.hpp"
#include "sysmon.hpp"
#include "text.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace undershell;
namespace fs = std::filesystem;

namespace {
void put(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << text;
}

bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }
}  // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / ("undershell-system-" + std::to_string(getpid()));
  fs::remove_all(root);
  // a machine with two cores, an Intel GPU, a CPU package sensor and a battery
  auto machine = [&](int busy, int idle, long rx, long tx, long rc6ms) {
    put(root / "proc/stat", std::format("cpu  {} 0 0 {} 0 0 0 0 0 0\ncpu0 {} 0 0 {} 0 0 0 0\ncpu1 {} 0 0 {} 0 0 0 0\nintr 1\n", busy, idle,
                                        busy / 2, idle / 2, busy / 2, idle / 2));
    put(root / "proc/meminfo", "MemTotal:       16000000 kB\nMemFree:  1000000 kB\nMemAvailable:   12000000 kB\n"
                               "SwapTotal: 4000000 kB\nSwapFree: 3000000 kB\n");
    put(root / "proc/net/dev", std::format("Inter-|   Receive\n face |bytes\n    lo: 999999 0 0 0 0 0 0 0 999999 0 0 0 0 0 0 0\n"
                                           "  wlan0: {} 0 0 0 0 0 0 0 {} 0 0 0 0 0 0 0\n", rx, tx));
    put(root / "sys/class/drm/card1/power/rc6_residency_ms", std::to_string(rc6ms));
    put(root / "sys/class/drm/card1-eDP-1/status", "connected");  // a connector: not a GPU
  };
  put(root / "sys/class/hwmon/hwmon3/name", "acpitz");
  put(root / "sys/class/hwmon/hwmon3/temp1_input", "40000");
  put(root / "sys/class/hwmon/hwmon5/name", "coretemp");
  put(root / "sys/class/hwmon/hwmon5/temp1_label", "Package id 0");
  put(root / "sys/class/hwmon/hwmon5/temp1_input", "68000");
  put(root / "sys/class/power_supply/hidpp_battery_0/type", "Battery");  // a mouse's
  put(root / "sys/class/power_supply/hidpp_battery_0/scope", "Device");
  put(root / "sys/class/power_supply/hidpp_battery_0/capacity", "20");
  put(root / "sys/class/power_supply/BAT0/type", "Battery");
  put(root / "sys/class/power_supply/BAT0/capacity", "87");
  put(root / "sys/class/power_supply/BAT0/status", "Charging");

  SystemReader reader(root.string());
  machine(100, 300, 1000, 500, 10'000);
  const SystemSample first = reader.sample(10.0, "/");
  CHECK(first.cpu == 0 && first.down == 0);  // loads and rates need two readings
  CHECK(near(first.memUsed, 4000000.0 * 1024) && near(first.memTotal, 16000000.0 * 1024));
  CHECK(near(first.swapUsed, 1000000.0 * 1024));
  CHECK(near(first.tempC, 68) && near(first.battery, 0.87) && first.charging);  // the package, the laptop's battery
  CHECK(first.cores.size() == 2 && first.diskTotal > 0);
  // two seconds later: 150 of 200 more ticks busy, 3 MB down, the GPU asleep 1.5 s of 2
  machine(250, 350, 3'001'000, 100'500, 11'500);
  const SystemSample second = reader.sample(12.0, "/");
  CHECK(near(second.cpu, 0.75));
  CHECK(near(second.cores[0], 0.75) && near(second.cores[1], 0.75));
  CHECK(near(second.down, 1'500'000) && near(second.up, 50'000));  // the loopback left out
  CHECK(near(second.gpu, 0.25));
  // AMD's own busy share wins over an idle counter
  put(root / "sys/class/drm/card0/device/gpu_busy_percent", "42");
  SystemReader amd(root.string());
  CHECK(near(amd.sample(1.0, "/").gpu, 0.42));
  // nothing to read: no GPU, no sensor, no battery
  {
    const fs::path bare = root / "bare";
    fs::create_directories(bare / "proc");
    SystemReader none(bare.string());
    const SystemSample s = none.sample(1.0, "/");
    CHECK(s.gpu < 0 && s.tempC < 0 && s.battery < 0);
  }

  // drawn: the bars are as long as the reading says
  Headless gl;
  if (gl.ok()) {
    const NoctaliaState pal = testPalette();
    const Color accent = pal.color("primary");
    SystemSample s;
    s.cpu = 0.25;
    s.memUsed = 6, s.memTotal = 8;  // 75 %
    s.tempC = 50;
    s.down = 2e6;
    auto draw = [&](const std::string& layout, int w, int h) {
      SystemWidget widget;
      WidgetConfig wc;
      wc.type = "system";
      wc.options.insert_or_assign("layout", layout);
      wc.options.insert_or_assign("language", "en");
      widget.configure(wc, pal);
      for (double f : {0.6, 0.4, 1.0}) {  // three readings: the graph's line has points to join
        SystemSample k = s;
        k.cpu *= f;
        k.tempC *= f;
        widget.setSample(k);
      }
      TextRenderer tr;
      Image img = renderDrawn(w, h, [&] {
        DrawContext dc;
        dc.w = static_cast<float>(w);
        dc.h = static_cast<float>(h);
        dc.text = &tr;
        widget.draw(dc);
      });
      tr.releaseGl();
      return img;
    };
    // pixels of the accent in a band of rows
    auto accentIn = [&](const Image& img, int y0, int y1) {
      int n = 0;
      for (int y = y0; y < y1; ++y)
        for (int x = 0; x < img.w; ++x) {
          const size_t i = (static_cast<size_t>(y) * img.w + x) * 4;
          const float a = img.rgba[i + 3] / 255.0F;
          if (a < 0.5F) continue;
          const float r = img.rgba[i] / 255.0F / a, g = img.rgba[i + 1] / 255.0F / a, b = img.rgba[i + 2] / 255.0F / a;
          if (std::abs(r - accent.r) + std::abs(g - accent.g) + std::abs(b - accent.b) < 0.12F) ++n;
        }
      return n;
    };
    const Image bars = draw("bars", 420, 240);  // CPU, RAM, Temp, Net: four rows
    const int pad = static_cast<int>(240 * 0.08F), rh = (240 - 2 * pad) / 4;
    const int cpu = accentIn(bars, pad, pad + rh), ram = accentIn(bars, pad + rh, pad + 2 * rh);
    std::fprintf(stderr, "bars: accent in the CPU row %d, in the RAM row %d\n", cpu, ram);
    CHECK(cpu > 50 && ram > cpu * 2);
    for (const char* layout : {"rings", "graph", "text"}) {
      const Image img = draw(layout, 460, 300);
      int drawn = 0;
      for (size_t i = 3; i < img.rgba.size(); i += 4) drawn += img.rgba[i] > 200;
      if (drawn < 800) std::fprintf(stderr, "%s drew %d pixels\n", layout, drawn);
      CHECK(drawn > 800);  // the readings (the glass plate is translucent and does not count)
    }
  } else {
    std::fputs("no surfaceless EGL: GL part skipped\n", stderr);
  }
  // the activity widget: repositories under the folders, commits per day, drawn by level
  {
    const fs::path home = root / "home";
    for (const char* d : {"code/app/.git", "code/app/sub/.git", "code/node_modules/dep/.git", "code/.hidden/x/.git", "notes/a/b/c/deep/.git", "w/lib/.git"})
      fs::create_directories(home / d);
    put(home / "w/file/.git", "gitdir: elsewhere");  // a worktree's .git is a file
    const auto repos = findRepositories({(home / "code").string(), (home / "w").string(), (home / "notes").string()});
    CHECK(repos.size() == 3);  // app (not its sub), lib, file; not node_modules, hidden or too deep
    CHECK(std::ranges::count_if(repos, [](const std::string& r) { return r.ends_with("/app") || r.ends_with("/lib") || r.ends_with("/file"); }) == 3);
    ActivityDays days;
    const int64_t noon = 20000LL * 86400 + 12 * 3600;  // midday: the same local day in any zone within ±11 h
    countDays(std::format("{}\n{}\n{}\nnot a time\n", noon, noon + 60, noon + 86400), days);
    CHECK(days.size() == 2 && days[localDay(noon)] == 2 && days[localDay(noon + 86400)] == 1);
    ActivityConfig ac = ActivityConfig::fromTable(toml::parse("folders = \"~/a, ~/b \"\nweeks = 99"));
    CHECK(ac.folders.size() == 2 && ac.folders[1] == "~/b" && ac.weeks == 53);
  }
  if (gl.ok()) {
    // a busy week and a quiet one: the busy days fill more accent
    const NoctaliaState pal = testPalette();
    const Color accent = pal.color("primary");
    ActivityWidget a;
    WidgetConfig wc;
    wc.type = "activity";
    wc.options.insert_or_assign("weeks", 4);
    wc.options.insert_or_assign("show_total", false);
    wc.options.insert_or_assign("plate", "none");
    a.configure(wc, pal);
    const int64_t today = 20004;  // a Monday: the last column holds only it
    ActivityDays d;
    for (int64_t k = today - 7 - ((today + 3) % 7); k < today - ((today + 3) % 7); ++k) d[k] = 9;  // last week, every day
    d[today - 20] = 1;
    a.setDays(d, today);
    TextRenderer tr;
    const Image img = renderDrawn(200, 100, [&] {
      DrawContext dc;
      dc.w = 200, dc.h = 100, dc.text = &tr;
      a.draw(dc);
    });
    tr.releaseGl();
    std::vector<int> col(4, 0);  // accent per column
    for (int y = 0; y < 100; ++y)
      for (int x = 0; x < 200; ++x) {
        const size_t i = (static_cast<size_t>(y) * 200 + x) * 4;
        const float al = img.rgba[i + 3] / 255.0F;
        if (al < 0.9F) continue;
        if (std::abs(img.rgba[i] / 255.0F / al - accent.r) + std::abs(img.rgba[i + 1] / 255.0F / al - accent.g) < 0.1F) ++col[std::min(3, (x - 14) * 4 / 172)];
      }
    std::fprintf(stderr, "activity: accent per week %d %d %d %d\n", col[0], col[1], col[2], col[3]);
    CHECK(col[2] > 300 && col[2] > 4 * (col[0] + col[1] + col[3]));  // the busy week, full; the quiet ones, a faint cell or nothing
  }
  // the pet: on its ground line, it climbs a window whose top is within reach,
  // falls when the window goes, and dozes off after a minute of silence
  {
    PetWidget pet;
    WidgetConfig wc;
    wc.type = "pet";
    pet.configure(wc, testPalette());
    std::vector<Rect> windows{{200, 520, 900, 300}};  // its top 120 px above the ground (640)
    TickContext tc;
    tc.box = {600, 544, 96, 96};
    tc.outputW = 1366, tc.outputH = 768;
    tc.dt = 1 / 30.0;
    tc.windows = &windows;
    bool climbed = false;
    for (int i = 0; i < 90 * 30; ++i) {
      tc.now = i / 30.0;
      pet.tick(tc);
      climbed = climbed || std::abs(pet.y() - 520) < 0.5;  // standing on the window's top
    }
    CHECK(climbed);  // it got up there
    windows.clear();                     // the window closes under it
    for (int i = 0; i < 45; ++i) pet.tick(tc);
    CHECK(std::abs(pet.y() - 640) < 1);  // back on its ground
    CHECK(pet.mode() == PetWidget::Mode::Sleep);  // 90 s without music
  }
  if (gl.ok()) {
    auto pose = [&](const char* p) {
      PetWidget pet;
      WidgetConfig wc;
      wc.type = "pet";
      wc.options.insert_or_assign("pet_preview", p);
      pet.configure(wc, testPalette());
      TextRenderer tr;
      Image img = renderDrawn(96, 96, [&] {
        DrawContext dc;
        dc.w = 96, dc.h = 96, dc.text = &tr;
        pet.draw(dc);
      });
      tr.releaseGl();
      return img;
    };
    const Image walk = pose("walk"), sleep = pose("sleep");
    size_t body = 0;
    for (size_t i = 3; i < walk.rgba.size(); i += 4) body += walk.rgba[i] > 200;
    CHECK(body > 800);                       // a creature, not a speck
    CHECK(imageDiff(walk, sleep) > 1);       // asleep it looks otherwise
  }
  fs::remove_all(root);
  return TEST_RESULT();
}
