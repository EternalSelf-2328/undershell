// SPDX-License-Identifier: GPL-3.0-or-later
#include "depth.hpp"

#include "depthfield.hpp"
#include "jobs.hpp"

#include <cairo.h>
#include <filesystem>
#include <fstream>
#include <glib.h>
#include <sys/stat.h>

namespace undershell {

namespace fs = std::filesystem;

std::string DepthMasks::maskDir() {
  const char* xdg = std::getenv("XDG_STATE_HOME");
  std::string base = xdg && *xdg ? xdg : expandHome("~/.local/state");
  return base + "/noctalia/plugins/data/noctalia/wallpaper_depth/cache/masks";
}

std::string DepthMasks::sha256Of(const std::string& path) {
  struct stat sb{};
  if (stat(path.c_str(), &sb) != 0) return {};
  auto& e = m_hashes[path];
  if (!e.sha.empty() && e.mtime == sb.st_mtime && e.size == sb.st_size) return e.sha;
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  GChecksum* ck = g_checksum_new(G_CHECKSUM_SHA256);
  std::vector<char> buf(1 << 16);
  while (f) {
    f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    if (f.gcount() > 0) g_checksum_update(ck, reinterpret_cast<const guchar*>(buf.data()), f.gcount());
  }
  e.sha = g_checksum_get_string(ck);
  g_checksum_free(ck);
  e.mtime = sb.st_mtime;
  e.size = sb.st_size;
  return e.sha;
}

// depth_helper.py: "{sha256}-{model}-d{v}-i{size}-v{maskv}-t{thr:.4f}-f{feather:.4f}.png"
std::string DepthMasks::findMask(const std::string& sha, double threshold, double feather) {
  std::error_code ec;
  const std::string suffix = std::format("-t{:.4f}-f{:.4f}.png", threshold, feather);
  std::string best;
  fs::file_time_type bestTime{};
  for (auto& ent : fs::directory_iterator(maskDir(), ec)) {
    const std::string name = ent.path().filename().string();
    if (name.size() <= sha.size() + suffix.size() || !name.starts_with(sha + "-") || !name.ends_with(suffix)) continue;
    auto t = ent.last_write_time(ec);
    if (best.empty() || t > bestTime) {
      best = ent.path().string();
      bestTime = t;
    }
  }
  return best;
}

bool DepthMasks::loadPng(const std::string& path, DepthMask& out) {
  cairo_surface_t* img = cairo_image_surface_create_from_png(path.c_str());
  if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(img);
    return false;
  }
  cairo_surface_flush(img);
  const int w = cairo_image_surface_get_width(img);
  const int h = cairo_image_surface_get_height(img);
  const int stride = cairo_image_surface_get_stride(img);
  const auto fmt = cairo_image_surface_get_format(img);
  const unsigned char* data = cairo_image_surface_get_data(img);
  out.pixels.resize(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y) {
    const unsigned char* row = data + static_cast<size_t>(y) * stride;
    for (int x = 0; x < w; ++x) {
      std::uint8_t v;
      if (fmt == CAIRO_FORMAT_A8) {
        v = row[x];
      } else {
        // ARGB32 / RGB24, native endian: grayscale luminance lives in each channel
        const std::uint32_t px = reinterpret_cast<const std::uint32_t*>(row)[x];
        v = static_cast<std::uint8_t>((px >> 8) & 0xff);
      }
      out.pixels[static_cast<size_t>(y) * w + x] = v;
    }
  }
  out.width = w;
  out.height = h;
  cairo_surface_destroy(img);
  return true;
}

