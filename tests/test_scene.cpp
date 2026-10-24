// undershell-scene, the helper that unpacks Wallpaper Engine scenes: a
// package and textures made here (no Workshop content in the tests), what
// it writes, what it refuses, and its confinement.
#include "check.hpp"

#include <lz4.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

static void u32(std::string& s, uint32_t v) {
  for (int i = 0; i < 4; ++i) s += static_cast<char>((v >> (8 * i)) & 0xff);
}
static void cstr(std::string& s, const char* v) {
  s += v;
  s += '\0';
}

// a TEXB0002 texture: format, size, one mipmap, optionally LZ4-packed
static std::string tex(uint32_t fmt, uint32_t w, uint32_t h, const std::string& pixels, bool pack) {
  std::string t;
  cstr(t, "TEXV0005");
  cstr(t, "TEXI0001");
  u32(t, fmt), u32(t, 0), u32(t, w), u32(t, h), u32(t, w), u32(t, h), u32(t, 0);
  cstr(t, "TEXB0002");
  u32(t, 1), u32(t, 1), u32(t, w), u32(t, h);
  if (pack) {
    std::string packed(static_cast<size_t>(LZ4_compressBound(static_cast<int>(pixels.size()))), '\0');
    const int n = LZ4_compress_default(pixels.data(), packed.data(), static_cast<int>(pixels.size()), static_cast<int>(packed.size()));
    u32(t, 1), u32(t, static_cast<uint32_t>(pixels.size())), u32(t, static_cast<uint32_t>(n));
    t.append(packed.data(), static_cast<size_t>(n));
  } else {
    u32(t, 0), u32(t, 0), u32(t, static_cast<uint32_t>(pixels.size()));
    t += pixels;
  }
  return t;
}

static std::string pkg(const std::vector<std::pair<std::string, std::string>>& files, uint32_t lieAbout = 0) {
  std::string head, body;
  u32(head, 8);
  head += "PKGV0019";
  u32(head, static_cast<uint32_t>(files.size()));
  for (const auto& [name, data] : files) {
    u32(head, static_cast<uint32_t>(name.size()));
    head += name;
    u32(head, static_cast<uint32_t>(body.size()));
    u32(head, static_cast<uint32_t>(data.size()) + (name == "big.json" ? lieAbout : 0));
    body += data;
  }
  return head + body;
}

static std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static int run(const std::string& cmd) { return std::system(cmd.c_str()); }

// the RGBA back out of a .usimg
static std::string usimg(const fs::path& p, uint32_t& w, uint32_t& h) {
  const std::string d = slurp(p);
  if (d.size() < 24 || d.compare(0, 8, "USIMG001") != 0) return {};
  auto at = [&](size_t i) {
    uint32_t v = 0;
    for (int k = 0; k < 4; ++k) v |= static_cast<uint32_t>(static_cast<unsigned char>(d[i + k])) << (8 * k);
    return v;
  };
  w = at(8), h = at(12);
  std::string raw(at(16), '\0');
  const int n = LZ4_decompress_safe(d.data() + 24, raw.data(), static_cast<int>(at(20)), static_cast<int>(raw.size()));
  return n < 0 ? std::string() : raw.substr(0, static_cast<size_t>(n));
}

int main() {
  const char* helper = std::getenv("UNDERSHELL_SCENE_HELPER");
  if (!helper || !fs::exists(helper)) {
    std::fputs("no undershell-scene: skipping\n", stderr);
    return 77;
  }
  const fs::path dir = fs::temp_directory_path() / ("undershell-scene-" + std::to_string(getpid()));
  fs::create_directories(dir / "wall");
  const std::string H = std::string(helper);

  // a package: JSON kept, textures converted, the rest left out
  const std::string rgba = std::string("\xff\x00\x00\xff\x00\xff\x00\xff\x00\x00\xff\xff\xff\xff\xff\x80", 16);
  // one DXT1 block: colour 0 white, colour 1 black, every texel index 1 (black)
  const std::string dxt1 = std::string("\xff\xff\x00\x00\x55\x55\x55\x55", 8);
  std::ofstream(dir / "wall/project.json") << R"({"type": "scene", "title": "test"})";
  std::ofstream(dir / "wall/scene.pkg", std::ios::binary)
      << pkg({{"scene.json", R"({"objects": []})"},
              {"materials/red.tex", tex(0, 2, 2, rgba, true)},
              {"materials/dark.tex", tex(7, 4, 4, dxt1, false)},
              {"sounds/song.mp3", "ID3"},
              {"../escape.json", "{}"},
              {"materials/broken.tex", "TEXV0005"}});
  CHECK(run(H + " unpack " + (dir / "wall").string() + " " + (dir / "out").string() + " >/dev/null 2>&1") == 0);
  CHECK(slurp(dir / "out/scene.json") == R"({"objects": []})" && fs::exists(dir / "out/project.json"));
  uint32_t w = 0, h = 0;
  CHECK(usimg(dir / "out/materials/red.tex.usimg", w, h) == rgba && w == 2 && h == 2);
  const std::string dark = usimg(dir / "out/materials/dark.tex.usimg", w, h);
  CHECK(dark.size() == 64 && static_cast<unsigned char>(dark[0]) == 0 && static_cast<unsigned char>(dark[3]) == 255);
  CHECK(slurp(dir / "out/materials/red.tex.info").rfind("2 2 2 2 0", 0) == 0);
  CHECK(!fs::exists(dir / "out/sounds/song.mp3") && !fs::exists(dir / "escape.json") && !fs::exists(dir / "out/../escape.json"));
  const std::string manifest = slurp(dir / "out/manifest.txt");
  CHECK(manifest.find("textures 2") != std::string::npos && manifest.find("errors 2") != std::string::npos);  // unsafe name, broken tex

  // a package that lies about its sizes is refused, not read past its end
  fs::create_directories(dir / "liar");
  std::ofstream(dir / "liar/scene.pkg", std::ios::binary) << pkg({{"big.json", "{}"}}, 1u << 30);
  run(H + " unpack " + (dir / "liar").string() + " " + (dir / "liar-out").string() + " >/dev/null 2>&1");
  CHECK(!fs::exists(dir / "liar-out/big.json"));
  std::ofstream(dir / "liar/scene.pkg", std::ios::binary) << "garbage";
  CHECK(run(H + " unpack " + (dir / "liar").string() + " " + (dir / "liar-out2").string() + " >/dev/null 2>&1") != 0);

  // confined: an asset that is a link to a file elsewhere cannot be read
  fs::create_directories(dir / "assets/materials");
  fs::create_directories(dir / "secret");
  std::ofstream(dir / "secret/key.json") << R"({"secret": true})";
  fs::create_symlink(dir / "secret/key.json", dir / "assets/materials/key.json");
  std::ofstream(dir / "assets/materials/fine.json") << "{}";
  run(H + " assets " + (dir / "assets").string() + " " + (dir / "assets-out").string() + " materials/fine.json materials/key.json ../secret/key.json >/dev/null 2>" +
      (dir / "confine.txt").string());
  CHECK(fs::exists(dir / "assets-out/materials/fine.json"));
  CHECK(!fs::exists(dir / "assets-out/secret/key.json") && !fs::exists(dir / "secret/key.json.part"));
  if (slurp(dir / "confine.txt").find("confined") != std::string::npos)  // a kernel with Landlock
    CHECK(!fs::exists(dir / "assets-out/materials/key.json"));
  else
    std::fputs("no Landlock on this kernel: confinement not checked\n", stderr);

  fs::remove_all(dir);
  return TEST_RESULT();
}
