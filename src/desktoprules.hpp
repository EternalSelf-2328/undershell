// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

namespace undershell {

struct WorkspaceState {
  std::string name, output;
  int index = 0;
  bool focused = false;
};

struct SceneRule {
  std::string profile, wallpaper, workspace, workspaceOutput;
  int workspaceIndex = 0;
  // Local minutes since midnight. A range may wrap across midnight.
  int from = 0, until = 1440;
  std::optional<bool> battery, playing, focus;
  bool operator==(const SceneRule&) const = default;
};

struct SceneInputs {
  int minute = 0;
  bool battery = false, playing = false, focus = false;
  const std::vector<WorkspaceState>* workspaces = nullptr;
};

[[nodiscard]] std::optional<int> sceneMinute(const std::string& time);
[[nodiscard]] std::string sceneTime(int minute);
[[nodiscard]] bool storeSceneRules(const std::string& path, const std::vector<SceneRule>& expected,
                                   const std::vector<SceneRule>& rules, std::string& error);
[[nodiscard]] std::vector<SceneRule> readSceneRules(const toml::array* rules);
[[nodiscard]] const SceneRule* matchSceneRule(const std::vector<SceneRule>& rules, const SceneInputs& inputs);
[[nodiscard]] bool parseWorkspaceEvent(const std::string& line, std::vector<WorkspaceState>& workspaces);

}  // namespace undershell
