// SPDX-License-Identifier: GPL-3.0-or-later
// The wallpaper as a GL texture, for every widget that draws it again on top
// of the wallpaper program (the filter now; effects and particles over the
// wallpaper next). Whatever program draws the wallpaper underneath --
// Noctalia, any other shell, awww, swaybg… -- undershell only needs its path.
//
// One decode per picture and size, on its own thread (no frame waits); the
// texture is made on the render thread and shared. Pictures nobody asked for
// in a while are let go. A WallpaperFade keeps the old picture until the new
// one is in and then crossfades, so a wallpaper change is never a jump.
#pragma once

#include <GLES3/gl3.h>

#include <map>
#include <memory>
#include <string>

namespace undershell {

struct WallPic {
  GLuint tex = 0;
  int imgW = 0, imgH = 0;  // the picture's own size (its aspect decides the fit)
  int texW = 0, texH = 0;  // the texture's (no larger than the output needs)
  bool failed = false;     // could not be read: draw nothing
};

class WallpaperStore {
public:
  ~WallpaperStore();
  // the picture at `path` decoded to at most `maxSide` px (rounded up to a
  // step of 256 so outputs of similar size share it), or nullptr while it
  // decodes; the first ask starts the decode
  const WallPic* get(const std::string& path, int maxSide, double now);
  [[nodiscard]] bool loading() const;
  // textures not asked for since `before` are deleted (needs the GL context)
  void trim(double before);
  [[nodiscard]] size_t size() const { return m_pics.size(); }

private:
  struct Decode;
  struct Entry {
    WallPic pic;
    std::shared_ptr<Decode> decode;
    double usedAt = 0;
  };
  std::map<std::string, Entry> m_pics;  // key: size step + '|' + path
  void* m_context = nullptr;            // the GL context the textures belong to
};

// The one store of the process (one GL context).
WallpaperStore& wallpaperStore();

// The picture a widget shows and the one it is fading from.
class WallpaperFade {
public:
  static constexpr double kFade = 0.45;  // seconds
  // call every draw; returns false while there is nothing to show yet
  bool update(const std::string& path, int maxSide, double now);
  [[nodiscard]] const WallPic* current() const { return m_cur; }
  [[nodiscard]] const WallPic* previous() const { return m_mix < 1 ? m_prev : nullptr; }
  [[nodiscard]] float mix() const { return m_mix; }  // 0 = all previous, 1 = all current
  [[nodiscard]] bool animating() const { return m_mix < 1 && m_prev; }
  [[nodiscard]] bool waiting() const { return !m_wantPath.empty() && m_wantPath != m_curPath; }

private:
  std::string m_curPath, m_prevPath, m_wantPath;
  const WallPic* m_cur = nullptr;
  const WallPic* m_prev = nullptr;
  int m_maxSide = 0;
  double m_fadeAt = 0;
  float m_mix = 1;
};

}  // namespace undershell
