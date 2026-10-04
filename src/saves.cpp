// SPDX-License-Identifier: GPL-3.0-or-later
// Layouts saved on purpose, like save slots in a game: the editor's Profiles
// panel (and `undershell msg save…`) keeps named copies of the widgets on
// screen in saves/<id>.toml and loads one back. Loading replaces the layout of
// the current wallpaper, so it also becomes that wallpaper's profile; it is one
// undo step.
#include "app.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace undershell {

std::string App::savesDir() const { return (fs::path(m_configPath).parent_path() / "saves").string(); }

void App::refreshSaves() {
  m_saves = Config::listSaves(savesDir());
  const std::string now = Config::widgetBlocks(readFile(m_configPath));
  m_saveCurrent.clear();
  for (const auto& s : m_saves)
    if (s.blocks == now) {
      m_saveCurrent = s.id;
      break;
    }
}

std::string App::saveLayout(const std::string& name) {
  std::string use = name;
  if (use.empty()) {
    // "Profile N": the first number not taken
    const bool es = spanishUi();
    for (int n = static_cast<int>(m_saves.size()) + 1;; ++n) {
      use = std::format("{} {}", es ? "Perfil" : "Profile", n);
      bool taken = false;
      for (const auto& s : m_saves) taken = taken || s.name == use;
      if (!taken) break;
    }
  }
  const std::string id = Config::writeSave(savesDir(), use, fs::path(profileWallpaper()).filename().string(),
                                           Config::widgetBlocks(readFile(m_configPath)));
  refreshSaves();
  markEditDirty();
  return id;
}

bool App::overwriteSave(const std::string& id) {
  const bool ok = !Config::writeSave(savesDir(), "", fs::path(profileWallpaper()).filename().string(),
                                     Config::widgetBlocks(readFile(m_configPath)), id)
                       .empty();
  refreshSaves();
  markEditDirty();
  return ok;
}

// replaces the widgets on screen with `blocks`, as one undo step
void App::replaceLayout(const std::string& blocks, bool record) {
  const std::string text = readFile(m_configPath);
  if (record) {
    EditOp op;
    op.kind = "layout";
    op.block = Config::widgetBlocks(text);
    m_undo.push_back(op);
    m_redo.clear();
    m_lastOpKind.clear();
  }
  if (!writeFileAtomic(m_configPath, Config::replaceWidgetBlocks(text, blocks))) return;
  m_selected = nullptr;
  m_pointerWidget = nullptr;
  m_drag = Drag::None;
  loadConfig();
  refreshSaves();
  markEditDirty();
}

bool App::loadSave(const std::string& id, const std::string& output) {
  refreshSaves();
  for (const auto& s : m_saves)
    if (s.id == id) {
      replaceLayout(output.empty() ? s.blocks : Config::moveToOutput(s.blocks, output), true);
      US_INFO("loaded saved layout \"{}\"{}", s.name, output.empty() ? std::string() : " on " + output);
      return true;
    }
  return false;
}

std::string App::savesJson() {
  refreshSaves();
  auto q = [](const std::string& v) {
    std::string o = "\"";
    for (char c : v) {
      if (c == '"' || c == '\\') o += '\\';
      if (static_cast<unsigned char>(c) < 0x20) continue;
      o += c;
    }
    return o + "\"";
  };
  std::string s = "[";
  for (size_t i = 0; i < m_saves.size(); ++i) {
    const auto& v = m_saves[i];
    s += std::format("{}{{\"id\":{},\"name\":{},\"created\":{},\"wallpaper\":{},\"widgets\":{},\"current\":{}}}", i ? "," : "",
                     q(v.id), q(v.name), q(v.created), q(v.wallpaper), v.widgets.size(), v.id == m_saveCurrent);
  }
  return s + "]";
}

}  // namespace undershell
