// SPDX-License-Identifier: GPL-3.0-or-later
#include "app.hpp"
#include "wallset.hpp"

#include <ctime>

namespace undershell {

bool App::rulesWantMedia() const {
  return !m_rulesPaused && !m_edit &&
         std::ranges::any_of(m_config.sceneRules, [](const SceneRule& rule) { return rule.playing.has_value(); });
}

bool App::rulesWantWorkspaces() const {
  return !m_rulesPaused && !m_edit && std::ranges::any_of(m_config.sceneRules, [](const SceneRule& rule) {
    return !rule.workspace.empty() || rule.workspaceIndex != 0;
  });
}

void App::updateDesktopRules(double now) {
  const bool enabled = !m_rulesPaused && !m_edit && !m_config.sceneRules.empty();
  if (!enabled && !m_ruleActive) return;
  const bool playing = m_media.state().present && m_media.state().playing;
  const bool focus = m_focus.phase() == FocusTimer::Phase::Work;
  if (enabled && !m_ruleDirty && now < m_ruleCheckAt && m_ruleBattery == m_onBattery &&
      m_rulePlaying == playing && m_ruleFocus == focus)
    return;

  m_ruleBattery = m_onBattery;
  m_rulePlaying = playing;
  m_ruleFocus = focus;
  const std::time_t time = std::time(nullptr);
  std::tm local{};
  localtime_r(&time, &local);
  m_ruleCheckAt = now + (60 - local.tm_sec);
  const SceneRule* rule = enabled ? matchSceneRule(m_config.sceneRules, {local.tm_hour * 60 + local.tm_min, m_onBattery,
                                                                       playing, focus, &m_windowFeed.workspaces()})
                                  : nullptr;
  const std::string profile = rule ? rule->profile : std::string();
  const std::string wallpaper = rule ? rule->wallpaper : std::string();
  if (!m_ruleDirty && profile == m_ruleProfile && wallpaper == m_ruleRequestedWallpaper) return;
  m_ruleDirty = false;
  m_ruleProfile = profile;
  m_ruleRequestedWallpaper = wallpaper;

  m_ruleActive = false;
  m_ruleWidgets.clear();
  if (!profile.empty()) {
    refreshSaves();
    const auto byId = std::ranges::find(m_saves, profile, &SavedLayout::id);
    if (byId != m_saves.end()) {
      m_ruleWidgets = byId->widgets;
      m_ruleActive = true;
    } else {
      US_WARN("scene rule: saved profile {} was not found", profile);
    }
  }

  m_ruleWallpaper.clear();
  if (!wallpaper.empty()) {
    std::string issue;
    const std::string checked = checkedWallpaperPath(wallpaper, issue);
    if (!checked.empty() && !isVideoPath(checked)) m_ruleWallpaper = checked;
    else US_WARN("scene rule: cannot show still wallpaper {}", wallpaper);
  }
  if (!m_ruleWallpaper.empty()) {
    if (!m_ruleActive) m_ruleWidgets = m_config.widgets;
    m_ruleActive = true;
    for (const auto& output : m_outputs) {
      const auto filter = std::ranges::find_if(m_ruleWidgets, [&](const WidgetConfig& widget) {
        return widget.type == "filter" && widget.enabled && !widget.hidden &&
               (widget.output == output->name || (widget.output.empty() && output == m_outputs.front()));
      });
      if (filter != m_ruleWidgets.end()) {
        filter->options.insert_or_assign("draw_wallpaper", true);
        continue;
      }
      WidgetConfig background;
      background.id = std::format("@scene-wallpaper:{}", output->name);
      background.type = "filter";
      background.output = output->name;
      background.depth = false;
      background.layer = -10000;
      background.options.insert("style", "custom");
      background.options.insert("draw_wallpaper", true);
      m_ruleWidgets.insert(m_ruleWidgets.begin(), std::move(background));
    }
  }
  const bool wallChanged = m_noctalia.setManual(m_ruleWallpaper.empty() ? m_config.wallpaper : m_ruleWallpaper,
                                               m_config.wallpaperFill);
  if (!m_ruleWallpaper.empty()) setMotion(false);
  syncWidgets();
  if (wallChanged) noctaliaChanged();
}

std::string App::sceneRulesCommand(const std::string& args) {
  if (args == "pause" || args == "resume") {
    m_rulesPaused = args == "pause";
    m_ruleDirty = true;
    m_ruleCheckAt = 0;
    updateDesktopRules(nowSeconds());
  } else if (!args.empty() && args != "status") {
    return "error: scene-rules pause|resume|status";
  }
  return std::format("{} · {} rule(s) · profile {} · wallpaper {}", m_rulesPaused ? "paused" : "enabled",
                     m_config.sceneRules.size(), m_ruleActive && !m_ruleProfile.empty() ? m_ruleProfile : "-",
                     m_ruleWallpaper.empty() ? "-" : m_ruleWallpaper);
}

}  // namespace undershell