bool DepthMasks::update(const NoctaliaState& st, const std::vector<std::string>& outputs) {
  bool changed = false;
  m_missing = false;
  for (const auto& out : outputs) {
    std::string wall = st.depthPluginEnabled ? st.wallpaperFor(out) : std::string{};
    std::string mask, npy;
    if (!wall.empty()) {
      std::string sha = sha256Of(wall);
      if (!sha.empty()) {
        mask = findMask(sha, st.depthThreshold, st.depthFeather);
        npy = findDepthNpy(sha);
      }
    }
    if (st.depthPluginEnabled && !wall.empty() && mask.empty()) m_missing = true;
    auto& m = m_masks[out];
    if (m.fieldNpy != npy) {
      // a new depth map: refine it against the image off the main thread
      m.fieldNpy = npy;
      m.fieldPixels.clear();
      m.fieldStale = m.field != 0;
      changed = true;
      if (!npy.empty() && m_jobs) {
        const std::string sha = sha256Of(wall);
        const std::string editsPath = m_editsDir.empty() ? std::string() : m_editsDir + "/" + sha + ".usde";
        m_jobs->run([this, out, wall, npy, sha, editsPath]() -> Jobs::Done {
          DepthField raw, refined;
          std::vector<float> guide;
          int iw = 0, ih = 0;
          const bool ok = loadNpyF32(npy, raw) && refineDepth(wall, raw, refined, &iw, &ih, &guide);
          DepthEdits edits;
          bool hasEdits = false;
          std::vector<std::uint16_t> half;
          if (ok) {
            guide = boxMean(guide, refined.w, refined.h, 6);  // hatching, ink lines and grain average out
            if (!editsPath.empty() && loadDepthEdits(editsPath, edits) && edits.w == refined.w && edits.h == refined.h)
              hasEdits = !edits.empty();
            else
              edits.reset(refined.w, refined.h);
            std::vector<float> applied = refined.v;
            if (hasEdits)
              for (size_t i = 0; i < applied.size(); ++i) applied[i] = editedDepth(applied[i], edits.target[i], edits.cover[i]);
            half = toHalf(applied);
          }
          return [this, out, npy, ok, iw, ih, sha, hasEdits, refined = std::move(refined), guide = std::move(guide),
                  edits = std::move(edits), half = std::move(half)]() mutable {
            auto it = m_masks.find(out);
            if (it == m_masks.end() || it->second.fieldNpy != npy) return;  // superseded
            if (!ok) {
              US_WARN("could not refine the depth map {}", fs::path(npy).filename().string());
              return;
            }
            const int fw = refined.w, fh = refined.h;
            it->second.fieldPixels = std::move(half);
            it->second.fieldW = fw;
            it->second.fieldH = fh;
            it->second.imageW = iw;
            it->second.imageH = ih;
            it->second.sha = sha;
            it->second.base = std::move(refined);
            it->second.guide = std::move(guide);
            it->second.edits = std::move(edits);
            it->second.hasEdits = hasEdits;
            US_INFO("depth field for {}: {}x{}", out, fw, fh);
            if (m_fieldReady) m_fieldReady();
          };
        });
      }
    }
    if (m.maskPath == mask && m.wallpaper == wall) continue;
    changed = true;
    m.wallpaper = wall;
    m.maskPath = mask;
    m.pixels.clear();
    m.width = m.height = 0;
    if (!mask.empty()) {
      if (loadPng(mask, m)) {
        US_INFO("depth mask for {}: {} ({}x{})", out, fs::path(mask).filename().string().substr(0, 16), m.width, m.height);
      } else {
        US_WARN("could not read depth mask {}", mask);
        m.maskPath.clear();
      }
    } else if (!st.depthPluginEnabled) {
      US_INFO("no depth mask for {} (wallpaper_depth disabled)", out);
    } else {
      US_INFO("no depth mask yet for {} (wallpaper {}, sha {}, t{:.4f} f{:.4f})", out, fs::path(wall).filename().string(),
              sha256Of(wall).substr(0, 12), st.depthThreshold, st.depthFeather);
    }
  }
  return changed;
}

