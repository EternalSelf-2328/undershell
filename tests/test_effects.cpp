// Wallpaper effects: the header format, what it refuses, the renaming that
// keeps two effects apart, the person's own folder, the inspector rows, and
// (with GL) that every built-in compiles and draws.
#include "check.hpp"
#include "effects.hpp"
#include "effectchain.hpp"
#include "filter.hpp"
#include "widget.hpp"
#include "noctalia.hpp"
#include "offscreen.hpp"
#include "text.hpp"

#include <cmath>

#include <GLES3/gl3.h>

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

// Exercise the factory wrapper with real audio, not just the bare Visualizer.
static Image effectVisualizer(const toml::table& opts) {
  auto widget = createWidget("visualizer");
  WidgetConfig wc;
  wc.type = "visualizer";
  wc.options = opts;
  widget->configure(wc, testPalette());
  std::vector<float> bands(64);
  for (int i = 0; i < 64; ++i) bands[i] = static_cast<float>(0.12 + 0.65 * std::exp(-i / 24.0));
  for (int i = 0; i < 90; ++i) {
    TickContext tc;
    tc.now = 1000 + i / 60.0;
    tc.dt = 1.0 / 60;
    tc.audio.bands = &bands;
    tc.audio.energy = 0.4;
    tc.audio.silent = false;
    widget->tick(tc);
  }
  TextRenderer text;
  Image img = renderDrawn(320, 180, [&] {
    DrawContext dc;
    dc.w = dc.outputW = 320;
    dc.h = dc.outputH = 180;
    dc.now = 1001.5;
    dc.text = &text;
    widget->draw(dc);
  });
  text.releaseGl();
  return img;
}

