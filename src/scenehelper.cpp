// SPDX-License-Identifier: GPL-3.0-or-later
// undershell-scene: unpacks a Wallpaper Engine scene into something simple
// that undershell reads (scene.cpp). Workshop scenes are content from
// strangers, and reading them means binary parsing (the .pkg table, .tex
// headers, LZ4, DXT blocks): that happens here, in a short-lived process
// that can only read the scene's folder and the Wallpaper Engine assets and
// only write its output folder (Landlock, where the kernel has it), so a bug
// in it cannot reach the person's files or take the widgets down.
//
// Out of the package it keeps the text it understands as is (.json and
// shader text) and turns each .tex into:
//   <name>.tex.usimg  a raw RGBA texture: "USIMG001", u32 width, height,
//                     raw size, packed size, then an LZ4 block
//   <name>.tex.png / .jpg / .mp4   when the .tex only wraps such a file
//   <name>.tex.info   "w h imageW imageH format" (+ sprite-sheet frames)
// and writes manifest.txt last (its presence means a finished unpack).
// Nothing else (sounds, models, scripts) is written.
//
// The formats were read from the files themselves (Fondos-Animados/01 in
// the author's notes): the .pkg is a sized "PKGV00NN" string, a u32 count
// and (path, offset, length) entries; a .tex is "TEXV0005" "TEXI0001", u32
// format, flags, texture and image sizes, [u32 when flags & 0x40], u32 mean
// colour, then "TEXB000N" with the images and their mipmaps.
//
//   undershell-scene unpack <wallpaper folder> <out folder>
//   undershell-scene assets <assets folder> <out folder> <relative path>...
//   undershell-scene --check
#include <lz4.h>

#include <linux/landlock.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxFile = 512u << 20;       // a package or texture file
constexpr size_t kMaxRaw = 256u << 20;        // one decoded image
constexpr uint32_t kMaxSide = 16384;
constexpr size_t kMaxEntries = 20000;

struct Reader {
  std::string_view d;
  size_t p = 0;
  bool ok = true;
  uint32_t u32() {
    if (p + 4 > d.size()) return ok = false, 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(static_cast<unsigned char>(d[p + i])) << (8 * i);
    p += 4;
    return v;
  }
  std::string_view bytes(size_t n) {
    if (n > d.size() - std::min(p, d.size())) return ok = false, std::string_view();
    auto v = d.substr(p, n);
    p += n;
    return v;
  }
  std::string cstr(size_t max = 64) {  // NUL-terminated
    std::string s;
    while (p < d.size() && d[p] != '\0' && s.size() < max) s += d[p++];
    if (p >= d.size()) return ok = false, s;
    ++p;
    return s;
  }
};

std::optional<std::string> readAll(const fs::path& path) {
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec || size > kMaxFile) return std::nullopt;
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::nullopt;
  std::string s(size, '\0');
  f.read(s.data(), static_cast<std::streamsize>(size));
  if (static_cast<size_t>(f.gcount()) != size) return std::nullopt;
  return s;
}

bool writeAll(const fs::path& path, std::string_view data) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  const fs::path tmp = path.string() + ".part";
  std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  f.close();
  if (!f) return false;
  fs::rename(tmp, path, ec);
  return !ec;
}

// a name from the package, safe to write under the output folder
bool safeName(const std::string& n) {
  if (n.empty() || n.size() > 400 || n[0] == '/' || n.find('\\') != std::string::npos) return false;
  for (const unsigned char c : n)
    if (c < 0x20 || c == 0x7f) return false;
  for (const auto& part : fs::path(n))
    if (part == ".." || part == ".") return false;
  return true;
}

bool textLike(const std::string& n) {
  static const char* kExt[] = {".json", ".frag", ".vert", ".glsl", ".h", ".txt"};
  return std::ranges::any_of(kExt, [&](const char* e) { return n.ends_with(e); });
}

