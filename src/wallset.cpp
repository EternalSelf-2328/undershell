// SPDX-License-Identifier: GPL-3.0-or-later
#include "wallset.hpp"

#include "common.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace undershell {

namespace fs = std::filesystem;

namespace {

bool has(const std::string& s, const char* chars) { return s.find_first_of(chars) != std::string::npos; }

// file:// URI with everything but unreserved characters and '/' escaped
std::string fileUri(const std::string& path) {
  std::string out = "file://";
  for (const unsigned char c : path) {
    if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
    else out += std::format("%{:02X}", c);
  }
  return out;
}

// center crop fit stretch repeat span → each tool's word
std::string awwwResize(int fill) { return fill == 0 ? "no" : fill == 2 ? "fit" : fill == 3 ? "stretch" : "crop"; }
std::string swaybgMode(int fill) { return fill == 0 ? "center" : fill == 2 ? "fit" : fill == 3 ? "stretch" : fill == 4 ? "tile" : "fill"; }
std::string waypaperFill(int fill) { return fill == 0 ? "center" : fill == 2 ? "fit" : fill == 3 ? "stretch" : fill == 4 ? "tile" : "fill"; }
// hyprpaper 0.8 over hyprctl: fit/stretch end up as cover (its toFitMode only
// knows "fill"), so only contain, cover and tile mean what they say
std::string hyprpaperFit(int fill) { return fill == 2 || fill == 0 ? "contain" : fill == 4 ? "tile" : "cover"; }
std::string plasmaFill(int fill) { return fill == 2 ? "preserveAspectFit" : fill == 3 ? "stretch" : fill == 0 ? "pad" : "preserveAspectCrop"; }

}  // namespace

bool wallpaperSetSupported(const std::string& source) {
  static const char* kNames[] = {"noctalia", "undershell-wall", "skwd", "caelestia", "dms", "ii", "ambxst", "hyprpanel", "awww", "swww",
                                 "hyprpaper", "wpaperd", "swaybg", "waypaper", "plasma", "gnome"};
  return std::ranges::any_of(kNames, [&](const char* n) { return source == n; });
}

WallSet wallpaperSetCommands(const std::string& source, const std::string& path, const std::string& output, const WallSetContext& ctx) {
  WallSet s;
  if (path.empty() || path[0] != '/') {
    s.error = "the path must be absolute";
    return s;
  }
  const bool one = !output.empty();
  if (source == "noctalia") {
    // noctalia src/shell/wallpaper/wallpaper.cpp:784-821: the arguments are
    // joined with single spaces, then "connector path" or just "path"
    if (path.find("  ") != std::string::npos || has(path, "\t\n")) {
      s.error = "Noctalia cannot take a path with repeated spaces or tabs";
      return s;
    }
    s.commands.push_back(one ? Argv{"noctalia", "msg", "wallpaper-set", output, path} : Argv{"noctalia", "msg", "wallpaper-set", path});
  } else if (source == "undershell-wall") {
    // undershell-wall (a fork of skwd-deck): the same apply verb
    const std::string ctl = findProgram("undershell-wallctl");
    s.commands.push_back(one ? Argv{ctl, "apply", path, "-o", output} : Argv{ctl, "apply", path});
  } else if (source == "skwd") {
    // skwd-deck crates/skwd-helm/src/commands.rs:170-197; output "*" by default
    s.commands.push_back(one ? Argv{"skwd-helm", "apply", path, "-o", output} : Argv{"skwd-helm", "apply", path});
  } else if (source == "caelestia") {
    // caelestia cli src/caelestia/parser.py:105-127: one wallpaper (path.txt)
    s.commands.push_back({"caelestia", "wallpaper", "--file=" + path});
    s.allOutputs = one;
  } else if (source == "dms") {
    // DankMaterialShell quickshell/Services/WallpaperCyclingService.qml:403-497
    s.commands.push_back(one ? Argv{"dms", "ipc", "call", "wallpaper", "setFor", output, path}
                             : Argv{"dms", "ipc", "call", "wallpaper", "set", path});
  } else if (source == "ii") {
    // end-4 dots-hyprland quickshell/ii/scripts/colors/switchwall.sh:319-383
    const std::string script = expandHome("~/.config/quickshell/ii/scripts/colors/switchwall.sh");
    if (!fs::exists(script)) {
      s.error = "illogical-impulse's switchwall.sh is not at " + script;
      return s;
    }
    s.commands.push_back({script, "--image", path});
    s.allOutputs = one;
  } else if (source == "ambxst") {
    // Ambxst backend/cmd/ambxst/cmds_wallpaper_preset.go:31-133 (it copies a
    // picture from outside its wallpaper folder into that folder)
    s.commands.push_back(one ? Argv{"ambxst", "wallpaper", path, "-monitor", output} : Argv{"ambxst", "wallpaper", path});
  } else if (source == "hyprpanel") {
    // HyprPanel src/services/cli/commander/commands/appearance/index.ts:9-33;
    // the launcher joins argv with "$*" and the wallpaper service runs swww
    // through a shell: only paths that cannot be split or expanded
    if (has(path, " \t\n\"'$`\\;&|<>(){}*?[]!#")) {
      s.error = "HyprPanel cannot safely take a path with spaces or shell characters";
      return s;
    }
    s.commands.push_back({"hyprpanel", "setWallpaper", path});
    s.allOutputs = one;
  } else if (source == "awww" || source == "swww") {
    // awww client/src/cli.rs:465-519 (-o a comma list; empty: every output)
    Argv a{source, "img"};
    if (one) a.insert(a.end(), {"-o", output});
    a.insert(a.end(), {"--resize", awwwResize(ctx.fillMode), "--", path});
    s.commands.push_back(a);
  } else if (source == "hyprpaper") {
    // Hyprland hyprctl/src/hyprpaper/Hyprpaper.cpp:55-140 (hyprpaper 0.8):
    // "monitor,path,fit"; an empty monitor is every one; no commas possible
    if (path.find(',') != std::string::npos) {
      s.error = "hyprpaper cannot take a path with a comma";
      return s;
    }
    s.commands.push_back({"hyprctl", "hyprpaper", "wallpaper", std::format("{},{},{}", output, path, hyprpaperFit(ctx.fillMode))});
  } else if (source == "wpaperd") {
    // wpaperd cli/src/opts.rs:39-46 (1.3.0): set <path> [outputs…]
    Argv a{"wpaperctl", "set", path};
    if (one) a.push_back(output);
    s.commands.push_back(a);
  } else if (source == "swaybg") {
    // swaybg main.c:478-509 has no IPC: a new one with every screen's
    // picture, then the old one ends (no flash in between)
    Argv a{"swaybg"};
    const std::string mode = swaybgMode(ctx.fillMode);
    if (!one) {
      a.insert(a.end(), {"-o", "*", "-i", path, "-m", mode});
    } else {
      if (!ctx.fallback.empty()) a.insert(a.end(), {"-o", "*", "-i", ctx.fallback, "-m", mode});
      for (const auto& [out, img] : ctx.byOutput)
        if (out != output && !img.empty()) a.insert(a.end(), {"-o", out, "-i", img, "-m", mode});
      a.insert(a.end(), {"-o", output, "-i", path, "-m", mode});
    }
    s.commands.push_back(a);
    s.replaces = "swaybg";
  } else if (source == "waypaper") {
    // waypaper __main__.py:33-91 (2.9)
    Argv a{"waypaper", "--wallpaper=" + path, "--fill", waypaperFill(ctx.fillMode)};
    if (one) a.insert(a.end(), {"--monitor", output});
    s.commands.push_back(a);
  } else if (source == "plasma") {
    // plasma-workspace wallpapers/image/plasma-apply-wallpaperimage.cpp:32-101:
    // every desktop; it refuses a path with a quote
    if (path.find('\'') != std::string::npos) {
      s.error = "Plasma cannot take a path with a quote";
      return s;
    }
    s.commands.push_back({"plasma-apply-wallpaperimage", "-f", plasmaFill(ctx.fillMode), path});
    s.allOutputs = one;
  } else if (source == "gnome") {
    // gsettings-desktop-schemas org.gnome.desktop.background: the light and
    // dark pictures; the value is a GVariant string, the URI escaped
    const std::string v = "'" + fileUri(path) + "'";
    s.commands.push_back({"gsettings", "set", "org.gnome.desktop.background", "picture-uri", v});
    s.commands.push_back({"gsettings", "set", "org.gnome.desktop.background", "picture-uri-dark", v});
    s.allOutputs = one;
  } else if (source == "cosmic") {
    s.error = "COSMIC has no command for it yet: set the wallpaper in COSMIC's settings";
  } else if (source == "mpvpaper") {
    s.error = "mpvpaper plays a video; set it with mpvpaper itself";
  } else {
    s.error = "no wallpaper program known to undershell is running (source: " + source + ")";
  }
  return s;
}