int main() {
  std::string pattern = (fs::temp_directory_path() / "undershell-effects-XXXXXX").string();
  const char* unique = mkdtemp(pattern.data());
  CHECK(unique);
  if (!unique) return TEST_RESULT();
  const fs::path dir(unique);
  fs::create_directories(dir / "undershell/effects");
  setenv("XDG_CONFIG_HOME", dir.c_str(), 1);  // never the person's own folders
  setenv("XDG_DATA_HOME", dir.c_str(), 1);
  const std::string fxDir = (dir / "undershell/effects").string();
  CHECK(userEffectsDir() == fxDir);

  // the built-ins: all well formed, warps first
  EffectLibrary& lib = effectLibrary();
  CHECK(lib.all().size() == 18);
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
  CHECK(lib.find("reflection") && lib.find("reflection")->timeWhen == "water");
  // A mirror sleeps; switching to water on the same chain requests frames.
  {
    EffectChain chain;
    toml::table mirror{{"fx_reflection", true}};
    chain.configure(mirror, testPalette());
    CHECK(!chain.empty() && !chain.moving() && !chain.audio());
    mirror.insert_or_assign("fx_reflection_water", true);
    chain.configure(mirror, testPalette());
    CHECK(chain.moving());
    mirror.insert_or_assign("fx_reflection_water", false);
    chain.configure(mirror, testPalette());
    chain.tick(TickContext{});
    CHECK(!chain.moving());
  }

  CHECK(lib.find("reflection") && lib.find("reflection")->rgba);
  const EffectSpec rgba = parseEffect("rgba", own(R"({"stage":"colour"})",
      "vec4 pixel(vec4 c, vec2 uv) { return wallPixel(uv); }\n"));
  CHECK(rgba.error.empty() && rgba.rgba);
  CHECK(effectCode({&rgba}).colours.find("fx0_pixel") != std::string::npos);

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
  CHECK(!parseEffect("x", own(R"({"stage": "warp", "time_when": "water"})", warp)).error.empty());
  CHECK(!parseEffect("x", own(R"({"stage": "warp", "time_when": "speed", "params": [{"key": "speed"}]})", warp)).error.empty());
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
  CHECK(lib.all().size() == 20);
  CHECK(!lib.refresh());  // nothing changed since

  // only effects that are on and work
  toml::table opts;
  opts.insert_or_assign("fx_waves", true);
  opts.insert_or_assign("fx_broken", true);
  opts.insert_or_assign("fx_mist", false);
  const auto on = enabledEffects(opts);
  CHECK(on.size() == 1 && on[0]->id == "waves");

  // the rows: only the effects that are on, each with its options and a
  // button that takes it away; "+ Add effect" for the others
  std::vector<PropSpec> rows = effectRows(opts, false);
  CHECK(!rows.empty() && rows[0].kind == PropSpec::Header);
  CHECK(hasRow(rows, removeKey("fx_waves")) && hasRow(rows, "fx_waves_speed") && hasRow(rows, "fx_waves_strength") &&
        hasRow(rows, "fx_waves_mask"));
  CHECK(!hasRow(rows, "fx_mist") && !hasRow(rows, removeKey("fx_mist")) && !hasRow(rows, "fx_waves"));
  CHECK(!hasRow(rows, "fx_mist_speed") && !hasRow(rows, "fx_waves_split") && hasRow(rows, "fx_fps"));
  CHECK(rows.back().key == "_fx_add" && rows.back().kind == PropSpec::Action);
  for (const auto& r : rows)
    if (r.key == removeKey("fx_broken")) CHECK(r.labelEn.find("(error)") != std::string::npos);
  // every built-in effect is in one of the picker's groups
  for (const EffectSpec& e : builtinEffects())
    CHECK(std::ranges::any_of(kEffectGroups, [&](const PickerGroup& g) { return *g.id && e.category == g.id; }));
  opts.insert_or_assign("fx_waves_mask", "below");
  CHECK(hasRow(effectRows(opts, false), "fx_waves_split"));
  CHECK(!hasRow(effectRows(toml::table{}, false), "fx_fps"));
  CHECK(hasRow(effectRows(toml::table{}, true), "fx_godrays_tint"));  // every default, for lookups
  for (const auto& r : effectRows(toml::table{}, true))
    if (r.key == "fx_pulse_source") CHECK(r.optionNames.size() == 4 && r.optionNames[0].second == "Graves");
  // in a widget's own box: everywhere or a line only
  for (const auto& r : effectRows(opts, false, true))
    if (r.key == "fx_waves_mask") CHECK(r.options.size() == 3 && r.optionNames[1].second == "Arriba de línea");
  CHECK(!hasRow(effectRows(opts, false, true), "fx_waves_paint"));
  // the wrapper keeps what the widget is: a note can still be typed on
  {
    auto note = createWidget("notes");
    WidgetConfig wc;
    wc.type = "notes";
    wc.id = "n";
    note->configure(wc, testPalette());
    CHECK(note->editable());
  }
  // a painted mask: its button opens the brush on that mask
  opts.insert_or_assign("fx_waves_mask", "paint3");
  bool button = false;
  for (const auto& r : effectRows(opts, false))
    if (r.key == "fx_waves_paint") button = r.kind == PropSpec::Action && r.defText == "paint3";
  CHECK(button && !hasRow(effectRows(opts, false), "fx_waves_split"));
  for (const auto& r : effectRows(opts, false))
    if (r.key == "fx_waves_mask") CHECK(r.options.size() == 9 && r.optionNames.size() == 9 && r.optionNames[7].second == "Pintura 3");

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
    // neutral and with no effects it draws nothing; one effect on, and it does
    CHECK(renderWidget("filter", plain, 160, 90, pal, 1000, 1, 3.0, nullptr, png).rgba[3] == 0);
    toml::table waves = plain;
    waves.insert_or_assign("fx_waves", true);
    CHECK(renderWidget("filter", waves, 160, 90, pal, 1000, 1, 3.0, nullptr, png).rgba[3] == 255);
    plain.insert_or_assign("brightness", 0.001);  // the picture, as good as untouched
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
    for (const char* id : {"waves", "ripple", "flow", "sway", "droplets", "reflection", "chroma", "vhs", "godrays", "aurora", "mist", "grain"}) {
      toml::table one = plain;
      one.insert_or_assign(effectKey(id), true);
      const Image img = renderWidget("filter", one, 160, 90, pal, 1000, 1, 3.0, nullptr, png);
      const double d = imageDiff(img, base);
      if (!(d > 0.2)) std::fprintf(stderr, "%s changed the picture by %.3f\n", id, d);
      CHECK(d > 0.2);
    }
    // An asymmetric image proves the mirror reads ABOVE its upper edge,
    // reverses Y, keeps X and leaves every pixel outside its rectangle alone.
    {
      toml::table mirror = plain;
      mirror.insert_or_assign("fx_reflection", true);
      mirror.insert_or_assign("fx_reflection_left", 0.25);
      mirror.insert_or_assign("fx_reflection_width", 0.5);
      mirror.insert_or_assign("fx_reflection_height", 0.4);
      auto draw = [&](const toml::table& o) {
        return renderWidget("filter", o, 160, 90, pal, 1000, 1, 3.0, nullptr, png);
      };
      const Image reflected = draw(mirror);
      double outside = 0, inside = 0;
      for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 160; ++x)
          for (int k = 0; k < 4; ++k) {
            const size_t i = (static_cast<size_t>(y) * 160 + x) * 4 + k;
            if (x >= 40 && x < 120 && y >= 45 && y < 81) {
              const size_t src = (static_cast<size_t>(89 - y) * 160 + x) * 4 + k;
              inside += std::abs(reflected.rgba[i] - base.rgba[src]);
            } else outside += std::abs(reflected.rgba[i] - base.rgba[i]);
          }
      if (!(outside < 1 && inside / (80 * 36 * 4) < 0.6))
        std::fprintf(stderr, "reflection rectangle: outside %.0f, inside mean %.4f\n", outside, inside / (80 * 36 * 4));
      CHECK(outside < 1 && inside / (80 * 36 * 4) < 0.6);
      toml::table weak = mirror;
      weak.insert_or_assign("fx_reflection_strength", 0.0);
      CHECK(imageDiff(draw(weak), base) < 0.05);
      weak.insert_or_assign("fx_reflection_strength", 0.5);
      const Image blended = draw(weak);
      const size_t probe = (60 * 160 + 80) * 4 + 1;
      CHECK(std::abs(blended.rgba[probe] * 2 - reflected.rgba[probe] - base.rgba[probe]) <= 2);
      toml::table empty = mirror;
      empty.insert_or_assign("fx_reflection_width", 0.0);
      CHECK(imageDiff(draw(empty), base) < 0.05);
      // A reflection taller than the available source must not repeat/smear.
      toml::table shortSource = mirror;
      shortSource.insert_or_assign("fx_reflection_top", 0.1);
      shortSource.insert_or_assign("fx_reflection_height", 0.8);
      CHECK(draw(shortSource).rgba[probe] == base.rgba[probe]);
      toml::table water = mirror;
      water.insert_or_assign("fx_reflection_water", true);
      water.insert_or_assign("fx_reflection_bend", 2.0);
      CHECK(imageDiff(draw(water), reflected) > 0.2);
      FilterWidget live;
      WidgetConfig wc;
      wc.type = "filter";
      wc.options = water;
      live.configure(wc, pal);
      auto at = [&](double now) {
        TickContext tick;
        tick.now = now;
        tick.dt = 1.0 / 30;
        live.tick(tick);
        return renderDrawn(160, 90, [&] {
          DrawContext dc;
          dc.w = 160;
          dc.h = 90;
          dc.outputW = 1920;  // same decode bucket as renderWidget above
          dc.outputH = 1080;
          dc.now = now;
          dc.wallpaper = png;
          live.draw(dc);
        });
      };
      const Image water0 = at(1000);
      CHECK(imageDiff(at(1001), water0) > 0.1);
      wc.options = mirror;
      live.configure(wc, pal);
      const Image mirror0 = at(1002);
      CHECK(imageDiff(at(1003), mirror0) < 0.01);
      // The inspector changes the existing widget, including its shoreline.
      toml::table moved = mirror;
      moved.insert_or_assign("fx_reflection_top", 0.6);
      const Image reconfigured = renderWidget("filter", mirror, 160, 90, pal, 1000, 1, 3.0, nullptr, png, 0, 0, 0.5F, 0, &moved);
      CHECK(imageDiff(reconfigured, draw(moved)) < 0.05);
      CHECK(hasRow(effectRows(mirror, false), "fx_reflection_water"));
      CHECK(hasRow(effectRows(mirror, false), "fx_reflection_top"));
    }
    // an option changed on the same widget (the inspector does that) takes
    // effect: the program is kept, its options are set again
    {
      toml::table strong = plain, calm = plain, big = plain;
      strong.insert_or_assign("fx_waves", true);
      calm = strong;
      calm.insert_or_assign("fx_waves_strength", 0.0);
      const Image again = renderWidget("filter", strong, 160, 90, pal, 1000, 1, 3.0, nullptr, png, 0, 0, 0.5F, 0, &calm);
      if (!(imageDiff(again, base) < 0.05)) std::fprintf(stderr, "strength 0 on the same widget: %.3f from the picture\n", imageDiff(again, base));
      CHECK(imageDiff(again, base) < 0.05);
      big = strong;
      big.insert_or_assign("fx_waves_size", 3.5);
      const Image fresh = renderWidget("filter", big, 160, 90, pal, 1000, 1, 3.0, nullptr, png);
      const Image resized = renderWidget("filter", strong, 160, 90, pal, 1000, 1, 3.0, nullptr, png, 0, 0, 0.5F, 0, &big);
      if (!(imageDiff(resized, fresh) < 0.05)) std::fprintf(stderr, "wave size on the same widget: %.3f from a new one\n", imageDiff(resized, fresh));
      CHECK(imageDiff(resized, fresh) < 0.05);
      // and on any widget's own effects (FxWidget wraps them)
      toml::table text;
      text.insert_or_assign("text", "Effects");
      text.insert_or_assign("fx_chroma", true);
      toml::table textCalm = text;
      textCalm.insert_or_assign("fx_chroma_strength", 0.0);
      toml::table textPlain;
      textPlain.insert_or_assign("text", "Effects");
      const Image t0 = renderWidget("text", textPlain, 320, 120, pal);
      const Image t1 = renderWidget("text", text, 320, 120, pal, 1000, 1, 0, nullptr, {}, 0, 0, 0.5F, 0, &textCalm);
      if (!(imageDiff(t1, t0) < 0.05)) std::fprintf(stderr, "text: chroma at strength 0 on the same widget: %.3f\n", imageDiff(t1, t0));
      CHECK(imageDiff(t1, t0) < 0.05);
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
    // a painted mask (channel 2, the lower half): the waves only there
    {
      toml::table painted = plain;
      painted.insert_or_assign("fx_waves", true);
      painted.insert_or_assign("fx_waves_mask", "paint2");
      std::vector<std::uint8_t> rgba(160 * 90 * 4, 0);
      for (int y = 45; y < 90; ++y)
        for (int x = 0; x < 160; ++x) rgba[(static_cast<size_t>(y) * 160 + x) * 4 + 1] = 255;
      GLuint masks = 0;
      glGenTextures(1, &masks);
      glBindTexture(GL_TEXTURE_2D, masks);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 160, 90, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      const Image masked = renderWidget("filter", painted, 160, 90, pal, 1000, 1, 3.0, nullptr, png, masks, 0, 0.5F, 0b0010);
      // nothing painted on this wallpaper, or only another mask: everywhere, as if there were no mask
      const Image unpainted = renderWidget("filter", painted, 160, 90, pal, 1000, 1, 3.0, nullptr, png);
      const Image otherOnly = renderWidget("filter", painted, 160, 90, pal, 1000, 1, 3.0, nullptr, png, masks, 0, 0.5F, 0b0001);
      toml::table everywhere = plain;
      everywhere.insert_or_assign("fx_waves", true);
      const Image whole = renderWidget("filter", everywhere, 160, 90, pal, 1000, 1, 3.0, nullptr, png);
      double up = 0, down = 0;
      for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 160; ++x)
          for (int k = 0; k < 3; ++k) {
            const size_t i = (static_cast<size_t>(y) * 160 + x) * 4 + k;
            const int d = std::abs(masked.rgba[i] - base.rgba[i]);
            if (y < 40) up += d;
            if (y > 50) down += d;
          }
      CHECK(up < 1 && down > 50);
      CHECK(imageDiff(unpainted, whole) < 0.01 && imageDiff(otherOnly, whole) < 0.01 && imageDiff(whole, base) > 0.2);
      glDeleteTextures(1, &masks);
    }
    // a light (drawn on its own smaller surface) lands where its mask says: mist above the line
    {
      toml::table fog = plain;
      fog.insert_or_assign("fx_mist", true);
      fog.insert_or_assign("fx_mist_density", 1.0);
      fog.insert_or_assign("fx_mist_mask", "above");
      fog.insert_or_assign("fx_mist_split", 0.5);
      const Image img = renderWidget("filter", fog, 160, 90, pal, 1000, 1, 3.0, nullptr, png);
      double up = 0, down = 0;
      for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 160; ++x)
          for (int k = 0; k < 3; ++k) {
            const size_t i = (static_cast<size_t>(y) * 160 + x) * 4 + k;
            const int d = std::abs(img.rgba[i] - base.rgba[i]);
            if (y < 35) up += d;
            if (y > 55) down += d;
          }
      if (!(up > 50 && down < 1)) std::fprintf(stderr, "mist above: up %.0f down %.0f\n", up, down);
      CHECK(up > 50 && down < 1);
    }
    // All classic renderers also pass through the real factory wrapper.
    // A lower-half mirror must match the upper half, including coverage.
    for (const char* look : {"bars", "split", "dots", "segments", "wave", "ribbon", "curtain", "line", "frame", "radial", "orb", "spiral"}) {
      toml::table opts{{"style", look}, {"reflection", 0.0}};
      const Image source = effectVisualizer(opts);
      opts.insert_or_assign("fx_reflection", true);
      const Image reflected = effectVisualizer(opts);
      double mismatch = 0, outside = 0;
      for (int y = 0; y < 180; ++y)
        for (int x = 0; x < 320; ++x)
          for (int k = 0; k < 4; ++k) {
            const size_t i = (static_cast<size_t>(y) * 320 + x) * 4 + k;
            const size_t src = (static_cast<size_t>(179 - y) * 320 + x) * 4 + k;
            if (y < 90) outside += std::abs(reflected.rgba[i] - source.rgba[i]);
            else mismatch += std::abs(reflected.rgba[i] - source.rgba[src]);
          }
      if (!(mismatch / (90 * 320 * 4) < 0.6 && outside < 1))
        std::fprintf(stderr, "%s reflection: mismatch %.4f outside %.0f\n", look, mismatch / (90 * 320 * 4), outside);
      CHECK(mismatch / (90 * 320 * 4) < 0.6 && outside < 1);
    }
    // Reflection must carry alpha into empty pixels of a visualizer. A colour
    // comparison on an opaque wallpaper cannot catch this failure.
    {
      toml::table opts{{"style", "bars"}, {"grow", "down"}, {"gain", 0.3},
                       {"glow", 0.0}, {"reflection", 0.0}};
      const Image plainViz = effectVisualizer(opts);
      opts.insert_or_assign("fx_reflection", true);
      const Image mirrored = effectVisualizer(opts);
      double originalBottom = 0, reflectedBottom = 0, mismatch = 0;
      for (int y = 92; y < 178; ++y)
        for (int x = 0; x < 320; ++x) {
          const size_t i = (static_cast<size_t>(y) * 320 + x) * 4;
          const size_t src = (static_cast<size_t>(179 - y) * 320 + x) * 4;
          originalBottom += plainViz.rgba[i + 3];
          reflectedBottom += mirrored.rgba[i + 3];
          for (int k = 0; k < 4; ++k) mismatch += std::abs(mirrored.rgba[i + k] - plainViz.rgba[src + k]);
        }
      CHECK(originalBottom == 0 && reflectedBottom > 1000);
      CHECK(mismatch / (86 * 320 * 4) < 0.6);
      toml::table translucent = opts;
      translucent.insert_or_assign("opacity", 0.5);
      const Image half = effectVisualizer(translucent);
      double halfAlpha = 0;
      for (int y = 92; y < 178; ++y)
        for (int x = 0; x < 320; ++x) halfAlpha += half.rgba[(static_cast<size_t>(y) * 320 + x) * 4 + 3];
      CHECK(reflectedBottom > 0 && halfAlpha / reflectedBottom > 0.45 && halfAlpha / reflectedBottom < 0.55);
      opts.insert_or_assign("fx_reflection_strength", 0.0);
      CHECK(imageDiff(effectVisualizer(opts), plainViz) < 0.05);
      opts.insert_or_assign("fx_reflection_strength", 0.5);
      const Image blend = effectVisualizer(opts);
      double blendAlpha = 0;
      for (int y = 92; y < 178; ++y)
        for (int x = 0; x < 320; ++x) blendAlpha += blend.rgba[(static_cast<size_t>(y) * 320 + x) * 4 + 3];
      CHECK(reflectedBottom > 0 && blendAlpha / reflectedBottom > 0.45 && blendAlpha / reflectedBottom < 0.55);
      opts.insert_or_assign("fx_reflection_strength", 1.0);
      opts.insert_or_assign("fx_reflection_water", true);
      CHECK(imageDiff(effectVisualizer(opts), mirrored) > 0.05);
    }
    // any other widget: drawn into its own texture, then through the effects
    {
      toml::table sq;
      sq.insert_or_assign("shape", "rect");
      const Image still = renderWidget("shape", sq, 120, 120, pal);
      sq.insert_or_assign("fx_waves", true);
      sq.insert_or_assign("fx_waves_strength", 2.0);
      const Image moved = renderWidget("shape", sq, 120, 120, pal);
      CHECK(still.w == 120 && moved.w == 120 && imageDiff(moved, still) > 0.2);
      sq.insert_or_assign("fx_waves_mask", "below");
      sq.insert_or_assign("fx_waves_split", 0.5);
      const Image half = renderWidget("shape", sq, 120, 120, pal);
      double up = 0;
      for (int y = 0; y < 50; ++y)
        for (int x = 0; x < 120; ++x)
          for (int k = 0; k < 4; ++k) up += std::abs(half.rgba[(static_cast<size_t>(y) * 120 + x) * 4 + k] - still.rgba[(static_cast<size_t>(y) * 120 + x) * 4 + k]);
      CHECK(up < 200);  // the top as it was (upright, not flipped)
      sq.erase("fx_waves");
      CHECK(imageDiff(renderWidget("shape", sq, 120, 120, pal), still) < 0.01);  // nothing on: drawn straight
    }
    // an effect that resamples the picture (VHS) reads it straight: a widget at
    // half opacity comes out at half the brightness, not a quarter
    {
      auto green = [&](double opacity) {
        toml::table t;
        t.insert_or_assign("shape", "rect");
        t.insert_or_assign("fill", "#40c080");
        t.insert_or_assign("opacity", opacity);
        t.insert_or_assign("fx_vhs", true);
        t.insert_or_assign("fx_vhs_strength", 1.0);
        t.insert_or_assign("fx_vhs_snow", 0.0);
        const Image img = renderWidget("shape", t, 120, 120, pal);
        double sum = 0;
        for (int y = 40; y < 80; ++y)
          for (int x = 40; x < 80; ++x) sum += img.rgba[(static_cast<size_t>(y) * 120 + x) * 4 + 1];
        return sum / 1600;
      };
      const double ratio = green(0.5) / green(1.0);
      if (!(ratio > 0.45 && ratio < 0.55)) std::fprintf(stderr, "vhs at half opacity: %.3f of opaque\n", ratio);
      CHECK(ratio > 0.45 && ratio < 0.55);
    }
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
