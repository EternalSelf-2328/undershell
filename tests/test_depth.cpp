// The depth field: .npy reading, the plugin's refinement maths, half floats.
#include "check.hpp"
#include "colorpick.hpp"
#include "common.hpp"
#include "depth.hpp"
#include "depthfield.hpp"
#include "depthgen.hpp"
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
#include <set>
#include <fstream>

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

  // effect masks: a stroke added to one channel and partly taken out, stored
  // and read back; another size is refused; nothing painted removes the file
  {
    std::vector<std::uint8_t> fx(40 * 20 * 4, 0);
    mergeMaskStroke(fx, 40, 2, stroke, box, false);
    CHECK(fx[(10 * 40 + 10) * 4 + 2] == 255 && fx[(10 * 40 + 10) * 4 + 0] == 0 && fx[(10 * 40 + 30) * 4 + 2] == 0);
    std::vector<float> half(40 * 20, 0.5F);
    mergeMaskStroke(fx, 40, 2, half, {0, 0, 40, 20}, true);
    CHECK(fx[(10 * 40 + 10) * 4 + 2] == 128);
    CHECK(saveEffectMasks(dir + "/m.usfm", 40, 20, fx));
    std::vector<std::uint8_t> got;
    CHECK(loadEffectMasks(dir + "/m.usfm", 40, 20, got) && got == fx);
    CHECK(!loadEffectMasks(dir + "/m.usfm", 41, 20, got));
    CHECK(saveEffectMasks(dir + "/m.usfm", 40, 20, std::vector<std::uint8_t>(40 * 20 * 4, 0)));
    CHECK(!std::filesystem::exists(dir + "/m.usfm"));
  }

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
    // a wallpaper's depth map may land after undershell switched to it: the
    // field must load once the map is there, and again when the same file is
    // rewritten (it used to be keyed by name only)
    setenv("XDG_CACHE_HOME", (dir + "/cache-maps").c_str(), 1);
    const std::filesystem::path cache = DepthGen::cacheDir();
    std::filesystem::create_directories(cache);
    auto paintWall = [](const std::string& path, double r) {
      cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 64, 36);
      cairo_t* cr = cairo_create(img);
      cairo_set_source_rgb(cr, r, 0.4, 0.6);
      cairo_paint(cr);
      cairo_destroy(cr);
      cairo_surface_write_to_png(img, path.c_str());
      cairo_surface_destroy(img);
    };
    const std::string wall = dir + "/wall.png";
    paintWall(wall, 0.2);

    Jobs jobs(1);
    DepthMasks masks;
    int ready = 0;
    masks.setJobs(&jobs, [&] { ++ready; });
    masks.setEnabled(true);
    NoctaliaState st;
    st.wallpaperByOutput["T-1"] = wall;
    auto settle = [&](int want, int ms) {  // run the refinement until `want` fields landed (or time is up)
      for (int t = 0; t < ms && ready < want; t += 5) {
        jobs.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      jobs.dispatch();
    };
    auto writeMap = [&](int w, int h, const std::string& forWall) {
      std::string hd = std::format("{{'descr': '<f4', 'fortran_order': False, 'shape': ({}, {}), }}", h, w);
      while ((10 + hd.size() + 1) % 64) hd += ' ';
      hd += '\n';
      std::string data = std::string("\x93NUMPY\x01\x00", 8);
      data += static_cast<char>(hd.size() & 0xff);
      data += static_cast<char>(hd.size() >> 8);
      data += hd;
      std::vector<float> v(static_cast<size_t>(w) * h, 0.5F);
      data.append(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(float));
      writeFileAtomic((cache / (masks.sha256Of(forWall) + "-m-d2-i518.npy")).string(), data);
    };
    masks.update(st, {"T-1"});  // no map yet
    settle(1, 300);
    CHECK(ready == 0);
    writeMap(16, 9, wall);  // the helper finishes it
    masks.update(st, {"T-1"});
    settle(1, 5000);
    CHECK(ready == 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    writeMap(32, 18, wall);  // rewritten under the same name
    masks.update(st, {"T-1"});
    settle(2, 5000);
    CHECK(ready == 2);
    masks.update(st, {"T-1"});  // nothing new: no work
    settle(3, 300);
    CHECK(ready == 2);

    // a second monitor with the same wallpaper shares the field: no second
    // refine, one copy; it gets its own when its wallpaper changes
    st.wallpaperByOutput["T-2"] = wall;
    masks.update(st, {"T-1", "T-2"});
    settle(3, 300);
    CHECK(ready == 2);
    const std::string wall2 = dir + "/wall2.png";
    paintWall(wall2, 0.8);
    writeMap(16, 9, wall2);
    st.wallpaperByOutput["T-2"] = wall2;
    masks.update(st, {"T-1", "T-2"});
    settle(3, 5000);
    CHECK(ready == 3);
    masks.update(st, {"T-1", "T-2"});  // T-1 kept its own: nothing to redo
    settle(4, 300);
    CHECK(ready == 3);
  }

  {
    // depth from undershell's own model (no plugin): maps in
    // ~/.cache/undershell/depth, made by the helper one wallpaper at a time,
    // used only when the service says so (setNative), the field alone
    namespace fs = std::filesystem;
    const std::string cacheHome = dir + "/cache", dataHome = dir + "/data";
    setenv("XDG_CACHE_HOME", cacheHome.c_str(), 1);
    setenv("XDG_DATA_HOME", dataHome.c_str(), 1);
    setenv("XDG_STATE_HOME", (dir + "/state-own").c_str(), 1);  // no plugin model either
    CHECK(DepthGen::cacheDir() == cacheHome + "/undershell/depth");
    setenv("UNDERSHELL_DEPTH_HELPER", (dir + "/no-helper").c_str(), 1);
    DepthGen gen;
    Jobs jobs(1);
    int changed = 0;
    gen.setJobs(&jobs, [&] { ++changed; });
    CHECK(!gen.canRun() && !gen.needsModel());  // nothing to run it with: nothing offered
    CHECK(!gen.download());

    // a stand-in helper: answers --check, fails on "bad" wallpapers, else
    // writes the map it was given
    const std::string helper = dir + "/fake-helper.sh", src = dir + "/fake.npy";
    writeFileAtomic(helper, "#!/bin/sh\n[ \"$1\" = --check ] && exit 0\ncase \"$2\" in *bad*) echo 'cannot read' >&2; exit 3;; esac\n"
                            "cp \"$FAKE_NPY\" \"$3\" && echo 'fake ok'\n");
    fs::permissions(helper, fs::perms::owner_all);
    std::string hd = "{'descr': '<f4', 'fortran_order': False, 'shape': (9, 16), }";
    while ((10 + hd.size() + 1) % 64) hd += ' ';
    hd += '\n';
    std::string data = std::string("\x93NUMPY\x01\x00", 8) + static_cast<char>(hd.size() & 0xff) + static_cast<char>(hd.size() >> 8) + hd;
    std::vector<float> v(16 * 9, 0.4F);
    data.append(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(float));
    writeFileAtomic(src, data);
    setenv("FAKE_NPY", src.c_str(), 1);
    setenv("UNDERSHELL_DEPTH_HELPER", helper.c_str(), 1);
    CHECK(DepthGen::helperPath() == helper);
    CHECK(gen.needsModel() && !gen.canRun());  // the button shows until the model is there
    fs::create_directories(fs::path(DepthGen::ownModel()).parent_path());
    { std::ofstream(DepthGen::ownModel()).put('x'); }
    fs::resize_file(DepthGen::ownModel(), static_cast<std::uintmax_t>(DepthGen::kModelSize));  // sparse: only the size is checked here
    CHECK(gen.canRun() && gen.model() == DepthGen::ownModel());

    auto pump = [&](int want) {
      for (int t = 0; t < 5000 && changed < want; t += 5) {
        jobs.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    };
    DepthMasks masks;
    const std::string good = dir + "/own.png", bad = dir + "/bad.png";
    writeFileAtomic(good, "good");
    writeFileAtomic(bad, "bad");
    const std::string sg = masks.sha256Of(good), sb = masks.sha256Of(bad);
    gen.want({{good, sg}, {bad, sb}});
    CHECK(gen.computing() && gen.computingWallpaper() == good);
    pump(2);
    CHECK(changed == 2 && !gen.computing());
    const std::string made = DepthGen::findNpy(sg);
    CHECK(!made.empty() && fs::path(made).filename().string().starts_with(sg + "-afb6a5c28f3b6bf1-d2-i518"));
    CHECK(DepthGen::findNpy(sb).empty());
    CHECK(findDepthNpy(sg) == made);  // the field finds undershell's map
    gen.want({{good, sg}, {bad, sb}});  // one done, one that failed: neither again
    CHECK(!gen.computing());

    // the masks take the wallpaper only once told depth is on
    int ready = 0;
    masks.setJobs(&jobs, [&] { ++ready; });
    NoctaliaState st;
    st.wallpaperByOutput["T-1"] = dir + "/wall.png";  // a real image (refined against it)
    const std::string wsha = masks.sha256Of(dir + "/wall.png");
    fs::copy_file(src, DepthGen::cacheDir() + "/" + wsha + "-afb6a5c28f3b6bf1-d2-i518.npy", fs::copy_options::overwrite_existing);
    masks.update(st, {"T-1"});
    for (int t = 0; t < 300; t += 5) jobs.dispatch(), std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(ready == 0);
    masks.setEnabled(true);
    masks.update(st, {"T-1"});
    for (int t = 0; t < 5000 && ready < 1; t += 5) jobs.dispatch(), std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(ready == 1);

    // wallpapers with a layout are computed ahead of time, behind the ones on
    // screen; one queued in the background jumps ahead when it is shown
    const std::string l1 = dir + "/l1.png", l2 = dir + "/l2.png", l3 = dir + "/l3.png", now = dir + "/now.png";
    for (const auto& f : {l1, l2, l3, now}) writeFileAtomic(f, f);
    const std::string s1 = masks.sha256Of(l1), s2 = masks.sha256Of(l2), s3 = masks.sha256Of(l3), sn = masks.sha256Of(now);
    const int before = changed;
    gen.want({{l1, s1}, {l2, s2}, {l3, s3}}, true);
    CHECK(gen.computingWallpaper() == l1 && gen.queued() == 2);
    gen.want({{now, sn}});      // on screen: next, ahead of l2 and l3
    gen.want({{l3, s3}});       // shown now too: behind `now`, ahead of l2
    gen.want({{l2, s2}}, true);  // already waiting: not twice
    CHECK(gen.queued() == 3);
    std::vector<std::string> order;
    for (int t = 0; t < 10000 && changed < before + 4; t += 5) {
      if (gen.computing() && (order.empty() || order.back() != gen.computingWallpaper())) order.push_back(gen.computingWallpaper());
      jobs.dispatch();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK((order == std::vector<std::string>{l1, now, l3, l2}));

    // the cache keeps every map with a layout, and only the newest others
    const std::set<std::string> keep = {s1, s2};
    DepthGen::prune(keep, 1);
    CHECK(!DepthGen::findNpy(s1).empty() && !DepthGen::findNpy(s2).empty());
    int others = 0;
    for (const auto& sha : {s3, sn, sg, wsha}) others += !DepthGen::findNpy(sha).empty();
    CHECK(others == 1);
    unsetenv("UNDERSHELL_DEPTH_HELPER");
    unsetenv("FAKE_NPY");
  }

  {
    // the colour picker: HSV and back, hex in and out
    for (const char* hex : {"#e2342a", "#000000", "#ffffff", "#7fc8ff", "#3a944a", "#808080", "#ff00ff"}) {
      const Color c = Color::fromHex(hex);
      CHECK(toHex(fromHsv(toHsv(c))) == hex);
    }
    CHECK(std::abs(toHsv(Color::fromHex("#ff0000")).h) < 1e-6 && std::abs(toHsv(Color::fromHex("#00ff00")).h - 120) < 1e-6);
    CHECK(toHsv(Color::fromHex("#808080")).s == 0);
    Color c;
    CHECK(parseHex("#ABC", c) && toHex(c) == "#aabbcc");
    CHECK(parseHex("e2342a", c) && toHex(c) == "#e2342a");
    CHECK(parseHex(" #E2342A ", c) && toHex(c) == "#e2342a");
    CHECK(!parseHex("#12", c) && !parseHex("#12345g", c) && !parseHex("", c) && !parseHex("#1234567", c));
    // a black widget still gives the editor an accent that reads; a bright one is kept
    const Color dark = readableAccent(Color::fromHex("#03231c"));
    CHECK(0.2126F * dark.r + 0.7152F * dark.g + 0.0722F * dark.b >= 0.45F);
    CHECK(toHex(readableAccent(Color::fromHex("#f5c96b"))) == "#f5c96b");

    // the wallpaper's colours: the most common first, no two alike
    const int w = 100, h = 50;
    std::vector<std::uint8_t> img(w * h * 4, 255);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        std::uint8_t* p = &img[(static_cast<size_t>(y) * w + x) * 4];
        const bool red = x < 60, blue = x >= 60 && x < 90;  // 60 % red, 30 % blue, 10 % white
        p[0] = red ? 220 : blue ? 30 : 250;
        p[1] = red ? 40 : blue ? 60 : 250;
        p[2] = red ? 30 : blue ? 200 : 250;
      }
    const auto dom = dominantColors(img.data(), w, h, 8);
    CHECK(dom.size() == 3);  // three colours, however many were asked for
    CHECK(dom.size() == 3 && toHex(dom[0]) == "#dc281e" && toHex(dom[1]) == "#1e3cc8" && toHex(dom[2]) == "#fafafa");
    CHECK(dominantColors(img.data(), w, h, 1).size() == 1);
    CHECK(dominantColors(nullptr, 0, 0, 4).empty());

    // a wallpaper on disk: decoded small, its colours found
    const std::string png = dir + "/pick.png";
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 400, 200);
    cairo_t* cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 26 / 255.0, 179 / 255.0, 77 / 255.0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_write_to_png(surf, png.c_str());
    cairo_surface_destroy(surf);
    WallpaperSample sample;
    CHECK(sampleWallpaper(png, 100, sample) && sample.w == 100 && sample.h == 50 && sample.rgba.size() == 100 * 50 * 4);
    CHECK(!sample.dominant.empty() && toHex(sample.dominant[0]) == "#1ab34d");
    CHECK(!sampleWallpaper(dir + "/missing.png", 100, sample));

    // recent colours: newest first, once each, eight kept, on disk
    setenv("XDG_STATE_HOME", (dir + "/state-colors").c_str(), 1);
    std::vector<std::string> recent;
    for (int i = 0; i < 10; ++i) pushRecentColor(recent, std::format("#00000{}", i));
    CHECK(recent.size() == 8 && recent.front() == "#000009" && recent.back() == "#000002");
    pushRecentColor(recent, "#000005");
    CHECK(recent.size() == 8 && recent.front() == "#000005" && std::count(recent.begin(), recent.end(), "#000005") == 1);
    pushRecentColor(recent, "not a colour");
    CHECK(recent.front() == "#000005");
    CHECK(saveRecentColors(recent) && loadRecentColors() == recent);
  }

  std::filesystem::remove_all(dir);
  return TEST_RESULT();
}
