// SPDX-License-Identifier: GPL-3.0-or-later
#include "offscreen.hpp"

#include "geom.hpp"
#include "overlay.hpp"

#include "clock.hpp"
#include "gl.hpp"
#include "jobs.hpp"
#include "media.hpp"
#include "nowplaying.hpp"
#include "text.hpp"
#include "visualizer.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cairo.h>
#include <cmath>
#include <ctime>
#include <filesystem>

namespace undershell {

namespace fs = std::filesystem;

Headless::Headless() {
  EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr)) return;
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
  if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
    eglTerminate(dpy);
    return;
  }
  m_dpy = dpy;
  m_ctx = ctx;
  m_ok = true;
}

Headless::~Headless() {
  if (!m_ok) return;
  eglMakeCurrent(m_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(m_dpy, m_ctx);
  eglTerminate(m_dpy);
}

NoctaliaState testPalette() {
  NoctaliaState s;
  s.palette = {{"primary", Color::fromHex("#c5c0fd")},    {"secondary", Color::fromHex("#c7c3de")},
               {"tertiary", Color::fromHex("#f2b3e1")},   {"surface", Color::fromHex("#141316")},
               {"on_surface", Color::fromHex("#e5e1e6")}, {"outline", Color::fromHex("#928f99")}};
  return s;
}

// Draws into an offscreen RGBA8 target and reads it back top-down.
template <typename Fn>
static Image renderToImage(int w, int h, Fn&& draw) {
  GLuint fbo = 0, tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  glViewport(0, 0, w, h);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  draw();
  Image img;
  img.w = w;
  img.h = h;
  std::vector<std::uint8_t> px(static_cast<size_t>(w) * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  img.rgba.resize(px.size());
  const size_t row = static_cast<size_t>(w) * 4;
  for (int y = 0; y < h; ++y) std::copy_n(px.data() + static_cast<size_t>(h - 1 - y) * row, row, img.rgba.data() + static_cast<size_t>(y) * row);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &tex);
  return img;
}

Image renderRotated(const Image& src, double degrees) {
  // upload bottom-up, as the daemon's off-screen target holds it
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  std::vector<std::uint8_t> flipped(src.rgba.size());
  const size_t row = static_cast<size_t>(src.w) * 4;
  for (int y = 0; y < src.h; ++y)
    std::copy_n(src.rgba.data() + static_cast<size_t>(src.h - 1 - y) * row, row, flipped.data() + static_cast<size_t>(y) * row);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, src.w, src.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  WidgetConfig c;
  c.width = src.w;
  c.height = src.h;
  c.rotation = degrees;
  const Box b = surfaceBox(c);
  RotatedBlit blit;
  Image img = renderToImage(b.w, b.h, [&] {
    blit.draw(tex, static_cast<float>(b.w), static_cast<float>(b.h), static_cast<float>(c.width / 2.0 - b.x),
              static_cast<float>(c.height / 2.0 - b.y), static_cast<float>(c.width), static_cast<float>(c.height),
              static_cast<float>(degrees));
  });
  glDeleteTextures(1, &tex);
  return img;
}

Image renderWarped(const Image& src, double tiltX, double tiltY, double skew, double degrees) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  std::vector<std::uint8_t> flipped(src.rgba.size());
  const size_t row = static_cast<size_t>(src.w) * 4;
  for (int y = 0; y < src.h; ++y)
    std::copy_n(src.rgba.data() + static_cast<size_t>(src.h - 1 - y) * row, row, flipped.data() + static_cast<size_t>(y) * row);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, src.w, src.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  WidgetConfig c;
  c.width = src.w;
  c.height = src.h;
  c.tiltX = tiltX;
  c.tiltY = tiltY;
  c.skewX = skew;
  c.rotation = degrees;
  const Box b = surfaceBox(c);
  const Homography toUv = surfaceToUv(c);
  PerspectiveBlit blit;
  Image img = renderToImage(b.w, b.h, [&] { blit.draw(tex, static_cast<float>(b.w), static_cast<float>(b.h), toUv.m); });
  glDeleteTextures(1, &tex);
  return img;
}

