// SPDX-License-Identifier: GPL-3.0-or-later
#include "depthfield.hpp"

#include "depth.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>

namespace fs = std::filesystem;

namespace undershell {

namespace {
// depth_helper.py
constexpr int kRefineMax = 1920;
constexpr int kRadius = 8;
constexpr float kEpsilon = 0.001F;
}  // namespace

bool loadNpyF32(const std::string& path, DepthField& out) {
  std::ifstream f(path, std::ios::binary);
  char magic[8];
  if (!f.read(magic, 8) || std::memcmp(magic, "\x93NUMPY", 6) != 0) return false;
  size_t headerLen = 0;
  if (magic[6] == 1) {
    unsigned char b[2];
    if (!f.read(reinterpret_cast<char*>(b), 2)) return false;
    headerLen = b[0] | (b[1] << 8);
  } else {
    unsigned char b[4];
    if (!f.read(reinterpret_cast<char*>(b), 4)) return false;
    headerLen = b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<size_t>(b[3]) << 24);
  }
  std::string header(headerLen, '\0');
  if (!f.read(header.data(), static_cast<std::streamsize>(headerLen))) return false;
  if (header.find("'descr': '<f4'") == std::string::npos || header.find("'fortran_order': False") == std::string::npos)
    return false;
  std::smatch m;
  if (!std::regex_search(header, m, std::regex(R"('shape': \((\d+), (\d+)\))"))) return false;
  const int h = std::stoi(m[1].str()), w = std::stoi(m[2].str());
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return false;
  out.w = w;
  out.h = h;
  out.v.resize(static_cast<size_t>(w) * h);
  return static_cast<bool>(f.read(reinterpret_cast<char*>(out.v.data()), static_cast<std::streamsize>(out.v.size() * 4)));
}

std::string findDepthNpy(const std::string& sha) {
  if (sha.empty()) return {};
  const fs::path dir = fs::path(DepthMasks::maskDir()).parent_path() / "depth";
  std::error_code ec;
  std::string best;
  fs::file_time_type bestTime{};
  for (auto& e : fs::directory_iterator(dir, ec)) {
    const std::string name = e.path().filename().string();
    if (!name.starts_with(sha + "-") || !name.ends_with(".npy")) continue;
    auto t = e.last_write_time(ec);
    if (best.empty() || t > bestTime) {
      best = e.path().string();
      bestTime = t;
    }
  }
  return best;
}

std::vector<float> boxMean(const std::vector<float>& a, int w, int h, int r) {
  // separable running sums with clamped (edge) indices
  std::vector<float> tmp(a.size()), out(a.size());
  const float inv = 1.0F / static_cast<float>(2 * r + 1);
  for (int y = 0; y < h; ++y) {
    const float* row = a.data() + static_cast<size_t>(y) * w;
    double s = 0;
    for (int k = -r; k <= r; ++k) s += row[std::clamp(k, 0, w - 1)];
    for (int x = 0; x < w; ++x) {
      tmp[static_cast<size_t>(y) * w + x] = static_cast<float>(s) * inv;
      s += row[std::min(x + r + 1, w - 1)] - row[std::max(x - r, 0)];
    }
  }
  for (int x = 0; x < w; ++x) {
    double s = 0;
    for (int k = -r; k <= r; ++k) s += tmp[static_cast<size_t>(std::clamp(k, 0, h - 1)) * w + x];
    for (int y = 0; y < h; ++y) {
      out[static_cast<size_t>(y) * w + x] = static_cast<float>(s) * inv;
      s += tmp[static_cast<size_t>(std::min(y + r + 1, h - 1)) * w + x] - tmp[static_cast<size_t>(std::max(y - r, 0)) * w + x];
    }
  }
  return out;
}

namespace {
double cubic(double x) {  // PIL's bicubic, a = -0.5
  constexpr double a = -0.5;
  x = std::abs(x);
  if (x < 1) return ((a + 2) * x - (a + 3)) * x * x + 1;
  if (x < 2) return (((x - 5) * x + 8) * x - 4) * a;
  return 0;
}

// one axis of PIL's resample: support widens when shrinking
void resampleAxis(const float* src, int n, int stride, float* dst, int m, int dstStride) {
  const double scale = static_cast<double>(n) / m;
  const double fscale = std::max(scale, 1.0);
  const double support = 2.0 * fscale;
  for (int i = 0; i < m; ++i) {
    const double centre = (i + 0.5) * scale;
    const int lo = std::max(0, static_cast<int>(centre - support + 0.5));
    const int hi = std::min(n, static_cast<int>(centre + support + 0.5));
    double sum = 0, wsum = 0;
    for (int j = lo; j < hi; ++j) {
      const double wgt = cubic((j - centre + 0.5) / fscale);
      sum += wgt * src[static_cast<size_t>(j) * stride];
      wsum += wgt;
    }
    dst[static_cast<size_t>(i) * dstStride] = static_cast<float>(wsum != 0 ? sum / wsum : 0);
  }
}
}  // namespace

