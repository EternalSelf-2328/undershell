// SPDX-License-Identifier: GPL-3.0-or-later
// Layouts saved on purpose, like save slots in a game: the editor's Profiles
// panel (and `undershell msg save…`) keeps named copies of the widgets on
// screen in saves/<id>.toml and loads one back, onto one monitor (whose widgets
// it replaces; the others keep theirs) or all of them. The result is the
// current wallpaper's profile; loading is one undo step.
#include "app.hpp"

#include <filesystem>
#include <regex>

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

std::string App::blockOutput(const std::string& block) const {
  static const std::regex kOutput(R"re((^|\n)\s*output\s*=\s*"([^"]*)")re");
  std::smatch m;
  const std::string named = std::regex_search(block, m, kOutput) ? m[2].str() : std::string();
  for (const auto& o : m_outputs)
    if (o->name == named) return named;
  return m_outputs.empty() ? named : m_outputs.front()->name;  // as syncWidgets places it
}

bool App::loadSave(const std::string& id, const std::string& output) {
  refreshSaves();
  for (const auto& s : m_saves)
    if (s.id == id) {
      std::string blocks = s.blocks;
      if (!output.empty()) {
        // the other monitors keep their widgets; the target gets the layout
        std::vector<std::string> targets;
        if (output == "all")
          for (const auto& o : m_outputs) targets.push_back(o->name);
        else
          targets.push_back(output);
        blocks.clear();
        for (const auto& b : Config::splitBlocks(Config::widgetBlocks(readFile(m_configPath))))
          if (std::find(targets.begin(), targets.end(), blockOutput(b)) == targets.end()) blocks += b + "\n";
        for (const auto& t : targets) blocks += Config::moveToOutput(s.blocks, t) + "\n";
        while (blocks.ends_with("\n\n")) blocks.pop_back();
      }
      replaceLayout(blocks, true);  // repeated ids are renamed on load
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
