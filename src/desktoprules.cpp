// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktoprules.hpp"

#include "common.hpp"

#include <nlohmann/json.hpp>

namespace undershell {
namespace {
std::optional<int> minuteOf(const std::string& time) {
  if (time.size() != 5 || time[2] != ':') return {};
  for (const size_t index : {0u, 1u, 3u, 4u})
    if (time[index] < '0' || time[index] > '9') return {};
  const int hour = (time[0] - '0') * 10 + time[1] - '0';
  const int minute = (time[3] - '0') * 10 + time[4] - '0';
  if (hour > 24 || minute > 59 || (hour == 24 && minute != 0)) return {};
  return hour * 60 + minute;
}

bool workspaceMatches(const SceneRule& rule, const std::vector<WorkspaceState>* workspaces) {
  if (rule.workspace.empty() && rule.workspaceIndex == 0) return true;
  if (!workspaces) return false;
  for (const auto& workspace : *workspaces) {
    if (rule.workspaceOutput.empty() ? !workspace.focused : workspace.output != rule.workspaceOutput) continue;
    if (!rule.workspace.empty() && workspace.name != rule.workspace) continue;
    if (rule.workspaceIndex != 0 && workspace.index != rule.workspaceIndex) continue;
    return true;
  }
  return false;
}
}  // namespace

std::vector<SceneRule> readSceneRules(const toml::array* rules) {
  std::vector<SceneRule> result;
  if (!rules) return result;
  for (const auto& node : *rules) {
    const auto* t = node.as_table();
    if (!t) continue;
    bool typed = true;
    for (const auto* key : {"profile", "wallpaper", "workspace", "workspace_output", "from", "until"})
      if (t->contains(key) && !(*t)[key].is_string()) typed = false;
    for (const auto* key : {"battery", "playing", "focus"})
      if (t->contains(key) && !(*t)[key].is_boolean()) typed = false;
    if (!typed || (t->contains("workspace_index") && !(*t)["workspace_index"].is_integer())) {
      US_WARN("scene rule ignored: wrong field type");
      continue;
    }
    const auto from = minuteOf((*t)["from"].value_or(std::string("00:00")));
    const auto until = minuteOf((*t)["until"].value_or(std::string("24:00")));
    auto text = [&](const char* key) { return (*t)[key].value_or(std::string()); };
    const auto index = (*t)["workspace_index"].value_or(int64_t{0});
    SceneRule rule;
    rule.profile = text("profile");
    rule.wallpaper = text("wallpaper");
    rule.workspace = text("workspace");
    rule.workspaceOutput = text("workspace_output");
    rule.battery = (*t)["battery"].value<bool>();
    rule.playing = (*t)["playing"].value<bool>();
    rule.focus = (*t)["focus"].value<bool>();
    if (!from || !until || *from >= 1440 || *from == *until || index < 0 || index > 1000) {
      US_WARN("scene rule ignored: invalid time range or workspace index");
      continue;
    }
    if (rule.profile.empty() && rule.wallpaper.empty()) continue;
    rule.from = *from;
    rule.until = *until;
    rule.workspaceIndex = static_cast<int>(index);
    result.push_back(std::move(rule));
  }
  return result;
}

const SceneRule* matchSceneRule(const std::vector<SceneRule>& rules, const SceneInputs& inputs) {
  for (const auto& rule : rules) {
    const bool inTime = rule.from < rule.until ? inputs.minute >= rule.from && inputs.minute < rule.until
                                               : inputs.minute >= rule.from || inputs.minute < rule.until;
    if (!inTime || (rule.battery && *rule.battery != inputs.battery) || (rule.playing && *rule.playing != inputs.playing) ||
        (rule.focus && *rule.focus != inputs.focus) || !workspaceMatches(rule, inputs.workspaces))
      continue;
    return &rule;
  }
  return nullptr;
}

bool parseWorkspaceEvent(const std::string& line, std::vector<WorkspaceState>& workspaces) {
  using Json = nlohmann::json;
  try {
    const auto event = Json::parse(line, nullptr, false);
    if (!event.is_object() || event.value("event", std::string()) != "workspaces" || !event.contains("data") ||
        !event["data"].is_array())
      return false;
    std::vector<WorkspaceState> next;
    for (const auto& item : event["data"]) {
      if (!item.is_object() || !item.value("active", false)) continue;
      next.push_back({item.value("name", std::string()), item.value("output", std::string()), item.value("index", 0),
                      item.value("focused", false)});
    }
    workspaces = std::move(next);
    return true;
  } catch (const Json::exception&) {
    return false;
  }
}

}  // namespace undershell