DepthField resizeBicubic(const DepthField& src, int w, int h) {
  DepthField mid{w, src.h, std::vector<float>(static_cast<size_t>(w) * src.h)};
  for (int y = 0; y < src.h; ++y)
    resampleAxis(src.v.data() + static_cast<size_t>(y) * src.w, src.w, 1, mid.v.data() + static_cast<size_t>(y) * w, w, 1);
  DepthField out{w, h, std::vector<float>(static_cast<size_t>(w) * h)};
  for (int x = 0; x < w; ++x) resampleAxis(mid.v.data() + x, src.h, w, out.v.data() + x, h, w);
  return out;
}

DepthField guidedFilter(const std::vector<float>& guide, const DepthField& coarse, int radius, float eps) {
  const int w = coarse.w, h = coarse.h;
  const size_t n = coarse.v.size();
  std::vector<float> gg(n), gd(n);
  for (size_t i = 0; i < n; ++i) {
    gg[i] = guide[i] * guide[i];
    gd[i] = guide[i] * coarse.v[i];
  }
  const auto meanG = boxMean(guide, w, h, radius), meanD = boxMean(coarse.v, w, h, radius);
  const auto corrG = boxMean(gg, w, h, radius), corrGD = boxMean(gd, w, h, radius);
  std::vector<float> a(n), b(n);
  for (size_t i = 0; i < n; ++i) {
    const float var = corrG[i] - meanG[i] * meanG[i];
    const float cov = corrGD[i] - meanG[i] * meanD[i];
    a[i] = cov / (var + eps);
    b[i] = meanD[i] - a[i] * meanG[i];
  }
  const auto meanA = boxMean(a, w, h, radius), meanB = boxMean(b, w, h, radius);
  DepthField out{w, h, std::vector<float>(n)};
  for (size_t i = 0; i < n; ++i) out.v[i] = std::clamp(meanA[i] * guide[i] + meanB[i], 0.0F, 1.0F);
  return out;
}

