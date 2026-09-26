// Rendering regression: every look against its golden image, and text.
// Regenerate goldens with:  build/test_render --update
#include "check.hpp"
#include "offscreen.hpp"
#include "overlay.hpp"
#include "text.hpp"

#include <stdexcept>

#include <cstring>
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
  for (const char* look : {"bars", "split", "dots", "segments", "wave", "ribbon", "curtain", "line", "frame", "radial", "orb", "spiral"}) {
    const bool polar = !std::strcmp(look, "radial") || !std::strcmp(look, "orb") || !std::strcmp(look, "spiral");
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