const DepthMask* DepthMasks::get(const std::string& output) {
  auto it = m_masks.find(output);
  if (it == m_masks.end()) return nullptr;
  auto& m = it->second;
  if (m.fieldStale) {
    if (m.field) glDeleteTextures(1, &m.field);
    m.field = 0;
    m.fieldStale = false;
  }
  if (!m.fieldPixels.empty()) {
    if (!m.field) glGenTextures(1, &m.field);
    glBindTexture(GL_TEXTURE_2D, m.field);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, m.fieldW, m.fieldH, 0, GL_RED, GL_HALF_FLOAT, m.fieldPixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    m.fieldPixels.clear();
    m.fieldPixels.shrink_to_fit();
  }
  if (m.maskPath.empty() && !m.field) return nullptr;
  if (!m.pixels.empty()) {
    if (!m.texture) glGenTextures(1, &m.texture);
    glBindTexture(GL_TEXTURE_2D, m.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m.width, m.height, 0, GL_RED, GL_UNSIGNED_BYTE, m.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    m.pixels.clear();
    m.pixels.shrink_to_fit();
  }
  return (m.texture || m.field) ? &m : nullptr;
}

DepthMask* DepthMasks::paintable(const std::string& output) {
  auto it = m_masks.find(output);
  if (it == m_masks.end() || !it->second.field || it->second.base.v.empty()) return nullptr;
  return &it->second;
}

void DepthMasks::uploadRect(DepthMask& m, PixelBox box, const std::vector<float>* stroke, DepthTool tool, float value) {
  box.clip(m.fieldW, m.fieldH);
  if (box.empty() || !m.field) return;
  const int bw = box.x1 - box.x0, bh = box.y1 - box.y0;
  std::vector<float> vals(static_cast<size_t>(bw) * bh);
  DepthEdits preview;  // the stroke merged over a copy of just this box
  if (stroke) {
    preview.w = bw;
    preview.h = bh;
    preview.target.resize(vals.size());
    preview.cover.resize(vals.size());
  }
  std::vector<float> sub(stroke ? vals.size() : 0);
  for (int y = 0; y < bh; ++y)
    for (int x = 0; x < bw; ++x) {
      const size_t src = static_cast<size_t>(y + box.y0) * m.fieldW + (x + box.x0), dst = static_cast<size_t>(y) * bw + x;
      if (stroke) {
        preview.target[dst] = m.edits.target[src];
        preview.cover[dst] = m.edits.cover[src];
        sub[dst] = (*stroke)[src];
      }
    }
  if (stroke) mergeStroke(preview, sub, tool, value, {0, 0, bw, bh});
  for (int y = 0; y < bh; ++y)
    for (int x = 0; x < bw; ++x) {
      const size_t src = static_cast<size_t>(y + box.y0) * m.fieldW + (x + box.x0), dst = static_cast<size_t>(y) * bw + x;
      const std::uint8_t t = stroke ? preview.target[dst] : m.edits.target[src];
      const std::uint8_t c = stroke ? preview.cover[dst] : m.edits.cover[src];
      vals[dst] = editedDepth(m.base.v[src], t, c);
    }
  const auto half = toHalf(vals);
  glBindTexture(GL_TEXTURE_2D, m.field);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
  glTexSubImage2D(GL_TEXTURE_2D, 0, box.x0, box.y0, bw, bh, GL_RED, GL_HALF_FLOAT, half.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

bool DepthMasks::saveEdits(DepthMask& m) {
  m.hasEdits = !m.edits.empty();
  if (m_editsDir.empty() || m.sha.empty()) return false;
  const std::string path = m_editsDir + "/" + m.sha + ".usde";
  if (!m.hasEdits) {
    std::error_code ec;
    fs::remove(path, ec);
    return true;
  }
  return saveDepthEdits(path, m.edits);
}

void DepthMasks::releaseGl() {
  for (auto& [k, m] : m_masks) {
    if (m.texture) glDeleteTextures(1, &m.texture);
    if (m.field) glDeleteTextures(1, &m.field);
    m.texture = m.field = 0;
  }
}

}  // namespace undershell
