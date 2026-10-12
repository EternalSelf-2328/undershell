// SPDX-License-Identifier: GPL-3.0-or-later
#include "colorpick.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>

namespace undershell {

namespace fs = std::filesystem;

Hsv toHsv(const Color& c) {
  const double r = c.r, g = c.g, b = c.b;
  const double mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
  Hsv o;
  o.v = mx;
  o.s = mx > 0 ? d / mx : 0;
  if (d > 1e-9) {
    if (mx == r) o.h = 60 * std::fmod((g - b) / d, 6.0);
    else if (mx == g) o.h = 60 * ((b - r) / d + 2);
    else o.h = 60 * ((r - g) / d + 4);
    if (o.h < 0) o.h += 360;
  }
  return o;
}

Color fromHsv(const Hsv& in) {
  const double h = std::fmod(std::fmod(in.h, 360.0) + 360.0, 360.0) / 60.0;
  const double s = std::clamp(in.s, 0.0, 1.0), v = std::clamp(in.v, 0.0, 1.0);
  const double c = v * s, x = c * (1 - std::abs(std::fmod(h, 2.0) - 1)), m = v - c;
  double r = 0, g = 0, b = 0;
  switch (static_cast<int>(h)) {
    case 0: r = c, g = x; break;
    case 1: r = x, g = c; break;
    case 2: g = c, b = x; break;
    case 3: g = x, b = c; break;
    case 4: r = x, b = c; break;
    default: r = c, b = x; break;
  }
  return {static_cast<float>(r + m), static_cast<float>(g + m), static_cast<float>(b + m), 1};
}

Color readableAccent(Color c) {
  for (int i = 0; i < 8 && 0.2126F * c.r + 0.7152F * c.g + 0.0722F * c.b < 0.45F; ++i) c = c.mix(Color{1, 1, 1, 1}, 0.18F);
  c.a = 1;
  return c;
}

std::string toHex(const Color& c) {
  auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255)); };
  return std::format("#{:02x}{:02x}{:02x}", byte(c.r), byte(c.g), byte(c.b));
}

bool parseHex(const std::string& text, Color& out) {
  std::string h;
  for (char ch : text)
    if (ch != ' ' && ch != '#') h += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (h.size() == 3) h = {h[0], h[0], h[1], h[1], h[2], h[2]};
  if (h.size() != 6 || !std::ranges::all_of(h, [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)); })) return false;
  out = Color::fromHex("#" + h);
  out.a = 1;
  return true;
}

