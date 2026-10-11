// Config loading and in-place editing (comments, hex colours, missing keys).
#include "check.hpp"
#include "common.hpp"
#include "clockparts.hpp"
#include "anchor.hpp"
#include "config.hpp"
#include "i18n.hpp"
#include "noctalia.hpp"
#include "plasma.hpp"

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
  // the starter widget names no monitor and carries its own space, so it
  // shows (in the same place on the wallpaper) on any screen
  CHECK(cfg.widgets[0].output.empty() && cfg.widgets[0].spaceW == 1920 && cfg.widgets[0].spaceH == 1080);
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

  // a list of paths: a "]" inside one, and a list written by hand over lines
  {
    writeFileAtomic(path, "[[widget]]\nid = \"t\"\ntv_files = [\"~/a]b.gif\", '~/c.mp4']  # mine\nstyle = \"tv\"\n");
    CHECK(Config::setKey(path, "t", "tv_files", "[\"~/d.webm\"]"));
    std::string t3 = readFile(path);
    CHECK(t3.find("tv_files = [\"~/d.webm\"]  # mine\n") != std::string::npos && t3.find("a]b") == std::string::npos);
    writeFileAtomic(path, "[[widget]]\nid = \"t\"\ntv_files = [\n  \"~/x [1].gif\",  # first\n  \"~/y.mp4\",\n] # end\nstyle = \"tv\"\n");
    CHECK(Config::setKey(path, "t", "tv_files", "[\"~/z.gif\"]"));
    t3 = readFile(path);
    CHECK(t3 == "[[widget]]\nid = \"t\"\ntv_files = [\"~/z.gif\"] # end\nstyle = \"tv\"\n");
    const Config tc = Config::load(path);
    CHECK(tc.widgets.size() == 1 && tc.widgets[0].options["tv_files"].as_array() &&
          tc.widgets[0].options["tv_files"].as_array()->size() == 1);
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
    // a block without one belongs to the screen it is shown on (identity there)
    CHECK(blockSpaceW(ac.widgets[1], 1366) == 1366 && blockSpaceH(ac.widgets[1], 768) == 768);
    CHECK(blockSpaceW(ac.widgets[0], 1366) == 1393);
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

  {
    // a saved layout loaded on another monitor: every block moves there
    const std::string blocks = "[[widget]]\nid = \"a\"\ntype = \"clock\"\noutput = \"HDMI-A-1\"   # monitor\nx = 1\n\n"
                               "[[widget]]\nid = \"b\"\ntype = \"visualizer\"\nx = 2\n";
    const std::string moved = Config::moveToOutput(blocks, "eDP-1");
    CHECK(moved.find("output = \"eDP-1\"   # monitor") != std::string::npos);  // in place, comment kept
    CHECK(moved.find("HDMI-A-1") == std::string::npos);
    size_t n = 0;
    for (size_t at = moved.find("output = \"eDP-1\""); at != std::string::npos; at = moved.find("output = \"eDP-1\"", at + 1)) ++n;
    CHECK(n == 2);
    CHECK(moved.find("output = \"eDP-1\"", moved.find("id = \"b\"")) < moved.size());  // added to the block that had none
  }
  {
    // blocks one by one (a layout loaded on one monitor keeps the others')
    const auto parts = Config::splitBlocks("[[widget]]\nid = \"a\"\n\n\n[[widget]]\nid = \"b\"\nx = 1\n");
    CHECK(parts.size() == 2 && parts[0] == "[[widget]]\nid = \"a\"\n" && parts[1] == "[[widget]]\nid = \"b\"\nx = 1\n");
    CHECK(Config::splitBlocks("").empty());
  }

  // [general] wallpaper: read, and expanded over whatever the source says
  {
    std::filesystem::create_directories(dir);
    writeFileAtomic(path, "[general]\nwallpaper = \"~/w/a b.jpg\"\nwallpaper_fill = \"fit\"\n");
    const Config c = Config::load(path);
    CHECK(c.wallpaper == "~/w/a b.jpg" && c.wallpaperFill == "fit");
    Noctalia n;
    CHECK(n.setManual(c.wallpaper, c.wallpaperFill));
    CHECK(!n.setManual(c.wallpaper, c.wallpaperFill));
    CHECK(n.state().manualWallpaper && n.state().defaultWallpaper == expandHome("~/w/a b.jpg") && n.state().fillMode == 2);
    CHECK(n.state().wallpaperFor("HDMI-A-1") == expandHome("~/w/a b.jpg"));
    CHECK(n.setManual("", ""));
    CHECK(!n.state().manualWallpaper);
  }

  // KDE Plasma: KConfig groups, escapes, translations and [$e] keys
  {
    const KConfig k = parseKConfig("[A][B]\nx=1\nName[de]=Hallo\nName=Hi\nPath[$e]=$HOME/x\n[C][$i]\nv=\\sa\\tb\\\\c\n");
    CHECK(k.at("A][B").at("x") == "1" && k.at("A][B").at("Name") == "Hi" && k.at("A][B").contains("Path"));
    CHECK(k.at("C").at("v") == " a\tb\\c");
  }
  // a wallpaper package: the image of the screen's shape, then size; dark
  // variant when the scheme is dark
  {
    const std::string pkg = dir + "/Next";
    for (const char* f : {"/contents/images/1440x2960.png", "/contents/images/5120x2880.png", "/contents/images/7680x2160.png",
                          "/contents/images/1920x1080.jpg", "/contents/images_dark/5120x2880.png"}) {
      std::filesystem::create_directories(std::filesystem::path(pkg + f).parent_path());
      writeFileAtomic(pkg + f, "x");
    }
    CHECK(plasmaPackageImage(pkg, 1920, 1080, false) == pkg + "/contents/images/1920x1080.jpg");
    CHECK(plasmaPackageImage(pkg, 3840, 2160, false) == pkg + "/contents/images/5120x2880.png");
    CHECK(plasmaPackageImage(pkg, 5120, 1440, false) == pkg + "/contents/images/7680x2160.png");
    CHECK(plasmaPackageImage(pkg, 1080, 2340, false) == pkg + "/contents/images/1440x2960.png");
    CHECK(plasmaPackageImage(pkg, 1366, 768, true) == pkg + "/contents/images_dark/5120x2880.png");
    CHECK(plasmaPackageImage("/a/file.jpg", 1, 1, false) == "/a/file.jpg");

    // two screens, two activities, a panel; one desktop names a file url
    const std::string applets =
        "[Containments][1]\nactivityId=act-1\nformfactor=0\nlastScreen=0\nlocation=0\nplugin=org.kde.plasma.folder\n"
        "wallpaperplugin=org.kde.image\n\n"
        "[Containments][1][Wallpaper][org.kde.image][General]\nFillMode=1\nImage=file://" + dir + "/caf%C3%A9.jpg\n\n"
        "[Containments][2]\nactivityId=act-1\nformfactor=0\nlastScreen=1\nlocation=0\nplugin=org.kde.desktopcontainment\n"
        "wallpaperplugin=org.kde.image\n\n"
        "[Containments][2][Wallpaper][org.kde.image][General]\nImage=" + pkg + "/\n\n"
        "[Containments][3]\nactivityId=act-2\nformfactor=0\nlastScreen=0\nplugin=org.kde.plasma.folder\nwallpaperplugin=org.kde.image\n\n"
        "[Containments][3][Wallpaper][org.kde.image][General]\nImage=/other/activity.jpg\n\n"
        "[Containments][4]\nactivityId=\nformfactor=2\nlastScreen=0\nlocation=4\nplugin=org.kde.panel\nwallpaperplugin=org.kde.image\n";
    const std::string shellrc = "[ScreenConnectors]\n0=eDP-1\n1=HDMI-A-1\n";
    const std::string acts = "[activities]\nact-1=Default\nact-2=Work\n\n[main]\ncurrentActivity=act-1\n";
    const std::map<std::string, std::pair<int, int>> screens = {{"eDP-1", {1920, 1080}}, {"HDMI-A-1", {1366, 768}}};
    PlasmaWallpapers w = plasmaWallpapers(applets, plasmaScreenNumbers("", shellrc, screens), acts, screens, false);
    CHECK(w.byOutput.size() == 2);
    CHECK(w.byOutput["eDP-1"] == dir + "/café.jpg");
    CHECK(w.byOutput["HDMI-A-1"] == pkg + "/contents/images/1920x1080.jpg");
    CHECK(w.fallback == dir + "/café.jpg");
    CHECK(w.fillMode == 2);  // Qt's PreserveAspectFit
    // the other activity
    w = plasmaWallpapers(applets, plasmaScreenNumbers("", shellrc, screens), "[main]\ncurrentActivity=act-2\n", screens, false);
    CHECK(w.byOutput.size() == 1 && w.byOutput["eDP-1"] == "/other/activity.jpg" && w.fillMode == 1);
    // one screen and no connector list: that screen
    w = plasmaWallpapers(applets, {}, acts, {{"Virtual-1", {1280, 800}}}, false);
    CHECK(w.byOutput["Virtual-1"] == dir + "/café.jpg");
    // a slideshow is noted, not guessed
    w = plasmaWallpapers("[Containments][1]\nformfactor=0\nlastScreen=0\nplugin=org.kde.plasma.folder\nwallpaperplugin=org.kde.slideshow\n", {}, "",
                         screens, false);
    CHECK(w.slideshow && w.byOutput.empty() && w.fallback.empty());
    // Plasma 6: screens numbered by KWin's priority, in the setup of the
    // screens connected now
    const std::string kwin = R"([{"name":"outputs","data":[{"connectorName":"eDP-1"},{"connectorName":"HDMI-A-1"},{"connectorName":"DP-2"}]},
      {"name":"setups","data":[{"outputs":[{"enabled":true,"outputIndex":0,"priority":0}]},
                               {"outputs":[{"enabled":true,"outputIndex":0,"priority":1},{"enabled":true,"outputIndex":1,"priority":0}]},
                               {"outputs":[{"enabled":true,"outputIndex":2,"priority":0},{"enabled":true,"outputIndex":0,"priority":1}]}]}])";
    auto nums = plasmaScreenNumbers(kwin, shellrc, screens);
    CHECK(nums.size() == 2 && nums["0"] == "HDMI-A-1" && nums["1"] == "eDP-1");
    nums = plasmaScreenNumbers(kwin, "", {{"eDP-1", {1920, 1080}}});
    CHECK(nums.size() == 1 && nums["0"] == "eDP-1");
    w = plasmaWallpapers(applets, plasmaScreenNumbers(kwin, "", screens), acts, screens, false);
    CHECK(w.byOutput["HDMI-A-1"] == dir + "/café.jpg" && w.byOutput["eDP-1"] == pkg + "/contents/images/1920x1080.jpg");
    // no Wallpaper group at all (a fresh desktop): Plasma's default package
    w = plasmaWallpapers("[Containments][1]\nformfactor=0\nlastScreen=0\nplugin=org.kde.plasma.folder\nwallpaperplugin=org.kde.image\n", {}, "",
                         {{"Virtual-0", {1366, 768}}}, false);
    CHECK(w.byOutput.size() == (std::filesystem::is_directory("/usr/share/wallpapers/Next") ? 1U : 0U));
  }
  // the palette: accent over the selection colour, dark from the window
  {
    const std::string scheme = "[Colors:Window]\nBackgroundNormal=32,35,38\nForegroundNormal=252,252,252\nForegroundInactive=161,169,177\n"
                               "[Colors:Selection]\nBackgroundNormal=61,174,233\nForegroundNormal=255,255,255\n"
                               "[Colors:View]\nBackgroundAlternate=29,31,34\nForegroundNegative=218,68,83\nForegroundLink=29,153,243\n";
    bool dark = false;
    auto pal = plasmaPalette("", scheme, dark);
    CHECK(dark && pal.size() >= 8);
    CHECK(std::abs(pal["primary"].r - 61 / 255.0F) < 1e-4F && std::abs(pal["surface"].g - 35 / 255.0F) < 1e-4F);
    CHECK(std::abs(pal["error"].r - 218 / 255.0F) < 1e-4F);
    pal = plasmaPalette("[General]\nAccentColor=233,100,10\n[Colors:Window]\nBackgroundNormal=239,240,241\n", scheme, dark);
    CHECK(!dark && std::abs(pal["primary"].g - 100 / 255.0F) < 1e-4F);
    CHECK(plasmaPalette("", "", dark).empty());
  }
  std::filesystem::remove_all(dir);

  return TEST_RESULT();
}
