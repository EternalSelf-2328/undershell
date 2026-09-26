// Rendering regression: every look against its golden image, and text.
// Regenerate goldens with:  build/test_render --update
#include "check.hpp"
#include "offscreen.hpp"
#include "text.hpp"

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