// ── S3TC (DXT1/3/5) to RGBA ─────────────────────────────────────────────────

void rgb565(uint16_t c, uint8_t out[3]) {
  out[0] = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
  out[1] = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
  out[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
}

bool decodeDxt(std::string_view data, uint32_t w, uint32_t h, int kind, std::vector<uint8_t>& out) {
  const size_t bs = kind == 7 ? 8 : 16, bw = (w + 3) / 4, bh = (h + 3) / 4;
  if (data.size() < bw * bh * bs) return false;
  out.assign(static_cast<size_t>(w) * h * 4, 0);
  for (size_t by = 0; by < bh; ++by)
    for (size_t bx = 0; bx < bw; ++bx) {
      const auto* b = reinterpret_cast<const uint8_t*>(data.data()) + (by * bw + bx) * bs;
      uint8_t alpha[16];
      std::fill(alpha, alpha + 16, 255);
      if (kind == 6) {  // DXT3: 4 bits each
        for (int i = 0; i < 16; ++i) alpha[i] = static_cast<uint8_t>(((b[i / 2] >> (4 * (i % 2))) & 15) * 17);
        b += 8;
      } else if (kind == 4) {  // DXT5: two ends and 3-bit steps
        uint8_t pal[8] = {b[0], b[1]};
        if (b[0] > b[1])
          for (int i = 1; i < 7; ++i) pal[i + 1] = static_cast<uint8_t>(((7 - i) * b[0] + i * b[1]) / 7);
        else {
          for (int i = 1; i < 5; ++i) pal[i + 1] = static_cast<uint8_t>(((5 - i) * b[0] + i * b[1]) / 5);
          pal[6] = 0, pal[7] = 255;
        }
        uint64_t bits = 0;
        for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(b[2 + i]) << (8 * i);
        for (int i = 0; i < 16; ++i) alpha[i] = pal[(bits >> (3 * i)) & 7];
        b += 8;
      }
      const uint16_t c0 = static_cast<uint16_t>(b[0] | (b[1] << 8)), c1 = static_cast<uint16_t>(b[2] | (b[3] << 8));
      uint8_t col[4][4] = {};
      rgb565(c0, col[0]);
      rgb565(c1, col[1]);
      col[0][3] = col[1][3] = col[2][3] = col[3][3] = 255;
      for (int k = 0; k < 3; ++k) {
        if (c0 > c1 || kind != 7) {
          col[2][k] = static_cast<uint8_t>((2 * col[0][k] + col[1][k]) / 3);
          col[3][k] = static_cast<uint8_t>((col[0][k] + 2 * col[1][k]) / 3);
        } else {
          col[2][k] = static_cast<uint8_t>((col[0][k] + col[1][k]) / 2);
          col[3][k] = 0;
        }
      }
      if (!(c0 > c1) && kind == 7) col[3][3] = 0;  // DXT1's transparent black
      const uint32_t idx = b[4] | (b[5] << 8) | (b[6] << 16) | (static_cast<uint32_t>(b[7]) << 24);
      for (int i = 0; i < 16; ++i) {
        const size_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
        if (x >= w || y >= h) continue;
        const uint8_t* c = col[(idx >> (2 * i)) & 3];
        uint8_t* o = &out[(y * w + x) * 4];
        o[0] = c[0], o[1] = c[1], o[2] = c[2], o[3] = static_cast<uint8_t>(c[3] * alpha[i] / 255);
      }
    }
  return true;
}

// ── .tex ────────────────────────────────────────────────────────────────────

struct TexOut {
  std::string error;
};

// Writes base + ".usimg" | ".png" | ".jpg" | ".mp4", and base + ".info".
TexOut convertTex(std::string_view file, const fs::path& base) {
  Reader r{file};
  if (r.cstr(16) != "TEXV0005" || r.cstr(16) != "TEXI0001") return {"not a TEXV0005 texture"};
  const uint32_t fmt = r.u32(), flags = r.u32(), tw = r.u32(), th = r.u32(), iw = r.u32(), ih = r.u32();
  if (flags & 0x40) r.u32();  // a 3D LUT's depth
  r.u32();                    // mean colour
  const std::string texb = r.cstr(16);
  if (!r.ok || texb.size() != 8 || texb.rfind("TEXB000", 0) != 0) return {"no image block"};
  const int version = texb[7] - '0';
  if (version < 1 || version > 4) return {"image block " + texb + " unknown"};
  const uint32_t images = r.u32();
  int32_t fif = -1;
  uint32_t mp4 = 0;
  if (version >= 3) fif = static_cast<int32_t>(r.u32());
  if (version >= 4) mp4 = r.u32();
  if (!r.ok || images == 0) return {"no images"};
  const uint32_t mips = r.u32();
  if (!r.ok || mips == 0) return {"no mipmaps"};
  if (mp4) {  // only then does each mipmap carry this
    r.u32();
    r.u32();
    r.cstr(4096);
    r.u32();
  }
  const uint32_t w = r.u32(), h = r.u32();
  uint32_t packed = 0, raw = 0;
  if (version >= 2) packed = r.u32(), raw = r.u32();
  const uint32_t size = r.u32();
  std::string_view data = r.bytes(size);
  if (!r.ok) return {"truncated"};
  if (w == 0 || h == 0 || w > kMaxSide || h > kMaxSide || iw > kMaxSide || ih > kMaxSide) return {"bad size"};
  std::string unpacked;
  if (packed) {
    if (raw == 0 || raw > kMaxRaw) return {"bad unpacked size"};
    unpacked.resize(raw);
    const int got = LZ4_decompress_safe(data.data(), unpacked.data(), static_cast<int>(data.size()), static_cast<int>(raw));
    if (got < 0) return {"LZ4 data broken"};
    unpacked.resize(static_cast<size_t>(got));
    data = unpacked;
  }
  const std::string info = std::format("{} {} {} {} {}\n", w, h, iw ? iw : w, ih ? ih : h, fmt);
  if (mp4) {
    if (!writeAll(base.string() + ".mp4", data)) return {"cannot write"};
    writeAll(base.string() + ".info", info);
    return {};
  }
  if (fif != -1) {  // a whole image file inside: kept as it is, decoded by undershell
    const bool png = data.size() > 8 && data.substr(1, 3) == "PNG";
    const bool jpg = data.size() > 3 && static_cast<unsigned char>(data[0]) == 0xff && static_cast<unsigned char>(data[1]) == 0xd8;
    if (!png && !jpg) return {"embedded image of an unknown kind"};
    if (!writeAll(base.string() + (png ? ".png" : ".jpg"), data)) return {"cannot write"};
    writeAll(base.string() + ".info", info);
    return {};
  }
  std::vector<uint8_t> rgba;
  const size_t n = static_cast<size_t>(w) * h;
  if (fmt == 0) {
    if (data.size() < n * 4) return {"RGBA data short"};
    rgba.assign(data.begin(), data.begin() + static_cast<long>(n * 4));
  } else if (fmt == 9) {  // R8: grey, opaque (masks read its red)
    if (data.size() < n) return {"R8 data short"};
    rgba.resize(n * 4);
    for (size_t i = 0; i < n; ++i) rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = static_cast<uint8_t>(data[i]), rgba[i * 4 + 3] = 255;
  } else if (fmt == 8) {  // RG88: luminance and alpha
    if (data.size() < n * 2) return {"RG88 data short"};
    rgba.resize(n * 4);
    for (size_t i = 0; i < n; ++i)
      rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = static_cast<uint8_t>(data[i * 2]), rgba[i * 4 + 3] = static_cast<uint8_t>(data[i * 2 + 1]);
  } else if (fmt == 4 || fmt == 6 || fmt == 7) {
    if (!decodeDxt(data, w, h, static_cast<int>(fmt), rgba)) return {"DXT data short"};
  } else {
    return {"texture format " + std::to_string(fmt) + " unknown"};
  }
  (void)tw, (void)th;
  std::string out = "USIMG001";
  auto put = [&](uint32_t v) {
    for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xff);
  };
  const int bound = LZ4_compressBound(static_cast<int>(rgba.size()));
  std::string packedOut(static_cast<size_t>(bound), '\0');
  const int len = LZ4_compress_default(reinterpret_cast<const char*>(rgba.data()), packedOut.data(), static_cast<int>(rgba.size()), bound);
  if (len <= 0) return {"cannot pack"};
  put(w), put(h), put(static_cast<uint32_t>(rgba.size())), put(static_cast<uint32_t>(len));
  out.append(packedOut.data(), static_cast<size_t>(len));
  if (!writeAll(base.string() + ".usimg", out)) return {"cannot write"};
  writeAll(base.string() + ".info", info);
  return {};
}

