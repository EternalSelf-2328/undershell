// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's wallpaper gallery (phase 5 of the plan): pictures from the
// person's wallpaper folders, set through whatever program draws the
// wallpaper (wallset.hpp), and Wallpaper Engine wallpapers from their
// Workshop folder, played by a scene widget. This part lists them and makes
// their thumbnails: decoded on a thread of its own, small, kept on disk in
// ~/.cache/undershell/thumbs so the next time is instant, and uploaded a few
// per frame (needs GL).
#pragma once

#include <GLES3/gl3.h>

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace undershell {

struct GalleryItem {
  std::string path;     // a picture; for the Workshop, its preview
  std::string label;    // the file's name, or the wallpaper's title
  std::string workshop; // a Workshop id ("" for a picture)
  std::string kind;     // the Workshop item's type: scene, video, web…
};

// Pictures in these folders (not below them), newest first.
[[nodiscard]] std::vector<GalleryItem> galleryPictures(const std::vector<std::string>& dirs);
// Wallpaper Engine's Workshop wallpapers that undershell can play (scenes and videos).
[[nodiscard]] std::vector<GalleryItem> galleryWorkshop();
// Where to look for pictures: the folders asked for, else the current
// wallpaper's folder and the usual ones under the pictures folder.
[[nodiscard]] std::vector<std::string> galleryDirs(const std::vector<std::string>& asked, const std::string& current);

class ThumbCache {
public:
  ThumbCache();
  ~ThumbCache();
  ThumbCache(const ThumbCache&) = delete;
  ThumbCache& operator=(const ThumbCache&) = delete;
  // The thumbnail of `path` (at most kW x kH), or 0 while it is made; asks
  // for it the first time. `size` gets its own width and height.
  GLuint get(const std::string& path, int size[2]);
  // uploads up to `n` finished thumbnails (needs GL); true when more wait
  bool upload(int n);
  // forgets the GL textures (the context goes away)
  void releaseGl();
  static constexpr int kW = 240, kH = 136;

private:
  struct Thumb {
    GLuint tex = 0;
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;  // made, waiting for upload
    bool asked = false, failed = false;
  };
  void run();
  std::mutex m_m;
  std::condition_variable m_cv;
  std::deque<std::string> m_queue;
  std::map<std::string, Thumb> m_thumbs;
  bool m_stop = false;
  std::thread m_worker;
};

}  // namespace undershell
