// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's wallpaper gallery (see wallgallery.hpp).
#include "wallgallery.hpp"

#include "common.hpp"
#include "offscreen.hpp"
#include "wescene.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include <algorithm>
#include <filesystem>
#include <format>

namespace undershell {

namespace fs = std::filesystem;

namespace {

bool pictureName(const fs::path& p) {
  std::string ext = p.extension().string();
  std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".bmp" || ext == ".gif" || ext == ".avif" ||
         ext == ".jxl";
}

std::string thumbDir() {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  return std::string(xdg && *xdg ? xdg : expandHome("~/.cache")) + "/undershell/thumbs";
}

// a picture, small: from memory (the loaders refuse long file names), scaled to fit kW x kH
bool makeThumb(const std::string& path, std::vector<unsigned char>& rgba, int& w, int& h) {
  const std::string data = readFile(path);
  if (data.empty() || data.size() > (96u << 20)) return false;
  GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
  const bool fed = gdk_pixbuf_loader_write(loader, reinterpret_cast<const guchar*>(data.data()), data.size(), nullptr);
  const bool closed = gdk_pixbuf_loader_close(loader, nullptr);
  GdkPixbuf* pb = fed && closed ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
  if (pb) g_object_ref(pb);
  g_object_unref(loader);
  if (!pb) return false;
  const int iw = gdk_pixbuf_get_width(pb), ih = gdk_pixbuf_get_height(pb);
  // covering the thumbnail's box, then cut to it (a wallpaper's middle)
  const double s = std::max(static_cast<double>(ThumbCache::kW) / iw, static_cast<double>(ThumbCache::kH) / ih);
  const int sw = std::max(1, static_cast<int>(iw * s)), sh = std::max(1, static_cast<int>(ih * s));
  GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pb, sw, sh, GDK_INTERP_BILINEAR);
  g_object_unref(pb);
  if (!scaled) return false;
  GdkPixbuf* alpha = gdk_pixbuf_add_alpha(scaled, FALSE, 0, 0, 0);
  g_object_unref(scaled);
  if (!alpha) return false;
  w = std::min(sw, ThumbCache::kW), h = std::min(sh, ThumbCache::kH);
  const int ox = (sw - w) / 2, oy = (sh - h) / 2, stride = gdk_pixbuf_get_rowstride(alpha);
  const guchar* px = gdk_pixbuf_get_pixels(alpha);
  rgba.resize(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) std::copy_n(px + static_cast<size_t>(y + oy) * stride + ox * 4, w * 4, rgba.data() + static_cast<size_t>(y) * w * 4);
  g_object_unref(alpha);
  return true;
}

}  // namespace

std::vector<std::string> galleryDirs(const std::vector<std::string>& asked, const std::string& current) {
  std::vector<std::string> dirs;
  std::error_code ec;
  auto add = [&](const fs::path& p) {
    if (!fs::is_directory(p, ec)) return;
    const std::string c = fs::canonical(p, ec).string();
    if (!ec && std::ranges::find(dirs, c) == dirs.end()) dirs.push_back(c);
  };
  for (const auto& a : asked) add(expandHome(a));
  if (!dirs.empty()) return dirs;
  if (!current.empty() && current.rfind("color:", 0) != 0) add(fs::path(current).parent_path());
  const char* pics = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
  const fs::path base = pics ? fs::path(pics) : fs::path(expandHome("~/Pictures"));
  for (const char* sub : {"Wallpapers", "wallpapers", "Fondos", "fondos"}) add(base / sub);
  return dirs;
}

std::vector<GalleryItem> galleryPictures(const std::vector<std::string>& dirs) {
  std::vector<std::pair<fs::file_time_type, GalleryItem>> found;
  std::error_code ec;
  for (const auto& d : dirs)
    for (const auto& e : fs::directory_iterator(d, ec)) {
      if (!e.is_regular_file(ec) || !pictureName(e.path())) continue;
      found.push_back({e.last_write_time(ec), {e.path().string(), e.path().filename().string(), "", ""}});
      if (found.size() > 4000) break;
    }
  std::ranges::sort(found, [](auto& a, auto& b) { return a.first > b.first; });
  std::vector<GalleryItem> out;
  for (auto& [t, it] : found) out.push_back(std::move(it));
  return out;
}

