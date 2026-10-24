// SPDX-License-Identifier: GPL-3.0-or-later
#include "wallstore.hpp"

#include <EGL/egl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

namespace undershell {

namespace {
constexpr double kKeepUnused = 30;  // seconds a picture nobody draws stays decoded
}

struct WallpaperStore::Decode {
  std::mutex lock;
  std::atomic<bool> done{false};
  std::vector<uint8_t> rgba;
  int w = 0, h = 0, imgW = 0, imgH = 0;
};

WallpaperStore& wallpaperStore() {
  static WallpaperStore store;
  return store;
}

WallpaperStore::~WallpaperStore() {
  if (eglGetCurrentContext() == EGL_NO_CONTEXT) return;  // the context went first: the driver frees them
  for (auto& [k, e] : m_pics)
    if (e.pic.tex) glDeleteTextures(1, &e.pic.tex);
}

bool WallpaperStore::loading() const {
  return std::ranges::any_of(m_pics, [](const auto& kv) { return kv.second.decode != nullptr; });
}

void WallpaperStore::trim(double before) {
  for (auto it = m_pics.begin(); it != m_pics.end();) {
    if (it->second.usedAt < before) {
      if (it->second.pic.tex) glDeleteTextures(1, &it->second.pic.tex);
      it = m_pics.erase(it);
    } else {
      ++it;
    }
  }
}

const WallPic* WallpaperStore::get(const std::string& path, int maxSide, double now) {
  if (path.empty()) return nullptr;
  // another GL context (a headless render): its textures are not these
  if (void* c = eglGetCurrentContext(); c != m_context) {
    m_pics.clear();
    m_context = c;
  }
  trim(now - kKeepUnused);
  const int side = std::max(256, (maxSide + 255) / 256 * 256);
  const std::string key = std::format("{}|{}", side, path);
  auto [it, fresh] = m_pics.try_emplace(key);
  Entry& e = it->second;
  e.usedAt = now;
  if (fresh) {
    // decoded on its own thread, no larger than the output needs
    auto d = std::make_shared<Decode>();
    e.decode = d;
    std::thread([d, path, side] {
      int iw = 0, ih = 0;
      if (gdk_pixbuf_get_file_info(path.c_str(), &iw, &ih) && iw > 0 && ih > 0 && iw <= 32768 && ih <= 32768) {
        GError* err = nullptr;
        GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), std::min(iw, side), std::min(ih, side), TRUE, &err);
        if (err) g_error_free(err);
        if (pb) {
          GdkPixbuf* rgba = gdk_pixbuf_add_alpha(pb, FALSE, 0, 0, 0);
          std::lock_guard g(d->lock);
          d->w = gdk_pixbuf_get_width(rgba);
          d->h = gdk_pixbuf_get_height(rgba);
          d->imgW = iw;
          d->imgH = ih;
          const int stride = gdk_pixbuf_get_rowstride(rgba);
          const guchar* src = gdk_pixbuf_read_pixels(rgba);
          d->rgba.resize(static_cast<size_t>(d->w) * d->h * 4);
          for (int y = 0; y < d->h; ++y)
            std::copy_n(src + static_cast<size_t>(y) * stride, d->w * 4, d->rgba.data() + static_cast<size_t>(y) * d->w * 4);
          g_object_unref(rgba);
          g_object_unref(pb);
        }
      }
      d->done = true;
    }).detach();
    return nullptr;
  }
  if (e.decode) {
    if (!e.decode->done) return nullptr;
    std::lock_guard g(e.decode->lock);
    if (e.decode->rgba.empty()) {
      e.pic.failed = true;
    } else {
      glGenTextures(1, &e.pic.tex);
      glBindTexture(GL_TEXTURE_2D, e.pic.tex);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, e.decode->w, e.decode->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, e.decode->rgba.data());
      glGenerateMipmap(GL_TEXTURE_2D);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
      e.pic.imgW = e.decode->imgW;
      e.pic.imgH = e.decode->imgH;
      e.pic.texW = e.decode->w;
      e.pic.texH = e.decode->h;
    }
    e.decode.reset();
  }
  return &e.pic;
}

bool WallpaperFade::update(const std::string& path, int maxSide, double now) {
  WallpaperStore& store = wallpaperStore();
  m_wantPath = path;
  m_maxSide = maxSide;
  // the new picture takes over once it is decoded; until then the old one stays
  if (!path.empty() && path != m_curPath) {
    if (const WallPic* want = store.get(path, maxSide, now)) {
      m_prevPath = (m_cur && !m_cur->failed) ? m_curPath : std::string();
      m_curPath = path;
      m_fadeAt = now;
      m_mix = (m_prevPath.empty() || want->failed) ? 1.0F : 0.0F;
    }
  }
  m_cur = m_curPath.empty() ? nullptr : store.get(m_curPath, maxSide, now);
  m_prev = nullptr;
  if (m_mix < 1) {
    m_mix = static_cast<float>(std::clamp((now - m_fadeAt) / kFade, 0.0, 1.0));
    // smooth start and end
    m_mix = m_mix * m_mix * (3 - 2 * m_mix);
    if (m_mix < 1) m_prev = store.get(m_prevPath, maxSide, now);
    if (m_prev && (m_prev->failed || !m_prev->tex)) m_prev = nullptr;
  }
  return m_cur && m_cur->tex && !m_cur->failed;
}

}  // namespace undershell
