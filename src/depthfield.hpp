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
                 int* imageH = nullptr);

// IEEE half floats for an R16F texture
std::vector<std::uint16_t> toHalf(const std::vector<float>& v);

}  // namespace undershell
