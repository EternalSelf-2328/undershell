// The depth field: .npy reading, the plugin's refinement maths, half floats.
#include "check.hpp"
#include "common.hpp"
#include "depth.hpp"
#include "depthfield.hpp"
#include "jobs.hpp"
#include "noctalia.hpp"
#include "wallkind.hpp"
#include "m3shapes.hpp"

#include <cairo.h>
#include <chrono>
#include <cmath>
#include <thread>
#include <cstring>
#include <filesystem>

using namespace undershell;

int main() {
  // a numpy.save'd float32 (2, 3) array
  const std::string dir = std::filesystem::temp_directory_path() / "undershell-test-depth";
  std::filesystem::create_directories(dir);
  std::string header = "{'descr': '<f4', 'fortran_order': False, 'shape': (2, 3), }";
  while ((10 + header.size() + 1) % 64) header += ' ';
  header += '\n';
  std::string npy = std::string("\x93NUMPY\x01\x00", 8);
  npy += static_cast<char>(header.size() & 0xff);
  npy += static_cast<char>(header.size() >> 8);
  npy += header;
  const float vals[6] = {0, 0.2F, 0.4F, 0.6F, 0.8F, 1};
  npy.append(reinterpret_cast<const char*>(vals), sizeof vals);
  writeFileAtomic(dir + "/d.npy", npy);
  DepthField f;
  CHECK(loadNpyF32(dir + "/d.npy", f));
  CHECK(f.w == 3 && f.h == 2 && std::abs(f.v[4] - 0.8F) < 1e-6F);
  writeFileAtomic(dir + "/bad.npy", "not numpy");
  CHECK(!loadNpyF32(dir + "/bad.npy", f));

  // box mean: constants stay, an edge-padded ramp averages symmetrically
  std::vector<float> c(20 * 10, 0.3F);
  for (float v : boxMean(c, 20, 10, 3)) CHECK(std::abs(v - 0.3F) < 1e-5F);
  const auto m = boxMean({0, 1, 2}, 3, 1, 1);  // edge padding: (0+0+1)/3, 1, (1+2+2)/3
  CHECK(std::abs(m[0] - 1 / 3.0F) < 1e-5F && std::abs(m[1] - 1) < 1e-5F && std::abs(m[2] - 5 / 3.0F) < 1e-5F);

  // guided filter: a flat depth stays flat whatever the guide
  DepthField flat{16, 16, std::vector<float>(256, 0.7F)};
  std::vector<float> guide(256);
  for (int i = 0; i < 256; ++i) guide[static_cast<size_t>(i)] = (i % 16) < 8 ? 0.1F : 0.9F;
  for (float v : guidedFilter(guide, flat, 4, 1e-3F).v) CHECK(std::abs(v - 0.7F) < 1e-4F);
  // ... and a depth step that follows the guide's edge keeps its edge
  DepthField step{16, 16, std::vector<float>(256)};
  for (int i = 0; i < 256; ++i) step.v[static_cast<size_t>(i)] = (i % 16) < 8 ? 0.0F : 1.0F;
  const DepthField g = guidedFilter(guide, step, 4, 1e-3F);
  CHECK(g.v[5 * 16 + 2] < 0.05F && g.v[5 * 16 + 13] > 0.95F);

  // bicubic: same size is the identity; a flat field stays flat when scaled
  DepthField same = resizeBicubic(step, 16, 16);
  CHECK(std::abs(same.v[3] - step.v[3]) < 1e-5F && std::abs(same.v[12] - step.v[12]) < 1e-5F);
  for (float v : resizeBicubic(flat, 37, 5).v) CHECK(std::abs(v - 0.7F) < 1e-4F);

  // half floats
  const auto h = toHalf({0.0F, 0.5F, 1.0F, 0.3F});
  CHECK(h[0] == 0 && h[1] == 0x3800 && h[2] == 0x3C00 && h[3] == 0x34CD);

  // output -> image uv, as Noctalia samples the wallpaper
  double u = 0, v = 0;
  CHECK(wallpaperUv(960, 540, 1920, 1080, 3840, 2160, 1, u, v) && std::abs(u - 0.5) < 1e-9 && std::abs(v - 0.5) < 1e-9);
  CHECK(wallpaperUv(0, 0, 1920, 1080, 1000, 1000, 1, u, v) && std::abs(u) < 1e-9 && std::abs(v - 0.21875) < 1e-9);  // crop trims top/bottom
  CHECK(!wallpaperUv(10, 540, 1920, 1080, 1000, 1000, 2, u, v));  // fit: the letterbox shows no image

  // depth edits: a Front stroke pulls depth to 1, Erase takes it back
  DepthEdits e;
  e.reset(40, 20);
  CHECK(e.empty());
  std::vector<float> stroke(40 * 20, 0.0F);
  PixelBox box;
  stampDab(stroke, 40, 20, 10, 10, 5, box);
  CHECK(!box.empty() && stroke[10 * 40 + 10] == 1.0F && stroke[10 * 40 + 30] == 0.0F);
  mergeStroke(e, stroke, DepthTool::Front, 0, box);
  CHECK(!e.empty() && std::abs(editedDepth(0.2F, e.target[10 * 40 + 10], e.cover[10 * 40 + 10]) - 1.0F) < 0.01F);
  CHECK(editedDepth(0.2F, e.target[10 * 40 + 30], e.cover[10 * 40 + 30]) == 0.2F);  // untouched
  mergeStroke(e, stroke, DepthTool::Match, 0.4F, box);  // a second stroke over it wins
  CHECK(std::abs(editedDepth(0.9F, e.target[10 * 40 + 10], e.cover[10 * 40 + 10]) - 0.4F) < 0.01F);
  // stored and read back (run-length packed)
  CHECK(saveDepthEdits(dir + "/e.usde", e));
  DepthEdits back;
  CHECK(loadDepthEdits(dir + "/e.usde", back) && back.w == 40 && back.target == e.target && back.cover == e.cover);
  std::vector<float> wide(40 * 20, 0.0F);  // an eraser a bit bigger than the stroke clears it
  PixelBox wb;
  stampDab(wide, 40, 20, 10, 10, 8, wb);
  mergeStroke(e, wide, DepthTool::Erase, 0, wb);
  CHECK(e.empty());

  // the smart brush: dabs centred on the dark half stop at its edge (x = 32)
  const int sw = 64, sh = 32;
  std::vector<float> img(static_cast<size_t>(sw) * sh), dep(static_cast<size_t>(sw) * sh), rough(static_cast<size_t>(sw) * sh, 0.0F),
      plain(static_cast<size_t>(sw) * sh, 0.0F);
  for (int y = 0; y < sh; ++y)
    for (int x = 0; x < sw; ++x) {
      img[static_cast<size_t>(y) * sw + x] = x < 32 ? 0.05F : 0.95F;
      dep[static_cast<size_t>(y) * sw + x] = x < 32 ? 0.3F : 0.8F;
    }
  PixelBox rb, pb;
  for (int x = 8; x <= 30; x += 2) {
    stampDab(rough, sw, sh, x, 16, 7, rb, &img, &dep);
    stampDab(plain, sw, sh, x, 16, 7, pb);
  }
  CHECK(rough[16 * sw + 20] == 1.0F);   // inside the dark half: taken
  CHECK(plain[16 * sw + 34] > 0.9F);    // a plain brush spills over the edge
  CHECK(rough[16 * sw + 34] == 0.0F);   // the smart one does not

  // the magic wand: a click on the dark half selects all of it (a gentle
  // ramp inside is fine) and nothing of the bright half
  {
    std::vector<float> dp(static_cast<size_t>(sw) * sh);
    for (int y = 0; y < sh; ++y)
      for (int x = 0; x < sw; ++x) dp[static_cast<size_t>(y) * sw + x] = x < 32 ? 0.2F + x * 0.004F : 0.9F;
    PixelBox wb2;
    const auto sel = regionGrow(img, dp, sw, sh, 5, 5, 0.3F, wb2);
    CHECK(sel[10 * sw + 28] == 1.0F && sel[20 * sw + 2] == 1.0F);
    CHECK(sel[10 * sw + 40] == 0.0F && sel[10 * sw + 60] == 0.0F);
  }
  // the lasso: a square polygon fills its inside only
  {
    std::vector<float> m(static_cast<size_t>(sw) * sh, 0.0F);
    PixelBox lb;
    fillPolygon(m, sw, sh, {{10, 5}, {20, 5}, {20, 15}, {10, 15}}, lb);
    CHECK(m[10 * sw + 15] == 1.0F && m[10 * sw + 25] == 0.0F && m[2 * sw + 15] == 0.0F && !lb.empty());
    // a rough outline 3 px past the image edge is pulled back onto it
    std::vector<float> r2(static_cast<size_t>(sw) * sh, 0.0F);
    PixelBox rb2;
    fillPolygon(r2, sw, sh, {{4, 4}, {35, 4}, {35, 28}, {4, 28}}, rb2);
    snapMask(img, r2, sw, sh, rb2, 6);
    CHECK(r2[16 * sw + 20] > 0.9F && r2[16 * sw + 34] < 0.1F);
  }
  // smoothing pulls a step towards its neighbourhood's mean
  {
    DepthField stepF{sw, sh, std::vector<float>(static_cast<size_t>(sw) * sh)};
    for (int y = 0; y < sh; ++y)
      for (int x = 0; x < sw; ++x) stepF.v[static_cast<size_t>(y) * sw + x] = x < 32 ? 0.0F : 1.0F;
    DepthEdits none;
    none.reset(sw, sh);
    std::vector<float> blur;
    blurredDepth(stepF, none, {28, 10, 36, 20}, 4, blur);
    CHECK(blur[15 * sw + 31] > 0.3F && blur[15 * sw + 31] < 0.6F);
  }

  // moving wallpapers: skwd's report, and video paths
  const std::string still = R"({"outputs":[{"connected":true,"name":"HDMI-A-1","type":"static"},{"connected":false,"name":"eDP-1","type":"video"}]})";
  const std::string moving = R"({"outputs":[{"connected":true,"name":"HDMI-A-1","type":"we"}]})";
  CHECK(!skwdShowsMotion(still, {}));                 // the video is on a disconnected output
  CHECK(skwdShowsMotion(moving, {}) && skwdShowsMotion(moving, {"HDMI-A-1"}));
  CHECK(!skwdShowsMotion(moving, {"DP-2"}));          // not one of ours
  CHECK(!skwdShowsMotion("", {}) && !skwdShowsMotion("not json", {}));
  CHECK(isVideoPath("/w/rain.MP4") && isVideoPath("a.webm") && !isVideoPath("/w/katana.png") && !isVideoPath("noext"));

  // Material 3 shapes: a circle is round, a 12-sided cookie has 12 lobes,
  // every shape stays within its box
  {
    CHECK(m3ShapeNames().size() == 18);
    const auto circ = m3ShapeRadii("circle", 360);
    float lo = 9, hi = 0;
    for (float r : circ) lo = std::min(lo, r), hi = std::max(hi, r);
    CHECK(hi - lo < 0.02F && hi <= 1.001F);
    const auto cookie = m3ShapeRadii("cookie12Sided", 720);
    int peaks = 0;
    for (size_t i = 0; i < cookie.size(); ++i) {
      const float a = cookie[(i + cookie.size() - 1) % cookie.size()], b = cookie[i], c = cookie[(i + 1) % cookie.size()];
      peaks += b > a && b >= c;
    }
    CHECK(peaks == 12);
    for (const auto& name : m3ShapeNames())
      for (float r : m3ShapeRadii(name, 256)) CHECK(r > 0.05F && r <= 1.5F);
    CHECK(m3ShapeRadii("none", 16)[3] == 1.0F);
  }

  {
    // wallpaper_depth may write a wallpaper's depth map while undershell
    // switches to it: the field must load once the map is there, and again
    // when the same file is rewritten (it used to be keyed by name only)
    const std::string state = dir + "/state";
    setenv("XDG_STATE_HOME", state.c_str(), 1);
    const std::filesystem::path cache = std::filesystem::path(DepthMasks::maskDir()).parent_path();
    std::filesystem::create_directories(cache / "masks");
    std::filesystem::create_directories(cache / "depth");
    const std::string wall = dir + "/wall.png";
    cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 64, 36);
    cairo_t* cr = cairo_create(img);
    cairo_set_source_rgb(cr, 0.2, 0.4, 0.6);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_write_to_png(img, wall.c_str());
    cairo_surface_destroy(img);

    Jobs jobs(1);
    DepthMasks masks;
    int ready = 0;
    masks.setJobs(&jobs, [&] { ++ready; });
    NoctaliaState st;
    st.depthPluginEnabled = true;
    st.wallpaperByOutput["T-1"] = wall;
    auto settle = [&](int want, int ms) {  // run the refinement until `want` fields landed (or time is up)
      for (int t = 0; t < ms && ready < want; t += 5) {
        jobs.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      jobs.dispatch();
    };
    auto writeMap = [&](int w, int h) {
      std::string hd = std::format("{{'descr': '<f4', 'fortran_order': False, 'shape': ({}, {}), }}", h, w);
      while ((10 + hd.size() + 1) % 64) hd += ' ';
      hd += '\n';
      std::string data = std::string("\x93NUMPY\x01\x00", 8);
      data += static_cast<char>(hd.size() & 0xff);
      data += static_cast<char>(hd.size() >> 8);
      data += hd;
      std::vector<float> v(static_cast<size_t>(w) * h, 0.5F);
      data.append(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(float));
      writeFileAtomic((cache / "depth" / (masks.sha256Of(wall) + "-m-d2-i518.npy")).string(), data);
    };
    masks.update(st, {"T-1"});  // no map yet
    settle(1, 300);
    CHECK(ready == 0);
    writeMap(16, 9);  // the plugin finishes it
    masks.update(st, {"T-1"});
    settle(1, 5000);
    CHECK(ready == 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    writeMap(32, 18);  // rewritten under the same name
    masks.update(st, {"T-1"});
    settle(2, 5000);
    CHECK(ready == 2);
    masks.update(st, {"T-1"});  // nothing new: no work
    settle(3, 300);
    CHECK(ready == 2);
  }

  std::filesystem::remove_all(dir);
  return TEST_RESULT();
}