// ── what goes out ───────────────────────────────────────────────────────────

struct Tally {
  int text = 0, textures = 0, skipped = 0;
  std::vector<std::string> errors;
};

void take(const std::string& name, std::string_view data, const fs::path& out, Tally& t) {
  if (!safeName(name)) {
    t.errors.push_back("unsafe name skipped");
    return;
  }
  if (textLike(name)) {
    if (data.size() > (16u << 20) || !writeAll(out / name, data)) t.errors.push_back(name + ": cannot write");
    else ++t.text;
  } else if (name.ends_with(".tex")) {
    const TexOut r = convertTex(data, out / name);
    if (r.error.empty()) ++t.textures;
    else t.errors.push_back(name + ": " + r.error);
  } else {
    ++t.skipped;
  }
}

bool unpackPkg(std::string_view pkg, const fs::path& out, Tally& t) {
  Reader r{pkg};
  const uint32_t magicLen = r.u32();
  const std::string_view magic = r.bytes(magicLen);
  if (!r.ok || magicLen > 32 || magic.rfind("PKGV", 0) != 0) return false;
  const uint32_t n = r.u32();
  if (!r.ok || n > kMaxEntries) return false;
  struct Entry {
    std::string name;
    uint32_t off, len;
  };
  std::vector<Entry> entries;
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t nl = r.u32();
    if (nl > 1024) return false;
    Entry e{std::string(r.bytes(nl)), r.u32(), r.u32()};
    if (!r.ok) return false;
    entries.push_back(std::move(e));
  }
  const size_t base = r.p;  // offsets count from the end of the table
  for (const Entry& e : entries) {
    if (base + static_cast<size_t>(e.off) + e.len > pkg.size()) {
      t.errors.push_back(e.name + ": past the end of the package");
      continue;
    }
    take(e.name, pkg.substr(base + e.off, e.len), out, t);
  }
  return true;
}

