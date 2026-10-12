// SPDX-License-Identifier: GPL-3.0-or-later
#include "depthgen.hpp"

#include "common.hpp"
#include "jobs.hpp"

#include <curl/curl.h>
#include <glib.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>

#ifndef US_LIBEXEC_DIR
#define US_LIBEXEC_DIR "/usr/libexec"
#endif

namespace undershell {

namespace fs = std::filesystem;

namespace {

bool executable(const std::string& p) { return !p.empty() && access(p.c_str(), X_OK) == 0 && fs::is_regular_file(p); }

std::string pluginModel() {
  const char* xdg = std::getenv("XDG_STATE_HOME");
  return std::string(xdg && *xdg ? xdg : expandHome("~/.local/state")) +
         "/noctalia/plugins/data/noctalia/wallpaper_depth/models/depth-anything-v2-small/model.onnx";
}

bool rightSize(const std::string& p) {
  std::error_code ec;
  return fs::file_size(p, ec) == static_cast<std::uintmax_t>(DepthGen::kModelSize) && !ec;
}

std::string sha256File(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  GChecksum* ck = g_checksum_new(G_CHECKSUM_SHA256);
  std::vector<char> buf(1 << 16);
  while (f) {
    f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    if (f.gcount() > 0) g_checksum_update(ck, reinterpret_cast<const guchar*>(buf.data()), f.gcount());
  }
  std::string out = g_checksum_get_string(ck);
  g_checksum_free(ck);
  return out;
}

int onProgress(void* ud, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
  const double t = total > 0 ? static_cast<double>(total) : static_cast<double>(DepthGen::kModelSize);
  static_cast<std::atomic<double>*>(ud)->store(std::clamp(static_cast<double>(now) / t, 0.0, 1.0));
  return 0;
}

}  // namespace

void DepthGen::prune(const std::set<std::string>& keep, size_t others) {
  std::vector<std::pair<fs::file_time_type, fs::path>> maps;
  std::error_code ec;
  for (auto& e : fs::directory_iterator(cacheDir(), ec)) {
    if (e.path().extension() != ".npy") continue;
    const std::string name = e.path().filename().string();
    if (keep.contains(name.substr(0, name.find('-')))) continue;
    maps.emplace_back(e.last_write_time(ec), e.path());
  }
  std::sort(maps.begin(), maps.end(), [](auto& a, auto& b) { return a.first > b.first; });
  for (size_t i = others; i < maps.size(); ++i) fs::remove(maps[i].second, ec);
}

std::string DepthGen::helperPath() {
  std::string found;
  if (const char* env = std::getenv("UNDERSHELL_DEPTH_HELPER"); env && *env) {
    if (executable(env)) found = env;
  } else {
    // beside the binary (the build tree), else where it is installed
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    const std::string installed = std::string(US_LIBEXEC_DIR) + "/undershell-depth";
    if (!ec && executable((self.parent_path() / "undershell-depth").string())) found = (self.parent_path() / "undershell-depth").string();
    else if (executable(installed)) found = installed;
  }
  // it has to start, too (ONNX Runtime may have been uninstalled since): asked once per path
  static std::string checked, runs;
  if (found != checked) {
    checked = found;
    gint status = 0;
    const gchar* argv[] = {found.c_str(), "--check", nullptr};
    const bool ok = !found.empty() && g_spawn_sync(nullptr, const_cast<gchar**>(argv), nullptr, G_SPAWN_STDOUT_TO_DEV_NULL,
                                                   nullptr, nullptr, nullptr, nullptr, &status, nullptr) &&
                    g_spawn_check_wait_status(status, nullptr);
    if (!found.empty() && !ok) US_WARN("depth: {} does not start (is ONNX Runtime installed?)", found);
    runs = ok ? found : std::string();
  }
  return runs;
}

std::string DepthGen::cacheDir() {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  return std::string(xdg && *xdg ? xdg : expandHome("~/.cache")) + "/undershell/depth";
}

std::string DepthGen::ownModel() {
  const char* xdg = std::getenv("XDG_DATA_HOME");
  return std::string(xdg && *xdg ? xdg : expandHome("~/.local/share")) + "/undershell/models/depth-anything-v2-small.onnx";
}

std::string DepthGen::model() const {
  for (const std::string& p : {ownModel(), pluginModel()})
    if (rightSize(p)) return p;
  return {};
}

std::string DepthGen::findNpy(const std::string& sha) {
  if (sha.empty()) return {};
  std::error_code ec;
  std::string best;
  fs::file_time_type bestTime{};
  for (auto& e : fs::directory_iterator(cacheDir(), ec)) {
    const std::string name = e.path().filename().string();
    if (!name.starts_with(sha + "-") || !name.ends_with(".npy")) continue;
    const auto t = e.last_write_time(ec);
    if (best.empty() || t > bestTime) {
      best = e.path().string();
      bestTime = t;
    }
  }
  return best;
}

bool DepthGen::download() {
  if (m_downloading || !m_jobs || helperPath().empty()) return false;
  m_downloading = true;
  m_progress = 0;
  m_error.clear();
  US_INFO("depth model: downloading {}", kModelUrl);
  m_jobs->run([this]() -> Jobs::Done {
    const std::string dest = ownModel(), part = dest + ".part";
    std::error_code ec;
    fs::create_directories(fs::path(dest).parent_path(), ec);
    std::string error;
    FILE* f = std::fopen(part.c_str(), "wb");
    CURL* c = f ? curl_easy_init() : nullptr;
    if (!c) {
      error = f ? "curl" : "cannot write " + part;
    } else {
      curl_easy_setopt(c, CURLOPT_URL, kModelUrl);
      curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
      curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
      curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
      curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
      curl_easy_setopt(c, CURLOPT_XFERINFODATA, &m_progress);
      curl_easy_setopt(c, CURLOPT_USERAGENT, "undershell (https://github.com/EternalSelf-2328/undershell)");
      curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
      curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);  // stalled for a minute: give up
      curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
      const CURLcode r = curl_easy_perform(c);
      if (r != CURLE_OK) error = curl_easy_strerror(r);
      curl_easy_cleanup(c);
    }
    if (f) std::fclose(f);
    if (error.empty() && !rightSize(part)) error = "unexpected size";
    if (error.empty() && sha256File(part) != kModelSha256) error = "checksum mismatch";
    if (error.empty()) fs::rename(part, dest, ec);
    if (error.empty() && ec) error = ec.message();
    if (!error.empty()) fs::remove(part, ec);
    return [this, error]() {
      m_downloading = false;
      m_error = error;
      if (error.empty()) {
        US_INFO("depth model: ready ({})", ownModel());
        m_failed.clear();
      } else {
        US_WARN("depth model: download failed: {}", error);
      }
      if (m_changed) m_changed();
    };
  });
  return true;
}

