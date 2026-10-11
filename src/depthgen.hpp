// SPDX-License-Identifier: GPL-3.0-or-later
// Depth without Noctalia's wallpaper_depth plugin: undershell's own copy of
// the same model (Depth Anything V2 Small, fetched only when asked, checked by
// size and sha256; the plugin's copy is used when it is there) and the helper
// that runs it (undershell-depth, a process of its own) to write the depth map
// (.npy) of each wallpaper into ~/.cache/undershell/depth. The maps are then
// refined and cut exactly like the plugin's (depthfield.hpp).
#pragma once

#include <atomic>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace undershell {

class Jobs;

class DepthGen {
public:
  static constexpr const char* kModelUrl =
      "https://huggingface.co/onnx-community/depth-anything-v2-small/resolve/4472b7362082ad9968fee890ca0f1e5aca36b93d/onnx/"
      "model.onnx?download=true";
  static constexpr const char* kModelSha256 = "afb6a5c28f3b6bf1618c6e43f02073ef9dfdc70e937502d51603e57b0a1df10c";
  static constexpr long long kModelSize = 99'060'839;

  // `changed` runs on the main thread when a map lands, a download ends or fails
  void setJobs(Jobs* jobs, std::function<void()> changed) {
    m_jobs = jobs;
    m_changed = std::move(changed);
  }

  static std::string helperPath();  // "" when this build has no helper
  static std::string cacheDir();    // where the maps go
  static std::string ownModel();    // where undershell keeps its copy
  // a model to run: undershell's or the plugin's, "" if neither is there
  [[nodiscard]] std::string model() const;
  [[nodiscard]] bool canRun() const { return !helperPath().empty() && !model().empty(); }
  // the build can compute depth but the model still has to be fetched
  [[nodiscard]] bool needsModel() const { return !helperPath().empty() && model().empty(); }

  bool download();  // starts fetching the model (false: busy, or nothing to run it with)
  [[nodiscard]] bool downloading() const { return m_downloading; }
  [[nodiscard]] double progress() const { return m_progress.load(); }
  [[nodiscard]] const std::string& error() const { return m_error; }

  // The map of each of these wallpapers (path, sha256), computed one at a
  // time; ones already cached or that failed before are skipped.
  void want(const std::vector<std::pair<std::string, std::string>>& walls);
  [[nodiscard]] bool computing() const { return !m_running.empty(); }
  [[nodiscard]] const std::string& computingWallpaper() const { return m_running; }
  // undershell's own map for a wallpaper, or ""
  static std::string findNpy(const std::string& sha);

private:
  void next();
  Jobs* m_jobs = nullptr;
  std::function<void()> m_changed;
  bool m_downloading = false;
  std::atomic<double> m_progress{0};
  std::string m_error;
  std::vector<std::pair<std::string, std::string>> m_queue;
  std::string m_running;            // the wallpaper being computed
  std::set<std::string> m_failed;   // shas the helper could not do (not retried until restart or a new model)
};

}  // namespace undershell
