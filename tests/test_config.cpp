// Config loading and in-place editing (comments, hex colours, missing keys).
#include "check.hpp"
#include "common.hpp"
#include "config.hpp"

#include <filesystem>

using namespace undershell;

int main() {
  const std::string dir = std::filesystem::temp_directory_path() / "undershell-test-config";
  std::filesystem::remove_all(dir);
  const std::string path = dir + "/config.toml";

  // a missing file is created with a working default
  Config cfg = Config::load(path);
  CHECK(std::filesystem::exists(path));
  CHECK(cfg.widgets.size() == 1);
  CHECK(cfg.widgets[0].type == "visualizer");
  CHECK(VisualizerConfig::fromTable(cfg.widgets[0].options).style == "bars");

  // a value with '#' inside quotes and a trailing comment
  CHECK(Config::setKey(path, "visualizer", "color", "\"#ff0000\""));
  CHECK(Config::setKey(path, "visualizer", "color", "\"#00ff00\""));
  std::string text = readFile(path);
  CHECK(text.find("color = \"#00ff00\"         # palette role or #rrggbb") != std::string::npos);
  CHECK(text.find("#ff0000") == std::string::npos);

  // a new key is appended inside the right block; others untouched
  CHECK(Config::setKey(path, "visualizer", "spin", "12.5"));
  CHECK(Config::setKey(path, "visualizer", "brand_new", "3"));
  cfg = Config::load(path);
  CHECK(cfg.widgets[0].options["brand_new"].value_or(0) == 3);
  CHECK(VisualizerConfig::fromTable(cfg.widgets[0].options).spin == 12.5);
  CHECK(!Config::setKey(path, "nope", "style", "\"orb\""));

  // geometry round-trip
  WidgetConfig w = cfg.widgets[0];
  w.x = -40;
  w.y = 12;
  w.width = 640;
  w.height = 200;
  CHECK(Config::saveGeometry(path, w));
  cfg = Config::load(path);
  CHECK(cfg.widgets[0].x == -40 && cfg.widgets[0].y == 12);
  CHECK(cfg.widgets[0].width == 640 && cfg.widgets[0].height == 200);
  CHECK(readFile(path).find("# Edit this file and save") != std::string::npos);  // header comments kept

  // numeric keys accept integers where doubles are expected
  CHECK(Config::setKey(path, "visualizer", "glow", "1"));
  CHECK(VisualizerConfig::fromTable(Config::load(path).widgets[0].options).glow == 1.0);

  std::filesystem::remove_all(dir);
  return TEST_RESULT();
}