Image renderLook(const std::string& look, int w, int h, const NoctaliaState& noct, const toml::table* options) {
  VisualizerConfig vc;
  vc.style = look;
  vc.peaks = look == "bars" || look == "segments";
  const bool frame = look == "frame";
  Visualizer viz;
  WidgetConfig wc;
  wc.type = "visualizer";
  wc.options.insert_or_assign("style", look);
  wc.options.insert_or_assign("peaks", vc.peaks);
  if (options)
    for (auto&& [k, v] : *options) wc.options.insert_or_assign(k, v);
  viz.configure(wc, noct);
  std::vector<float> raw(64);
  for (int step = 0; step < 90; ++step) {
    const double t = step / 60.0;
    for (int i = 0; i < 64; ++i) {
      const double x = i / 63.0;
      raw[static_cast<size_t>(i)] = static_cast<float>(std::clamp(
          0.6 * std::exp(-x * 2.2) + 0.28 * (0.5 + 0.5 * std::sin(t * 3 + i * 0.35)) * (1 - x * 0.5), 0.0, 1.0));
    }
    TickContext tc;
    tc.dt = 1.0 / 60;
    tc.audio.bands = &raw;
    tc.audio.energy = 0.4;
    tc.audio.silent = false;
    viz.tick(tc);
  }
  return renderToImage(w, h, [&] {
    DrawContext dc;
    dc.w = static_cast<float>(w);
    dc.h = static_cast<float>(h);
    dc.outputW = frame ? static_cast<float>(w) : 1920.0F;
    dc.outputH = frame ? static_cast<float>(h) : 1080.0F;
    viz.draw(dc);
  });
}

Image renderClock(const ClockConfig& cfg, int w, int h, const NoctaliaState& noct, long fixedTime, const toml::table* options) {
  ClockWidget::setFixedTime(static_cast<std::time_t>(fixedTime));
  ClockWidget clock;
  if (options) {
    WidgetConfig wc;
    wc.type = "clock";
    wc.options = *options;
    clock.configure(wc, noct);
  } else {
    clock.configure(cfg, noct);
  }
  TextRenderer tr;
  TickContext tc;
  tc.now = 1000;
  clock.tick(tc);
  Image img = renderToImage(w, h, [&] {
    DrawContext dc;
    dc.w = static_cast<float>(w);
    dc.h = static_cast<float>(h);
    dc.outputW = 1920;
    dc.outputH = 1080;
    dc.text = &tr;
    clock.draw(dc);
  });
  tr.releaseGl();
  ClockWidget::setFixedTime(0);
  return img;
}

Image renderNowPlaying(const MediaState* state, int w, int h, const NoctaliaState& noct, double now, const toml::table* options,
                       int settleSteps) {
  Jobs jobs(1);
  MediaService media(jobs);
  if (state) media.setStateForTest(*state);
  NowPlayingWidget np;
  if (options) {
    WidgetConfig wc;
    wc.type = "now_playing";
    wc.options = *options;
    np.configure(wc, noct);
  } else {
    np.configure(NowPlayingConfig{}, noct);
  }
  TextRenderer tr;
  // settle the spectrum and the lyric glide
  std::vector<float> bands(64);
  for (int i = 0; i < 64; ++i) bands[static_cast<size_t>(i)] = 0.15F + 0.7F * std::exp(-i / 20.0F) * (0.6F + 0.4F * std::sin(i * 0.7F));
  for (int step = 0; step < settleSteps; ++step) {
    TickContext tc;
    tc.now = now - 1 + step / 60.0;
    tc.dt = 1.0 / 60;
    tc.audio.bands = &bands;
    tc.audio.energy = 0.4;
    tc.audio.silent = false;
    tc.media = &media;
    np.tick(tc);
  }
  Image img = renderToImage(w, h, [&] {
    DrawContext dc;
    dc.w = static_cast<float>(w);
    dc.h = static_cast<float>(h);
    dc.outputW = 1920;
    dc.outputH = 1080;
    dc.text = &tr;
    dc.media = &media;
    dc.now = now;
    np.draw(dc);  // first draw fixes the lyric index...
    glClear(GL_COLOR_BUFFER_BIT);
    for (int step = 0; step < 60; ++step) {  // ...then let the glide settle
      TickContext tc;
      tc.now = now;
      tc.dt = 1.0 / 60;
      tc.media = &media;
      np.tick(tc);
    }
    np.draw(dc);
  });
  tr.releaseGl();
  return img;
}

Image renderText(const std::string& text, const std::string& family, float size, int weight) {
  TextRenderer tr;
  TextStyle st;
  st.family = family;
  st.size = size;
  st.weight = weight;
  float w = 0, h = 0, base = 0;
  TextRenderer::measure(text, st, w, h, base);
  const int W = static_cast<int>(std::ceil(w)) + 8, H = static_cast<int>(std::ceil(h)) + 8;
  Image img = renderToImage(W, H, [&] {
    const TextImage& ti = tr.get(text, st, 1);
    tr.draw(ti, 4, 4, Color{1, 1, 1, 1}, static_cast<float>(W), static_cast<float>(H));
  });
  tr.releaseGl();
  return img;
}

