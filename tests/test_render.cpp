// Rendering regression: every look against its golden image, and text.
// Regenerate goldens with:  build/test_render --update
#include "check.hpp"
#include "clock.hpp"
#include "media.hpp"
#include "offscreen.hpp"
#include "overlay.hpp"
#include "text.hpp"

#include <stdexcept>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>

using namespace undershell;

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
                           "halo"}) {
    const bool polar = !std::strcmp(look, "radial") || !std::strcmp(look, "orb") || !std::strcmp(look, "spiral") ||
                       !std::strcmp(look, "halo");
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
    CHECK(d >= 0 && d <= 1.5);
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
    CHECK(d >= 0 && d <= 1.5);
  }

  // the now-playing card: synced lyrics, the no-lyrics spectrum, nothing
  {
    MediaState st;
    st.present = true;
    st.playing = true;
    st.title = "Cariño mío";
    st.artist = "Paloma San Basilio";
    st.lengthUs = 208'000'000;
    st.positionUs = 40'000'000;
    st.positionAt = 500;
    st.canNext = st.canPrev = st.canToggle = st.canSeek = true;
    st.hasAccent = true;
    st.accent = Color::fromHex("#e0654a");
    st.lyrics = MediaState::Lyrics::Synced;
    st.lines = parseLrc("[00:31.84]Sé que estás pensando que te soy infiel\n[00:36.05]Que te estoy mintiendo\n"
                        "[00:39.50]Que no te quiero\n[00:43.10]Y que busco en otros brazos lo que no me das\n[00:48.00]\n");
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
      CHECK(d >= 0 && d <= 1.5);
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
      CHECK(d >= 0 && d <= 1.5);
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
      CHECK(d >= 0 && d <= 1.5);
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
      const std::string path = golden + "/" + ed.name + ".png";
      if (update) {
        writePng(img, path, false);
        continue;
      }
      Image ref;
      CHECK(readPng(path, ref));
      const double d = imageDiff(img, ref);
      if (d < 0 || d > 1.5) std::fprintf(stderr, "%s differs from golden: %.3f\n", ed.name, d);
      CHECK(d >= 0 && d <= 1.5);
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
  return TEST_RESULT();
}
