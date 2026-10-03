// Config loading and in-place editing (comments, hex colours, missing keys).
#include "check.hpp"
#include "common.hpp"
#include "clockparts.hpp"
#include "anchor.hpp"
#include "config.hpp"
#include "i18n.hpp"

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

  // blocks: append a starter, read it back, remove it, put it back (undo)
  const std::string clockBlock = Config::defaultBlock("clock", "clock", "HDMI-A-1", 10, 20, 560, 240, "flip");
  CHECK(Config::appendBlock(path, clockBlock));
  cfg = Config::load(path);
  CHECK(cfg.widgets.size() == 2);
  CHECK(cfg.widgets[1].type == "clock" && cfg.widgets[1].options["face"].value_or(std::string()) == "flip");
  const std::string got = Config::blockText(path, "clock");
  CHECK(got.find("[[widget]]") == 0 && got.find("id = \"clock\"") != std::string::npos);
  CHECK(got.find("visualizer") == std::string::npos);  // only its own block
  std::string removed;
  CHECK(Config::removeBlock(path, "clock", &removed));
  CHECK(removed == got);
  cfg = Config::load(path);
  CHECK(cfg.widgets.size() == 1 && cfg.widgets[0].id == "visualizer");
  CHECK(Config::appendBlock(path, removed));
  CHECK(Config::load(path).widgets.size() == 2);
  // removing the first block keeps the second intact
  CHECK(Config::removeBlock(path, "visualizer"));
  cfg = Config::load(path);
  CHECK(cfg.widgets.size() == 1 && cfg.widgets[0].id == "clock" && cfg.widgets[0].x == 10);
  CHECK(!Config::removeBlock(path, "visualizer"));
  // every starter block parses
  for (const char* t : {"visualizer", "clock", "now_playing"})
    CHECK(Config::appendBlock(path, Config::defaultBlock(t, std::string("t-") + t, "", 0, 0, 100, 100, "")));
  CHECK(Config::load(path).widgets.size() == 4);
  // TOML text of values, for undo records
  toml::table tt{{"s", "orb"}, {"b", true}, {"n", 12.5}, {"i", 64}};
  CHECK(Config::tomlText(*tt.get("s")) == "\"orb\"");
  CHECK(Config::tomlText(*tt.get("b")) == "true");
  CHECK(Config::tomlText(*tt.get("i")) == "64");

  // repeated ids (older Duplicate kept the id) are renamed in place, and the
  // two widgets then load as two
  writeFileAtomic(path, "[[widget]]\nid = \"visualizer\"\nx = 10\n\n[[widget]]\nid = \"visualizer\" # copy\nx = 20\n\n"
                        "[[widget]]\nid = \"visualizer-2\"\nx = 30\n");
  CHECK(Config::uniquifyIds(path) == 1);
  text = readFile(path);
  CHECK(text.find("id = \"visualizer-3\" # copy") != std::string::npos);
  CHECK(Config::uniquifyIds(path) == 0);
  cfg = Config::load(path);
  CHECK(cfg.widgets.size() == 3 && cfg.widgets[1].id == "visualizer-3" && cfg.widgets[1].x == 20);

  // duplicating rewrites the copy's id and position, keeping the rest
  const std::string copy = Config::retargetBlock("[[widget]]\nid = \"clock\"\ntype = \"clock\"\nx = 5\ny = 6 # top\n", "clock-2", 37, 38);
  CHECK(copy.find("id = \"clock-2\"") != std::string::npos && copy.find("x = 37") != std::string::npos &&
        copy.find("y = 38 # top") != std::string::npos && copy.find("type = \"clock\"") != std::string::npos);

  // wallpaper profiles: only [[widget]] blocks move, [general] stays
  {
    const std::string two = "# top\n[general]\ngrid = 8\n\n[[widget]]\nid = \"a\"\nx = 1\n\n[[widget]]\nid = \"b\"\nx = 2\n";
    const std::string blocks = Config::widgetBlocks(two);
    CHECK(blocks == "[[widget]]\nid = \"a\"\nx = 1\n\n[[widget]]\nid = \"b\"\nx = 2\n");
    const std::string swapped = Config::replaceWidgetBlocks(two, "[[widget]]\nid = \"c\"\nx = 3\n");
    CHECK(swapped == "# top\n[general]\ngrid = 8\n\n[[widget]]\nid = \"c\"\nx = 3\n");
    CHECK(Config::replaceWidgetBlocks(two, "") == "# top\n[general]\ngrid = 8\n");

    // A -> B (new: keeps A's layout) -> edit on B -> A (restored) -> B (restored)
    const std::string pdir = dir + "/profiles";
    writeFileAtomic(path, two);
    CHECK(Config::switchProfile(path, pdir, "A", "a.png", "B") == 0);
    CHECK(readFile(path) == two);
    writeFileAtomic(path, Config::replaceWidgetBlocks(readFile(path), "[[widget]]\nid = \"only-b\"\nx = 9\n"));
    CHECK(Config::switchProfile(path, pdir, "B", "b.png", "A") == 1);
    Config back = Config::load(path);
    CHECK(back.widgets.size() == 2 && back.widgets[0].id == "a" && back.gridSize == 8);
    CHECK(readFile(pdir + "/A.toml").find("# undershell layout for a.png") == 0);
    CHECK(Config::switchProfile(path, pdir, "A", "a.png", "B") == 1);
    back = Config::load(path);
    CHECK(back.widgets.size() == 1 && back.widgets[0].id == "only-b" && back.widgets[0].x == 9);
  }

  // saved layouts (the Profiles panel)
  {
    const std::string sdir = dir + "/saves";
    const std::string blocks = "[[widget]]\nid = \"a\"\ntype = \"clock\"\nx = 4\n";
    const std::string id = Config::writeSave(sdir, "Mi\nescritorio $1", "w.png", blocks);
    CHECK(!id.empty());
    auto saves = Config::listSaves(sdir);
    CHECK(saves.size() == 1 && saves[0].name == "Mi escritorio $1" && saves[0].wallpaper == "w.png");
    CHECK(saves[0].widgets.size() == 1 && saves[0].widgets[0].x == 4 && saves[0].blocks == blocks);
    const std::string id2 = Config::writeSave(sdir, "", "w.png", blocks);  // same second: still unique
    CHECK(!id2.empty() && id2 != id);
    CHECK(Config::renameSave(sdir, id, "Noche $&"));
    CHECK(Config::writeSave(sdir, "", "x.png", "[[widget]]\nid = \"b\"\n", id) == id);  // overwrite keeps the name
    saves = Config::listSaves(sdir);
    bool found = false;
    for (auto& sv : saves)
      if (sv.id == id) found = sv.name == "Noche $&" && sv.wallpaper == "x.png" && sv.widgets.size() == 1 && sv.widgets[0].id == "b";
    CHECK(found);
    CHECK(!Config::deleteSave(sdir, "../config"));  // ids are never paths
    CHECK(Config::deleteSave(sdir, id2) && Config::listSaves(sdir).size() == 1);
  }

  // clock structures: order and overrides, defaults from the design
  {
    const auto* card = clockStructure("goodnight");
    CHECK(card && !clockStructure("digital"));
    auto t = toml::parse(R"(
      goodnight_order = "time, bogus, date,time"
      goodnight_date_font = "Fraunces 144pt"
      goodnight_date_weight = "black"
      goodnight_date_case = "normal"
      goodnight_greeting_show = false
      goodnight_time_size = 2.5
    )");
    const auto els = clockElements(t, *card);
    CHECK(els.size() == card->elements.size());
    CHECK(std::string(els[0].spec->id) == "time" && std::string(els[1].spec->id) == "date");  // then the rest, in design order
    CHECK(std::string(els[2].spec->id) == "rule_top");
    CHECK(els[1].family == "Fraunces 144pt" && els[1].weight == 900 && !els[1].upper);
    CHECK(els[0].scale == 2.5F && els[0].family == "Inter Display");
    for (const auto& e : els)
      if (std::string(e.spec->id) == "greeting") CHECK(!e.show);
    CHECK(weightName(600) == "semibold" && weightValue("light") == 300);
  }

  // an array value (corner pins) is replaced in place, never repeated
  {
    writeFileAtomic(path, "[[widget]]\nid = \"p\"\npin_corners = true\npin = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]\n");
    CHECK(Config::setKey(path, "p", "pin", "[9.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]"));
    const std::string t2 = readFile(path);
    CHECK(t2.find("pin = [9.0") != std::string::npos && t2.find("pin = [1.0") == std::string::npos);
    const Config pc = Config::load(path);
    CHECK(pc.widgets.size() == 1 && pc.widgets[0].pinned && pc.widgets[0].pin[0] == 9.0);
  }

  {
    // layouts follow the wallpaper: a 2560x1440 image cropped on 1920x1080
    // and on 1393x783 (1366x768 at scale 0.98) shows the same picture smaller
    const SpaceMap m = spaceMap(1920, 1080, 1393, 783, 2560, 1440, 1);
    CHECK(std::abs(m.ax - 1393.0 / 1920) < 1e-3 && std::abs(m.ay - m.ax) < 1e-9);  // uniform
    // the centre of the screen stays the centre of the image
    CHECK(std::abs(m.ax * 960 + m.bx - 1393 / 2.0) < 1e-6 && std::abs(m.ay * 540 + m.by - 783 / 2.0) < 1e-6);
    // a 4:3 screen crops the sides: a widget keeps its spot on the image
    const SpaceMap q = spaceMap(1920, 1080, 1024, 768, 1920, 1080, 1);
    CHECK(std::abs(q.ax - 768.0 / 1080) < 1e-9 && q.bx < 0);
    WidgetConfig c;
    c.x = 960 - 100, c.y = 540 - 50, c.width = 200, c.height = 100;
    c.pin[0] = 960, c.pin[1] = 540;
    mapWidget(c, q);
    CHECK(std::abs(c.x + c.width / 2.0 - 512) <= 1 && std::abs(c.y + c.height / 2.0 - 384) <= 1);
    CHECK(std::abs(c.pin[0] - 512) < 1e-6 && std::abs(c.pin[1] - 384) < 1e-6);
    // and back again, within rounding
    mapWidget(c, q.inverse());
    CHECK(std::abs(c.x - 860) <= 1 && std::abs(c.width - 200) <= 1 && std::abs(c.pin[0] - 960) < 1e-6);
    // the same size is the identity; unknown image (video) scales like a picture of the screen
    CHECK(spaceMap(1920, 1080, 1920, 1080, 800, 600, 1).identity());
    const SpaceMap v = spaceMap(1920, 1080, 960, 540, 0, 0, 1);
    CHECK(std::abs(v.ax - 0.5) < 1e-9 && std::abs(v.bx) < 1e-9);
    // stretch is per axis
    const SpaceMap st = spaceMap(1920, 1080, 1024, 768, 1920, 1080, 3);
    CHECK(std::abs(st.ax - 1024.0 / 1920) < 1e-9 && std::abs(st.ay - 768.0 / 1080) < 1e-9);

    // the block's space is read back; new blocks write it; legacy is 1920x1080
    const std::string b = Config::defaultBlock("clock", "anch", "", 1, 2, 300, 100, "", 1393, 783);
    CHECK(b.find("space = [1393, 783]") != std::string::npos);
    std::filesystem::create_directories(dir);
    writeFileAtomic(path, "[general]\n\n" + b + "\n[[widget]]\nid = \"old\"\ntype = \"clock\"\n");
    const Config ac = Config::load(path);
    CHECK(ac.widgets.size() == 2 && ac.widgets[0].spaceW == 1393 && ac.widgets[0].spaceH == 783);
    CHECK(blockSpaceW(ac.widgets[1]) == 1920 && blockSpaceH(ac.widgets[1]) == 1080);
  }

  {
    // the interface language: pinned or from the locale, and option labels
    setUiLanguage("es");
    CHECK(spanishUi() && spanishDates());
    setUiLanguage("en");
    CHECK(!spanishUi() && uiLanguage() == "en");
    setUiLanguage("klingon");
    CHECK(uiLanguage() == "system");
    CHECK(optionLabel("rounded", true) == "Redondeado" && optionLabel("rounded", false) == "Rounded");
    CHECK(optionLabel("inside_out", false) == "Inside out" && optionLabel("softBurst", false) == "Soft burst");
    // [general] keys are set in place (and [general] made when missing)
    writeFileAtomic(path, "[general]\ngrid = 16   # snap\n\n[[widget]]\nid = \"a\"\ntype = \"clock\"\n");
    CHECK(Config::setGeneral(path, "language", "\"es\""));
    CHECK(Config::setGeneral(path, "language", "\"en\""));
    std::string g = readFile(path);
    CHECK(g.find("language = \"en\"") != std::string::npos && g.find("\"es\"") == std::string::npos);
    CHECK(g.find("language") < g.find("[[widget]]") && g.find("grid = 16   # snap") != std::string::npos);
    CHECK(Config::load(path).language == "en");
    writeFileAtomic(path, "[[widget]]\nid = \"a\"\ntype = \"clock\"\n");
    CHECK(Config::setGeneral(path, "language", "\"es\""));
    const Config lc = Config::load(path);
    CHECK(lc.language == "es" && lc.widgets.size() == 1);
  }

  std::filesystem::remove_all(dir);
  return TEST_RESULT();
}
