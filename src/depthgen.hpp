// SPDX-License-Identifier: GPL-3.0-or-later
// undershell's depth: the model (Depth Anything V2 Small, fetched only when
// asked, checked by size and sha256; a copy Noctalia's wallpaper_depth plugin
// already downloaded is used when it is there) and the helper that runs it
// (undershell-depth, a process of its own) to write the depth map (.npy) of
// each wallpaper into ~/.cache/undershell/depth. The maps are then refined and
// cut at each widget's plane (depthfield.hpp). Wallpapers with a layout of
// their own get their map ahead of time, in the background, and keep it.
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
  // time; ones already cached or that failed before are skipped. Wallpapers
  // on screen go first; `background` ones (with a layout of their own, shown
  // some other time) after them, with the helper at the lowest priority.
  void want(const std::vector<std::pair<std::string, std::string>>& walls, bool background = false);
  [[nodiscard]] bool computing() const { return !m_running.empty(); }
  [[nodiscard]] const std::string& computingWallpaper() const { return m_running; }
  [[nodiscard]] size_t queued() const { return m_queue.size(); }
  // undershell's map for a wallpaper, or ""
  static std::string findNpy(const std::string& sha);
  // maps whose sha is in `keep` (wallpapers with a layout) stay; of the rest,
  // the newest `others`
  static void prune(const std::set<std::string>& keep, size_t others = 16);
  // what prune() keeps: the shas of the wallpapers with a layout
  void setKeep(std::set<std::string> keep) { m_keep = std::move(keep); }

private:
  void next();
  Jobs* m_jobs = nullptr;
  std::function<void()> m_changed;
  bool m_downloading = false;
  std::atomic<double> m_progress{0};
  std::string m_error;
  struct Want {
    std::string wall, sha;
    bool background = false;
  };
  std::vector<Want> m_queue;
  std::string m_running;            // the wallpaper being computed
  std::set<std::string> m_keep;
  std::set<std::string> m_failed;   // shas the helper could not do (not retried until restart or a new model)
};

}  // namespace undershell