bool writePng(const Image& img, const std::string& path, bool overDark) {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, img.w, img.h);
  unsigned char* dst = cairo_image_surface_get_data(s);
  const int stride = cairo_image_surface_get_stride(s);
  for (int y = 0; y < img.h; ++y)
    for (int x = 0; x < img.w; ++x) {
      const std::uint8_t* p = &img.rgba[(static_cast<size_t>(y) * img.w + x) * 4];
      std::uint32_t r = p[0], g = p[1], b = p[2], a = p[3];
      if (overDark) {
        const float k = 1.0F - a / 255.0F;
        r = std::min<std::uint32_t>(255, r + static_cast<std::uint32_t>(18 * k));
        g = std::min<std::uint32_t>(255, g + static_cast<std::uint32_t>(18 * k));
        b = std::min<std::uint32_t>(255, b + static_cast<std::uint32_t>(24 * k));
        a = 255;
      }
      *(reinterpret_cast<std::uint32_t*>(dst + y * stride) + x) = (a << 24) | (r << 16) | (g << 8) | b;
    }
  cairo_surface_mark_dirty(s);
  const bool ok = cairo_surface_write_to_png(s, path.c_str()) == CAIRO_STATUS_SUCCESS;
  cairo_surface_destroy(s);
  return ok;
}

bool readPng(const std::string& path, Image& img) {
  cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
  if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(s);
    return false;
  }
  cairo_surface_flush(s);
  img.w = cairo_image_surface_get_width(s);
  img.h = cairo_image_surface_get_height(s);
  const int stride = cairo_image_surface_get_stride(s);
  const unsigned char* src = cairo_image_surface_get_data(s);
  img.rgba.resize(static_cast<size_t>(img.w) * img.h * 4);
  for (int y = 0; y < img.h; ++y)
    for (int x = 0; x < img.w; ++x) {
      const std::uint32_t px = *(reinterpret_cast<const std::uint32_t*>(src + y * stride) + x);
      std::uint8_t* d = &img.rgba[(static_cast<size_t>(y) * img.w + x) * 4];
      d[0] = (px >> 16) & 0xff;
      d[1] = (px >> 8) & 0xff;
      d[2] = px & 0xff;
      d[3] = (px >> 24) & 0xff;
    }
  cairo_surface_destroy(s);
  return true;
}

double imageDiff(const Image& a, const Image& b) {
  if (a.w != b.w || a.h != b.h || a.rgba.size() != b.rgba.size()) return -1;
  double sum = 0;
  for (size_t i = 0; i < a.rgba.size(); ++i) sum += std::abs(static_cast<int>(a.rgba[i]) - static_cast<int>(b.rgba[i]));
  return sum / static_cast<double>(a.rgba.size());
}

static void lookSize(const std::string& look, int& w, int& h) {
  const bool polar = look == "radial" || look == "orb" || look == "spiral";
  w = look == "frame" ? 960 : (polar ? 420 : 900);
  h = look == "frame" ? 540 : (polar ? 420 : 280);
}

int snapshotLooks(const std::string& dir) {
  Headless gl;
  if (!gl.ok()) {
    US_ERROR("surfaceless EGL unavailable");
    return 1;
  }
  Noctalia noct;
  noct.refresh();
  fs::create_directories(dir);
  for (const char* look : {"bars", "split", "dots", "segments", "wave", "ribbon", "curtain", "line", "frame", "radial", "orb", "spiral"}) {
    int w = 0, h = 0;
    lookSize(look, w, h);
    writePng(renderLook(look, w, h, noct.state()), dir + "/" + look + ".png", true);
    US_INFO("wrote {}/{}.png", dir, look);
  }
  for (const auto& face : ClockWidget::faces()) {
    ClockConfig cc;
    cc.face = face;
    cc.seconds = true;
    const bool tall = face == "goodnight";
    const bool square = face == "analog" || face == "rings";
    writePng(renderClock(cc, tall ? 360 : (square ? 400 : 900), tall ? 640 : (square ? 460 : 360), noct.state(), std::time(nullptr)),
             dir + "/clock-" + face + ".png", true);
    US_INFO("wrote {}/clock-{}.png", dir, face);
  }
  return 0;
}

}  // namespace undershell
