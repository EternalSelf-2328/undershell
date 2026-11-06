// SPDX-License-Identifier: GPL-3.0-or-later
// Is the wallpaper on screen moving (a video, a Wallpaper Engine scene)? A
// depth mask belongs to one still picture, so while it moves undershell turns
// depth off for every widget and locks its controls.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace undershell {

// `skwd-helm current --json`: does a connected output among `outputs` (all
// when empty) show a video or a scene?
bool skwdShowsMotion(const std::string& json, const std::vector<std::string>& outputs);
// a wallpaper path that is a video by its extension
bool isVideoPath(const std::string& path);
// `skwd-helm current --json`: the picture each connected output shows
// (output name -> path); skwd (and its fork undershell-wall) may draw the
// wallpaper with the shell's own wallpaper service off, so what the shell
// keeps can be an old one
std::map<std::string, std::string> skwdPaths(const std::string& json);

// skwd's events, as they happen: a subscription on its daemon's socket
// ($SKWD_WALL_V2_SOCK, or wall.sock in $XDG_RUNTIME_DIR/skwd-wall-v2 or
// /undershell-wall), one JSON object per line. Retries every 10 s while the
// daemon is away; no daemon, no cost.
class SkwdEvents {
public:
  SkwdEvents() = default;
  SkwdEvents(const SkwdEvents&) = delete;
  SkwdEvents& operator=(const SkwdEvents&) = delete;
  ~SkwdEvents();
  void tick(double now);           // connects when due
  [[nodiscard]] int fd() const { return m_fd; }
  // reads what came; true when the wallpaper or the screens changed
  bool dispatch();
  // a line from the daemon: does it say the wallpaper or the screens changed?
  static bool changes(const std::string& line);

private:
  void close();
  int m_fd = -1;
  double m_retryAt = 0;
  std::string m_buf;
};

}  // namespace undershell