bool refineDepth(const std::string& wallpaper, const DepthField& depth, DepthField& out, int* imageW, int* imageH,
                 std::vector<float>* guideOut) {
  GError* err = nullptr;
  GdkPixbuf* src = gdk_pixbuf_new_from_file(wallpaper.c_str(), &err);
  if (!src) {
    if (err) g_error_free(err);
    return false;
  }
  GdkPixbuf* oriented = gdk_pixbuf_apply_embedded_orientation(src);
  g_object_unref(src);
  const int sw = gdk_pixbuf_get_width(oriented), sh = gdk_pixbuf_get_height(oriented);
  if (imageW) *imageW = sw;
  if (imageH) *imageH = sh;
  const double scale = std::min(1.0, static_cast<double>(kRefineMax) / std::max(sw, sh));
  const int rw = std::max(1, static_cast<int>(std::lround(sw * scale)));
  const int rh = std::max(1, static_cast<int>(std::lround(sh * scale)));
  GdkPixbuf* small = (rw == sw && rh == sh) ? static_cast<GdkPixbuf*>(g_object_ref(oriented))
                                            : gdk_pixbuf_scale_simple(oriented, rw, rh, GDK_INTERP_HYPER);
  g_object_unref(oriented);
  if (!small) return false;
  // PIL "L": ITU-R 601-2 luma
  std::vector<float> guide(static_cast<size_t>(rw) * rh);
  const int nch = gdk_pixbuf_get_n_channels(small), stride = gdk_pixbuf_get_rowstride(small);
  const guchar* px = gdk_pixbuf_read_pixels(small);
  for (int y = 0; y < rh; ++y)
    for (int x = 0; x < rw; ++x) {
      const guchar* p = px + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * nch;
      const int l = (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000;
      guide[static_cast<size_t>(y) * rw + x] = static_cast<float>(l) / 255.0F;
    }
  g_object_unref(small);
  out = guidedFilter(guide, resizeBicubic(depth, rw, rh), kRadius, kEpsilon);
  if (guideOut) *guideOut = std::move(guide);
  return true;
}

bool wallpaperUv(double ox, double oy, double sw, double sh, double iw, double ih, int fillMode, double& u, double& v) {
  if (sw <= 0 || sh <= 0 || iw <= 0 || ih <= 0) return false;
  const double x = ox / sw, y = oy / sh;
  if (fillMode == 0) {  // center: native size
    u = (x * sw - (sw - iw) * 0.5) / iw;
    v = (y * sh - (sh - ih) * 0.5) / ih;
  } else if (fillMode == 1 || fillMode == 5) {  // crop (span on one output)
    const double scale = std::max(sw / iw, sh / ih);
    const double offX = (iw * scale - sw) / (iw * scale), offY = (ih * scale - sh) / (ih * scale);
    u = x * (1 - offX) + offX * 0.5;
    v = y * (1 - offY) + offY * 0.5;
  } else if (fillMode == 2) {  // fit
    const double scale = std::min(sw / iw, sh / ih);
    u = ((x * sw - (sw - iw * scale) * 0.5) / scale) / iw;
    v = ((y * sh - (sh - ih * scale) * 0.5) / scale) / ih;
  } else if (fillMode == 3) {  // stretch
    u = x;
    v = y;
  } else {  // repeat
    u = x * sw / iw;
    v = y * sh / ih;
    u -= std::floor(u);
    v -= std::floor(v);
    return true;
  }
  return u >= 0 && u <= 1 && v >= 0 && v <= 1;
}

void DepthEdits::reset(int width, int height) {
  w = width;
  h = height;
  target.assign(static_cast<size_t>(w) * h, 0);
  cover.assign(static_cast<size_t>(w) * h, 0);
}

bool DepthEdits::empty() const {
  return std::all_of(cover.begin(), cover.end(), [](std::uint8_t c) { return c == 0; });
}

// a tiny container: "USDE", width, height (u32 LE), then target and cover
// planes, run-length packed (edits are mostly empty)
bool saveDepthEdits(const std::string& path, const DepthEdits& e) {
  std::string out = "USDE";
  auto u32 = [&](std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xff);
  };
  u32(static_cast<std::uint32_t>(e.w));
  u32(static_cast<std::uint32_t>(e.h));
  for (const auto* plane : {&e.target, &e.cover}) {
    for (size_t i = 0; i < plane->size();) {
      const std::uint8_t v = (*plane)[i];
      size_t n = 1;
      while (i + n < plane->size() && (*plane)[i + n] == v && n < 0xffffff) ++n;
      out += static_cast<char>(v);
      out += static_cast<char>(n & 0xff);
      out += static_cast<char>((n >> 8) & 0xff);
      out += static_cast<char>((n >> 16) & 0xff);
      i += n;
    }
  }
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  const std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f.write(out.data(), static_cast<std::streamsize>(out.size()))) return false;
  }
  fs::rename(tmp, path, ec);
  return !ec;
}

bool loadDepthEdits(const std::string& path, DepthEdits& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (data.size() < 12 || data.compare(0, 4, "USDE") != 0) return false;
  auto u32 = [&](size_t at) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + i])) << (8 * i);
    return v;
  };
  const std::uint32_t w = u32(4), h = u32(8);
  if (w == 0 || h == 0 || w > 16384 || h > 16384) return false;
  out.reset(static_cast<int>(w), static_cast<int>(h));
  size_t at = 12;
  for (auto* plane : {&out.target, &out.cover}) {
    size_t i = 0;
    while (i < plane->size()) {
      if (at + 4 > data.size()) return false;
      const auto v = static_cast<std::uint8_t>(data[at]);
      const size_t n = static_cast<unsigned char>(data[at + 1]) | (static_cast<unsigned char>(data[at + 2]) << 8) |
                       (static_cast<size_t>(static_cast<unsigned char>(data[at + 3])) << 16);
      at += 4;
      if (n == 0 || i + n > plane->size()) return false;
      std::fill_n(plane->begin() + static_cast<long>(i), n, v);
      i += n;
    }
  }
  return true;
}

void PixelBox::add(int x, int y, int r) {
  if (empty()) {
    x0 = x - r;
    y0 = y - r;
    x1 = x + r + 1;
    y1 = y + r + 1;
    return;
  }
  x0 = std::min(x0, x - r);
  y0 = std::min(y0, y - r);
  x1 = std::max(x1, x + r + 1);
  y1 = std::max(y1, y + r + 1);
}