void unpackFolder(const fs::path& in, const fs::path& out, Tally& t) {
  std::error_code ec;
  size_t seen = 0;
  for (auto it = fs::recursive_directory_iterator(in, fs::directory_options::none, ec); it != fs::recursive_directory_iterator() && !ec;
       it.increment(ec)) {
    if (it.depth() > 8 || ++seen > kMaxEntries) break;
    if (it->is_symlink(ec) || !it->is_regular_file(ec)) continue;  // never follow a link out
    const std::string rel = fs::relative(it->path(), in, ec).generic_string();
    if (ec || !(textLike(rel) || rel.ends_with(".tex"))) continue;
    if (const auto data = readAll(it->path())) take(rel, *data, out, t);
  }
}

// ── the sandbox ─────────────────────────────────────────────────────────────

// Landlock: from here on this process reads only `reads` and writes only
// `writes`. Best effort: an older kernel without it just runs as is.
void confine(const std::vector<fs::path>& reads, const fs::path& writes) {
  prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
  const long abi = syscall(SYS_landlock_create_ruleset, nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION);
  if (abi < 1) return;
  const uint64_t readOnly = LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR;
  const uint64_t write = readOnly | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_MAKE_REG |
                         LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_REMOVE_DIR;
  uint64_t handled = write | LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_MAKE_SYM | LANDLOCK_ACCESS_FS_MAKE_SOCK |
                     LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_BLOCK | LANDLOCK_ACCESS_FS_MAKE_CHAR;
  if (abi >= 2) handled |= LANDLOCK_ACCESS_FS_REFER;
  if (abi >= 3) handled |= LANDLOCK_ACCESS_FS_TRUNCATE;
  landlock_ruleset_attr attr{};
  attr.handled_access_fs = handled;
  const int rs = static_cast<int>(syscall(SYS_landlock_create_ruleset, &attr, sizeof(attr), 0));
  if (rs < 0) return;
  auto allow = [&](const fs::path& p, uint64_t access) {
    const int fd = open(p.c_str(), O_PATH | O_CLOEXEC);
    if (fd < 0) return;
    landlock_path_beneath_attr pb{};
    pb.allowed_access = access & handled;
    pb.parent_fd = fd;
    syscall(SYS_landlock_add_rule, rs, LANDLOCK_RULE_PATH_BENEATH, &pb, 0);
    close(fd);
  };
  for (const auto& p : reads) allow(p, readOnly);
  allow(writes, write | (abi >= 3 ? LANDLOCK_ACCESS_FS_TRUNCATE : 0) | (abi >= 2 ? LANDLOCK_ACCESS_FS_REFER : 0));
  if (syscall(SYS_landlock_restrict_self, rs, 0) == 0) std::fputs("confined\n", stderr);
  close(rs);
}

