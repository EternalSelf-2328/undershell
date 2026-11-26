// SPDX-License-Identifier: GPL-3.0-or-later
#include "check.hpp"
#include "common.hpp"
#include "image.hpp"
#include "focus.hpp"
#include "desktoprules.hpp"
#include "offscreen.hpp"
#include "textwidget.hpp"

#include <filesystem>
#include <unistd.h>

using namespace undershell;
namespace fs = std::filesystem;

int main() {
  FocusTimer focus;
  CHECK(focus.state() == "idle");
  focus.start(100, 1, 1);
  CHECK(focus.state() == "work" && focus.seconds(100) == 60 && focus.next(100) == 101);
  CHECK(focus.pause(126.25) && focus.seconds(400) == 34 && focus.next(400) > 1e8);
  CHECK(focus.resume(500) && focus.seconds(500) == 34);
  CHECK(focus.update(534) && focus.state() == "break" && focus.seconds(534) == 60);
  CHECK(focus.update(594) && focus.state() == "idle");
  focus.start(100, 1, 0);
  CHECK(focus.update(160) && focus.state() == "idle");
  CHECK(TextWidget::expand("{focus}", nullptr, nullptr, {}, "00:29") == "00:29");

  const auto rulesConfig = toml::parse(R"(
[[scene_rule]]
profile = "night"
from = "20:00"
until = "06:00"
battery = true
[[scene_rule]]
profile = "code"
workspace = "Code"
[[scene_rule]]
profile = "base"
)");
  const auto rules = readSceneRules(rulesConfig["scene_rule"].as_array());
  CHECK(rules.size() == 3);
  std::vector<WorkspaceState> workspaces;
  CHECK(parseWorkspaceEvent(R"({"event":"workspaces","data":[{"name":"Code","active":true,"focused":true,"index":2}]})", workspaces));
  CHECK(workspaces.size() == 1 && workspaces.front().name == "Code" && workspaces.front().index == 2);
  SceneInputs inputs{.minute = 180, .battery = true, .workspaces = &workspaces};
  CHECK(matchSceneRule(rules, inputs)->profile == "night");
  inputs.minute = 360;
  CHECK(matchSceneRule(rules, inputs)->profile == "code");
  workspaces.clear();
  CHECK(matchSceneRule(rules, inputs)->profile == "base");

  char directory[] = "/tmp/undershell-desktop-XXXXXX";
  if (!mkdtemp(directory)) return 1;
  const fs::path scratch(directory);
  const auto first = (scratch / "01.PNG").string();
  const auto second = (scratch / "02.png").string();
  CHECK(writeFileAtomic(first, "placeholder"));
  CHECK(writeFileAtomic(second, "placeholder"));
  CHECK(writeFileAtomic((scratch / "unrelated.txt").string(), "ignored"));

  ImageWidget photo;
  photo.configure(ImageConfig{.folder = scratch.string(), .interval = 2}, testPalette());
  photo.tick(TickContext{.now = 100});
  CHECK(photo.watchedFiles() == std::vector<std::string>{scratch.string() + "/"});
  CHECK(photo.nextWakeup(100) == 100.03);  // first picture needs decoding
  fs::remove(first);
  fs::remove(second);
  photo.filesChanged();
  CHECK(photo.nextWakeup(100) > 1e8);

  Headless egl;
  CHECK(egl.ok());
  if (egl.ok()) {
    const auto data = (scratch / "status.json").string();
    CHECK(writeFileAtomic(data, R"({"download":{"progress":42}})"));
    const toml::table opts{{"text", "{data}%"}, {"data_file", data}, {"data_key", "/download/progress"}};
    const auto palette = testPalette();
    const auto actual = renderWidget("text", opts, 240, 120, palette);
    const auto expected = renderWidget("text", toml::table{{"text", "42%"}}, 240, 120, palette);
    CHECK(imageDiff(actual, expected) == 0);
    CHECK(writeFileAtomic(data, "{"));
    CHECK(imageDiff(renderWidget("text", opts, 240, 120, palette),
                    renderWidget("text", toml::table{{"text", "%"}}, 240, 120, palette)) == 0);
  }
  fs::remove_all(scratch);
  return TEST_RESULT();
}
