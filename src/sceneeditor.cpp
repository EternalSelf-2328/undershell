// SPDX-License-Identifier: GPL-3.0-or-later
#include "app.hpp"
#include "scenebundle.hpp"

#include <filesystem>

namespace undershell {

std::string App::startSceneBundle(bool importing, const std::string& path) {
  if (m_sceneBundleBusy) return "error: a scene operation is already in progress";
  if (path.empty()) return "error: expected a scene directory";
  const auto assetDir = (std::filesystem::path(m_configPath).parent_path() / "scenes").string();
  std::string config;
  if (!importing && !readFileChecked(m_configPath, config)) return "error: could not read the desktop layout";
  m_sceneBundleBusy = true;
  m_sceneBundleStatus = importing ? "Importing scene…" : "Exporting scene…";
  markEditDirty();
  m_jobs.run([this, importing, path, assetDir, config = std::move(config)]() -> Jobs::Done {
    try {
      auto result = importing ? importScene(path, assetDir) : exportScene(config, path);
      return [this, importing, result = std::move(result)] {
        m_sceneBundleBusy = false;
        if (importing && !replaceLayout(result.widgets, true)) {
          std::error_code ec;
          std::filesystem::remove_all(result.path, ec);  // the newly created bundle copy
          m_sceneBundleStatus = "error: could not apply the imported scene";
        } else {
          m_sceneBundleStatus = std::string(importing ? "Imported scene: " : "Exported scene: ") + result.path;
        }
        US_INFO("{}", m_sceneBundleStatus);
        markEditDirty();
      };
    } catch (const std::exception& error) {
      return [this, detail = std::string(error.what())] {
        m_sceneBundleBusy = false;
        m_sceneBundleStatus = "error: " + detail;
        US_WARN("scene bundle: {}", detail);
        markEditDirty();
      };
    }
  });
  return m_sceneBundleStatus;
}

void App::chooseSceneBundle(bool importing) {
  if (m_sceneBundleBusy || m_picker.busy()) return;
  const bool es = spanishUi();
  FilePicker::Request request;
  request.folders = true;
  request.title = importing ? (es ? "Elegir la carpeta de la escena" : "Choose the scene folder")
                            : (es ? "Elegir dónde guardar la escena" : "Choose where to save the scene");
  setEditorAside(true);
  const bool opened = m_picker.open(std::move(request), [this, importing](std::vector<std::string> paths, std::string error) {
    setEditorAside(false);
    if (!paths.empty()) m_sceneBundleStatus = startSceneBundle(importing, paths.front());
    else if (!error.empty()) m_sceneBundleStatus = "error: " + error;
    markEditDirty();
  });
  if (!opened) {
    setEditorAside(false);
    m_sceneBundleStatus = es ? "error: no se pudo abrir el selector de carpetas" : "error: could not open the folder chooser";
    markEditDirty();
  }
}

}  // namespace undershell