std::string checkedWallpaperPath(const std::string& path, std::string& why) {
  if (path.empty()) {
    why = "no path";
    return {};
  }
  if (std::ranges::any_of(path, [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7f; })) {
    why = "control characters in the path";
    return {};
  }
  std::error_code ec;
  const fs::path p = fs::canonical(fs::path(expandHome(path)), ec);
  if (ec || !fs::is_regular_file(p, ec)) {
    why = "not a file: " + path;
    return {};
  }
  return p.string();
}

void spawnArgv(const Argv& argv) {
  if (argv.empty()) return;
  std::vector<char*> args;
  for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);
  const pid_t pid = fork();
  if (pid == 0) {
    setsid();
    if (fork() == 0) {
      execvp(args[0], args.data());
      _exit(127);
    }
    _exit(0);
  }
  if (pid > 0) waitpid(pid, nullptr, 0);  // the go-between exits at once: no zombie left behind
}

std::string findProgram(const std::string& name) {
  std::error_code ec;
  const char* path = std::getenv("PATH");
  std::string dirs = path ? path : "";
  dirs += ":" + expandHome("~/.local/bin");
  size_t start = 0;
  while (start <= dirs.size()) {
    const size_t end = std::min(dirs.find(':', start), dirs.size());
    const fs::path dir = dirs.substr(start, end - start);
    start = end + 1;
    if (dir.empty()) continue;
    const fs::path candidate = dir / name;
    if (fs::is_regular_file(candidate, ec) && access(candidate.c_str(), X_OK) == 0) return candidate.string();
  }
  return name;
}

bool programRunning(const std::string& name) {
  std::error_code ec;
  const uid_t me = getuid();
  for (const auto& e : fs::directory_iterator("/proc", ec)) {
    const std::string pid = e.path().filename().string();
    if (pid.empty() || !std::isdigit(static_cast<unsigned char>(pid[0]))) continue;
    struct stat st{};
    if (::stat(e.path().c_str(), &st) != 0 || st.st_uid != me) continue;
    const std::string cmdline = readFile((e.path() / "cmdline").string());
    const std::string argv0 = cmdline.substr(0, cmdline.find('\0'));
    if (!argv0.empty() && fs::path(argv0).filename() == name) return true;
  }
  return false;
}

}  // namespace undershell