std::vector<GalleryItem> galleryWorkshop() {
  std::vector<std::pair<fs::file_time_type, GalleryItem>> found;
  std::error_code ec;
  for (const auto& d : weWorkshopDirs())
    for (const auto& e : fs::directory_iterator(d, ec)) {
      if (!e.is_directory(ec)) continue;
      WeProject p;
      std::string why;
      if (!readWeProject(e.path().string(), p, why) || (p.type != "scene" && p.type != "video") || p.preview.empty()) continue;
      found.push_back({e.last_write_time(ec), {p.preview, p.title.empty() ? e.path().filename().string() : p.title, e.path().filename().string(), p.type}});
    }
  std::ranges::sort(found, [](auto& a, auto& b) { return a.first > b.first; });
  std::vector<GalleryItem> out;
  for (auto& [t, it] : found) out.push_back(std::move(it));
  return out;
}

ThumbCache::ThumbCache() : m_worker([this] { run(); }) {}

ThumbCache::~ThumbCache() {
  {
    std::lock_guard g(m_m);
    m_stop = true;
  }
  m_cv.notify_all();
  if (m_worker.joinable()) m_worker.join();
  for (auto& [p, t] : m_thumbs)
    if (t.tex) glDeleteTextures(1, &t.tex);
}

void ThumbCache::run() {
  for (;;) {
    std::string path;
    {
      std::unique_lock g(m_m);
      m_cv.wait(g, [&] { return m_stop || !m_queue.empty(); });
      if (m_stop) return;
      path = m_queue.back();  // the latest asked for first: what is on screen now
      m_queue.pop_back();
    }
    // from the disk cache, else made and kept there
    std::error_code ec;
    const auto t = fs::last_write_time(path, ec).time_since_epoch().count();
    const std::string key = std::format("{:016x}", std::hash<std::string>{}(std::format("{}|{}|{}", path, t, fs::file_size(path, ec))));
    const std::string cached = thumbDir() + "/" + key + ".png";
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    bool ok = false;
    Image img;
    if (fs::exists(cached, ec) && readPng(cached, img) && img.w > 0) {
      rgba = std::move(img.rgba), w = img.w, h = img.h;
      ok = true;
    } else if (makeThumb(path, rgba, w, h)) {
      ok = true;
      Image out;
      out.w = w, out.h = h, out.rgba = rgba;
      fs::create_directories(thumbDir(), ec);
      writePng(out, cached, false);
    }
    std::lock_guard g(m_m);
    Thumb& th = m_thumbs[path];
    if (ok) th.rgba = std::move(rgba), th.w = w, th.h = h;
    else th.failed = true;
  }
}

GLuint ThumbCache::get(const std::string& path, int size[2]) {
  std::lock_guard g(m_m);
  Thumb& t = m_thumbs[path];
  if (t.tex) {
    size[0] = t.w, size[1] = t.h;
    return t.tex;
  }
  if (!t.asked && !t.failed) {
    t.asked = true;
    m_queue.push_back(path);
    m_cv.notify_one();
  }
  return 0;
}

bool ThumbCache::upload(int n) {
  std::lock_guard g(m_m);
  bool more = false;
  for (auto& [p, t] : m_thumbs) {
    if (t.rgba.empty()) {
      more |= t.asked && !t.tex && !t.failed;
      continue;
    }
    if (n-- <= 0) {
      more = true;
      continue;
    }
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    t.rgba.clear();
    t.rgba.shrink_to_fit();
  }
  return more;
}

void ThumbCache::releaseGl() {
  std::lock_guard g(m_m);
  for (auto& [p, t] : m_thumbs)
    if (t.tex) glDeleteTextures(1, &t.tex), t.tex = 0, t.asked = false;
}

}  // namespace undershell
