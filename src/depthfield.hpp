// SPDX-License-Identifier: GPL-3.0-or-later
// The wallpaper's depth, not just one mask: wallpaper_depth caches the model's
// normalized depth map (.npy) apart from its masks. undershell refines it
// against the image exactly as the plugin does before thresholding (guided
// filter, depth_helper.py refine_depth, MIT (c) noctalia-dev), so each widget
// can cut its own mask at its own depth plane, live, on the GPU.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace undershell {

struct DepthField {
  int w = 0, h = 0;
  std::vector<float> v;  // row-major, top-down, 0 (far) .. 1 (near)
};

// A C-order little-endian float32 2-D .npy (what numpy.save writes).
bool loadNpyF32(const std::string& path, DepthField& out);
// the newest cached depth map for a wallpaper's sha256, or ""
std::string findDepthNpy(const std::string& sha);

// mean over a (2r+1)^2 window, edges repeated (numpy.pad mode="edge")
std::vector<float> boxMean(const std::vector<float>& a, int w, int h, int r);
// PIL-style bicubic (a = -0.5) resample
DepthField resizeBicubic(const DepthField& src, int w, int h);
// guided filter of `coarse` (already at the guide's size) by `guide`
DepthField guidedFilter(const std::vector<float>& guide, const DepthField& coarse, int radius, float eps);

// depth_helper.py refine_depth: the depth map aligned to the wallpaper's edges,
// at the plugin's refinement size (longest side <= 1920). Decodes the image.
bool refineDepth(const std::string& wallpaper, const DepthField& depth, DepthField& out, int* imageW = nullptr,
                 int* imageH = nullptr, std::vector<float>* guideOut = nullptr);

// Noctalia's calculateWallpaperUV on the CPU: an output pixel -> image uv
// (0..1). False when the pixel shows no image (fit/center letterbox).
bool wallpaperUv(double ox, double oy, double outW, double outH, double imgW, double imgH, int fillMode, double& u,
                 double& v);

// Hand corrections of a depth field (the editor's depth brush), at the
// field's size: where `cover` > 0 the depth is pulled towards `target`.
struct DepthEdits {
  int w = 0, h = 0;
  std::vector<std::uint8_t> target, cover;
  void reset(int width, int height);
  [[nodiscard]] bool empty() const;
};
bool loadDepthEdits(const std::string& path, DepthEdits& out);
bool saveDepthEdits(const std::string& path, const DepthEdits& e);

struct PixelBox {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // [x0, x1) x [y0, y1)
  [[nodiscard]] bool empty() const { return x1 <= x0 || y1 <= y0; }
  void add(int x, int y, int r);        // grow to hold a disc
  void clip(int w, int h);
};

// the corrected depth at one pixel
float editedDepth(float base, std::uint8_t target, std::uint8_t cover);
// A soft round dab into a stroke mask (0..1, kept at the max). With a
// `guide` (smoothed luma) and/or `depth`, it is a smart brush: it only takes
// pixels that look like the one under its centre, so it stops at edges.
void stampDab(std::vector<float>& stroke, int w, int h, double cx, double cy, double radius, PixelBox& box,
              const std::vector<float>* guide = nullptr, const std::vector<float>* depth = nullptr);
// the brush tools
enum class DepthTool { Front = 0, Back = 1, Match = 2, Erase = 3 };
// lays a stroke over the edits (Match pulls towards `value`, 0..1)
void mergeStroke(DepthEdits& e, const std::vector<float>& stroke, DepthTool tool, float value, PixelBox box);

// IEEE half floats for an R16F texture
std::vector<std::uint16_t> toHalf(const std::vector<float>& v);

}  // namespace undershell
