// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktoprules.hpp"

#include "common.hpp"

#include <nlohmann/json.hpp>
#include <sstream>

namespace undershell {
std::optional<int> sceneMinute(const std::string& time) {
  if (time.size() != 5 || time[2] != ':') return {};
  for (const size_t index : {0u, 1u, 3u, 4u})
    if (time[index] < '0' || time[index] > '9') return {};
  const int hour = (time[0] - '0') * 10 + time[1] - '0';
  const int minute = (time[3] - '0') * 10 + time[4] - '0';
  if (hour > 24 || minute > 59 || (hour == 24 && minute != 0)) return {};
  return hour * 60 + minute;
}

std::string sceneTime(int minute) {
  return std::format("{:02}:{:02}", minute / 60, minute % 60);
}

namespace {
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
    const auto from = sceneMinute((*t)["from"].value_or(std::string("00:00")));
    const auto until = sceneMinute((*t)["until"].value_or(std::string("24:00")));
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

bool storeSceneRules(const std::string& path, const std::vector<SceneRule>& expected,
                     const std::vector<SceneRule>& rules, std::string& error) {
  std::string original;
  if (!readFileChecked(path, original)) {
    error = "read";
    return false;
  }
  try {
    const auto root = toml::parse(original);
    const auto* existing = root["scene_rule"].as_array();
    if (readSceneRules(existing) != expected) {
      error = "changed";
      return false;
    }
    std::vector<bool> remove(std::count(original.begin(), original.end(), '\n') + 2, false);
    auto discard = [&](const auto& source) {
      for (size_t i = source.begin.line; i && i <= source.end.line && i < remove.size(); ++i) remove[i] = true;
    };
    if (root.contains("scene_rule") && (!existing || existing->size() != expected.size())) {
      error = "format";
      return false;
    }
    if (existing) {
      if (!existing->empty() && !(*existing)[0].as_table()) {
        error = "format";
        return false;
      }
      // Inline arrays (including an empty array) can also be present in hand-written configs.
      if (existing->empty() || (*existing)[0].as_table()->is_inline()) discard(existing->source());
      constexpr std::array<std::string_view, 10> keys = {"profile", "wallpaper", "workspace", "workspace_output",
          "workspace_index", "from", "until", "battery", "playing", "focus"};
      for (const auto& node : *existing) {
        const auto& table = *node.as_table();
        discard(table.source());
        for (const auto& [key, value] : table) {
          if (std::ranges::find(keys, key.str()) == keys.end()) {
            error = "format";
            return false;
          }
          discard(value.source());
        }
      }
    }
    std::ostringstream out;
    std::istringstream in(original);
    size_t lineNumber = 0;
    for (std::string line; std::getline(in, line);)
      if (!remove[++lineNumber]) out << line << '\n';
    for (const SceneRule& rule : rules) {
      toml::table table;
      if (!rule.profile.empty()) table.insert("profile", rule.profile);
      if (!rule.wallpaper.empty()) table.insert("wallpaper", rule.wallpaper);
      if (!rule.workspace.empty()) table.insert("workspace", rule.workspace);
      if (!rule.workspaceOutput.empty()) table.insert("workspace_output", rule.workspaceOutput);
      if (rule.workspaceIndex > 0) table.insert("workspace_index", rule.workspaceIndex);
      if (rule.from != 0) table.insert("from", sceneTime(rule.from));
      if (rule.until != 1440) table.insert("until", sceneTime(rule.until));
      if (rule.battery) table.insert("battery", *rule.battery);
      if (rule.playing) table.insert("playing", *rule.playing);
      if (rule.focus) table.insert("focus", *rule.focus);
      out << "\n[[scene_rule]]\n" << toml::toml_formatter(table, toml::format_flags::allow_unicode_strings) << '\n';
    }
    const std::string result = out.str();
    const auto checked = toml::parse(result);
    if (readSceneRules(checked["scene_rule"].as_array()) != rules) {
      error = "format";
      return false;
    }
    if (!writeFileAtomic(path, result)) {
      error = "write";
      return false;
    }
    error.clear();
    return true;
  } catch (const toml::parse_error&) {
    error = "format";
    return false;
  }
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
