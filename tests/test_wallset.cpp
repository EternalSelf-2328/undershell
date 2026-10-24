// Setting the wallpaper through each program: the argv built for every
// source, the paths refused, and the path check.
#include "check.hpp"
#include "wallset.hpp"

#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace undershell;
namespace fs = std::filesystem;

static Argv only(const WallSet& s) { return s.commands.size() == 1 ? s.commands[0] : Argv{}; }

int main() {
  const std::string pic = "/home/u/Pictures/a b.png";
  const WallSetContext ctx{{{"DP-1", "/w/one.png"}, {"HDMI-A-1", "/w/two.png"}}, "", 1};

  CHECK((only(wallpaperSetCommands("noctalia", pic, "", ctx)) == Argv{"noctalia", "msg", "wallpaper-set", pic}));
  CHECK((only(wallpaperSetCommands("noctalia", pic, "DP-1", ctx)) == Argv{"noctalia", "msg", "wallpaper-set", "DP-1", pic}));
  CHECK(!wallpaperSetCommands("noctalia", "/a  b.png", "", ctx).error.empty());  // it joins words with one space
  CHECK((only(wallpaperSetCommands("skwd", pic, "DP-1", ctx)) == Argv{"skwd-helm", "apply", pic, "-o", "DP-1"}));
  const WallSet cael = wallpaperSetCommands("caelestia", pic, "DP-1", ctx);
  CHECK((only(cael) == Argv{"caelestia", "wallpaper", "--file=" + pic}) && cael.allOutputs);
  CHECK((only(wallpaperSetCommands("dms", pic, "", ctx)) == Argv{"dms", "ipc", "call", "wallpaper", "set", pic}));
  CHECK((only(wallpaperSetCommands("dms", pic, "DP-1", ctx)) == Argv{"dms", "ipc", "call", "wallpaper", "setFor", "DP-1", pic}));
  CHECK((only(wallpaperSetCommands("ambxst", pic, "DP-1", ctx)) == Argv{"ambxst", "wallpaper", pic, "-monitor", "DP-1"}));
  // HyprPanel runs swww through a shell: nothing it could split or expand
  CHECK(!wallpaperSetCommands("hyprpanel", pic, "", ctx).error.empty());
  CHECK(!wallpaperSetCommands("hyprpanel", "/w/$(rm).png", "", ctx).error.empty());
  CHECK((only(wallpaperSetCommands("hyprpanel", "/w/a.png", "", ctx)) == Argv{"hyprpanel", "setWallpaper", "/w/a.png"}));
  CHECK((only(wallpaperSetCommands("awww", pic, "DP-1", ctx)) == Argv{"awww", "img", "-o", "DP-1", "--resize", "crop", "--", pic}));
  CHECK((only(wallpaperSetCommands("swww", pic, "", {{}, "", 2})) == Argv{"swww", "img", "--resize", "fit", "--", pic}));
  CHECK((only(wallpaperSetCommands("hyprpaper", pic, "", ctx)) == Argv{"hyprctl", "hyprpaper", "wallpaper", "," + pic + ",cover"}));
  CHECK((only(wallpaperSetCommands("hyprpaper", pic, "DP-1", {{}, "", 2})) == Argv{"hyprctl", "hyprpaper", "wallpaper", "DP-1," + pic + ",contain"}));
  CHECK(!wallpaperSetCommands("hyprpaper", "/w/a,b.png", "", ctx).error.empty());
  CHECK((only(wallpaperSetCommands("wpaperd", pic, "DP-1", ctx)) == Argv{"wpaperctl", "set", pic, "DP-1"}));
  // swaybg starts again with every screen's picture, the old one ends after
  const WallSet sb = wallpaperSetCommands("swaybg", pic, "DP-1", ctx);
  CHECK(sb.replaces == "swaybg");
  CHECK((only(sb) == Argv{"swaybg", "-o", "HDMI-A-1", "-i", "/w/two.png", "-m", "fill", "-o", "DP-1", "-i", pic, "-m", "fill"}));
  CHECK((only(wallpaperSetCommands("swaybg", pic, "", ctx)) == Argv{"swaybg", "-o", "*", "-i", pic, "-m", "fill"}));
  CHECK((only(wallpaperSetCommands("waypaper", pic, "DP-1", ctx)) ==
         Argv{"waypaper", "--wallpaper=" + pic, "--fill", "fill", "--monitor", "DP-1"}));
  CHECK((only(wallpaperSetCommands("plasma", pic, "", ctx)) == Argv{"plasma-apply-wallpaperimage", "-f", "preserveAspectCrop", pic}));
  CHECK(!wallpaperSetCommands("plasma", "/w/it's.png", "", ctx).error.empty());
  const WallSet gn = wallpaperSetCommands("gnome", pic, "", ctx);
  CHECK(gn.commands.size() == 2 && gn.commands[0][4] == "'file:///home/u/Pictures/a%20b.png'" && gn.commands[1][3] == "picture-uri-dark");
  CHECK(!wallpaperSetCommands("cosmic", pic, "", ctx).error.empty());
  CHECK(!wallpaperSetCommands("mpvpaper", pic, "", ctx).error.empty());
  CHECK(!wallpaperSetCommands("none", pic, "", ctx).error.empty());
  CHECK(!wallpaperSetCommands("awww", "relative.png", "", ctx).error.empty());
  CHECK(wallpaperSetSupported("awww") && wallpaperSetSupported("gnome") && !wallpaperSetSupported("cosmic"));

  // the path check: absolute, canonical, a real file
  const fs::path dir = fs::temp_directory_path() / ("undershell-wallset-" + std::to_string(getpid()));
  fs::create_directories(dir / "sub");
  std::ofstream(dir / "pic.png") << "x";
  std::string why;
  CHECK(checkedWallpaperPath((dir / "sub/../pic.png").string(), why) == fs::canonical(dir / "pic.png").string());
  CHECK(checkedWallpaperPath((dir / "missing.png").string(), why).empty() && !why.empty());
  CHECK(checkedWallpaperPath(dir.string(), why).empty());  // a folder
  CHECK(checkedWallpaperPath((dir / "pic.png").string() + "\n", why).empty());
  fs::remove_all(dir);
  return TEST_RESULT();
}
