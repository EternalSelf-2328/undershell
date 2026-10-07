// Rendering regression: every look against its golden image, and text.
// Regenerate goldens with:  build/test_render --update
#include "check.hpp"
#include "clock.hpp"
#include "media.hpp"
#include "offscreen.hpp"
#include "canvas.hpp"
#include "m3shapes.hpp"
#include "overlay.hpp"
#include "text.hpp"

#include <stdexcept>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>

using namespace undershell;

// Golden images pin the pixels of the machine they were made on; another GPU,
// Mesa or Pango version draws a little differently and is not a bug. So a
// mismatch is reported, and only fails with UNDERSHELL_GOLDEN=strict (on the
// machine that made them). Everything else here (GL errors, shaders, empty
// drawings, glow edges) always fails.
static int g_goldenMismatches = 0;
static bool strictGolden() {
  const char* s = std::getenv("UNDERSHELL_GOLDEN");
  return s && std::string(s) == "strict";
}
#define GOLDEN(d)                                  \
  do {                                             \
    const bool ok_ = (d) >= 0 && (d) <= 1.5;       \
    if (strictGolden()) CHECK(ok_);                \
    else if (!ok_) ++g_goldenMismatches;           \
  } while (0)

int main(int argc, char** argv) {
  const bool update = argc > 1 && !std::strcmp(argv[1], "--update");
  Headless gl;
  if (!gl.ok()) {
    std::fputs("no surfaceless EGL: skipping\n", stderr);
    return 77;  // meson: skipped
  }
  const std::string golden = std::string(US_SOURCE_DIR) + "/tests/golden";
  const NoctaliaState pal = testPalette();
  for (const char* look : {"bars", "split", "dots", "segments", "wave", "ribbon", "curtain", "line", "frame", "radial", "orb", "spiral",
                           "halo", "vortex", "fire"}) {
    const bool polar = !std::strcmp(look, "radial") || !std::strcmp(look, "orb") || !std::strcmp(look, "spiral") ||
                       !std::strcmp(look, "halo") || !std::strcmp(look, "vortex");
    const int w = !std::strcmp(look, "frame") ? 480 : (polar ? 240 : 480);
    const int h = !std::strcmp(look, "frame") ? 270 : (polar ? 240 : 160);
    Image img = renderLook(look, w, h, pal);
    size_t lit = 0;
    for (size_t i = 3; i < img.rgba.size(); i += 4) lit += img.rgba[i] > 8;
    CHECK(lit > static_cast<size_t>(w * h / 200));  // something was drawn
    const std::string path = golden + "/" + look + ".png";
    if (update) {
      std::filesystem::create_directories(golden);
      writePng(img, path, false);
      continue;
    }
    Image ref;
    CHECK(readPng(path, ref));
    const double d = imageDiff(img, ref);
    if (d < 0 || d > 1.5) std::fprintf(stderr, "look %s differs from golden: %.3f\n", look, d);
    GOLDEN(d);
  }

  // clock faces at a fixed moment (Sat 2026-09-26 21:07:42 local), English
  // names and no weather, so the goldens do not depend on this machine
  setenv("TZ", "UTC", 1);
  tzset();
  for (const auto& face : ClockWidget::faces()) {
    ClockConfig cc;
    cc.face = face;
    cc.seconds = true;
    cc.clock24 = face != "banner";  // exercise the AM/PM path once
    cc.language = "en";
    cc.weather = false;
    cc.date = face == "analog" ? "stacked" : (face == "rings" ? "badge" : "inline");
    const bool tall = face == "goodnight", square = face == "analog" || face == "rings";
    const int w = tall ? 216 : (square ? 240 : 480), h = tall ? 384 : (square ? 276 : 190);
    Image img = renderClock(cc, w, h, pal, 1790457462);
    size_t lit = 0;
    for (size_t i = 3; i < img.rgba.size(); i += 4) lit += img.rgba[i] > 8;
    if (lit <= static_cast<size_t>(w * h / 100)) std::fprintf(stderr, "clock %s drew almost nothing\n", face.c_str());
    CHECK(lit > static_cast<size_t>(w * h / 100));
    const std::string path = golden + "/clock-" + face + ".png";
    if (update) {
      writePng(img, path, false);
      continue;
    }
    Image ref;
    CHECK(readPng(path, ref));
    const double d = imageDiff(img, ref);
    if (d < 0 || d > 1.5) std::fprintf(stderr, "clock %s differs from golden: %.3f\n", face.c_str(), d);
    GOLDEN(d);
  }

  // a bright glow fades out before the surface edge: no hard rectangle
  // around a small widget (the line look at full glow)
  for (const char* look : {"line", "wave", "bars"}) {
    toml::table opts;
    opts.insert_or_assign("glow", 1.0);
    opts.insert_or_assign("thickness", 1.0);
    opts.insert_or_assign("bars", 40);  // wide bands: a wide glow
    const int w = 385, h = 94;
    const Image img = renderLook(look, w, h, pal, &opts);
    int edgeMax = 0;
    for (int x = 0; x < w; ++x)
      for (int y : {0, h - 1}) edgeMax = std::max<int>(edgeMax, img.rgba[(static_cast<size_t>(y) * w + x) * 4 + 3]);
    for (int y = 0; y < h; ++y)
      for (int x : {0, w - 1}) edgeMax = std::max<int>(edgeMax, img.rgba[(static_cast<size_t>(y) * w + x) * 4 + 3]);
    if (edgeMax > 4) std::fprintf(stderr, "%s: glow reaches the surface edge (alpha %d)\n", look, edgeMax);
    CHECK(edgeMax <= 4);
  }

  // the muzzle flash: a shot frozen in time. It starts at the left (the
  // muzzle), burns brighter there than at the far right, is gone after two
  // seconds, and the manga drawing is only white, black and grey
  {
    auto shot = [&](double age, const char* style) {
      toml::table o;
      o.insert_or_assign("muzzle_preview", age);
      o.insert_or_assign("muzzle_style", style);
      return renderLook("muzzle", 480, 200, pal, &o);
    };
    auto alphaSum = [](const Image& im, int x0, int x1) {
      double sum = 0;
      for (int y = 0; y < im.h; ++y)
        for (int x = x0; x < x1; ++x) sum += im.rgba[(static_cast<size_t>(y) * im.w + x) * 4 + 3];
      return sum;
    };
    const Image flash = shot(0.03, "flash");
    CHECK(alphaSum(flash, 0, 120) > 4 * alphaSum(flash, 360, 480));  // hottest at the muzzle
    CHECK(alphaSum(flash, 0, 480) > 480.0 * 200 * 255 * 0.03);      // a real burst
    const Image gone = shot(1.9, "flash");
    CHECK(alphaSum(gone, 0, 480) < alphaSum(flash, 0, 480) * 0.05);
    const Image manga = shot(0.03, "manga");
    bool inkOnly = true;
    for (size_t i = 0; i < manga.rgba.size(); i += 4) {
      const int r = manga.rgba[i], g = manga.rgba[i + 1], b = manga.rgba[i + 2];
      if (std::abs(r - g) > 3 || std::abs(g - b) > 3) inkOnly = false;  // greys only
    }
    CHECK(inkOnly);
    struct G {
      const char* name;
      double age;
      const char* style;
    };
    for (const G& gg : {G{"muzzle-flash", 0.03, "flash"}, G{"muzzle-manga", 0.03, "manga"}, G{"muzzle-smoke", 0.3, "manga"}}) {
      const Image img = shot(gg.age, gg.style);
      const std::string path = golden + "/" + gg.name + ".png";
      if (update) {
        writePng(img, path, false);
      } else {
        Image ref;
        CHECK(readPng(path, ref));
        const double d = imageDiff(img, ref);
        if (d < 0 || d > 1.5) std::fprintf(stderr, "%s differs from golden: %.3f\n", gg.name, d);
        GOLDEN(d);
      }
    }
  }

  // the line floats in its box: its glow fades below it too, with no cut
  // where a bar look's root would be
  {
    toml::table opts;
    opts.insert_or_assign("glow", 0.55);
    opts.insert_or_assign("thickness", 1.0);
    opts.insert_or_assign("bars", 40);
    const int w = 385, h = 94;
    const Image img = renderLook("line", w, h, pal, &opts);
    auto alpha = [&](int x, int y) { return static_cast<int>(img.rgba[(static_cast<size_t>(y) * w + x) * 4 + 3]); };
    int worst = 0;
    for (int x = 40; x < w - 40; x += 7) {
      int peak = 0;
      for (int y = 0; y < h; ++y)
        if (alpha(x, y) > alpha(x, peak)) peak = y;
      for (int y = peak; y < h - 1; ++y)
        if (alpha(x, y) < 14) worst = std::max(worst, alpha(x, y) - alpha(x, y + 1));  // the faint skirt
    }
    if (worst > 5) std::fprintf(stderr, "line: its glow is cut below it (a drop of %d in one row)\n", worst);
    CHECK(worst <= 5);
  }

  // the now-playing card: synced lyrics, the no-lyrics spectrum, nothing
  {
    MediaState st;
    st.present = true;
    st.playing = true;
    st.title = "Canción de prueba";
    st.artist = "Banda Imaginaria";
    st.lengthUs = 208'000'000;
    st.positionUs = 40'000'000;
    st.positionAt = 500;
    st.canNext = st.canPrev = st.canToggle = st.canSeek = true;
    st.hasAccent = true;
    st.accent = Color::fromHex("#e0654a");
    st.lyrics = MediaState::Lyrics::Synced;
    // invented lines (accents and ñ exercise the text path)
    st.lines = parseLrc("[00:31.84]La luz se queda quieta en la ventana\n[00:36.05]y el reloj no sabe esperar\n"
                        "[00:39.50]Canta bajito la ciudad\n[00:43.10]mientras el año vuelve a empezar, otra canción\n[00:48.00]\n");
    MediaState viz = st;
    viz.lyrics = MediaState::Lyrics::None;
    viz.lines.clear();
    viz.playing = false;
    struct Case {
      const char* name;
      const MediaState* s;
    } cases[] = {{"np-lyrics", &st}, {"np-viz", &viz}, {"np-empty", nullptr}};
    for (auto& cs : cases) {
      Image img = renderNowPlaying(cs.s, 560, 302, pal, 500);
      size_t lit = 0;
      for (size_t i = 3; i < img.rgba.size(); i += 4) lit += img.rgba[i] > 8;
      CHECK(lit > 560u * 302u / 4);  // the plate at least
      const std::string path = golden + "/" + cs.name + ".png";
      if (update) {
        writePng(img, path, false);
        continue;
      }
      Image ref;
      CHECK(readPng(path, ref));
      const double d = imageDiff(img, ref);
      if (d < 0 || d > 1.5) std::fprintf(stderr, "%s differs from golden: %.3f\n", cs.name, d);
      GOLDEN(d);
    }
  }

  // a turned widget: at 0° the blit reproduces the picture (orientation is
  // right), and a 30° turn matches its golden
  {
    const Image src = renderLook("bars", 480, 160, pal);
    const Image same = renderRotated(src, 0);
    const double d0 = imageDiff(same, src);
    if (d0 < 0 || d0 > 0.5) std::fprintf(stderr, "unturned blit differs from its source: %.3f\n", d0);
    CHECK(d0 >= 0 && d0 <= 0.5);
    const Image turned = renderRotated(src, 30);
    CHECK(turned.w > src.w && turned.h > src.h);
    const std::string path = golden + "/rotated-bars-30.png";
    if (update) {
      writePng(turned, path, false);
    } else {
      Image ref;
      CHECK(readPng(path, ref));
      const double d = imageDiff(turned, ref);
      if (d < 0 || d > 1.5) std::fprintf(stderr, "turned bars differ from golden: %.3f\n", d);
      GOLDEN(d);
    }
  }

  // an edited card: the weekday in a font, other fonts and sizes, a new order
  {
    auto opts = toml::parse(R"(
      face = "goodnight"
      language = "en"
      weather = false
      goodnight_day_style = "font"
      goodnight_day_font = "Fraunces 144pt"
      goodnight_day_size = 1.2
      goodnight_time_font = "JetBrains Mono"
      goodnight_time_size = 2.0
      goodnight_date_case = "normal"
      goodnight_greeting_show = false
      goodnight_order = "rule_top,time,day,date,rule_bottom"
    )");
    setenv("TZ", "UTC", 1);
    tzset();
    Image img = renderClock(ClockConfig{}, 240, 400, pal, 1790456862, &opts);
    size_t lit = 0;
    for (size_t i = 3; i < img.rgba.size(); i += 4) lit += img.rgba[i] > 8;
    CHECK(lit > 400);
    const std::string path = golden + "/clock-card-edited.png";
    if (update) {
      writePng(img, path, false);
    } else {
      Image ref;
      CHECK(readPng(path, ref));
      const double d = imageDiff(img, ref);
      if (d < 0 || d > 1.5) std::fprintf(stderr, "edited card differs from golden: %.3f\n", d);
      GOLDEN(d);
    }
  }

  // the other structures, edited
  {
    struct Edited {
      const char* name;
      const char* toml;
      int w, h;
    };
    const Edited edits[] = {
        {"clock-column-edited", R"(
          face = "column"
          column_align = "center"
          column_leading = -0.05
          column_minutes_color = "ink"
          column_hours_font = "Fraunces 144pt"
          column_minutes_font = "Fraunces 144pt")", 240, 400},
        {"clock-flip-edited", R"(
          face = "flip"
          flip_radius = 0.5
          flip_pulse = false
          flip_cards_color = "#e2342a"
          flip_digits_color = "#ffffff"
          flip_digits_font = "Space Grotesk")", 480, 160},
        {"clock-metal-edited", R"(
          face = "metal"
          weather = false
          metal_separator = "dot"
          metal_date_show = true
          metal_weekday_color = "accent"
          metal_order = "weekday,date,time")", 480, 240},
        {"clock-stacked-edited", R"(
          face = "stacked"
          stacked_align = "left"
          stacked_date_format = "short"
          stacked_day_font = "Fraunces 144pt"
          stacked_time_size = 3.0
          stacked_order = "time,day,date")", 480, 300},
    };
    for (const auto& ed : edits) {
      auto opts = toml::parse(std::string("language = \"en\"\ndate = \"none\"\n") + ed.toml);
      Image img = renderClock(ClockConfig{}, ed.w, ed.h, pal, 1790456862, &opts);
      // nothing is cut at the widget's edge: fonts that paint past their
      // metrics (here Fraunces' serifs) are fitted by what they draw
      int edge = 0;
      for (int y = 0; y < img.h; ++y)
        for (int x : {0, img.w - 1}) edge = std::max<int>(edge, img.rgba[(static_cast<size_t>(y) * img.w + x) * 4 + 3]);
      if (edge > 8) std::fprintf(stderr, "%s: text reaches the widget's edge (alpha %d)\n", ed.name, edge);
      CHECK(edge <= 8);
      const std::string path = golden + "/" + ed.name + ".png";
      if (update) {
        writePng(img, path, false);
        continue;
      }
      Image ref;
      CHECK(readPng(path, ref));
      const double d = imageDiff(img, ref);
      if (d < 0 || d > 1.5) std::fprintf(stderr, "%s differs from golden: %.3f\n", ed.name, d);
      GOLDEN(d);
    }
  }

  // perspective: no warp reproduces the picture; a tilt and a skew match their golden
  {
    const Image src = renderLook("bars", 480, 160, pal);
    const Image same = renderWarped(src, 0, 0, 0);
    const double d0 = imageDiff(same, src);
    if (d0 < 0 || d0 > 0.5) std::fprintf(stderr, "unwarped blit differs from its source: %.3f\n", d0);
    CHECK(d0 >= 0 && d0 <= 0.5);
    const Image tilted = renderWarped(src, 35, -20, 12);
    const std::string path = golden + "/warped-bars.png";
    if (update) {
      writePng(tilted, path, false);
    } else {
      Image ref;
      CHECK(readPng(path, ref));
      const double d = imageDiff(tilted, ref);
      if (d < 0 || d > 1.5) std::fprintf(stderr, "warped bars differ from golden: %.3f\n", d);
      GOLDEN(d);
    }
  }

  // mesh warp: a flat mesh reproduces the picture; an arc (smooth) and a flag
  // (straight, folded along the grid) match their goldens
  {
    const Image src = renderLook("bars", 480, 160, pal);
    const Image flat = renderMeshed(src, "flat", 0);
    CHECK(flat.w == src.w + 4 && flat.h == src.h + 4);  // 2 px of room for the soft edge
    Image inner;
    inner.w = src.w;
    inner.h = src.h;
    inner.rgba.resize(src.rgba.size());
    for (int y = 0; y < src.h; ++y)
      std::copy_n(flat.rgba.data() + (static_cast<size_t>(y + 2) * flat.w + 2) * 4, static_cast<size_t>(src.w) * 4,
                  inner.rgba.data() + static_cast<size_t>(y) * src.w * 4);
    const double d0 = imageDiff(inner, src);
    if (d0 < 0 || d0 > 0.5) std::fprintf(stderr, "flat mesh differs from its source: %.3f\n", d0);
    CHECK(d0 >= 0 && d0 <= 0.5);
    struct M {
      const char* name;
      const char* preset;
      double amount;
      int n;
      bool smooth;
    };
    for (const M& mm : {M{"mesh-arc", "arc", 70, 3, true}, M{"mesh-flag-straight", "flag", 80, 5, false}}) {
      const Image bent = renderMeshed(src, mm.preset, mm.amount, mm.n, mm.smooth);
      size_t lit = 0;
      for (size_t i = 3; i < bent.rgba.size(); i += 4) lit += bent.rgba[i] > 8;
      CHECK(lit > 480u * 160u / 20);  // the picture is there
      const std::string path = golden + "/" + mm.name + ".png";
      if (update) {
        writePng(bent, path, false);
      } else {
        Image ref;
        CHECK(readPng(path, ref));
        const double d = imageDiff(bent, ref);
        if (d < 0 || d > 1.5) std::fprintf(stderr, "%s differs from golden: %.3f\n", mm.name, d);
        GOLDEN(d);
      }
    }
  }

  // every auxiliary pass compiles and draws (a GLSL error must fail here,
  // never in the running daemon)
  while (glGetError() != GL_NO_ERROR) {}
  auto glcheck = [](const char* where) {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) std::fprintf(stderr, "GL error 0x%x after %s\n", e, where);
    return e == GL_NO_ERROR;
  };
  GLuint fbo = 0, target = 0;  // headless: no default framebuffer
  glGenTextures(1, &target);
  glBindTexture(GL_TEXTURE_2D, target);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 320, 200, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
  glViewport(0, 0, 320, 200);
  try {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    const unsigned char px[4] = {0, 128, 255, 64};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 2, 2, 0, GL_RED, GL_UNSIGNED_BYTE, px);
    OverlayPass ov;
    CHECK(glcheck("texture upload"));
    ov.draw(320, 200, {{10, 10, 100, 60}, {150, 40, 80, 80}}, 0, 1, 1, Color{1, 0, 0, 1}, 16, {160}, {100});
    CHECK(glcheck("overlay"));
    ov.drawPill(10, 10, 120, 30, 9, Color{0, 0, 0, 0.7F}, 320, 200);
    CHECK(glcheck("pill"));
    {
      // a cover cut to a Material shape, and the plain one
      Canvas cv;
      cv.begin(320, 200, 1, nullptr);
      const auto flower = m3ShapeRadii("flower", 128);
      cv.image(tex, 2, 2, 10, 10, 100, 100, 10, 1, 0, flower.data());
      cv.image(tex, 2, 2, 120, 10, 100, 100, 10, 1, 0);
      CHECK(glcheck("shaped image"));
    }
    MaskPass mask;
    for (int mode = 0; mode < 6; ++mode) {
      MaskParams mp;
      mp.texture = tex;
      mp.surfaceW = 100;
      mp.surfaceH = 50;
      mp.outputW = 1920;
      mp.outputH = 1080;
      mp.imageW = 2;
      mp.imageH = 2;
      mp.fillMode = mode;
      mask.draw(mp);
      CHECK(glcheck("mask"));
      mp.field = tex;  // a widget with its own depth plane
      mp.level = 0.4F;
      mp.feather = 0.1F;
      mask.draw(mp);
      CHECK(glcheck("mask (depth field)"));
    }
    glDeleteTextures(1, &tex);
    CHECK(glcheck("cleanup"));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "pass failed: %s\n", e.what());
    CHECK(false);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &target);

  // text: the bundled fonts resolve and draw
  TextRenderer::registerBundledFonts();
  Image t1 = renderText("12:34", "Space Grotesk", 48, 700);
  Image t2 = renderText("12:34", "Space Grotesk", 96, 700);
  CHECK(t1.w > 60 && t1.h > 30);
  CHECK(t2.w > t1.w * 1.7);
  size_t ink = 0;
  for (size_t i = 3; i < t1.rgba.size(); i += 4) ink += t1.rgba[i] > 128;
  CHECK(ink > 200);
  float w = 0, h = 0, b = 0;
  TextRenderer::measure("12:34", {.family = "JetBrains Mono", .size = 40, .weight = 700}, w, h, b);
  CHECK(w > 100 && b > 0 && b < h);
  if (g_goldenMismatches > 0)
    std::fprintf(stderr,
                 "%d image(s) differ from tests/golden: expected on a different GPU, driver or font stack; "
                 "not a failure (UNDERSHELL_GOLDEN=strict makes it one)\n",
                 g_goldenMismatches);
  return TEST_RESULT();
}
