// Config loading and in-place editing (comments, hex colours, missing keys).
#include "check.hpp"
#include "common.hpp"
#include "clockparts.hpp"
#include "anchor.hpp"
#include "config.hpp"
#include "i18n.hpp"
#include "noctalia.hpp"
#include "gnome.hpp"
#include "ics.hpp"
#include "calsources.hpp"

#include <glib.h>
#ifdef US_HAVE_SQLITE
#include <sqlite3.h>
#endif
#include "plasma.hpp"

#include <filesystem>

using namespace undershell;
namespace fs = std::filesystem;

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
  // other shells and wallpaper tools (shells.hpp): each format as its project writes it
  {
    using namespace std::string_literals;
    auto near = [](Color c, int r, int g, int b) {
      return std::abs(c.r - r / 255.0F) < 0.003F && std::abs(c.g - g / 255.0F) < 0.003F && std::abs(c.b - b / 255.0F) < 0.003F;
    };
    CHECK(snakeRole("onSurfaceVariant") == "on_surface_variant" && snakeRole("overPrimary") == "on_primary" &&
          snakeRole("surface_container") == "surface_container" && snakeRole("overall") == "overall");
    bool dark = false;
    // caelestia scheme.json: camelCase roles, hex without '#'
    auto pal = caelestiaScheme(R"({"name":"dynamic","mode":"dark","colours":{"primary":"a6c8ff","onSurface":"dce8e6","surface":"0a0f0f"}})", dark);
    CHECK(dark && pal.size() == 3 && near(pal["primary"], 0xa6, 0xc8, 0xff) && pal.contains("on_surface"));
    // DMS dms-colors.json: both modes, the one in "mode"
    pal = dmsColors(R"({"mode":"light","colors":{"dark":{"primary":"#000000"},"light":{"primary":"#ff8800","surface_container":"#eeeeee"}}})", dark);
    CHECK(!dark && near(pal["primary"], 255, 0x88, 0) && pal.contains("surface_container"));
    // end-4 / Ambxst: flat; Ambxst's "over" roles
    pal = flatPalette(R"({"background":"#111318","primary":"#a8c8ff","overSurface":"#e2e2e9","sourceColor":"x"})", dark);
    CHECK(dark && pal.size() == 3 && pal.contains("on_surface"));
    // pywal: its wallpaper and a reading of the sixteen colours
    std::string walWall;
    pal = pywalPalette(R"({"wallpaper":"/w/a.jpg","special":{"background":"#f0f0f0","foreground":"#101010"},"colors":{"color4":"#3366cc","color1":"#cc3333"}})",
                       walWall, dark);
    CHECK(walWall == "/w/a.jpg" && !dark && near(pal["primary"], 0x33, 0x66, 0xcc) && near(pal["error"], 0xcc, 0x33, 0x33));

    // DMS session.json + settings.json
    ShellState sh;
    CHECK(dmsSession(R"({"wallpaperPath":"~/w/all.png","perMonitorWallpaper":true,"monitorWallpapers":{"DP-1":"/w/dp1.png","HDMI-A-1":"#1e1e2e"}})",
                     R"({"wallpaperFillMode":"Fit"})", true, sh));
    CHECK(sh.fallback == expandHome("~/w/all.png") && sh.byOutput.size() == 1 && sh.byOutput["DP-1"] == "/w/dp1.png" && sh.fillMode == 2);
    sh = {};
    CHECK(dmsSession(R"({"perModeWallpaper":true,"wallpaperPathDark":"/w/d.png","wallpaperPathLight":"/w/l.png"})", "", false, sh));
    CHECK(sh.fallback == "/w/l.png" && sh.fillMode == 1);
    // Ambxst wallpapers.json
    sh = {};
    CHECK(ambxstWallpapers(R"({"currentWall":"/w/a.jpg","perScreenWallpapers":{"DP-1":"/w/b.jpg"}})", sh));
    CHECK(sh.fallback == "/w/a.jpg" && sh.byOutput["DP-1"] == "/w/b.jpg");
    // end-4 config.json; HyprPanel flat or nested keys, only when enabled
    CHECK(iiWallpaper(R"({"background":{"wallpaperPath":"/w/ii.png","thumbnailPath":"/t"}})") == "/w/ii.png");
    CHECK(hyprpanelWallpaper(R"({"wallpaper.enable":true,"wallpaper.image":"/w/hp.png"})") == "/w/hp.png");
    CHECK(hyprpanelWallpaper(R"({"wallpaper":{"enable":true,"image":"/w/n.png"}})") == "/w/n.png");
    CHECK(hyprpanelWallpaper(R"({"wallpaper.enable":false,"wallpaper.image":"/w/hp.png"})").empty());
    // COSMIC: the state file per output, scaling from the `all` entry
    sh = {};
    CHECK(cosmicWallpapers(R"([("eDP-1", Path("/usr/share/backgrounds/a b.jpg")), ("DP-2", Path("/w/c.png"))])",
                           "(\n output: \"all\",\n source: Path(\"/x\"),\n filter_by_theme: #true,\n scaling_mode: Fit((0.0, 0.0, 0.0)),\n)", sh));
    CHECK(sh.byOutput["eDP-1"] == "/usr/share/backgrounds/a b.jpg" && sh.byOutput["DP-2"] == "/w/c.png" && sh.fillMode == 2);
    // awww cache: namespace, resize[:gravity], filter, path (NUL-separated)
    auto e = awwwCacheEntry("\0crop:center\0Lanczos3\0/w/aw.png\0"s);
    CHECK(e.first == "/w/aw.png" && fillFrom("awww", e.second) == 1);
    e = awwwCacheEntry("other\0fit\0Nearest\0/w/o.png\0\0no\0Lanczos3\0/w/default.png"s);
    CHECK(e.first == "/w/default.png" && fillFrom("awww", e.second) == 0);
    CHECK(awwwCacheEntry("fit\0/w/old.png"s).first == "/w/old.png");
    // hyprpaper: listactive (0.8 and 0.7), the config (0.8 blocks, 0.7 lines)
    sh = {};
    CHECK(hyprpaperActive("DP-1: /w/h1.png\nHDMI-A-1 = /w/h2.png\n", sh) && sh.byOutput.size() == 2 && sh.byOutput["HDMI-A-1"] == "/w/h2.png");
    sh = {};
    CHECK(!hyprpaperActive("no wallpapers active\n", sh));
    sh = {};
    CHECK(hyprpaperConfig("splash = 1\nwallpaper {\n    monitor = DP-1\n    path = /w/c1.png  # mine\n    fit_mode = contain\n}\n", sh));
    CHECK(sh.byOutput["DP-1"] == "/w/c1.png" && sh.fillMode == 2);
    sh = {};
    CHECK(hyprpaperConfig("preload = /w/t.png\nwallpaper = ,tile:/w/t.png\n", sh) && sh.fallback == "/w/t.png" && sh.fillMode == 4);
    // swaybg: global options, then per output; no -m = stretch
    sh = {};
    CHECK(swaybgArgs({"swaybg", "-o", "DP-1", "-i", "/w/s1.png", "-m", "fill", "-o", "HDMI-A-1", "-i", "/w/s2.jpg", "-m", "fit"}, sh));
    CHECK(sh.byOutput["DP-1"] == "/w/s1.png" && sh.byOutput["HDMI-A-1"] == "/w/s2.jpg" && sh.fillMode == 1);
    sh = {};
    CHECK(swaybgArgs({"swaybg", "-i", "/w/all.png"}, sh) && sh.fallback == "/w/all.png" && sh.fillMode == 3);
    // mpvpaper: the last two words
    sh = {};
    CHECK(mpvpaperArgs({"mpvpaper", "-o", "no-audio loop", "ALL", "/w/v.mp4"}, sh) && sh.fallback == "/w/v.mp4");
    // waypaper: continuation lines, index-aligned
    sh = {};
    CHECK(waypaperIni("[Settings]\nbackend = awww\nmonitors = DP-1\n\tHDMI-A-1\nwallpaper = /w/p1.jpg\n\t/w/p2.png\nfill = center\n", sh));
    CHECK(sh.byOutput["DP-1"] == "/w/p1.jpg" && sh.byOutput["HDMI-A-1"] == "/w/p2.png" && sh.fillMode == 0);
    // fill names across tools
    CHECK(fillFrom("wpaperd", "center") == 1 && fillFrom("swaybg", "center") == 0 && fillFrom("hyprpaper", "fill") == 3 &&
          fillFrom("dms", "Pad") == 0 && fillFrom("cosmic", "Zoom") == 1 && fillFrom("wpaperd", "fit-border-color") == 2);

    // detection: the running process picks the tool, its files say the rest
    const std::string home = dir + "/shellhome";
    setenv("XDG_STATE_HOME", (home + "/state").c_str(), 1);
    setenv("XDG_CACHE_HOME", (home + "/cache").c_str(), 1);
    setenv("XDG_CONFIG_HOME", (home + "/config").c_str(), 1);
    std::filesystem::create_directories(home + "/state/caelestia/wallpaper");
    writeFileAtomic(home + "/state/caelestia/wallpaper/path.txt", "/w/cael.jpg");
    writeFileAtomic(home + "/state/caelestia/scheme.json", R"({"mode":"dark","colours":{"primary":"a6c8ff"}})");
    sh = {};
    CHECK(readShell({{"/usr/bin/qs", "-c", "caelestia", "-n"}}, sh) && sh.name == "caelestia" && sh.fallback == "/w/cael.jpg" &&
          sh.palette.contains("primary"));
    std::filesystem::create_directories(home + "/cache/awww/0.12.1");
    writeFileAtomic(home + "/cache/awww/0.12.1/eDP-1", "\0stretch\0Lanczos3\0/w/aw2.png"s);
    std::filesystem::create_directories(home + "/cache/wal");
    writeFileAtomic(home + "/cache/wal/colors.json",
                    R"({"wallpaper":"/w/aw2.png","special":{"background":"#101010","foreground":"#eeeeee"},"colors":{"color4":"#3366cc"}})");
    sh = {};
    CHECK(readShell({{"awww-daemon"}, {"bash"}}, sh) && sh.name == "awww" && sh.byOutput["eDP-1"] == "/w/aw2.png" && sh.fillMode == 3 &&
          sh.palette.contains("primary"));  // pywal made it from this very wallpaper
    sh = {};
    CHECK(!readShell({{"bash"}, {"vim", "hyprpanel.conf"}}, sh));  // nothing of the kind running
    unsetenv("XDG_STATE_HOME");
    unsetenv("XDG_CACHE_HOME");
    unsetenv("XDG_CONFIG_HOME");
  }

  // GNOME: gsettings' five values, one per line
  {
    GnomeSettings g = parseGnomeSettings("'file:///usr/share/backgrounds/gnome/caf%C3%A9-l.jpg'\n'file:///usr/share/backgrounds/gnome/d.jpg'\n'zoom'\n'default'\n'green'\n");
    CHECK(g.wallpaper == "/usr/share/backgrounds/gnome/café-l.jpg" && g.fillMode == 1 && !g.dark);
    CHECK(std::abs(g.palette["primary"].g - 0x94 / 255.0F) < 0.003F);  // libadwaita's green #3a944a
    g = parseGnomeSettings("'file:///a/l.png'\n'file:///a/d.png'\n'scaled'\n'prefer-dark'\n'blue'\n");
    CHECK(g.wallpaper == "/a/d.png" && g.fillMode == 2 && g.dark);  // the dark picture only under prefer-dark
    g = parseGnomeSettings("'file:///a/l.png'\n''\n'spanned'\n'prefer-light'\n'slate'\n");
    CHECK(g.wallpaper == "/a/l.png" && g.fillMode == 5 && !g.dark);
    g = parseGnomeSettings("'file:///usr/share/backgrounds/gnome/adwaita-timed.xml'\n''\n'zoom'\n'default'\n'blue'\n");
    CHECK(g.wallpaper.empty());  // an XML slideshow: no single picture
    g = parseGnomeSettings("'file:///a/l.png'\n''\n'none'\n'default'\n'blue'\n");
    CHECK(g.wallpaper.empty());  // a plain colour
    g = parseGnomeSettings("'file:///a/l.png'\n''\n'wallpaper'\n'default'\n");  // GNOME 46: no accent-color
    CHECK(g.fillMode == 4 && std::abs(g.palette["primary"].b - 0xe4 / 255.0F) < 0.003F);
  }

  // the Layers panel's keys: a name, a group, hidden, locked; a block edited as text
  {
    const std::string block = "[[widget]]\nid = \"clock\"\ntype = \"clock\"\nlayer = 2    # stacking\nhidden = true\n";
    CHECK(Config::blockId(block) == "clock");
    CHECK(Config::blockId("[[widget]]\ntype = \"clock\"\n").empty());
    std::string b = Config::setBlockKey(block, "group", "\"Grupo 1\"");
    CHECK(b.find("group = \"Grupo 1\"\n") != std::string::npos);
    b = Config::setBlockKey(b, "layer", "5");
    CHECK(b.find("layer = 5    # stacking") != std::string::npos && b.find("layer = 2") == std::string::npos);
    b = Config::setBlockKey(b, "hidden", "");  // taken out
    CHECK(b.find("hidden") == std::string::npos && b.find("group = ") != std::string::npos);
    CHECK(Config::setBlockKey(b, "absent", "") == b);  // nothing to take out
    b = Config::setBlockKey(b, "name", "\"Reloj \\\"grande\\\"\"");
    const std::string lpath = dir + "/layers.toml";
    writeFileAtomic(lpath, b + "locked = true\n");
    Config lc = Config::load(lpath);
    CHECK(lc.widgets.size() == 1 && lc.widgets[0].group == "Grupo 1" && lc.widgets[0].name == "Reloj \"grande\"");
    CHECK(lc.widgets[0].layer == 5 && !lc.widgets[0].hidden && lc.widgets[0].locked);
    writeFileAtomic(lpath, "[[widget]]\nid = \"v\"\nlayer = 4000\n");
    CHECK(Config::load(lpath).widgets[0].layer == 999);  // a long stack, within bounds
  }

  std::filesystem::remove_all(dir);

  // iCalendar: timed and all-day events, repeating rules, exceptions, moved
  // occurrences, time zones across daylight saving, folding, cancellations
  {
    setenv("TZ", "UTC", 1);
    tzset();
    auto at = [](int y, int m, int d, int h = 0, int mi = 0) {
      GDateTime* t = g_date_time_new_utc(y, m, d, h, mi, 0);
      const int64_t u = g_date_time_to_unix(t);
      g_date_time_unref(t);
      return u;
    };
    const std::string ics =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nX-APPLE-CALENDAR-COLOR:#33aa55\r\n"
        "BEGIN:VEVENT\r\nUID:a\r\nDTSTART:20261014T150000Z\r\nDTEND:20261014T160000Z\r\nSUMMARY:Dentista\\, revisión\r\n"
        "  anual\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:b\r\nDTSTART;VALUE=DATE:20261020\r\nDTEND;VALUE=DATE:20261022\r\nSUMMARY:Viaje\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:c\r\nDTSTART:20261005T090000Z\r\nDURATION:PT30M\r\nRRULE:FREQ=WEEKLY;BYDAY=MO,WE;COUNT=4\r\n"
        "EXDATE:20261007T090000Z\r\nSUMMARY:Standup\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:d\r\nDTSTART:20260130T180000Z\r\nRRULE:FREQ=MONTHLY;BYDAY=-1FR\r\nSUMMARY:Cierre\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:e\r\nDTSTART:20261001T100000Z\r\nRRULE:FREQ=DAILY\r\nSUMMARY:Café\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:e\r\nRECURRENCE-ID:20261003T100000Z\r\nDTSTART:20261003T120000Z\r\nSUMMARY:Café tarde\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:f\r\nSTATUS:CANCELLED\r\nDTSTART:20261015T100000Z\r\nSUMMARY:No\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:g\r\nDTSTART;TZID=America/New_York:20261030T090000\r\nRRULE:FREQ=DAILY;COUNT=4\r\nSUMMARY:NY\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:h\r\nDTSTART:20100101T080000Z\r\nRRULE:FREQ=DAILY\r\nSUMMARY:Old daily\r\nEND:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    const auto ev = parseIcs(ics, at(2026, 10, 1), at(2026, 11, 3), "#000000");
    auto named = [&](const std::string& t) {
      std::vector<CalEvent> v;
      for (const auto& e : ev)
        if (e.title == t) v.push_back(e);
      return v;
    };
    const auto dent = named("Dentista, revisión anual");  // unfolded, unescaped
    CHECK(dent.size() == 1 && dent[0].start == at(2026, 10, 14, 15) && dent[0].end == at(2026, 10, 14, 16) && dent[0].color == "#33aa55");
    const auto trip = named("Viaje");
    CHECK(trip.size() == 1 && trip[0].allDay && trip[0].end - trip[0].start == 2 * 86400);
    const auto standup = named("Standup");  // Mon 5, (Wed 7 excluded), Mon 12, Wed 14: COUNT counts the excluded one
    CHECK(standup.size() == 3 && standup[0].start == at(2026, 10, 5, 9) && standup[1].start == at(2026, 10, 12, 9) &&
          standup[2].start == at(2026, 10, 14, 9) && standup[0].end - standup[0].start == 1800);
    const auto closing = named("Cierre");  // the last Friday: Oct 30
    CHECK(closing.size() == 1 && closing[0].start == at(2026, 10, 30, 18));
    const auto coffee = named("Café"), moved = named("Café tarde");
    CHECK(coffee.size() == 32 && moved.size() == 1 && moved[0].start == at(2026, 10, 3, 12));  // 33 days less the moved one
    CHECK(named("No").empty());
    const auto ny = named("NY");  // 09:00 in New York: 13:00 UTC, then 14:00 once DST ends (Nov 1)
    CHECK(ny.size() == 4 && ny[0].start == at(2026, 10, 30, 13) && ny[3].start == at(2026, 11, 2, 14));
    const auto old = named("Old daily");  // since 2010, found without walking 16 years
    CHECK(old.size() == 33 && old[0].start == at(2026, 10, 1, 8));
    CHECK(parseIcs("garbage\nBEGIN:VEVENT\nDTSTART:bad\nEND:VEVENT", 0, INT64_MAX).empty());

    // where events come from: khal's vdirs (with their colour), Evolution's
    // local calendars (a hidden one left out) and its cache of online ones
    const fs::path root = fs::temp_directory_path() / "undershell-test-cal";
    fs::remove_all(root);
    fs::create_directories(root / "cfg/khal");
    fs::create_directories(root / "cals/home");
    writeFileAtomic((root / "cfg/khal/config").string(), "[calendars]\n  [[home]]\n    path = " + (root / "cals/*").string() +
                                                             "\n    type = discover\n[default]\nhighlight_event_days = True\n");
    writeFileAtomic((root / "cals/home/color").string(), "#11aa22\n");
    writeFileAtomic((root / "cals/home/one.ics").string(),
                    "BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:k1\nDTSTART:20261014T150000Z\nSUMMARY:khal event\nEND:VEVENT\nEND:VCALENDAR\n");
    setenv("XDG_CONFIG_HOME", (root / "cfg").c_str(), 1);
    setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
    setenv("XDG_CACHE_HOME", (root / "cache").c_str(), 1);
    const auto khal = khalSources();
    CHECK(khal.size() == 1 && khal[0].folder && khal[0].color == "#11aa22");
    const auto kev = loadEvents(khal, at(2026, 10, 1), at(2026, 11, 1));
    CHECK(kev.size() == 1 && kev[0].title == "khal event" && kev[0].color == "#11aa22");
    // Evolution: a shown local calendar, a hidden one
    fs::create_directories(root / "data/evolution/calendar/system");
    fs::create_directories(root / "data/evolution/calendar/hidden1");
    fs::create_directories(root / "cfg/evolution/sources");
    writeFileAtomic((root / "data/evolution/calendar/system/calendar.ics").string(),
                    "BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:e1\nDTSTART:20261015T150000Z\nSUMMARY:personal\nEND:VEVENT\nEND:VCALENDAR\n");
    writeFileAtomic((root / "data/evolution/calendar/hidden1/calendar.ics").string(),
                    "BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:e2\nDTSTART:20261015T150000Z\nSUMMARY:hidden\nEND:VEVENT\nEND:VCALENDAR\n");
    writeFileAtomic((root / "cfg/evolution/sources/system-calendar.source").string(), "[Data Source]\nEnabled=true\n\n[Calendar]\nColor=#ff0088\nSelected=true\n");
    writeFileAtomic((root / "cfg/evolution/sources/hidden1.source").string(), "[Data Source]\nEnabled=true\n\n[Calendar]\nSelected=false\n");
#ifdef US_HAVE_SQLITE
    // an online calendar as Evolution keeps it
    fs::create_directories(root / "cache/evolution/calendar/online1");
    sqlite3* db = nullptr;
    CHECK(sqlite3_open((root / "cache/evolution/calendar/online1/cache.db").c_str(), &db) == SQLITE_OK);
    sqlite3_exec(db, "CREATE TABLE ECacheObjects (ECacheUID TEXT, ECacheOBJ TEXT);"
                     "INSERT INTO ECacheObjects VALUES ('o1', 'BEGIN:VEVENT\r\nUID:o1\r\nDTSTART:20261016T150000Z\r\nSUMMARY:online\r\nEND:VEVENT');",
                 nullptr, nullptr, nullptr);
    sqlite3_close(db);
#endif
    const auto evo = evolutionSources();
    const auto eev = loadEvents(evo, at(2026, 10, 1), at(2026, 11, 1));
    bool personal = false, hidden = false, online = false;
    for (const auto& e : eev) {
      personal = personal || (e.title == "personal" && e.color == "#ff0088");
      hidden = hidden || e.title == "hidden";
      online = online || e.title == "online";
    }
    CHECK(personal && !hidden);
#ifdef US_HAVE_SQLITE
    CHECK(online);
#endif
    const auto plain = icsSources((root / "cals/home").string() + " ; ~/nowhere.ics");
    CHECK(plain.size() == 2 && plain[0].folder && !plain[1].folder);
    CHECK(!sourcesStamp(khal).empty());
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_DATA_HOME");
    unsetenv("XDG_CACHE_HOME");
    fs::remove_all(root);
  }

  return TEST_RESULT();
}