int finish(const fs::path& out, const Tally& t) {
  std::string m = std::format("text {}\ntextures {}\nskipped {}\nerrors {}\n", t.text, t.textures, t.skipped, t.errors.size());
  for (size_t i = 0; i < t.errors.size() && i < 200; ++i) m += "error " + t.errors[i] + "\n";
  if (!writeAll(out / "manifest.txt", m)) return 1;
  std::printf("%d text, %d textures, %zu errors\n", t.text, t.textures, t.errors.size());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--check") return 0;
  if (argc < 4) {
    std::fputs("usage: undershell-scene unpack <wallpaper folder> <out folder>\n"
                "       undershell-scene assets <assets folder> <out folder> <relative path>...\n",
                stderr);
    return 2;
  }
  const std::string_view cmd = argv[1];
  std::error_code ec;
  const fs::path in = fs::weakly_canonical(argv[2], ec), out = fs::weakly_canonical(argv[3], ec);
  fs::create_directories(out, ec);
  if (ec) {
    std::fprintf(stderr, "cannot make %s\n", argv[3]);
    return 1;
  }
  confine({in}, out);
  Tally t;
  if (cmd == "unpack") {
    if (const auto proj = readAll(in / "project.json"); proj && proj->size() < (4u << 20)) writeAll(out / "project.json", *proj);
    if (fs::exists(in / "scene.pkg", ec)) {
      const auto pkg = readAll(in / "scene.pkg");
      if (!pkg || !unpackPkg(*pkg, out, t)) {
        std::fputs("scene.pkg could not be read\n", stderr);
        return 1;
      }
    } else {
      unpackFolder(in, out, t);
    }
    return finish(out, t);
  }
  if (cmd == "assets") {
    for (int i = 4; i < argc; ++i) {
      const std::string rel = argv[i];
      if (!safeName(rel)) continue;
      if (const auto data = readAll(in / rel)) take(rel, *data, out, t);
      else t.errors.push_back(rel + ": cannot read");
    }
    return finish(out, t);
  }
  return 2;
}
