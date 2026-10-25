// SPDX-License-Identifier: GPL-3.0-or-later
// Setting the wallpaper through whatever program draws it, with that
// program's own command (undershell reads the wallpaper from all of them;
// this is the other direction, for `undershell msg wallpaper-set` and a
// wallpaper gallery). Each command form was read from the program's own
// source on 2026-10-23 (file and lines noted in wallset.cpp). They run as
// argv lists, never through a shell, with an absolute canonical path.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace undershell {

using Argv = std::vector<std::string>;

struct WallSet {
  std::vector<Argv> commands;  // run in order, detached
  std::string error;           // empty: possible
  bool allOutputs = false;     // the tool has one wallpaper for every screen (an output was ignored)
  std::string replaces;        // a daemon without IPC started again: the old one (this name) is ended after
};

// What the source shows now (to keep the other screens' wallpapers when a
// tool can only be started again with all of them).
struct WallSetContext {
  std::map<std::string, std::string> byOutput;
  std::string fallback;
  int fillMode = 1;  // center crop fit stretch repeat span
};

// The commands that set `path` (absolute, from checkedWallpaperPath) on
// `output` ("" = every screen) through `source`: noctalia, undershell-wall, skwd, caelestia,
// dms, ii, ambxst, hyprpanel, awww, swww, hyprpaper, wpaperd, swaybg,
// waypaper, plasma, gnome. cosmic and mpvpaper give an error.
[[nodiscard]] WallSet wallpaperSetCommands(const std::string& source, const std::string& path, const std::string& output,
                                           const WallSetContext& ctx);
[[nodiscard]] bool wallpaperSetSupported(const std::string& source);

// `path` made absolute and canonical (symlinks resolved); "" with `why` when
// it is not an existing regular file or holds control characters.
[[nodiscard]] std::string checkedWallpaperPath(const std::string& path, std::string& why);

// Runs argv detached (double fork, its own session, execvp: no shell).
void spawnArgv(const Argv& argv);

// `name` in PATH or in ~/.local/bin (user installs, which a systemd user
// service's PATH leaves out); `name` itself when neither has it.
[[nodiscard]] std::string findProgram(const std::string& name);
// A process of this user started as `name` (the basename of its argv[0]):
// /proc's comm is cut at 15 characters, too short for undershell-walld.
[[nodiscard]] bool programRunning(const std::string& name);
// undershell-wall's daemon answers on its socket ($XDG_RUNTIME_DIR/undershell-wall/wall.sock),
// the one undershell-wallctl will use from this environment.
[[nodiscard]] bool wallDaemonAnswers();
// Workshop wallpapers go to undershell-wall as the wallpaper itself while it
// answers; UNDERSHELL_SCENE_SELF=1 keeps them in undershell's scene widget (tests).
[[nodiscard]] bool scenesGoToWallDaemon();
// undershell-wallctl's argv to apply a Workshop id or folder, on `output` or on every screen.
[[nodiscard]] Argv wallDaemonApplyScene(const std::string& scene, const std::string& output);

}  // namespace undershell
