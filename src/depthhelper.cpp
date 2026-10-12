// SPDX-License-Identifier: GPL-3.0-or-later
// undershell-depth: one wallpaper's depth map, computed with Depth Anything V2
// Small (ONNX Runtime, CPU) the way Noctalia's wallpaper_depth plugin does it
// (depth_helper.py: input of 518 px on the short side in steps of 14, ImageNet
// normalisation, the output scaled to 0..1), written as the same float32 .npy
// (the plugin's maps and undershell's are interchangeable). It runs as its own process,
// niced, so the model's memory never stays in the widgets' service.
//
//   undershell-depth <model.onnx> <wallpaper> <out.npy>
//   undershell-depth --check      (exits 0 when it can run at all)
#include <onnxruntime_cxx_api.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kInput = 518;  // INPUT_SIZE
constexpr int kPatch = 14;   // MODEL_PATCH_SIZE

// inference_size(): the short side to 518, both sides to a multiple of 14
std::pair<int, int> inferenceSize(int w, int h) {
  const double scale = std::max(static_cast<double>(kInput) / w, static_cast<double>(kInput) / h);
  const int mw = std::max(kInput, static_cast<int>(std::lround(w * scale / kPatch)) * kPatch);
  const int mh = std::max(kInput, static_cast<int>(std::lround(h * scale / kPatch)) * kPatch);
  return {mw, mh};
}

bool writeNpy(const std::string& path, const std::vector<float>& v, int w, int h) {
  std::string header = "{'descr': '<f4', 'fortran_order': False, 'shape': (" + std::to_string(h) + ", " + std::to_string(w) + "), }";
  // magic (6) + version (2) + length (2) + header, padded with spaces to a
  // multiple of 64 and ended by a newline
  const size_t total = (10 + header.size() + 1 + 63) / 64 * 64;
  header.append(total - 10 - header.size() - 1, ' ');
  header += '\n';
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  const unsigned char pre[10] = {0x93, 'N', 'U', 'M', 'P', 'Y', 1, 0, static_cast<unsigned char>(header.size() & 0xff),
                                 static_cast<unsigned char>(header.size() >> 8)};
  f.write(reinterpret_cast<const char*>(pre), 10);
  f.write(header.data(), static_cast<std::streamsize>(header.size()));
  f.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size() * sizeof(float)));
  return static_cast<bool>(f);
}

}  // namespace

int main(int argc, char** argv) {
  // started at all means ONNX Runtime loaded: the service asks before offering depth
  if (argc == 2 && std::strcmp(argv[1], "--check") == 0) return 0;
  // --background: a wallpaper not on screen, computed ahead of time
  const bool background = argc == 5 && std::strcmp(argv[4], "--background") == 0;
  if (argc != 4 && !background) {
    std::fprintf(stderr, "usage: undershell-depth <model.onnx> <wallpaper> <out.npy> [--background]\n");
    return 2;
  }
  setpriority(PRIO_PROCESS, 0, background ? 19 : 10);  // the widgets keep their frames
  const auto start = std::chrono::steady_clock::now();
  const std::string model = argv[1], wallpaper = argv[2], out = argv[3];

  int sw = 0, sh = 0;
  if (!gdk_pixbuf_get_file_info(wallpaper.c_str(), &sw, &sh) || sw <= 0 || sh <= 0) {
    std::fprintf(stderr, "undershell-depth: cannot read %s\n", wallpaper.c_str());
    return 3;
  }
  const auto [mw, mh] = inferenceSize(sw, sh);
  // decoded at twice the model's size at most (a JPEG shrinks while it is
  // decoded): an 8K wallpaper no longer needs its full size in memory
  GError* err = nullptr;
  GdkPixbuf* src = sw > 2 * mw ? gdk_pixbuf_new_from_file_at_scale(wallpaper.c_str(), 2 * mw, 2 * mh, FALSE, &err)
                               : gdk_pixbuf_new_from_file(wallpaper.c_str(), &err);
  if (!src) {
    std::fprintf(stderr, "undershell-depth: cannot read %s: %s\n", wallpaper.c_str(), err ? err->message : "?");
    if (err) g_error_free(err);
    return 3;
  }
  GdkPixbuf* small = gdk_pixbuf_scale_simple(src, mw, mh, GDK_INTERP_HYPER);
  g_object_unref(src);
  if (!small) return 3;
  const int nc = gdk_pixbuf_get_n_channels(small), stride = gdk_pixbuf_get_rowstride(small);
  const guchar* px = gdk_pixbuf_read_pixels(small);
  const size_t plane = static_cast<size_t>(mw) * mh;
  std::vector<float> input(plane * 3);
  const float mean[3] = {0.485F, 0.456F, 0.406F}, sd[3] = {0.229F, 0.224F, 0.225F};
  for (int y = 0; y < mh; ++y)
    for (int x = 0; x < mw; ++x) {
      const guchar* p = px + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * nc;
      for (int c = 0; c < 3; ++c)
        input[c * plane + static_cast<size_t>(y) * mw + x] = (p[c] / 255.0F - mean[c]) / sd[c];
    }
  g_object_unref(small);

  std::vector<float> depth;
  try {
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "undershell-depth");
    Ort::SessionOptions so;
    so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    // one run: no arena or memory pattern kept for the next
    so.DisableCpuMemArena();
    so.DisableMemPattern();
    // leave a core for the compositor and the widgets (half of them when
    // nobody is waiting for this map)
    const unsigned cores = std::thread::hardware_concurrency();
    so.SetIntraOpNumThreads(static_cast<int>(std::max(1U, background ? cores / 2 : cores - 1)));
    Ort::Session session(env, model.c_str(), so);
    Ort::AllocatorWithDefaultOptions alloc;
    const auto inName = session.GetInputNameAllocated(0, alloc);
    const auto outName = session.GetOutputNameAllocated(0, alloc);
    const int64_t shape[4] = {1, 3, mh, mw};
    const auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape, 4);
    const char* inNames[] = {inName.get()};
    const char* outNames[] = {outName.get()};
    auto result = session.Run(Ort::RunOptions{nullptr}, inNames, &tensor, 1, outNames, 1);
    const auto info = result[0].GetTensorTypeAndShapeInfo();
    if (info.GetElementCount() != plane) {
      std::fprintf(stderr, "undershell-depth: unexpected output of %zu values, expected %dx%d\n", info.GetElementCount(), mw, mh);
      return 4;
    }
    const float* d = result[0].GetTensorData<float>();
    depth.assign(d, d + plane);
  } catch (const Ort::Exception& e) {
    std::fprintf(stderr, "undershell-depth: %s\n", e.what());
    return 4;
  }
  const auto [lo, hi] = std::minmax_element(depth.begin(), depth.end());
  const float mn = *lo, mx = *hi;
  if (!std::isfinite(mn) || !std::isfinite(mx) || mx <= mn) {
    std::fprintf(stderr, "undershell-depth: the model returned an invalid depth map\n");
    return 4;
  }
  for (float& v : depth) v = (v - mn) / (mx - mn);
  const std::string tmp = out + ".part";
  if (!writeNpy(tmp, depth, mw, mh)) return 5;
  std::error_code ec;
  std::filesystem::rename(tmp, out, ec);
  if (ec) return 5;
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("%dx%d from %dx%d in %.1f s\n", mw, mh, sw, sh, secs);
  return 0;
}