void DepthGen::want(const std::vector<std::pair<std::string, std::string>>& walls, bool background) {
  for (const auto& [wall, sha] : walls) {
    if (wall.empty() || sha.empty() || m_failed.contains(sha) || wall == m_running || !findNpy(sha).empty()) continue;
    auto it = std::ranges::find_if(m_queue, [&](auto& q) { return q.sha == sha; });
    if (it != m_queue.end()) {
      if (background || !it->background) continue;
      m_queue.erase(it);  // waiting in the background, and now on screen: to the front
    }
    if (background) {
      m_queue.push_back({wall, sha, true});
    } else {
      // after the other on-screen ones, before the background
      auto at = std::ranges::find_if(m_queue, [](auto& q) { return q.background; });
      m_queue.insert(at, {wall, sha, false});
    }
  }
  next();
}

void DepthGen::next() {
  if (!m_running.empty() || m_queue.empty() || !m_jobs || !canRun()) return;
  const Want job = m_queue.front();
  const std::string wall = job.wall, sha = job.sha;
  const bool background = job.background;
  m_queue.erase(m_queue.begin());
  if (!findNpy(sha).empty()) {
    next();
    return;
  }
  m_running = wall;
  const std::string helper = helperPath(), mdl = model();
  // the plugin's naming: <sha256>-<model sha, 16>-d<pipeline>-i<input>.npy
  const std::string out = std::format("{}/{}-{}-d2-i518.npy", cacheDir(), sha, std::string(kModelSha256).substr(0, 16));
  US_INFO("depth: computing the map of {}{}", fs::path(wall).filename().string(),
          background ? std::format(" (ahead of time, {} more)", m_queue.size()) : std::string());
  m_jobs->run([this, helper, mdl, wall, sha, out, background, keep = m_keep]() -> Jobs::Done {
    std::error_code ec;
    fs::create_directories(cacheDir(), ec);
    const gchar* argv[] = {helper.c_str(), mdl.c_str(), wall.c_str(), out.c_str(), background ? "--background" : nullptr, nullptr};
    gchar *sout = nullptr, *serr = nullptr;
    gint status = 0;
    GError* gerr = nullptr;
    const bool spawned = g_spawn_sync(nullptr, const_cast<gchar**>(argv), nullptr, static_cast<GSpawnFlags>(0), nullptr, nullptr, &sout,
                                      &serr, &status, &gerr);
    const bool ok = spawned && g_spawn_check_wait_status(status, nullptr) && fs::exists(out);
    std::string said = ok ? (sout ? sout : "") : (gerr ? gerr->message : (serr ? serr : ""));
    while (!said.empty() && (said.back() == '\n' || said.back() == ' ')) said.pop_back();
    g_free(sout);
    g_free(serr);
    if (gerr) g_error_free(gerr);
    if (ok) prune(keep);
    return [this, ok, wall, sha, said, background]() {
      m_running.clear();
      if (ok) {
        if (background) US_DEBUG("depth: map of {} ready ({})", fs::path(wall).filename().string(), said);
        else US_INFO("depth: map of {} ready ({})", fs::path(wall).filename().string(), said);
      } else {
        m_failed.insert(sha);
        US_WARN("depth: no map for {}: {}", fs::path(wall).filename().string(), said.empty() ? "the helper failed" : said);
      }
      if (m_changed) m_changed();
      next();
    };
  });
}

}  // namespace undershell
