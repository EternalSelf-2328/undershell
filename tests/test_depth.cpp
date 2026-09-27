// The depth field: .npy reading, the plugin's refinement maths, half floats.
#include "check.hpp"
#include "common.hpp"
#include "depthfield.hpp"

#include <cmath>
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

  std::filesystem::remove_all(dir);
  return TEST_RESULT();
}
