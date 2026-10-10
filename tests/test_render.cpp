// Rendering regression: every look against its golden image, and text.
// Regenerate goldens with:  build/test_render --update
#include "check.hpp"
#include "clock.hpp"
#include "media.hpp"
#include "nowplaying.hpp"
#include "offscreen.hpp"
#include "sfx.hpp"
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

  // electricity, frozen just after a kick: the arc runs end to end across the
  // box, a bolt comes down from the top edge, the plasma stays in its globe;
  // ink is only greys; and each matches its golden
  {
    auto look = [&](const char* form, const char* style, int w, int h) {
      toml::table o;
      o.insert_or_assign("electric_preview", 0.04);
      o.insert_or_assign("electric_form", form);
      o.insert_or_assign("electric_style", style);
      return renderLook("electric", w, h, pal, &o);
    };
    auto colAlpha = [](const Image& im, int x0, int x1, int y0, int y1) {
      double a = 0;
      for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) a += im.rgba[(static_cast<size_t>(y) * im.w + x) * 4 + 3];
      return a;
    };
    const Image arc = look("arc", "flash", 480, 160);
    CHECK(colAlpha(arc, 10, 40, 0, 160) > 0 && colAlpha(arc, 440, 470, 0, 160) > 0);  // both ends lit
    CHECK(colAlpha(arc, 200, 280, 60, 100) > 0);                                     // and the middle
    const Image bolt = look("bolts", "flash", 320, 320);
    CHECK(colAlpha(bolt, 0, 320, 0, 20) > 0);       // from the top edge
    CHECK(colAlpha(bolt, 0, 320, 260, 320) > 0);    // down to the ground
    const Image globe = look("plasma", "flash", 300, 300);
    // nothing much outside the globe's circle (its glow fades by the edge)
    CHECK(colAlpha(globe, 0, 20, 0, 20) < colAlpha(globe, 140, 160, 140, 160) * 0.05);
    const Image ink = look("arc", "manga", 480, 160);
    bool greys = true;
    for (size_t i = 0; i < ink.rgba.size(); i += 4)
      if (std::abs(ink.rgba[i] - ink.rgba[i + 1]) > 3 || std::abs(ink.rgba[i + 1] - ink.rgba[i + 2]) > 3) greys = false;
    CHECK(greys);
    CHECK(colAlpha(ink, 0, 480, 0, 160) > 0);
    struct G {
      const char* name;
      const Image* img;
    };
    for (const G& gg : {G{"electric-arc", &arc}, G{"electric-bolts", &bolt}, G{"electric-plasma", &globe}, G{"electric-manga", &ink}}) {
      const std::string path = golden + "/" + gg.name + ".png";
      if (update) {
        writePng(*gg.img, path, false);
      } else {
        Image ref;
        CHECK(readPng(path, ref));
        const double d = imageDiff(*gg.img, ref);
        if (d < 0 || d > 1.5) std::fprintf(stderr, "%s differs from golden: %.3f\n", gg.name, d);
        GOLDEN(d);
      }
    }
  }

  // onomatopoeia: each language letters a word (CJK down a tall box), ink is
  // only greys, the words follow the kick's strength, and the English one
  // (its face is bundled) matches its golden
  {
    CHECK(SfxLayer::vertical("ドカーン!!") == "ド\nカ\n｜\nン\n‼");
    CHECK(SfxLayer::vertical("쾅!") == "쾅\n！");
    CHECK(std::string(SfxLayer::words(SfxLayer::English, 1.0)[0]) == "BOOM!");
    CHECK(std::string(SfxLayer::words(SfxLayer::Japanese, 0.6)[0]) == "ドン!");
    CHECK(SfxLayer::words(SfxLayer::Korean, 0.2).size() == 3 && SfxLayer::words(SfxLayer::Chinese, 0.9).size() == 4);
    auto word = [&](const char* lang, const char* style, int w, int h) {
      toml::table o;
      o.insert_or_assign("sfx_preview", 0.2);
      o.insert_or_assign("sfx_language", lang);
      o.insert_or_assign("sfx_style", style);
      return renderLook("sfx", w, h, pal, &o);
    };
    auto lit = [](const Image& im) {
      size_t n = 0;
      for (size_t i = 3; i < im.rgba.size(); i += 4) n += im.rgba[i] > 8;
      return n;
    };
    for (const char* lang : {"english", "japanese", "korean", "chinese"}) {
      const Image wide = word(lang, "comic", 420, 200), tall = word(lang, "manga", 200, 360);
      if (lit(wide) < 420u * 200u / 10) std::fprintf(stderr, "sfx %s letters almost nothing\n", lang);
      CHECK(lit(wide) > 420u * 200u / 10);
      CHECK(lit(tall) > 200u * 360u / 10);
      bool greys = true;
      for (size_t i = 0; i < tall.rgba.size(); i += 4)
        if (std::abs(tall.rgba[i] - tall.rgba[i + 1]) > 3 || std::abs(tall.rgba[i + 1] - tall.rgba[i + 2]) > 3) greys = false;
      CHECK(greys);
    }
    const Image en = word("english", "comic", 420, 200);
    const std::string path = golden + "/sfx-english.png";
    if (update) {
      writePng(en, path, false);
    } else {
      Image ref;
      CHECK(readPng(path, ref));
      const double d = imageDiff(en, ref);
      if (d < 0 || d > 1.5) std::fprintf(stderr, "sfx-english differs from golden: %.3f\n", d);
      GOLDEN(d);
    }
  }

  // speed lines: focus lines leave the centre clear and ink the edges; motion
  // lines streak across; the focus lines match their golden
  {
    auto lines = [&](const char* form, int w, int h) {
      toml::table o;
      o.insert_or_assign("lines_preview", 0.05);
      o.insert_or_assign("lines_form", form);
      return renderLook("speedlines", w, h, pal, &o);
    };
    auto alphaIn = [](const Image& im, int x0, int x1, int y0, int y1) {
      double a = 0;
      for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) a += im.rgba[(static_cast<size_t>(y) * im.w + x) * 4 + 3];
      return a / ((x1 - x0) * (y1 - y0) * 255.0);
    };
    const Image focus = lines("focus", 400, 240);
    CHECK(alphaIn(focus, 180, 220, 105, 135) < 0.02);  // the clear centre
    CHECK(alphaIn(focus, 0, 40, 0, 40) > 0.25);         // inked corners
    const Image par = lines("parallel", 400, 160);
    CHECK(alphaIn(par, 0, 400, 0, 160) > 0.03);
    const std::string path = golden + "/speedlines-focus.png";
    if (update) {
      writePng(focus, path, false);
    } else {
      Image ref;
      CHECK(readPng(path, ref));
      const double d = imageDiff(focus, ref);
      if (d < 0 || d > 1.5) std::fprintf(stderr, "speedlines-focus differs from golden: %.3f\n", d);
      GOLDEN(d);
    }
  }

  // rain, snow and petals fill the box (three seconds of it, then frozen);
  // the rain matches its golden
  {
    for (const char* kind : {"rain", "snow", "petals"}) {
      toml::table o;
      o.insert_or_assign("rain_preview", 0.1);
      o.insert_or_assign("rain_kind", kind);
      o.insert_or_assign("rain_amount", 0.8);
      const Image img = renderLook("rain", 320, 240, pal, &o);
      // drops in the top half and in the bottom half: falling through, not piled up
      size_t top = 0, bottom = 0;
      for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x)
          if (img.rgba[(static_cast<size_t>(y) * 320 + x) * 4 + 3] > 20) (y < 120 ? top : bottom)++;
      if (bottom < 30 || top < 30) std::fprintf(stderr, "%s: %zu lit above, %zu below\n", kind, top, bottom);
      CHECK(top > 30 && bottom > 30);
      if (std::string(kind) != "rain") continue;
      const std::string path = golden + "/rain.png";
      if (update) {
        writePng(img, path, false);
      } else {
        Image ref;
        CHECK(readPng(path, ref));
        const double d = imageDiff(img, ref);
        if (d < 0 || d > 1.5) std::fprintf(stderr, "rain differs from golden: %.3f\n", d);
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
    // `focus`: the sung line well apart from its neighbours, with a long line
    // that has to wrap at the larger size
    MediaState focus = st;
    focus.lines = parseLrc("[00:31.84]La luz se queda quieta en la ventana y el reloj no sabe esperar\n"
                           "[00:39.50]Electroencefalografistas\n[00:48.00]fin\n");
    toml::table focusOpts;
    focusOpts.insert("lyrics_style", "focus");
    struct Case {
      const char* name;
      const MediaState* s;
      const toml::table* opts;
    } cases[] = {{"np-lyrics", &st, nullptr},
                 {"np-viz", &viz, nullptr},
                 {"np-empty", nullptr, nullptr},
                 {"np-lyrics-focus", &focus, &focusOpts}};
    // A card closes up around the pieces it is showing: with the three moves,
    // the rail and the artist left out, the ones that stack have to be
    // shorter rather than keep the holes. The strip is one line, so what a
    // missing piece frees there goes to the track and its shape does not move.
    auto sizeOf = [](const char* layout, bool bare, float& w, float& h) {
      toml::table o;
      o.insert("layout", layout);
      if (bare)
        for (const char* off : {"show_transport", "show_rail", "show_artist"}) o.insert(off, false);
      nowPlayingDesignSize(o, w, h);
    };
    for (const char* name : {"sheet", "vinyl", "tile", "poster", "portrait"}) {
      float fw = 0, fh = 0, bw = 0, bh = 0;
      sizeOf(name, false, fw, fh);
      sizeOf(name, true, bw, bh);
      if (bh >= fh) std::fprintf(stderr, "%s keeps its height with three pieces gone (%.0f -> %.0f)\n", name, fh, bh);
      CHECK(bh < fh);
      CHECK(bh > 20 && bw > 60);
    }
    {
      float fw = 0, fh = 0, bw = 0, bh = 0;
      sizeOf("strip", false, fw, fh);
      sizeOf("strip", true, bw, bh);
      CHECK(bw == fw && bh == fh);
    }

    // every layout, at the size it asks for -- which now follows the pieces it
    // is showing, so a card closes up around what is left of it
    for (const char* name : {"vinyl", "tile", "poster", "strip", "portrait"}) {
      float dw = 0, dh = 0;
      nowPlayingDesignSize(name, dw, dh);
      const int w = static_cast<int>(dw), h = static_cast<int>(dh);
      CHECK(w > 100 && h > 40);
      toml::table o;
      o.insert("layout", name);
      Image img = renderNowPlaying(&st, w, h, pal, 500, &o);
      size_t lit = 0;
      for (size_t i = 3; i < img.rgba.size(); i += 4) lit += img.rgba[i] > 8;
      if (lit < static_cast<size_t>(w) * h / 5) std::fprintf(stderr, "np-%s draws almost nothing\n", name);
      CHECK(lit > static_cast<size_t>(w) * h / 5);
      const std::string path = golden + "/np-" + name + ".png";
      if (update) {
        writePng(img, path, false);
        continue;
      }
      Image ref;
      CHECK(readPng(path, ref));
      const double d = imageDiff(img, ref);
      if (d < 0 || d > 1.5) std::fprintf(stderr, "np-%s differs from golden: %.3f\n", name, d);
      GOLDEN(d);
    }
    for (auto& cs : cases) {
      Image img = renderNowPlaying(cs.s, 560, 302, pal, 500, cs.opts);
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

  // the text layout: only the track, filling the box; ranged where asked; a
  // long title broken in two without losing its end
  {
    MediaState st;
    st.present = st.playing = true;
    st.title = "Web";
    st.artist = "070 Shake";
    // the columns and rows with ink, as a box
    struct Span {
      int x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1;
    };
    auto inkOf = [](const Image& im) {
      Span s;
      for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x)
          if (im.rgba[(static_cast<size_t>(y) * im.w + x) * 4 + 3] > 40) {
            s.x0 = std::min(s.x0, x), s.x1 = std::max(s.x1, x), s.y0 = std::min(s.y0, y), s.y1 = std::max(s.y1, y);
          }
      return s;
    };
    auto card = [&](const MediaState& m, const char* form, const char* align, int w, int h) {
      toml::table o;
      o.insert("layout", "text");
      o.insert("plate", "none");
      o.insert("text_form", form);
      o.insert("text_align", align);
      return renderNowPlaying(&m, w, h, pal, 500, &o);
    };
    for (const char* form : {"stacked", "line"}) {
      const Image img = card(st, form, "right", 420, 140);
      const Span s = inkOf(img);
      const bool fills = s.x1 - s.x0 > 420 * 0.85 || s.y1 - s.y0 > 140 * 0.85;
      if (!fills) std::fprintf(stderr, "text %s: ink %d..%d x %d..%d does not fill the box\n", form, s.x0, s.x1, s.y0, s.y1);
      CHECK(fills);
      CHECK(s.x1 >= 420 - 6);  // ranged right
    }
    // a long title stacked in a squarish box takes two lines and keeps them
    // both: its ink is far taller than one line of the same width would be
    MediaState lng = st;
    lng.title = "Don't Dream It's Over (Remastered 2023 Version)";
    lng.artist = "Crowded House";
    const Span two = inkOf(card(lng, "stacked", "left", 300, 220));
    const Span one = inkOf(card(lng, "line", "left", 300, 220));
    CHECK(two.y1 - two.y0 > 3 * (one.y1 - one.y0));
  }

  // the sheet with every piece but the track off draws no stray button in its corner
  {
    MediaState st;
    st.present = st.playing = true;
    st.title = "Web";
    st.artist = "070 Shake";
    toml::table o;
    o.insert("plate", "none");
    for (const char* k : {"show_cover", "show_lyrics", "show_viz", "show_time", "show_rail", "show_transport", "show_open", "show_pulse"})
      o.insert(k, false);
    const Image bare = renderNowPlaying(&st, 420, 90, pal, 500, &o);
    size_t corner = 0;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 8; ++x) corner += bare.rgba[(static_cast<size_t>(y) * bare.w + x) * 4 + 3] > 8;
    CHECK(corner == 0);
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
  // The bundled fonts are resolved against a font map of their own, which is
  // far quicker than matching the whole system. A script none of them covers
  // still has to come out drawn, through the system's fallback.
  for (const char* txt : {"夜に駆ける", "아무노래", "Пачка сигарет", "love 💔 song"}) {
    const auto own = TextRenderer::measureInk(txt, {.family = "Google Sans Flex", .size = 20, .weight = 500});
    if (own.w <= 0 || own.h <= 0) std::fprintf(stderr, "\"%s\" draws nothing in the bundled font map\n", txt);
    CHECK(own.w > 0 && own.h > 0);
  }
  if (g_goldenMismatches > 0)
    std::fprintf(stderr,
                 "%d image(s) differ from tests/golden: expected on a different GPU, driver or font stack; "
                 "not a failure (UNDERSHELL_GOLDEN=strict makes it one)\n",
                 g_goldenMismatches);
  return TEST_RESULT();
}