void PixelBox::clip(int w, int h) {
  x0 = std::clamp(x0, 0, w);
  x1 = std::clamp(x1, 0, w);
  y0 = std::clamp(y0, 0, h);
  y1 = std::clamp(y1, 0, h);
}

float editedDepth(float base, std::uint8_t target, std::uint8_t cover) {
  const float c = cover / 255.0F;
  return base + (target / 255.0F - base) * c;
}

void stampDab(std::vector<float>& stroke, int w, int h, double cx, double cy, double radius, PixelBox& box,
              const std::vector<float>* guide, const std::vector<float>* depth) {
  const int r = static_cast<int>(std::ceil(radius)) + 1;
  PixelBox d;
  d.add(static_cast<int>(std::lround(cx)), static_cast<int>(std::lround(cy)), r);
  d.clip(w, h);
  if (d.empty()) return;
  const size_t centre = static_cast<size_t>(std::clamp(static_cast<int>(cy), 0, h - 1)) * w +
                        static_cast<size_t>(std::clamp(static_cast<int>(cx), 0, w - 1));
  const float g0 = guide ? (*guide)[centre] : 0, d0 = depth ? (*depth)[centre] : 0;
  constexpr float kToneSpread = 0.14F, kDepthSpread = 0.10F;
  for (int y = d.y0; y < d.y1; ++y)
    for (int x = d.x0; x < d.x1; ++x) {
      const double dist = std::hypot(x + 0.5 - cx, y + 0.5 - cy) / std::max(radius, 0.5);
      if (dist >= 1) continue;
      // full in the middle, a soft outer fifth
      float a = static_cast<float>(std::clamp((1.0 - dist) / 0.2, 0.0, 1.0));
      const size_t i = static_cast<size_t>(y) * w + x;
      if (guide) {
        const float t = ((*guide)[i] - g0) / kToneSpread;
        a *= std::exp(-t * t);
      }
      if (depth) {
        const float t = ((*depth)[i] - d0) / kDepthSpread;
        a *= std::exp(-t * t);
      }
      // a like pixel counts fully; the curve only decides where the edge is
      a = std::clamp((a - 0.15F) / 0.5F, 0.0F, 1.0F);
      float& sv = stroke[i];
      sv = std::max(sv, a);
    }
  box.add(d.x0, d.y0, 0);
  box.add(d.x1 - 1, d.y1 - 1, 0);
}

void mergeStroke(DepthEdits& e, const std::vector<float>& stroke, DepthTool tool, float value, PixelBox box) {
  box.clip(e.w, e.h);
  const float t = tool == DepthTool::Front ? 1.0F : tool == DepthTool::Back ? 0.0F : std::clamp(value, 0.0F, 1.0F);
  for (int y = box.y0; y < box.y1; ++y)
    for (int x = box.x0; x < box.x1; ++x) {
      const size_t i = static_cast<size_t>(y) * e.w + x;
      const float s = stroke[i];
      if (s <= 0) continue;
      const float c = e.cover[i] / 255.0F;
      if (tool == DepthTool::Erase) {
        e.cover[i] = static_cast<std::uint8_t>(std::lround(c * (1 - s) * 255));
        continue;
      }
      const float nc = c + s * (1 - c);
      const float nt = nc > 0 ? ((e.target[i] / 255.0F) * c * (1 - s) + t * s) / nc : t;
      e.cover[i] = static_cast<std::uint8_t>(std::lround(nc * 255));
      e.target[i] = static_cast<std::uint8_t>(std::lround(std::clamp(nt, 0.0F, 1.0F) * 255));
    }
}

std::vector<std::uint16_t> toHalf(const std::vector<float>& v) {
  std::vector<std::uint16_t> out(v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    std::uint32_t f;
    std::memcpy(&f, &v[i], 4);
    const std::uint32_t sign = (f >> 16) & 0x8000;
    const int exp = static_cast<int>((f >> 23) & 0xff) - 127 + 15;
    std::uint32_t mant = f & 0x7fffff;
    if (exp <= 0) {
      out[i] = static_cast<std::uint16_t>(sign);  // tiny: flush to zero
    } else if (exp >= 31) {
      out[i] = static_cast<std::uint16_t>(sign | 0x7bff);  // clamp to the largest finite
    } else {
      mant += 0x1000;  // round to nearest
      if (mant & 0x800000) {
        mant = 0;
        out[i] = static_cast<std::uint16_t>(sign | ((exp + 1) << 10));
        continue;
      }
      out[i] = static_cast<std::uint16_t>(sign | (exp << 10) | (mant >> 13));
    }
  }
  return out;
}

}  // namespace undershell
