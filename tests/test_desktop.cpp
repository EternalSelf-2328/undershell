// SPDX-License-Identifier: GPL-3.0-or-later
#include "check.hpp"
#include "common.hpp"
#include "image.hpp"
#include "audio.hpp"
#include "focus.hpp"
#include "desktoprules.hpp"
#include "layermotion.hpp"
#include "scenebundle.hpp"
#include "offscreen.hpp"
#include "textwidget.hpp"

#include <filesystem>
#include <sstream>
#include <unistd.h>

using namespace undershell;
namespace fs = std::filesystem;

static TickContext tickAt(double now) {
  TickContext context;
  context.now = now;
  return context;
}

static DrawContext surface(float w, float h) {
  DrawContext context;
  context.w = w;
  context.h = h;
  return context;
}


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

[[scene_rule]]
profile = "malformed"
battery = "true"
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

  Audio audio;
  std::array<float, StereoFrame::samples * 8> stereoSamples;
  for (size_t i = 0; i < stereoSamples.size(); i += 2) {
    stereoSamples[i] = 0.5F;
    stereoSamples[i + 1] = -0.25F;
  }
  CHECK(!audio.feedStereo(stereoSamples.data(), stereoSamples.size() / 2, 2, 0, 1));
  audio.setStereoWanted(true);
  CHECK(audio.feedStereo(stereoSamples.data(), stereoSamples.size() / 2, 2, 0, 1));
  const auto* stereo = audio.stereo(nowSeconds());
  CHECK(stereo && stereo->left == 0.5F && stereo->right == 0.25F);
  CHECK(stereo && stereo->wave.front() == 0.5F && stereo->wave.back() == -0.25F);
  CHECK(!audio.stereo(nowSeconds() + 1));
  CHECK(LayerMotion::nightAmount(0, 1080, 360, 30) == 1);
  CHECK(LayerMotion::nightAmount(1095, 1080, 360, 30) == 0.5);
  CHECK(LayerMotion::nightAmount(360, 1080, 360, 30) == 0);
  CHECK(LayerMotion::nightAmount(360, 360, 360, 0) == 0);

  char directory[] = "/tmp/undershell-desktop-XXXXXX";
  if (!mkdtemp(directory)) return 1;
  const fs::path scratch(directory);
  const auto first = (scratch / "01.PNG").string();
  const auto second = (scratch / "02.png").string();
  CHECK(writeFileAtomic(first, "placeholder"));
  CHECK(writeFileAtomic(second, "placeholder"));
  CHECK(writeFileAtomic((scratch / "unrelated.txt").string(), "ignored"));

  ImageWidget photo;
  ImageConfig photoConfig;
  photoConfig.folder = scratch.string();
  photoConfig.interval = 2;
  photo.configure(photoConfig, testPalette());
  photo.tick(tickAt(100));
  CHECK(photo.watchedFiles() == std::vector<std::string>{scratch.string() + "/"});
  CHECK(photo.nextWakeup(100) == 100.03);  // first picture needs decoding
  fs::remove(first);
  fs::remove(second);
  photo.filesChanged();
  CHECK(photo.nextWakeup(100) > 1e8);

  const auto asset = scratch / "test photo.png";
  CHECK(writeFileAtomic(asset.string(), "photo data"));
  toml::array widgets;
  widgets.push_back(toml::table{{"id", "picture"}, {"type", "image"}, {"file", asset.string()},
                               {"output", "OTHER-MONITOR"}, {"scene_motion", "loop"}});
  widgets.push_back(toml::table{{"id", "music"}, {"type", "now_playing"}, {"music_app", "example-command"}});
  std::ostringstream layout;
  layout << toml::table{{"widget", widgets}};
  const auto exported = exportScene(layout.str(), scratch.string());
  const auto sceneManifest = toml::parse_file(exported.path + "/scene.toml");
  const auto path = sceneManifest["widget"][0]["file"].value_or(std::string());
  CHECK(path.starts_with("assets/"));
  CHECK(readFile((fs::path(exported.path) / path).string()) == "photo data");
  CHECK(!sceneManifest["widget"][0]["output"] && !sceneManifest["widget"][1]["music_app"]);
  const auto imported = importScene(exported.path, (scratch / "imports").string());
  const auto importedScene = toml::parse(imported.widgets);
  const auto localPath = importedScene["widget"][0]["file"].value_or(std::string());
  CHECK(localPath.starts_with(imported.path + "/"));
  CHECK(readFile(localPath) == "photo data");
  CHECK(importedScene["widget"][0]["scene_motion"].value_or(std::string()) == "loop");
  const auto importedConfig = (scratch / "imported.toml").string();
  CHECK(writeFileAtomic(importedConfig, imported.widgets));
  CHECK(Config::setKey(importedConfig, "picture", "file", "\"alternate.png\""));
  const auto editedScene = toml::parse_file(importedConfig);
  CHECK(editedScene["widget"][0]["file"].value_or(std::string()) == "alternate.png");
  CHECK(writeFileAtomic(exported.path + "/scene.toml", R"(
[undershell_scene]
version = 1
[[widget]]
id = "image"
type = "image"
file = "assets/../outside"
)"));
  bool rejected = false;
  try {
    importScene(exported.path, (scratch / "imports").string());
  } catch (const std::exception&) {
    rejected = true;
  }
  CHECK(rejected);

  Headless egl;
  CHECK(egl.ok());
  if (egl.ok()) {
    for (const auto style : {"scope", "meters", "spectrogram"}) {
      auto instrument = createWidget("instrument");
      WidgetConfig configuration;
      configuration.type = "instrument";
      configuration.options.insert("style", style);
      instrument->configure(configuration, testPalette());
      std::vector<float> bands(64, 0.7F);
      auto playback = tickAt(100);
      playback.dt = 0.1;
      playback.audio.bands = &bands;
      playback.audio.silent = false;
      playback.audio.stereo = stereo;
      instrument->tick(playback);
      const auto image = renderDrawn(240, 120, [&] { instrument->draw(surface(240, 120)); });
      CHECK(image.rgba.size() == 240 * 120 * 4 && image.rgba[3] > 220);
      CHECK(instrument->usesStereo() == (std::string_view(style) != "spectrogram"));
    }
    std::vector<Image> dioramas;
    for (const auto style : {"aquarium", "terrarium", "rainy_room"}) {
      auto diorama = createWidget("diorama");
      WidgetConfig configuration;
      configuration.type = "diorama";
      configuration.options.insert("style", style);
      diorama->configure(configuration, testPalette());
      diorama->tick(tickAt(100));
      auto draw = [&] { return renderDrawn(240, 150, [&] { diorama->draw(surface(240, 150)); }); };
      dioramas.push_back(draw());
      CHECK(dioramas.back().rgba[3] == 0);
      CHECK(dioramas.back().rgba[(75 * 240 + 120) * 4 + 3] > 240);
      diorama->tick(tickAt(107));
      CHECK(imageDiff(dioramas.back(), draw()) > 0.01);
    }
    CHECK(imageDiff(dioramas[0], dioramas[1]) > 8);
    CHECK(imageDiff(dioramas[1], dioramas[2]) > 8);
    auto shape = createWidget("shape");
    WidgetConfig configuration;
    configuration.type = "shape";
    configuration.options = toml::table{{"shape", "rect"}, {"fill", "solid"}, {"color", "#ffffff"},
                                        {"scene_motion", "loop"}, {"scene_preview", 100}, {"scene_opacity", 0.5},
                                        {"scene_brightness", 0.5}, {"scene_x", 25}};
    shape->configure(configuration, testPalette());
    auto draw = [&] { return renderDrawn(160, 120, [&] { shape->draw(surface(160, 120)); }); };
    shape->tick(tickAt(100));
    const auto still = draw();
    auto preview = tickAt(100);
    preview.editing = true;
    shape->tick(preview);
    const auto shifted = draw();
    constexpr int center = (60 * 160 + 80) * 4;
    constexpr int left = (60 * 160 + 30) * 4;
    CHECK(still.rgba[center + 3] > 247 && still.rgba[left + 3] > 247);
    CHECK(shifted.rgba[center + 3] > 117 && shifted.rgba[center + 3] < 138);
    CHECK(shifted.rgba[center] > 55 && shifted.rgba[center] < 75);
    CHECK(shifted.rgba[left + 3] == 0);
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
