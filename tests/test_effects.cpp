// Wallpaper effects: the header format, what it refuses, the renaming that
// keeps two effects apart, the person's own folder, the inspector rows, and
// (with GL) that every built-in compiles and draws.
#include "check.hpp"
#include "effects.hpp"
#include "noctalia.hpp"
#include "offscreen.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <unistd.h>

using namespace undershell;
namespace fs = std::filesystem;

static std::string own(const std::string& header, const std::string& glsl) {
  return "/* undershell-effect\n" + header + "\n*/\n" + glsl;
}

static bool hasRow(const std::vector<PropSpec>& rows, const std::string& key) {
  for (const auto& r : rows)
    if (r.key == key) return true;
  return false;
}

int main() {
  const fs::path dir = fs::temp_directory_path() / ("undershell-effects-" + std::to_string(getpid()));
  fs::create_directories(dir / "undershell/effects");
  setenv("XDG_CONFIG_HOME", dir.c_str(), 1);  // never the person's own folder
  const std::string fxDir = (dir / "undershell/effects").string();
  CHECK(userEffectsDir() == fxDir);

  // the built-ins: all well formed, warps first
  EffectLibrary& lib = effectLibrary();
  CHECK(lib.all().size() == 13);
  std::set<std::string> ids;
  int stage = 0;
  bool ordered = true;
  for (const auto& e : lib.all()) {
    if (!e.error.empty()) std::fprintf(stderr, "%s: %s\n", e.id.c_str(), e.error.c_str());
    CHECK(e.error.empty());
    CHECK(ids.insert(e.id).second);
    CHECK(e.nameEs != e.id && e.nameEn != e.id);
    ordered = ordered && e.stage >= stage;  // warps, colours, lights
    stage = e.stage;
  }
  CHECK(ordered);
  CHECK(lib.find("parallax") && lib.find("parallax")->depth && lib.find("parallax")->audioWhen == "beat");
  CHECK(lib.find("pulse") && lib.find("pulse")->audio && !lib.find("pulse")->time);
  CHECK(lib.find("godrays") && lib.find("godrays")->stage == EffectSpec::Light);

  // what the format refuses
  const std::string warp = "vec2 warp(vec2 uv) { return uv; }\n";
  CHECK(!parseEffect("x", warp).error.empty());                                                // no header
  CHECK(!parseEffect("x", own("{ nope", warp)).error.empty());                                 // not JSON
  CHECK(!parseEffect("x", own(R"({"stage": "blur"})", warp)).error.empty());                   // no such stage
  CHECK(!parseEffect("Bad-Name", own(R"({"stage": "warp"})", warp)).error.empty());            // file name
  CHECK(!parseEffect("x", own(R"({"stage": "colour"})", warp)).error.empty());                 // no color()
  CHECK(!parseEffect("x", own(R"({"stage": "light"})", warp)).error.empty());                  // no light()
  CHECK(!parseEffect("x", own(R"({"stage": "warp", "params": [{"key": "length"}]})", warp)).error.empty());  // a GLSL name
  CHECK(!parseEffect("x", own(R"({"stage": "warp", "params": [{"key": "mask"}]})", warp)).error.empty());    // its own
  CHECK(!parseEffect("x", own(R"({"stage": "warp"})", "uniform float k;\n" + warp)).error.empty());
  CHECK(!parseEffect("x", own(R"({"stage": "warp"})", "#define K 2.0\n" + warp)).error.empty());
  CHECK(!parseEffect("x", own(R"({"stage": "warp", "audio_when": "beat"})", warp)).error.empty());
  CHECK(!parseEffect("x", own(R"({"stage": "warp"})", "vec2 warp(vec2 uv) { return uv;\n")).error.empty());
  const EffectSpec ok = parseEffect("x", own(R"({"stage": "warp", "name": "Plain",
      "params": [{"key": "k", "kind": "number", "min": 0, "max": 2, "default": 5},
                 {"key": "pick", "kind": "choice", "options": ["a", "b"], "default": "zz"}]})", warp));
  CHECK(ok.error.empty() && ok.nameEs == "Plain" && ok.params.size() == 2);
  CHECK(ok.params[0].def == 2);            // clamped into its range
  CHECK(ok.params[1].defText == "a");      // an unknown default falls back to the first

  // two effects with the same helper names stay apart; swizzles are left alone
  const std::string helper = "const float K = 0.5;\nfloat wobble(float x) { return x * K; }\n"
                             "vec2 warp(vec2 uv) { return uv + vec2(wobble(uv.x), k); }\n";
  const EffectSpec a = parseEffect("a", own(R"({"stage": "warp", "params": [{"key": "k"}]})", helper));
  const EffectSpec b = parseEffect("b", own(R"({"stage": "warp", "params": [{"key": "k"}]})", helper));
  CHECK(a.error.empty() && b.error.empty());
  const EffectCode code = effectCode({&a, &b});
  CHECK(code.decls.find("float fx0_wobble(") != std::string::npos && code.decls.find("float fx1_wobble(") != std::string::npos);
  CHECK(code.decls.find("uniform float fx1_k;") != std::string::npos && code.decls.find("fx1_wobble(uv.x)") != std::string::npos);
  CHECK(code.decls.find(".fx") == std::string::npos);
  CHECK(code.warps.find("fx0_warp(uv)") != std::string::npos && code.warps.find("fxo1_strength") != std::string::npos);
  CHECK(code.colours.empty() && code.lights.empty());

  // the person's own folder: a new effect, one that replaces a built-in, one broken
  std::ofstream(dir / "undershell/effects/tilt.glsl") << own(R"({"stage": "warp", "name": {"en": "Tilt", "es": "Inclinar"}})", warp);
  std::ofstream(dir / "undershell/effects/grain.glsl") << own(R"({"stage": "colour", "name": "My grain"})",
                                                              "vec3 color(vec3 c, vec2 uv) { return c * 0.5; }\n");
  std::ofstream(dir / "undershell/effects/broken.glsl") << "no header here";
  CHECK(lib.refresh());
  CHECK(lib.find("tilt") && lib.find("tilt")->nameEs == "Inclinar" && !lib.find("tilt")->file.empty());
  CHECK(lib.find("grain") && lib.find("grain")->nameEn == "My grain" && !lib.find("grain")->file.empty());
  CHECK(lib.find("broken") && !lib.find("broken")->error.empty());
  CHECK(lib.all().size() == 15);
  CHECK(!lib.refresh());  // nothing changed since

  // only effects that are on and work
  toml::table opts;
  opts.insert_or_assign("fx_waves", true);
  opts.insert_or_assign("fx_broken", true);
  opts.insert_or_assign("fx_mist", false);
  const auto on = enabledEffects(opts);
  CHECK(on.size() == 1 && on[0]->id == "waves");

  // the rows: a switch per effect; the options of those that are on
  std::vector<PropSpec> rows = effectRows(opts, false);
  CHECK(!rows.empty() && rows[0].kind == PropSpec::Header);
  CHECK(hasRow(rows, "fx_mist") && hasRow(rows, "fx_waves_speed") && hasRow(rows, "fx_waves_strength") && hasRow(rows, "fx_waves_mask"));
  CHECK(!hasRow(rows, "fx_mist_speed") && !hasRow(rows, "fx_waves_split") && hasRow(rows, "fx_fps"));
  for (const auto& r : rows)
    if (r.key == "fx_broken") CHECK(r.labelEn.find("(error)") != std::string::npos);
  opts.insert_or_assign("fx_waves_mask", "below");
  CHECK(hasRow(effectRows(opts, false), "fx_waves_split"));
  CHECK(!hasRow(effectRows(toml::table{}, false), "fx_fps"));
  CHECK(hasRow(effectRows(toml::table{}, true), "fx_godrays_tint"));  // every default, for lookups
  for (const auto& r : effectRows(toml::table{}, true))
    if (r.key == "fx_pulse_source") CHECK(r.optionNames.size() == 4 && r.optionNames[0].second == "Graves");

  // with GL: every built-in compiles and draws on a picture, alone and all together
  Headless gl;
  if (gl.ok()) {
    fs::remove(dir / "undershell/effects/grain.glsl");
    fs::remove(dir / "undershell/effects/broken.glsl");
    lib.refresh();
    Image wall;
    wall.w = 160, wall.h = 90;
    wall.rgba.assign(160 * 90 * 4, 255);
    for (int y = 0; y < 90; ++y)
      for (int x = 0; x < 160; ++x) {
        uint8_t* p = &wall.rgba[(static_cast<size_t>(y) * 160 + x) * 4];
        p[0] = static_cast<uint8_t>(x * 255 / 159), p[1] = static_cast<uint8_t>(y * 255 / 89), p[2] = ((x / 8 + y / 8) % 2) ? 230 : 20;
      }
    const std::string png = (dir / "wall.png").string();
    CHECK(writePng(wall, png, false));
    const NoctaliaState pal = testPalette();
    toml::table plain;
    plain.insert_or_assign("style", "custom");
    const Image base = renderWidget("filter", plain, 160, 90, pal, 1000, 1, 3.0, nullptr, png);
    CHECK(base.w == 160 && base.rgba[3] == 255);
    toml::table all = plain;
    for (const auto& e : lib.all()) {
      toml::table one = plain;
      one.insert_or_assign(effectKey(e.id), true);
      all.insert_or_assign(effectKey(e.id), true);
      const Image img = renderWidget("filter", one, 160, 90, pal, 1000, 30, 3.0, nullptr, png);
      if (!lib.brokenWhy(e).empty()) std::fprintf(stderr, "%s: %s\n", e.id.c_str(), lib.brokenWhy(e).c_str());
      CHECK(lib.brokenWhy(e).empty());
      CHECK(img.w == 160 && img.rgba[(45 * 160 + 80) * 4 + 3] == 255);
    }
    // the ones that do something with no music and no depth change the picture
    for (const char* id : {"waves", "ripple", "flow", "sway", "chroma", "vhs", "godrays", "mist", "grain"}) {
      toml::table one = plain;
      one.insert_or_assign(effectKey(id), true);
      const Image img = renderWidget("filter", one, 160, 90, pal, 1000, 1, 3.0, nullptr, png);
      const double d = imageDiff(img, base);
      if (!(d > 0.2)) std::fprintf(stderr, "%s changed the picture by %.3f\n", id, d);
      CHECK(d > 0.2);
    }
    // strength 0 and a mask that covers nothing leave it as it was
    toml::table none = plain;
    none.insert_or_assign("fx_waves", true);
    none.insert_or_assign("fx_waves_strength", 0.0);
    CHECK(imageDiff(renderWidget("filter", none, 160, 90, pal, 1000, 1, 3.0, nullptr, png), base) < 0.05);
    none.insert_or_assign("fx_waves_strength", 1.0);
    none.insert_or_assign("fx_waves_mask", "subject");  // no depth here: no subject
    CHECK(imageDiff(renderWidget("filter", none, 160, 90, pal, 1000, 1, 3.0, nullptr, png), base) < 0.05);
    none.insert_or_assign("fx_waves_mask", "below");
    none.insert_or_assign("fx_waves_split", 0.5);
    const Image half = renderWidget("filter", none, 160, 90, pal, 1000, 1, 3.0, nullptr, png);
    double top = 0, bottom = 0;
    for (int y = 0; y < 90; ++y)
      for (int x = 0; x < 160; ++x)
        for (int k = 0; k < 3; ++k) {
          const size_t i = (static_cast<size_t>(y) * 160 + x) * 4 + k;
          const int d = std::abs(half.rgba[i] - base.rgba[i]);
          if (y < 38) top += d;  // above the line's soft edge
          if (y > 52) bottom += d;
        }
    CHECK(top < 1 && bottom > 50);
    // all together in one pass
    const Image every = renderWidget("filter", all, 160, 90, pal, 1000, 30, 3.0, nullptr, png);
    CHECK(every.w == 160 && every.rgba[(45 * 160 + 80) * 4 + 3] == 255);
    // an own effect that does not compile is found, named and left out; the rest still draws
    std::ofstream(dir / "undershell/effects/oops.glsl") << own(R"({"stage": "colour"})", "vec3 color(vec3 c, vec2 uv) { return c * nothing; }\n");
    lib.refresh();
    toml::table bad = plain;
    bad.insert_or_assign("fx_oops", true);
    bad.insert_or_assign("fx_mist", true);
    const Image still = renderWidget("filter", bad, 160, 90, pal, 1000, 1, 3.0, nullptr, png);
    CHECK(lib.find("oops") && !lib.brokenWhy(*lib.find("oops")).empty());
    CHECK(still.w == 160 && still.rgba[3] == 255 && imageDiff(still, base) > 0.2);
  } else {
    std::fputs("no surfaceless EGL: GL part skipped\n", stderr);
  }
  fs::remove_all(dir);
  return TEST_RESULT();
}
