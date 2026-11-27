// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace undershell {

struct SceneBundle {
  std::string path;
  std::string widgets;
};

// The destination is an existing directory. Each operation creates its own
// scene folder, and an incomplete folder is removed on error.
SceneBundle exportScene(const std::string& config, const std::string& destination);
SceneBundle importScene(const std::string& scene, const std::string& destination);

}  // namespace undershell
