// SPDX-License-Identifier: GPL-3.0-or-later
// Is the wallpaper on screen moving (a video, a Wallpaper Engine scene)? A
// depth mask belongs to one still picture, so while it moves undershell turns
// depth off for every widget and locks its controls.
#pragma once

#include <string>
#include <vector>

namespace undershell {

// `skwd-helm current --json`: does a connected output among `outputs` (all
// when empty) show a video or a scene?
bool skwdShowsMotion(const std::string& json, const std::vector<std::string>& outputs);
// a wallpaper path that is a video by its extension
bool isVideoPath(const std::string& path);

}  // namespace undershell