std::vector<Color> dominantColors(const std::uint8_t* rgba, int w, int h, int n) {
  std::vector<Color> out;
  if (!rgba || w <= 0 || h <= 0 || n <= 0) return out;
  // a coarse copy: at most ~64 x 64 samples
  const int step = std::max(1, std::max(w, h) / 64);
  struct P {
    float r, g, b;
  };
  std::vector<P> px;
  for (int y = step / 2; y < h; y += step)
    for (int x = step / 2; x < w; x += step) {
      const std::uint8_t* p = rgba + (static_cast<size_t>(y) * w + x) * 4;
      if (p[3] < 128) continue;
      px.push_back({p[0] / 255.F, p[1] / 255.F, p[2] / 255.F});
    }
  if (px.empty()) return out;
  auto dist = [](const P& a, const P& b) {
    const float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return dr * dr * 0.3F + dg * dg * 0.59F + db * db * 0.11F + (dr * dr + dg * dg + db * db) * 0.5F;
  };
  // seeds: the image's mean, then each time the sample farthest from every seed
  const int k = std::min<int>(std::max(n + 2, 8), static_cast<int>(px.size()));
  std::vector<P> centre;
  P mean{0, 0, 0};
  for (const auto& p : px) mean.r += p.r, mean.g += p.g, mean.b += p.b;
  mean = {mean.r / px.size(), mean.g / px.size(), mean.b / px.size()};
  centre.push_back(mean);
  std::vector<float> nearest(px.size(), 1e9F);
  while (static_cast<int>(centre.size()) < k) {
    size_t far = 0;
    for (size_t i = 0; i < px.size(); ++i) {
      nearest[i] = std::min(nearest[i], dist(px[i], centre.back()));
      if (nearest[i] > nearest[far]) far = i;
    }
    centre.push_back(px[far]);
  }
  std::vector<int> label(px.size()), count(centre.size());
  for (int iter = 0; iter < 10; ++iter) {
    for (size_t i = 0; i < px.size(); ++i) {
      int best = 0;
      for (size_t c = 1; c < centre.size(); ++c)
        if (dist(px[i], centre[c]) < dist(px[i], centre[static_cast<size_t>(best)])) best = static_cast<int>(c);
      label[i] = best;
    }
    std::vector<P> sum(centre.size(), P{0, 0, 0});
    std::fill(count.begin(), count.end(), 0);
    for (size_t i = 0; i < px.size(); ++i) {
      auto& s = sum[static_cast<size_t>(label[i])];
      s.r += px[i].r, s.g += px[i].g, s.b += px[i].b;
      ++count[static_cast<size_t>(label[i])];
    }
    for (size_t c = 0; c < centre.size(); ++c)
      if (count[c]) centre[c] = {sum[c].r / count[c], sum[c].g / count[c], sum[c].b / count[c]};
  }
  std::vector<size_t> order(centre.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::ranges::stable_sort(order, [&](size_t a, size_t b) { return count[a] > count[b]; });
  std::vector<P> kept;
  for (size_t c : order) {
    if (!count[c]) continue;
    if (std::ranges::any_of(kept, [&](const P& q) { return dist(q, centre[c]) < 0.012F; })) continue;  // alike
    kept.push_back(centre[c]);
    out.push_back({centre[c].r, centre[c].g, centre[c].b, 1});
    if (static_cast<int>(out.size()) == n) break;
  }
  return out;
}

bool sampleWallpaper(const std::string& path, int maxSide, WallpaperSample& out) {
  out = {};
  out.path = path;
  GError* err = nullptr;
  GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), maxSide, maxSide, TRUE, &err);
  if (!pb) {
    if (err) g_error_free(err);
    return false;
  }
  GdkPixbuf* rgba = gdk_pixbuf_get_has_alpha(pb) ? g_object_ref(pb) : gdk_pixbuf_add_alpha(pb, FALSE, 0, 0, 0);
  g_object_unref(pb);
  if (!rgba) return false;
  out.w = gdk_pixbuf_get_width(rgba);
  out.h = gdk_pixbuf_get_height(rgba);
  const int stride = gdk_pixbuf_get_rowstride(rgba);
  const guchar* src = gdk_pixbuf_read_pixels(rgba);
  out.rgba.resize(static_cast<size_t>(out.w) * out.h * 4);
  for (int y = 0; y < out.h; ++y) std::copy_n(src + static_cast<size_t>(y) * stride, out.w * 4, out.rgba.data() + static_cast<size_t>(y) * out.w * 4);
  g_object_unref(rgba);
  out.dominant = dominantColors(out.rgba.data(), out.w, out.h, 8);
  return true;
}

std::string recentColorsPath() {
  const char* xdg = std::getenv("XDG_STATE_HOME");
  return std::string(xdg && *xdg ? xdg : expandHome("~/.local/state")) + "/undershell/recent-colors";
}

std::vector<std::string> loadRecentColors() {
  std::vector<std::string> out;
  std::ifstream f(recentColorsPath());
  std::string line;
  Color c;
  while (std::getline(f, line) && out.size() < 16)
    if (parseHex(line, c)) out.push_back(toHex(c));
  return out;
}

void pushRecentColor(std::vector<std::string>& list, const std::string& hex, size_t keep) {
  Color c;
  if (!parseHex(hex, c)) return;
  const std::string h = toHex(c);
  std::erase(list, h);
  list.insert(list.begin(), h);
  if (list.size() > keep) list.resize(keep);
}

bool saveRecentColors(const std::vector<std::string>& list) {
  std::string text;
  for (const auto& h : list) text += h + "\n";
  std::error_code ec;
  fs::create_directories(fs::path(recentColorsPath()).parent_path(), ec);
  return writeFileAtomic(recentColorsPath(), text);
}

}  // namespace undershell
