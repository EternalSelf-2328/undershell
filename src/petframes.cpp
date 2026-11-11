// SPDX-License-Identifier: GPL-3.0-or-later
// Painted companions (petframes.hpp): the embedded atlases and the choice of frame.
#include "petframes.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>

namespace undershell {

namespace {
struct Embedded {
  const char* id;
  int style;  // 0 chibi, 1 pixel
  const unsigned char* begin;
  const unsigned char* end;
};
#include "petframes.inc"  // generated from art/pets/frames by tools/embed-pet-frames.py

constexpr int kCols = 6, kRows = 4;

struct Store {
  std::mutex mutex;
  std::map<std::pair<std::string, int>, FrameAtlas> decoded;
  std::map<std::pair<std::string, int>, const FrameAtlas*> overrides;
};
Store& store() {
  static Store s;
  return s;
}

void measure(FrameAtlas& a) {
  a.idleLeft = kFrameCell, a.idleRight = 0, a.idleTop = kFrameCell;
  for (int y = 0; y < kFrameCell; ++y)
    for (int x = 0; x < kFrameCell; ++x)
      if (a.rgba[(static_cast<size_t>(y) * kCols * kFrameCell + x) * 4 + 3])
        a.idleLeft = std::min(a.idleLeft, x), a.idleRight = std::max(a.idleRight, x), a.idleTop = std::min(a.idleTop, y);
  if (a.idleRight < a.idleLeft) a.idleLeft = 0, a.idleRight = kFrameCell - 1, a.idleTop = 0;
}

bool decode(const Embedded& e, FrameAtlas& out) {
  GdkPixbufLoader* loader = gdk_pixbuf_loader_new_with_type("png", nullptr);
  if (!loader) return false;
  const bool fed = gdk_pixbuf_loader_write(loader, e.begin, static_cast<gsize>(e.end - e.begin), nullptr);
  const bool closed = gdk_pixbuf_loader_close(loader, nullptr);
  GdkPixbuf* pb = fed && closed ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
  bool ok = false;
  if (pb && gdk_pixbuf_get_width(pb) == kCols * kFrameCell && gdk_pixbuf_get_height(pb) == kRows * kFrameCell &&
      gdk_pixbuf_get_n_channels(pb) == 4) {
    const guint8* data = gdk_pixbuf_read_pixels(pb);
    const int stride = gdk_pixbuf_get_rowstride(pb);
    const int row = kCols * kFrameCell * 4;
    out.rgba.resize(static_cast<size_t>(row) * kRows * kFrameCell);
    for (int y = 0; y < kRows * kFrameCell; ++y) std::copy_n(data + static_cast<size_t>(y) * stride, row, out.rgba.data() + static_cast<size_t>(y) * row);
    // binary alpha, whatever the file says
    for (size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = out.rgba[i] >= 128 ? 255 : 0;
    measure(out);
    ok = true;
  } else {
    std::fprintf(stderr, "undershell: could not decode the frames of %s\n", e.id);
  }
  g_object_unref(loader);
  return ok;
}

const FrameAtlas* find(std::string_view id, int style) {
  Store& s = store();
  std::lock_guard lock(s.mutex);
  const auto key = std::make_pair(std::string(id), style);
  if (auto o = s.overrides.find(key); o != s.overrides.end()) return o->second;
  if (auto d = s.decoded.find(key); d != s.decoded.end()) return d->second.rgba.empty() ? nullptr : &d->second;
  FrameAtlas& a = s.decoded[key];
  for (const Embedded& e : kEmbedded)
    if (e.begin && e.style == style && id == e.id) {
      if (!decode(e, a)) a.rgba.clear();
      break;
    }
  return a.rgba.empty() ? nullptr : &a;
}
}  // namespace

const FrameAtlas* frameAtlas(std::string_view id, FrameStyle style) { return find(id, static_cast<int>(style)); }

const FrameAtlas* frameAtlasOrOther(std::string_view id, FrameStyle style) {
  if (const FrameAtlas* a = frameAtlas(id, style)) return a;
  return frameAtlas(id, style == FrameStyle::Chibi ? FrameStyle::Pixel : FrameStyle::Chibi);
}

std::vector<std::string_view> framedSpecies(FrameStyle style) {
  std::vector<std::string_view> out;
  for (const Embedded& e : kEmbedded)
    if (e.begin && e.style == static_cast<int>(style)) out.emplace_back(e.id);
  Store& s = store();
  std::lock_guard lock(s.mutex);
  for (const auto& [key, atlas] : s.overrides)
    if (atlas && key.second == static_cast<int>(style) && std::find(out.begin(), out.end(), key.first) == out.end()) out.emplace_back(key.first);
  return out;
}

void frameAtlasOverride(std::string_view id, FrameStyle style, const FrameAtlas* atlas) {
  Store& s = store();
  std::lock_guard lock(s.mutex);
  const auto key = std::make_pair(std::string(id), static_cast<int>(style));
  if (atlas) s.overrides[key] = atlas;
  else s.overrides.erase(key);
}

int frameCell(const LifeIn& in, double time, RigPlan plan) {
  using namespace fcell;
  if (in.carried) return Held;
  if (!in.grounded) return in.vy < 0 ? Rise : Fall;
  if (in.crouch >= 0) return Crouch;
  if (in.landAge < 0.14) return Land;
  if (in.petted >= 0 && in.petted < 1.6) return Happy;
  if (in.alert >= 0 && in.alert < 1.2) return Alert;
  const bool sitting = in.mode == PetPose::Sit;
  if (in.act != RigAct::None && in.actDur > 0) {
    switch (in.act) {
      case RigAct::LookAround: case RigAct::LookAtYou: case RigAct::HeadTilt: case RigAct::Sniff: case RigAct::Scan:
        return sitting ? SitLook : Look;
      case RigAct::LieDown: return Sit;
      default: {
        const double t = std::clamp(in.actAge / in.actDur, 0.0, 1.0);
        return Act + (t < 0.28 ? 0 : t < 0.72 ? 1 : 2);
      }
    }
  }
  switch (in.mode) {
    case PetPose::Walk: {
      const double ph = in.phase - std::floor(in.phase);
      return Walk + std::clamp(static_cast<int>(ph * 6), 0, 5);
    }
    case PetPose::Sit: return Sit;
    case PetPose::Sleep: return std::fmod(time, 3.2) < 1.5 ? SleepBreathe : Sleep;
    case PetPose::Dance: return (in.beat & 1) ? Happy : Idle;
    case PetPose::Jump: return Idle;
    case PetPose::Idle: break;
  }
  (void)plan;
  if (in.blink) return Blink;
  return std::fmod(time, 2.8) < 1.2 ? Breathe : Idle;
}

double frameCycle(const FrameAtlas& a, RigPlan plan) {
  const double w = a.idleRight - a.idleLeft + 1, h = kFrameGround - a.idleTop;
  switch (plan) {
    case RigPlan::Quad: return std::max(18.0, 0.62 * w);
    case RigPlan::Bird: return std::max(12.0, 0.42 * w);
    case RigPlan::Blob: return std::max(14.0, 0.55 * w);
    case RigPlan::Serpent: return std::max(18.0, 0.5 * w);
    case RigPlan::Biped: return std::max(16.0, 0.5 * h);
  }
  return 30;
}

void frameCompose(const FrameAtlas& a, int cell, bool flip, PetPixels& out) {
  out.fill(0);
  cell = std::clamp(cell, 0, kFrameCount - 1);
  const int cx = (cell % kCols) * kFrameCell, cy = (cell / kCols) * kFrameCell;
  const int down = (kPetPixels - 1) - kFrameGround;  // the ground onto the last row
  const size_t row = static_cast<size_t>(kCols) * kFrameCell * 4;
  for (int y = 0; y + down < kPetPixels && y < kFrameCell; ++y) {
    const std::uint8_t* src = a.rgba.data() + (cy + y) * row + static_cast<size_t>(cx) * 4;
    std::uint8_t* dst = out.data() + static_cast<size_t>(y + down) * kPetPixels * 4;
    if (!flip) {
      std::copy_n(src, kFrameCell * 4, dst);
    } else {
      for (int x = 0; x < kFrameCell; ++x) std::copy_n(src + x * 4, 4, dst + (kPetPixels - 1 - x) * 4);
    }
  }
}

}  // namespace undershell
