// SPDX-License-Identifier: GPL-3.0-or-later
#include "depth.hpp"

#include "depthfield.hpp"
#include "jobs.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <glib.h>
#include <sys/stat.h>

namespace undershell {

namespace fs = std::filesystem;

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

bool DepthMasks::update(const NoctaliaState& st, const std::vector<std::string>& outputs) {
  bool changed = false;
  for (const auto& out : outputs) {
    const std::string wall = m_enabled ? st.wallpaperFor(out) : std::string{};
    const std::string npy = wall.empty() ? std::string() : findDepthNpy(sha256Of(wall));
    // a map rewritten in place (a new model) is a new map: key it by time and
    // size too, so the finished file is read again
    std::string key;
    if (!npy.empty()) {
      std::error_code ec;
      const auto t = fs::last_write_time(npy, ec).time_since_epoch().count();
      key = std::format("{}@{}:{}", npy, t, fs::file_size(npy, ec));
    }
    auto& slot = m_masks[out];
    const auto same = [&](const DepthMask& d) { return d.wallpaper == wall && d.fieldKey == key; };
    if (!slot || !same(*slot)) {
      // another output with this very depth (the same wallpaper on both
      // monitors): share it rather than decode and refine it twice
      std::shared_ptr<DepthMask> twin;
      if (!wall.empty())
        for (auto& [o, d] : m_masks)
          if (o != out && d && same(*d)) twin = d;
      if (twin) {
        if (slot && slot.use_count() == 1) m_retired.push_back(slot);
        slot = twin;
        changed = true;
        continue;
      }
      // its own from here (it was shared, the other output keeps that one);
      // one of its own is updated in place below
      if (!slot || slot.use_count() > 1) slot = std::make_shared<DepthMask>();
    }
    auto& m = *slot;
    if (m.fieldKey != key) {
      // a new depth map: refine it against the image off the main thread
      m.fieldNpy = npy;
      m.fieldKey = key;
      m.fieldPixels.clear();
      m.fieldStale = m.field != 0;
      changed = true;
      if (!npy.empty() && m_jobs) {
        const std::string sha = sha256Of(wall);
        const std::string editsPath = m_editsDir.empty() ? std::string() : m_editsDir + "/" + sha + ".usde";
        m_jobs->run([this, out, wall, npy, key, sha, editsPath]() -> Jobs::Done {
          DepthField raw, refined;
          std::vector<float> guide;
          std::vector<std::uint8_t> colour;
          int iw = 0, ih = 0;
          const bool ok = loadNpyF32(npy, raw) && refineDepth(wall, raw, refined, &iw, &ih, &guide, &colour);
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
          return [this, out, npy, key, ok, iw, ih, sha, hasEdits, refined = std::move(refined), guide = std::move(guide),
                  edits = std::move(edits), half = std::move(half), colour = std::move(colour)]() mutable {
            auto found = m_masks.find(out);
            if (found == m_masks.end() || !found->second || found->second->fieldKey != key) return;  // superseded
            DepthMask* it = found->second.get();
            if (!ok) {  // a broken map or an unreadable wallpaper
              US_WARN("could not refine the depth map {}", fs::path(npy).filename().string());
              return;
            }
            const int fw = refined.w, fh = refined.h;
            it->fieldPixels = std::move(half);
            it->fieldW = fw;
            it->fieldH = fh;
            it->imageW = iw;
            it->imageH = ih;
            it->sha = sha;
            it->base = std::move(refined);
            it->guide = std::move(guide);
            it->edits = std::move(edits);
            it->hasEdits = hasEdits;
            it->colour = std::move(colour);
            it->wallStale = it->wallTexture != 0;
            US_INFO("depth field for {}: {}x{}", out, fw, fh);
            if (m_fieldReady) m_fieldReady();
          };
        });
      }
    }
    if (m.wallpaper == wall) continue;
    changed = true;
    m.wallpaper = wall;
    if (!wall.empty() && npy.empty()) US_INFO("depth for {}: waiting for the map of {}", out, fs::path(wall).filename().string());
  }
  return changed;
}

void DepthMasks::freeRetired() {
  for (auto& r : m_retired) {
    if (r.use_count() > 1) continue;  // taken back by an output
    if (r->field) glDeleteTextures(1, &r->field);
    if (r->wallTexture) glDeleteTextures(1, &r->wallTexture);
  }
  m_retired.clear();
}

const DepthMask* DepthMasks::get(const std::string& output) {
  if (!m_retired.empty()) freeRetired();
  auto it = m_masks.find(output);
  if (it == m_masks.end() || !it->second) return nullptr;
  auto& m = *it->second;
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
  return m.field ? &m : nullptr;
}

DepthMask* DepthMasks::paintable(const std::string& output) {
  auto it = m_masks.find(output);
  if (it == m_masks.end() || !it->second || !it->second->field || it->second->base.v.empty()) return nullptr;
  return it->second.get();
}

GLuint DepthMasks::wallpaperTexture(DepthMask& m) {
  if (m.wallStale && m.wallTexture) {
    glDeleteTextures(1, &m.wallTexture);
    m.wallTexture = 0;
  }
  m.wallStale = false;
  if (!m.wallTexture && !m.colour.empty()) {
    glGenTextures(1, &m.wallTexture);
    glBindTexture(GL_TEXTURE_2D, m.wallTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m.fieldW, m.fieldH, 0, GL_RGBA, GL_UNSIGNED_BYTE, m.colour.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }
  return m.wallTexture;
}

void DepthMasks::uploadRect(DepthMask& m, PixelBox box, const std::vector<float>* stroke, DepthTool tool, float value,
                            const std::vector<float>* perPixel) {
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
  std::vector<float> sub(stroke ? vals.size() : 0), subTarget(stroke && perPixel ? vals.size() : 0);
  for (int y = 0; y < bh; ++y)
    for (int x = 0; x < bw; ++x) {
      const size_t src = static_cast<size_t>(y + box.y0) * m.fieldW + (x + box.x0), dst = static_cast<size_t>(y) * bw + x;
      if (stroke) {
        preview.target[dst] = m.edits.target[src];
        preview.cover[dst] = m.edits.cover[src];
        sub[dst] = (*stroke)[src];
        if (perPixel) subTarget[dst] = (*perPixel)[src];
      }
    }
  if (stroke) mergeStroke(preview, sub, tool, value, {0, 0, bw, bh}, perPixel ? &subTarget : nullptr);
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
  freeRetired();
  for (auto& [k, m] : m_masks) {
    if (!m) continue;
    if (m->field) glDeleteTextures(1, &m->field);  // a shared one is zeroed on its first visit
    if (m->wallTexture) glDeleteTextures(1, &m->wallTexture);
    m->field = m->wallTexture = 0;
  }
}

}  // namespace undershell
