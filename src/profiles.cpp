// SPDX-License-Identifier: GPL-3.0-or-later
// A widget layout per wallpaper. config.toml always holds the layout of the
// wallpaper on screen; every other wallpaper's layout waits in
// profiles/<sha256>.toml next to it. When the wallpaper changes (Noctalia's
// settings, whoever set it: its picker, skwd-wall, a script) the layout on
// screen is filed under the old wallpaper and the new one's comes back, if it
// has one; a wallpaper seen for the first time keeps the current layout.
// Only the [[widget]] blocks move: [general] stays shared.
#include "app.hpp"
#include "wallset.hpp"
#include "wallkind.hpp"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace undershell {

std::string App::profileDir() const { return (fs::path(m_configPath).parent_path() / "profiles").string(); }

// sha256 of the image (renaming a file keeps its layout); solid colours by name
std::string App::profileKeyFor(const std::string& wallpaper) {
  if (wallpaper.empty()) return {};
  if (wallpaper.starts_with("color:")) {
    std::string k = "color-";
    for (char c : wallpaper.substr(6))
      if (std::isalnum(static_cast<unsigned char>(c))) k += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return k;
  }
  return m_depth.sha256Of(wallpaper);
}

// the wallpaper that decides: the one under the widgets (the output they are
// shown on), else the first output's, else Noctalia's default. A widget
// whose output is not connected (a laptop's panel with the lid shut) is
// shown on the first output, so that one decides: the panel's wallpaper is
// not seen, and is often stale (a picker that sets only the monitor in use).
std::string App::profileWallpaper() const {
  const NoctaliaState& st = m_noctalia.state();
  for (const auto& w : m_config.widgets) {
    if (!w.enabled || w.output.empty()) continue;
    for (const auto& o : m_outputs)
      if (o->name == w.output) return st.wallpaperFor(o->name);
  }
  if (!m_outputs.empty()) return st.wallpaperFor(m_outputs.front()->name);
  return st.defaultWallpaper;
}

void App::checkProfile() {
  if (!m_config.profiles || !m_noctalia.ready()) return;
  const std::string wall = profileWallpaper();
  const std::string key = profileKeyFor(wall);
  if (key.empty()) return;
  const std::string dir = profileDir();
  const std::string marker = dir + "/current";
  if (m_profileKey.empty()) {
    // which wallpaper does config.toml belong to? (it may have changed while
    // undershell was not running)
    std::string owner = readFile(marker);
    while (!owner.empty() && std::isspace(static_cast<unsigned char>(owner.back()))) owner.pop_back();
    // a key names a file in profiles/: a SHA-256 or a colour's name, nothing else
    const bool sane = (owner.size() == 64 && std::ranges::all_of(owner, [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)); })) ||
                      (owner.starts_with("color-") && owner.size() < 64 &&
                       std::ranges::all_of(owner.substr(6), [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)); }));
    if (!sane) owner.clear();
    m_profileKey = owner.empty() ? key : owner;
    m_profileWall = wall;
    if (owner.empty()) {
      fs::create_directories(dir);
      writeFileAtomic(marker, key + "\n");
      return;
    }
  }
  if (key == m_profileKey) {
    m_profileWall = wall;
    return;
  }
  // file what is on screen under the wallpaper it was made for
  const int r = Config::switchProfile(m_configPath, dir, m_profileKey, m_profileWall, key);
  if (r < 0) {
    US_WARN("could not switch the widget layout; keeping the current one");
    return;
  }
  const bool restored = r > 0;
  if (restored) {
    // the old layout's undo steps and selection name widgets that are gone
    m_undo.clear();
    m_redo.clear();
    m_selected = nullptr;
    m_pointerWidget = nullptr;
    m_drag = Drag::None;
    loadConfig();
  }
  US_INFO("wallpaper {}: {}", fs::path(wall).filename().string(),
          restored ? "restored its widget layout" : "no saved layout yet, keeping the current one");
  m_profileKey = key;
  m_profileWall = wall;
  writeFileAtomic(marker, key + "\n");
}

// Asks skwd (when it is installed) what each output shows, off the main
// thread; a video path set by anything else counts too.
void App::checkWallpaperKind() {
  if (m_motionCheckBusy) {
    m_motionCheckAgain = true;
    return;
  }
  std::vector<std::string> outs = outputNames();
  bool videoPath = false;
  for (const auto& o : outs) videoPath = videoPath || isVideoPath(m_noctalia.state().wallpaperFor(o));
  // undershell-wall (skwd's fork) answers like skwd-helm when its daemon runs
  std::string ctl;
  if (programRunning("undershell-walld")) ctl = findProgram("undershell-wallctl");
  else if (fs::exists("/usr/bin/skwd-helm") || fs::exists("/usr/local/bin/skwd-helm")) ctl = "skwd-helm";
  if (ctl.empty()) {
    setMotion(videoPath);
    return;
  }
  m_motionCheckBusy = true;
  m_jobs.run([this, outs, videoPath, ctl]() -> Jobs::Done {
    const std::string json = runCommand("'" + ctl + "' current --json", 4);
    const bool motion = videoPath || skwdShowsMotion(json, outs);
    return [this, motion] {
      m_motionCheckBusy = false;
      setMotion(motion);
      if (m_motionCheckAgain) {
        m_motionCheckAgain = false;
        checkWallpaperKind();
      }
    };
  });
}

void App::setMotion(bool on) {
  if (on == m_motion) return;
  m_motion = on;
  US_INFO("{}", on ? "moving wallpaper on screen: depth off and locked" : "still wallpaper: depth back on");
  if (on && m_paintMode) setPaintMode(false);
  for (auto& w : m_widgets) w->needsRender = true;
  markEditDirty();
}

std::string App::profileStatus() const {
  if (!m_config.profiles) return "profiles: off";
  std::error_code ec;
  size_t n = 0;
  for (const auto& e : fs::directory_iterator(profileDir(), ec))
    if (e.path().extension() == ".toml") ++n;
  return std::format("profile: {} ({}) · {} other layout(s) saved", m_profileKey.empty() ? "-" : m_profileKey.substr(0, 12),
                     m_profileWall.empty() ? "-" : fs::path(m_profileWall).filename().string(),
                     n - (fs::exists(profileDir() + "/" + m_profileKey + ".toml", ec) ? 1 : 0));
}

}  // namespace undershell
