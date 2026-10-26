// undershell-scene, the helper that unpacks Wallpaper Engine scenes: a
// package and textures made here (no Workshop content in the tests), what
// it writes, what it refuses, and its confinement.
#include "check.hpp"
#include "noctalia.hpp"
#include "offscreen.hpp"
#include "wescene.hpp"
#include "wallgallery.hpp"
#include "scene.hpp"
#include "text.hpp"

#include <lz4.h>

#include <chrono>
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

  // the model: a parent and a child placed in it, an aligned layer, y up
  {
    using namespace undershell;
    WeScene sc;
    sc.width = 200, sc.height = 100;
    WeLayer parent;
    parent.id = 1, parent.ox = 100, parent.oy = 50, parent.sx = 2, parent.sy = 2;
    WeLayer child;
    child.id = 2, child.parent = 1, child.ox = 10, child.oy = 0, child.w = 10, child.h = 10;
    WeLayer floor;
    floor.id = 3, floor.ox = 100, floor.oy = 0, floor.w = 200, floor.h = 20, floor.align = "bottom";
    sc.layers = {parent, child, floor};
    const auto q = weLayerQuad(sc, sc.layers[1], 0, 0);
    // the child: 10 to the right of the parent, scaled by it: 20 wide, centred at (120, 50)
    CHECK(std::abs(q[0] - 110) < 1e-3 && std::abs(q[1] - 60) < 1e-3 && std::abs(q[6] - 130) < 1e-3 && std::abs(q[7] - 40) < 1e-3);
    const auto f = weLayerQuad(sc, sc.layers[2], 0, 0);  // bottom-aligned: from y 0 up to 20
    CHECK(std::abs(f[1] - 20) < 1e-3 && std::abs(f[7] - 0) < 1e-3 && std::abs(f[0] - 0) < 1e-3);
    const auto t = weLayerQuad(sc, sc.layers[0], 4, 6);  // no size: the texture's
    CHECK(std::abs(t[0] - 96) < 1e-3 && std::abs(t[1] - 56) < 1e-3);
  }

  // end to end: a package unpacked, read and drawn; the layer lands where the scene says
  {
    using namespace undershell;
    setenv("XDG_CACHE_HOME", (dir / "cache").c_str(), 1);
    setenv("UNDERSHELL_SCENE_HELPER", helper, 1);
    fs::create_directories(dir / "scene");
    std::ofstream(dir / "scene/project.json") << R"({"type": "Scene", "title": "test", "file": "scene.json"})";
    std::string red;
    for (int i = 0; i < 4; ++i) red += std::string("\xff\x00\x00\xff", 4);
    std::ofstream(dir / "scene/scene.pkg", std::ios::binary)
        << pkg({{"scene.json", R"({"general": {"orthogonalprojection": {"width": 100, "height": 50}, "clearcolor": "0 0 1"},
                   "objects": [{"id": 1, "image": "models/m.json", "origin": "25 37.5 0", "size": "50 25"},
                               {"id": 2, "text": "not drawn yet", "visible": false, "origin": "0 0 0"}]})"},
                {"models/m.json", R"({"material": "materials/m.json"})"},
                {"materials/m.json", R"({"passes": [{"blending": "translucent", "textures": ["red"]}]})"},
                {"materials/red.tex", tex(0, 2, 2, red, true)}});
    WeProject proj;
    std::string why;
    CHECK(readWeProject((dir / "scene").string(), proj, why) && proj.type == "scene");
    Headless gl;
    if (gl.ok()) {
      toml::table o;
      o.insert_or_assign("scene", (dir / "scene").string());
      const Image img = renderWidget("scene", o, 200, 100, testPalette(), 1000, 1, 5.0);
      auto px = [&](int x, int y) { return &img.rgba[(static_cast<size_t>(y) * img.w + x) * 4]; };
      CHECK(img.w == 200);
      CHECK(px(50, 25)[0] > 200 && px(50, 25)[2] < 50);   // top left: the red layer (y up in the scene)
      CHECK(px(150, 75)[2] > 200 && px(150, 75)[0] < 50);  // bottom right: the clear colour
    }
  }

  // Wallpaper Engine's effects: an opacity mask baked in (the left half
  // gone), waves run through undershell's own (the picture moves)
  {
    using namespace undershell;
    fs::create_directories(dir / "fx");
    std::ofstream(dir / "fx/project.json") << R"({"type": "scene", "title": "fx"})";
    std::string stripes;  // 4x4: red and white columns, so a wave shows
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x) stripes += x % 2 ? std::string("\xff\xff\xff\xff", 4) : std::string("\xff\x00\x00\xff", 4);
    const std::string half = std::string("\x00\x00\xff\xff\x00\x00\xff\xff\x00\x00\xff\xff\x00\x00\xff\xff", 16);  // R8: right half on
    std::ofstream(dir / "fx/scene.pkg", std::ios::binary)
        << pkg({{"scene.json", R"({"general": {"orthogonalprojection": {"width": 100, "height": 50}, "clearcolor": "0 0 1"},
                   "objects": [{"id": 1, "image": "models/m.json", "origin": "50 25 0", "size": "100 50",
                     "effects": [{"file": "effects/opacity/effect.json", "passes": [{"constantshadervalues": {"alpha": 1}, "textures": [null, "masks/half"]}]},
                                 {"file": "effects/waterwaves/effect.json", "passes": [{"constantshadervalues": {"speed": 25, "scale": 400, "strength": 0.3}}]},
                                 {"file": "effects/workshop/123/custom/effect.json"}]}]})"},
                {"models/m.json", R"({"material": "materials/m.json"})"},
                {"materials/m.json", R"({"passes": [{"textures": ["stripes"]}]})"},
                {"materials/stripes.tex", tex(0, 4, 4, stripes, false)},
                {"materials/masks/half.tex", tex(9, 4, 4, half, false)}});
    WeProject proj;
    std::string why;
    CHECK(readWeProject((dir / "fx").string(), proj, why));
    Headless gl;
    if (gl.ok()) {
      toml::table o;
      o.insert_or_assign("scene", (dir / "fx").string());
      const Image a = renderWidget("scene", o, 200, 100, testPalette(), 1000, 1, 5.0);
      auto px = [](const Image& im, int x, int y) { return &im.rgba[(static_cast<size_t>(y) * im.w + x) * 4]; };
      CHECK(a.w == 200 && px(a, 30, 50)[2] > 200 && px(a, 30, 50)[0] < 60);  // left: masked out, the clear colour shows
      CHECK(px(a, 150, 50)[0] > 200);                                        // right: the layer (red or white)
    }
  }

  // the author's properties: read, set from the config, and a layer bound to one
  {
    using namespace undershell;
    fs::create_directories(dir / "props");
    std::ofstream(dir / "props/project.json") << R"({"type": "scene", "general": {"properties": {
      "show": {"type": "bool", "value": true, "text": "<b>Show</b> the layer", "order": 2},
      "tone": {"type": "color", "value": "1 0 0", "text": "ui_browse_properties_scheme_color", "order": 1},
      "speed": {"type": "slider", "value": 0.5, "min": 0, "max": 2, "step": 0.1, "text": "Speed", "order": 3},
      "mode": {"type": "combo", "value": "2", "options": [{"label": "One", "value": "1"}, {"label": "Two", "value": "2"}], "text": "Mode", "order": 4},
      "note": {"type": "text", "value": "hi", "text": "Note"}}}})";
    std::string red;
    for (int i = 0; i < 4; ++i) red += std::string("\xff\x00\x00\xff", 4);
    std::ofstream(dir / "props/scene.pkg", std::ios::binary)
        << pkg({{"scene.json", R"({"general": {"orthogonalprojection": {"width": 100, "height": 50}, "clearcolor": "0 0 1"},
                   "objects": [{"id": 1, "image": "models/m.json", "origin": "50 25 0", "size": "100 50", "visible": {"user": "show", "value": true}}]})"},
                {"models/m.json", R"({"material": "materials/m.json"})"},
                {"materials/m.json", R"({"passes": [{"textures": ["red"]}]})"},
                {"materials/red.tex", tex(0, 2, 2, red, true)}});
    WeProject proj;
    std::string why;
    CHECK(readWeProject((dir / "props").string(), proj, why));
    CHECK(proj.props.size() == 4 && proj.props[0].name == "tone" && proj.props[0].label == "scheme color");  // the text one is not offered
    CHECK(proj.props[1].label == "Show the layer" && proj.props[1].key == "we_show");
    toml::table o;
    o.insert_or_assign("we_tone", "#00ff00");
    o.insert_or_assign("we_speed", 9.0);
    o.insert_or_assign("we_show", false);
    CHECK(weOverride(proj.props[0], o) == "0.0000 1.0000 0.0000");
    CHECK(weOverride(proj.props[2], o) == "2");  // clamped to the slider
    CHECK(weOverride(proj.props[1], o) == "false" && weOverride(proj.props[3], o).empty());
    Headless gl;
    if (gl.ok()) {
      toml::table shown;
      shown.insert_or_assign("scene", (dir / "props").string());
      const Image a = renderWidget("scene", shown, 200, 100, testPalette(), 1000, 1, 5.0);
      toml::table hidden = shown;
      hidden.insert_or_assign("we_show", false);
      const Image b = renderWidget("scene", hidden, 200, 100, testPalette(), 1000, 1, 5.0);
      auto px = [](const Image& im, int x, int y) { return &im.rgba[(static_cast<size_t>(y) * im.w + x) * 4]; };
      CHECK(px(a, 100, 50)[0] > 200 && px(b, 100, 50)[2] > 200 && px(b, 100, 50)[0] < 50);  // switched off: the clear colour
      // its still, beside the unpacked files: the same picture, to stand for the wallpaper
      bool still = false;
      for (int i = 0; i < 200 && !still; ++i) {
        for (const auto& e : fs::recursive_directory_iterator(dir / "cache"))
          if (e.path().filename() == "still.png") still = true;
        if (!still) usleep(10000);
      }
      CHECK(still);
    }
  }

  // the gallery: pictures listed newest first, other files left out, thumbnails made off the frame
  {
    using namespace undershell;
    const fs::path pics = dir / "pics";
    fs::create_directories(pics);
    Image im;
    im.w = 64, im.h = 32;
    im.rgba.assign(64 * 32 * 4, 200);
    CHECK(writePng(im, (pics / "old.png").string(), false));
    CHECK(writePng(im, (pics / "new.png").string(), false));
    std::ofstream(pics / "notes.txt") << "x";
    fs::last_write_time(pics / "old.png", fs::file_time_type::clock::now() - std::chrono::hours(2));
    const auto dirs = galleryDirs({pics.string()}, "");
    CHECK(dirs.size() == 1);
    const auto list = galleryPictures(dirs);
    CHECK(list.size() == 2 && list[0].label == "new.png" && list[1].label == "old.png");
    CHECK(galleryDirs({}, (pics / "new.png").string()).front() == fs::canonical(pics).string());  // the current one's folder
    Headless gl;
    if (gl.ok()) {
      ThumbCache thumbs;
      int size[2] = {0, 0};
      GLuint tex = 0;
      for (int i = 0; i < 300 && !tex; ++i) {
        thumbs.upload(4);
        tex = thumbs.get(list[0].path, size);
        if (!tex) usleep(10000);
      }
      CHECK(tex != 0 && size[0] <= ThumbCache::kW && size[1] <= ThumbCache::kH && size[0] > 0);
    }
  }

  // a text layer whose script (run by undershell-script, confined) says what it shows
  if (const char* sh = std::getenv("UNDERSHELL_SCRIPT_HELPER"); sh && fs::exists(sh)) {
    using namespace undershell;
    fs::create_directories(dir / "txt");
    std::ofstream(dir / "txt/project.json") << R"({"type": "scene"})";
    std::ofstream(dir / "txt/scene.pkg", std::ios::binary)
        << pkg({{"scene.json", R"({"general": {"orthogonalprojection": {"width": 200, "height": 100}, "clearcolor": "0 0 0"},
                   "objects": [{"id": 1, "origin": "100 50 0", "pointsize": 30, "color": "1 1 1", "systemfont": true, "font": "systemfont_arial",
                     "text": {"value": "start", "script": "'use strict';\nexport function update(value) { return 'WALL ' + (value === 'start' ? 'E' : value.length); }"}}]})"}});
    Headless gl;
    if (gl.ok()) {
      SceneWidget s;
      WidgetConfig wc;
      wc.type = "scene";
      wc.options.insert_or_assign("scene", (dir / "txt").string());
      s.configure(wc, testPalette());
      TextRenderer tr;
      Image last;
      bool lit = false;
      for (int k = 0; k < 200 && !lit; ++k) {
        TickContext tc;
        tc.now = 1000 + k * 0.05;
        tc.dt = 0.05;
        s.tick(tc);
        last = renderDrawn(200, 100, [&] {
          DrawContext dc;
          dc.w = 200, dc.h = 100, dc.outputW = 200, dc.outputH = 100, dc.now = tc.now, dc.text = &tr;
          s.draw(dc);
        });
        size_t white = 0;
        for (size_t i = 0; i < last.rgba.size(); i += 4) white += last.rgba[i] > 200 && last.rgba[i + 1] > 200 && last.rgba[i + 2] > 200;
        lit = white > 30;  // the script's words, drawn
        usleep(20000);
      }
      CHECK(lit);
      tr.releaseGl();
    }
  }

  // Wallpaper Engine particles, into undershell's: units of a 1080-high scene with y down
  {
    using namespace undershell;
    const fs::path u = dir / "pt";
    fs::create_directories(u / "particles");
    fs::create_directories(u / "materials");
    std::ofstream(u / "scene.json") << R"({"general": {"orthogonalprojection": {"width": 1920, "height": 1080}},
      "objects": [{"id": 5, "particle": "particles/p.json", "origin": "960 540 0", "instanceoverride": {"rate": 2, "size": 0.5}},
                  {"id": 6, "particle": "particles/r.json", "origin": "0 0 0"}]})";
    std::ofstream(u / "particles/p.json") << R"({"maxcount": 300, "material": "materials/pm.json",
      "emitter": [{"name": "sphererandom", "rate": 10, "distancemax": 108, "distancemin": 0, "origin": "0 54 0"}],
      "initializer": [{"name": "lifetimerandom", "min": 2, "max": 4}, {"name": "sizerandom", "min": 20, "max": 40},
                      {"name": "velocityrandom", "min": "-10 50 0", "max": "10 100 0"}, {"name": "colorrandom", "min": "255 0 0", "max": "255 255 0"}],
      "operator": [{"name": "movement", "gravity": "0 -50 0"}, {"name": "alphafade", "fadeintime": 0.2, "fadeouttime": 0.5}],
      "renderer": [{"name": "sprite"}]})";
    std::ofstream(u / "materials/pm.json") << R"({"passes": [{"blending": "additive", "textures": ["dot"]}]})";
    std::ofstream(u / "materials/dot.tex-json") << R"({"spritesheetsequences": [{"frames": 4, "width": 2, "height": 2, "duration": 2}]})";
    std::ofstream(u / "particles/r.json") << R"({"material": "materials/rm.json", "emitter": [{"name": "boxrandom"}]})";
    std::ofstream(u / "materials/rm.json") << R"({"passes": [{"combos": {"REFRACT": 1}, "textures": ["dot"]}]})";
    WeProject proj;
    WeScene sc;
    std::string why;
    CHECK(readWeScene(u.string(), "", proj, sc, why));
    CHECK(sc.particles.size() == 1);  // the refracting one is left out
    if (!sc.particles.empty()) {
      const ParticleDef& d = sc.particles[0];
      CHECK(d.sprite == ParticleDef::Texture && d.texture == "materials/dot" && d.additive);
      CHECK(d.rate == 20 && d.max == 300 && d.frames == 4);
      CHECK(std::abs(d.ox - 0.5F) < 1e-3 && std::abs(d.oy - (540 - 54) / 1080.0F) < 1e-3);  // the centre, 54 up
      CHECK(std::abs(d.radius - 0.1F) < 1e-3);
      CHECK(d.size[0] == 10 && d.size[1] == 20);                         // halved by the override
      CHECK(d.velBox && d.vel[1] == -100 && d.vel[3] == -50);            // rising in the scene: up here too (y down)
      CHECK(d.c0.r == 1 && d.c0.g == 0 && d.c1.g == 1);
      bool gravity = false, fade = false;
      for (const auto& op : d.ops) {
        if (op.type == ParticleOp::Gravity) gravity = op.v[1] == 50;     // falling: down
        if (op.type == ParticleOp::Fade) fade = op.v[0] == 0.2F && op.v[1] == 0.5F;
      }
      CHECK(gravity && fade);
    }
    CHECK(std::ranges::find(sc.needs, "materials/dot.tex") != sc.needs.end());
  }

  fs::remove_all(dir);
  return TEST_RESULT();
}
