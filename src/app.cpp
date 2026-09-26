// SPDX-License-Identifier: GPL-3.0-or-later
#include "app.hpp"

#include "visualizer.hpp"

// the generated header names a parameter `namespace`
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace

#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <cerrno>
#include <cmath>
#include <sstream>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>
#include <cairo.h>

namespace undershell {

namespace fs = std::filesystem;
static volatile std::sig_atomic_t g_quit = 0;

// ── listener trampolines ────────────────────────────────────────────────────

static void regGlobal(void* d, wl_registry* r, uint32_t n, const char* i, uint32_t v) {
  static_cast<App*>(d)->onGlobal(r, n, i, v);
}
static void regRemove(void* d, wl_registry*, uint32_t n) { static_cast<App*>(d)->onGlobalRemove(n); }
static const wl_registry_listener kRegistry = {regGlobal, regRemove};

struct OutputCtx {
  App* app;
  Output* out;
};
static void outGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
static void outMode(void* d, wl_output*, uint32_t flags, int32_t w, int32_t h, int32_t) {
  if (flags & WL_OUTPUT_MODE_CURRENT) {
    auto* c = static_cast<OutputCtx*>(d);
    c->out->modeW = w;
    c->out->modeH = h;
  }
}
static void outDone(void* d, wl_output*) {
  auto* c = static_cast<OutputCtx*>(d);
  c->app->onOutputDone(c->out);
}
static void outScale(void* d, wl_output*, int32_t s) { static_cast<OutputCtx*>(d)->out->scale = std::max(1, s); }
static void outName(void* d, wl_output*, const char* n) { static_cast<OutputCtx*>(d)->out->name = n; }
static void outDesc(void*, wl_output*, const char*) {}
static const wl_output_listener kOutput = {outGeometry, outMode, outDone, outScale, outName, outDesc};

static void lsConfigure(void* d, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t w, uint32_t h);
static void lsClosed(void* d, zwlr_layer_surface_v1*);
static const zwlr_layer_surface_v1_listener kLayer = {lsConfigure, lsClosed};

struct WidgetCtx {
  App* app;
  Widget* w;
};
static void lsConfigure(void* d, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t w, uint32_t h) {
  auto* c = static_cast<WidgetCtx*>(d);
  c->app->onLayerConfigure(c->w, ls, serial, w, h);
}
static void lsClosed(void* d, zwlr_layer_surface_v1*) {
  auto* c = static_cast<WidgetCtx*>(d);
  c->app->onLayerClosed(c->w);
}

static void frameDone(void* d, wl_callback* cb, uint32_t) {
  auto* c = static_cast<WidgetCtx*>(d);
  wl_callback_destroy(cb);
  c->w->frameCb = nullptr;
  App* app = c->app;
  Widget* w = c->w;
  delete c;
  app->onFrameDone(w);
}
static const wl_callback_listener kFrame = {frameDone};



struct EditCtx {
  App* app;
  EditSurface* e;
};
static void editConfigure(void* d, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t w, uint32_t h) {
  auto* c = static_cast<EditCtx*>(d);
  c->app->onEditConfigure(c->e, ls, serial, w, h);
}
static void editClosed(void*, zwlr_layer_surface_v1*) {}
static const zwlr_layer_surface_v1_listener kEditLayer = {editConfigure, editClosed};
static void editFrame(void* d, wl_callback* cb, uint32_t) {
  auto* c = static_cast<EditCtx*>(d);
  wl_callback_destroy(cb);
  c->e->frameCb = nullptr;
  App* app = c->app;
  EditSurface* e = c->e;
  delete c;
  app->onEditFrameDone(e);
}
static const wl_callback_listener kEditFrame = {editFrame};

static void kbKeymap(void* d, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
  if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    static_cast<App*>(d)->onKeymap(fd, size);
  } else {
    close(fd);
  }
}
static void kbEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
static void kbLeave(void* d, wl_keyboard*, uint32_t, wl_surface*) { static_cast<App*>(d)->onKeyboardLeave(); }
static void kbKey(void* d, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
  static_cast<App*>(d)->onKey(key, state);
}
static void kbMods(void* d, wl_keyboard*, uint32_t, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) {
  static_cast<App*>(d)->onModifiers(dep, lat, lock, group);
}
static void kbRepeat(void* d, wl_keyboard*, int32_t rate, int32_t delay) { static_cast<App*>(d)->onRepeatInfo(rate, delay); }
static const wl_keyboard_listener kKeyboard = {kbKeymap, kbEnter, kbLeave, kbKey, kbMods, kbRepeat};

static void ptrEnter(void* d, wl_pointer*, uint32_t serial, wl_surface* s, wl_fixed_t x, wl_fixed_t y) {
  static_cast<App*>(d)->onPointerEnter(s, serial, wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void ptrLeave(void* d, wl_pointer*, uint32_t, wl_surface* s) { static_cast<App*>(d)->onPointerLeave(s); }
static void ptrMotion(void* d, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
  static_cast<App*>(d)->onPointerMotion(wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void ptrButton(void* d, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) {
  static_cast<App*>(d)->onPointerButton(serial, button, state);
}
static void ptrAxis(void* d, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
  if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) static_cast<App*>(d)->onScroll(wl_fixed_to_double(value));
}
static void ptrFrame(void*, wl_pointer*) {}
static void ptrAxisSource(void*, wl_pointer*, uint32_t) {}
static void ptrAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
static void ptrAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
static void ptrAxis120(void*, wl_pointer*, uint32_t, int32_t) {}
static void ptrAxisRel(void*, wl_pointer*, uint32_t, uint32_t) {}
static const wl_pointer_listener kPointer = {ptrEnter,      ptrLeave,    ptrMotion,       ptrButton,  ptrAxis,   ptrFrame,
                                             ptrAxisSource, ptrAxisStop, ptrAxisDiscrete, ptrAxis120, ptrAxisRel};

static void seatCaps(void* d, wl_seat* s, uint32_t caps) { static_cast<App*>(d)->setupSeat(s, caps); }
static void seatName(void*, wl_seat*, const char*) {}
static const wl_seat_listener kSeat = {seatCaps, seatName};

// ── lifecycle ───────────────────────────────────────────────────────────────

App::App() = default;

App::~App() {
  for (auto& e : m_editSurfaces) destroyEditSurface(*e);
  for (auto& w : m_widgets) destroySurface(*w);
  if (m_egl != EGL_NO_DISPLAY) {
    eglMakeCurrent(m_egl, EGL_NO_SURFACE, EGL_NO_SURFACE, m_eglContext);
    m_depth.releaseGl();
    eglMakeCurrent(m_egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (m_eglContext != EGL_NO_CONTEXT) eglDestroyContext(m_egl, m_eglContext);
    eglTerminate(m_egl);
  }
  if (m_cursorSurface) wl_surface_destroy(m_cursorSurface);
  if (m_cursorTheme) wl_cursor_theme_destroy(m_cursorTheme);
  if (m_inotify >= 0) close(m_inotify);
  if (m_display) wl_display_disconnect(m_display);
}

bool App::initWayland() {
  m_display = wl_display_connect(nullptr);
  if (!m_display) {
    US_ERROR("cannot connect to the Wayland display");
    return false;
  }
  m_registry = wl_display_get_registry(m_display);
  wl_registry_add_listener(m_registry, &kRegistry, this);
  wl_display_roundtrip(m_display);
  wl_display_roundtrip(m_display);  // output names/modes
  if (!m_compositor || !m_layerShell) {
    US_ERROR("compositor lacks wl_compositor or zwlr_layer_shell_v1");
    return false;
  }
  if (m_shm) {
    const char* size = std::getenv("XCURSOR_SIZE");
    m_cursorTheme = wl_cursor_theme_load(std::getenv("XCURSOR_THEME"), size ? std::atoi(size) : 24, m_shm);
    m_cursorSurface = wl_compositor_create_surface(m_compositor);
  }
  return true;
}

void App::onGlobal(wl_registry* reg, uint32_t name, const char* iface, uint32_t version) {
  std::string_view i(iface);
  if (i == wl_compositor_interface.name) {
    m_compositor = static_cast<wl_compositor*>(wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4u)));
  } else if (i == zwlr_layer_shell_v1_interface.name) {
    m_layerShell = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
  } else if (i == wl_shm_interface.name) {
    m_shm = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
  } else if (i == wl_seat_interface.name && !m_seat) {
    m_seat = static_cast<wl_seat*>(wl_registry_bind(reg, name, &wl_seat_interface, std::min(version, 7u)));
    wl_seat_add_listener(m_seat, &kSeat, this);
  } else if (i == wl_output_interface.name) {
    auto out = std::make_unique<Output>();
    out->global = name;
    out->wl = static_cast<wl_output*>(wl_registry_bind(reg, name, &wl_output_interface, std::min(version, 4u)));
    auto* ctx = new OutputCtx{this, out.get()};
    wl_output_add_listener(out->wl, &kOutput, ctx);
    m_outputs.push_back(std::move(out));
  }
}

void App::onGlobalRemove(uint32_t name) {
  for (auto it = m_outputs.begin(); it != m_outputs.end(); ++it) {
    if ((*it)->global != name) continue;
    for (auto& w : m_widgets)
      if (w->output == it->get()) destroySurface(*w);
    for (auto e = m_editSurfaces.begin(); e != m_editSurfaces.end();) {
      if ((*e)->output == it->get()) {
        destroyEditSurface(**e);
        e = m_editSurfaces.erase(e);
      } else {
        ++e;
      }
    }
    wl_output_destroy((*it)->wl);
    m_outputs.erase(it);
    return;
  }
}

void App::onOutputDone(Output*) {
  // outputs can appear after startup (hotplug): (re)attach widgets
  if (m_eglContext != EGL_NO_CONTEXT) syncWidgets();
}

Output* App::outputByWl(wl_output* o) {
  for (auto& out : m_outputs)
    if (out->wl == o) return out.get();
  return nullptr;
}

std::vector<std::string> App::outputNames() const {
  std::vector<std::string> v;
  for (auto& o : m_outputs) v.push_back(o->name);
  return v;
}

void App::setupSeat(wl_seat* seat, uint32_t caps) {
  const bool hasPointer = caps & WL_SEAT_CAPABILITY_POINTER;
  if (hasPointer && !m_pointer) {
    m_pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(m_pointer, &kPointer, this);
  } else if (!hasPointer && m_pointer) {
    wl_pointer_release(m_pointer);
    m_pointer = nullptr;
  }
  const bool hasKeyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;
  if (hasKeyboard && !m_keyboard) {
    m_keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(m_keyboard, &kKeyboard, this);
  } else if (!hasKeyboard && m_keyboard) {
    wl_keyboard_release(m_keyboard);
    m_keyboard = nullptr;
  }
}

bool App::initEgl() {
  m_egl = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, m_display, nullptr);
  if (m_egl == EGL_NO_DISPLAY || !eglInitialize(m_egl, nullptr, nullptr)) {
    US_ERROR("EGL initialisation failed");
    return false;
  }
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                          EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  EGLint n = 0;
  if (!eglChooseConfig(m_egl, attrs, &m_eglConfig, 1, &n) || n < 1) {
    US_ERROR("no suitable EGL config");
    return false;
  }
  const EGLint ctxAttrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  m_eglContext = eglCreateContext(m_egl, m_eglConfig, EGL_NO_CONTEXT, ctxAttrs);
  if (m_eglContext == EGL_NO_CONTEXT) {
    US_ERROR("could not create a GLES 3 context");
    return false;
  }
  eglMakeCurrent(m_egl, EGL_NO_SURFACE, EGL_NO_SURFACE, m_eglContext);
  US_INFO("GL: {} / {}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
          reinterpret_cast<const char*>(glGetString(GL_VERSION)));
  return true;
}

// ── config / noctalia / depth ──────────────────────────────────────────────

void App::loadConfig() {
  try {
    m_config = Config::load(m_configPath);
  } catch (...) {
    US_WARN("keeping the previous configuration");
    return;
  }
  int maxBars = 16;
  for (auto& w : m_config.widgets)
    if (w.type == "visualizer") maxBars = std::max(maxBars, VisualizerConfig::fromTable(w.options).bars);
  m_audio.setBandCount(maxBars);
  m_audio.setNoiseReduction(static_cast<float>(m_config.noiseReduction));
  m_audio.setMonstercat(m_config.monstercat);
  syncWidgets();
}

void App::refreshNoctalia() {
  if (m_noctalia.refresh()) {
    for (auto& w : m_widgets) {
      if (w->impl) w->impl->configure(w->cfg, m_noctalia.state());
      w->needsRender = true;
    }
    updateDepth();
  }
}

void App::updateDepth() {
  if (m_depth.update(m_noctalia.state(), outputNames())) {
    for (auto& w : m_widgets) w->needsRender = true;
  }
}

// Keeps m_widgets in step with the config: reconfigures existing widgets in
// place (so a saved file or an editor drop never flickers), adds new ones and
// drops removed ones.
void App::syncWidgets() {
  std::vector<std::unique_ptr<Widget>> next;
  for (const auto& wc : m_config.widgets) {
    if (!wc.enabled) continue;
    std::unique_ptr<Widget> w;
    for (auto& old : m_widgets) {
      if (old && old->cfg.id == wc.id) {
        w = std::move(old);
        break;
      }
    }
    if (!w) w = std::make_unique<Widget>();
    if (!w->impl || w->cfg.type != wc.type) {
      destroySurface(*w);
      w->impl = createWidget(wc.type);
      if (!w->impl) {
        US_WARN("widget {}: unknown type '{}'", wc.id, wc.type);
        continue;
      }
    }
    const bool wasFull = w->impl->fullscreen();
    const bool geomChanged = w->cfg.x != wc.x || w->cfg.y != wc.y || w->cfg.width != wc.width ||
                             w->cfg.height != wc.height || w->cfg.output != wc.output;
    w->cfg = wc;
    for (auto& o : m_outputs)
      if (wc.output.empty() || o->name == wc.output) {
        clampToOutput(w->cfg, o.get());
        break;
      }
    w->impl->configure(w->cfg, m_noctalia.state());
    w->needsRender = true;
    Output* out = nullptr;
    for (auto& o : m_outputs)
      if (wc.output.empty() ? true : o->name == wc.output) {
        out = o.get();
        break;
      }
    if (out != w->output || !w->surface || wasFull != w->impl->fullscreen()) {
      destroySurface(*w);
      w->output = out;
      if (out) createSurface(*w);
    } else if (geomChanged && w->layer) {
      if (w->impl->fullscreen()) {
        destroySurface(*w);
        createSurface(*w);
      } else {
        zwlr_layer_surface_v1_set_size(w->layer, static_cast<uint32_t>(w->cfg.width), static_cast<uint32_t>(w->cfg.height));
        zwlr_layer_surface_v1_set_margin(w->layer, w->cfg.y, 0, 0, w->cfg.x);
        wl_surface_commit(w->surface);
        w->appliedX = w->cfg.x;
        w->appliedY = w->cfg.y;
      }
    }
    next.push_back(std::move(w));
  }
  for (auto& old : m_widgets)
    if (old) destroySurface(*old);
  m_widgets = std::move(next);
  if (!m_pendingSelect.empty())
    for (auto& w : m_widgets)
      if (w->cfg.id == m_pendingSelect) {
        m_selected = w.get();
        m_pendingSelect.clear();
      }
  validateEditPointers();
}

// ── surfaces ────────────────────────────────────────────────────────────────

void App::createSurface(Widget& w) {
  if (!w.output) return;
  w.surface = wl_compositor_create_surface(m_compositor);
  w.scale = w.output->scale;
  wl_surface_set_buffer_scale(w.surface, w.scale);
  // Bottom layer, like Noctalia's own desktop widgets: above the wallpaper,
  // below every window.
  w.layer = zwlr_layer_shell_v1_get_layer_surface(m_layerShell, w.surface, w.output->wl,
                                                  ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "undershell");
  auto* ctx = new WidgetCtx{this, &w};
  zwlr_layer_surface_v1_add_listener(w.layer, &kLayer, ctx);
  if (w.impl->fullscreen()) {
    zwlr_layer_surface_v1_set_anchor(w.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(w.layer, 0, 0);
  } else {
    zwlr_layer_surface_v1_set_anchor(w.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_size(w.layer, static_cast<uint32_t>(w.cfg.width), static_cast<uint32_t>(w.cfg.height));
    zwlr_layer_surface_v1_set_margin(w.layer, w.cfg.y, 0, 0, w.cfg.x);
  }
  // -1: position against the output edge, ignoring bars' reserved space, so
  // coordinates match the wallpaper (and the depth mask) exactly.
  zwlr_layer_surface_v1_set_exclusive_zone(w.layer, -1);
  w.appliedX = w.cfg.x;
  w.appliedY = w.cfg.y;
  zwlr_layer_surface_v1_set_keyboard_interactivity(w.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
  applyInputRegion(w);
  wl_surface_commit(w.surface);
  w.configured = false;
}

void App::destroySurface(Widget& w) {
  if (w.eglSurface != EGL_NO_SURFACE) {
    eglMakeCurrent(m_egl, EGL_NO_SURFACE, EGL_NO_SURFACE, m_eglContext);
    eglDestroySurface(m_egl, w.eglSurface);
    w.eglSurface = EGL_NO_SURFACE;
  }
  if (w.eglWindow) wl_egl_window_destroy(w.eglWindow);
  w.eglWindow = nullptr;
  if (w.frameCb) {
    delete static_cast<WidgetCtx*>(wl_proxy_get_user_data(reinterpret_cast<wl_proxy*>(w.frameCb)));
    wl_callback_destroy(w.frameCb);
  }
  w.frameCb = nullptr;
  if (w.layer) zwlr_layer_surface_v1_destroy(w.layer);
  w.layer = nullptr;
  if (w.surface) wl_surface_destroy(w.surface);
  w.surface = nullptr;
  w.configured = false;
  if (m_pointerWidget == &w) {
    m_pointerWidget = nullptr;
    m_drag = Drag::None;
  }
}

void App::applyInputRegion(Widget& w) {
  if (!w.surface) return;
  // click-through except where the widget asks for input (buttons, rails)
  wl_region* region = wl_compositor_create_region(m_compositor);
  if (w.impl && !w.impl->fullscreen())
    for (const Rect& r : w.inputRects)
      wl_region_add(region, static_cast<int>(std::floor(r.x)), static_cast<int>(std::floor(r.y)),
                    static_cast<int>(std::ceil(r.w)), static_cast<int>(std::ceil(r.h)));
  wl_surface_set_input_region(w.surface, region);
  wl_region_destroy(region);
}

void App::updateInputRegion(Widget& w) {
  if (!w.impl) return;
  std::vector<Rect> rects = w.impl->inputRects();
  auto same = [](const std::vector<Rect>& a, const std::vector<Rect>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
      if (std::abs(a[i].x - b[i].x) > 0.5F || std::abs(a[i].y - b[i].y) > 0.5F || std::abs(a[i].w - b[i].w) > 0.5F ||
          std::abs(a[i].h - b[i].h) > 0.5F)
        return false;
    return true;
  };
  if (same(rects, w.inputRects)) return;
  w.inputRects = std::move(rects);
  applyInputRegion(w);  // double-buffered: lands with the next commit (swap)
}

// Pointer on a widget surface (outside the editor): forwarded to the widget.
bool App::widgetPointer(PointerEvent::Type type, wl_surface* s, double x, double y, uint32_t button) {
  if (type == PointerEvent::Enter) m_hoverWidget = widgetBySurface(s);
  Widget* w = m_hoverWidget;
  if (!w || !w->impl) return false;
  if (type != PointerEvent::Press && type != PointerEvent::Release) {
    m_px = x;
    m_py = y;
  }
  PointerEvent ev;
  ev.type = type;
  ev.x = static_cast<float>(type == PointerEvent::Press || type == PointerEvent::Release ? m_px : x);
  ev.y = static_cast<float>(type == PointerEvent::Press || type == PointerEvent::Release ? m_py : y);
  ev.button = button;
  if (w->impl->onPointer(ev)) {
    w->needsRender = true;
    w->drewEmpty = false;
  }
  if (type == PointerEvent::Leave) {
    m_hoverWidget = nullptr;
  } else {
    const char* want = w->impl->cursor();
    setCursor(std::string_view(want) == "pointer" ? "pointer" : "default");
  }
  return true;
}

void App::onLayerConfigure(Widget* w, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t width, uint32_t height) {
  zwlr_layer_surface_v1_ack_configure(ls, serial);
  int nw = width ? static_cast<int>(width) : w->cfg.width;
  int nh = height ? static_cast<int>(height) : w->cfg.height;
  if (!w->eglWindow) {
    w->eglWindow = wl_egl_window_create(w->surface, nw * w->scale, nh * w->scale);
    w->eglSurface = eglCreatePlatformWindowSurface(m_egl, m_eglConfig, w->eglWindow, nullptr);
    eglMakeCurrent(m_egl, w->eglSurface, w->eglSurface, m_eglContext);
    eglSwapInterval(m_egl, 0);  // pacing comes from frame callbacks
  } else if (nw != w->w || nh != w->h) {
    wl_egl_window_resize(w->eglWindow, nw * w->scale, nh * w->scale, 0, 0);
  }
  w->w = nw;
  w->h = nh;
  w->configured = true;
  w->needsRender = true;
  w->drewEmpty = false;
}

void App::onLayerClosed(Widget* w) {
  destroySurface(*w);
  w->output = nullptr;
}

void App::onFrameDone(Widget* w) {
  // keep drawing while the picture moves; otherwise stay asleep
  TickContext ctx;
  ctx.now = nowSeconds();
  ctx.audio = audioFrame();
  ctx.media = &m_media;
  if ((w->impl && w->impl->animating(ctx)) || m_drag != Drag::None) w->needsRender = true;
}

// The shared audio input: analyser bands normalised to 0..1 (or the demo).
AudioFrame App::audioFrame() {
  static std::vector<float> bands;
  AudioFrame f;
  // while editing in silence the looks move on the demo spectrum
  const bool demo = m_demo || (m_edit && (!m_audioOk || m_audio.idle()));
  const bool silent = !demo && (!m_audioOk || m_audio.idle());
  const auto& vals = demo ? m_demoBands : m_audio.values();
  bands.resize(vals.size());
  for (size_t i = 0; i < vals.size(); ++i) bands[i] = silent ? 0.0F : vals[i] / 0.9F;
  f.bands = &bands;
  f.silent = silent;
  f.energy = silent ? 0.0 : (demo ? 0.3 : m_audio.energy());
  return f;
}

void App::render(Widget& w) {
  if (!w.configured || w.eglSurface == EGL_NO_SURFACE || w.frameCb || !w.impl) return;
  const double now = nowSeconds();
  const double dt = w.lastTick > 0 ? std::min(0.1, now - w.lastTick) : 1.0 / 60;
  w.lastTick = now;

  TickContext tctx;
  tctx.now = now;
  tctx.dt = dt;
  tctx.audio = audioFrame();
  tctx.media = &m_media;
  w.impl->tick(tctx);

  const bool show = w.impl->visible();
  if (!show && w.drewEmpty) {
    w.needsRender = false;
    return;
  }

  eglMakeCurrent(m_egl, w.eglSurface, w.eglSurface, m_eglContext);
  glViewport(0, 0, w.w * w.scale, w.h * w.scale);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  const float ow = w.output ? w.output->logicalW() : 1920.0F;
  const float oh = w.output ? w.output->logicalH() : 1080.0F;
  if (show) {
    DrawContext dctx;
    dctx.w = static_cast<float>(w.w);
    dctx.h = static_cast<float>(w.h);
    dctx.outputW = ow;
    dctx.outputH = oh;
    dctx.scale = w.scale;
    dctx.text = &m_text;
    dctx.media = &m_media;
    dctx.now = now;
    m_media.uploadPending();
    try {
      w.impl->draw(dctx);
    } catch (const std::exception& ex) {
      // a broken look must not take the daemon (and every other widget) down
      US_ERROR("widget {}: draw failed: {}", w.cfg.id, ex.what());
    }
    if (w.cfg.depth && w.output) {
      if (const DepthMask* m = m_depth.get(w.output->name)) {
        MaskParams mp;
        mp.texture = m->texture;
        mp.surfaceW = static_cast<float>(w.w);
        mp.surfaceH = static_cast<float>(w.h);
        mp.offsetX = w.impl->fullscreen() ? 0.0F : static_cast<float>(w.cfg.x);
        mp.offsetY = w.impl->fullscreen() ? 0.0F : static_cast<float>(w.cfg.y);
        mp.outputW = ow;
        mp.outputH = oh;
        mp.imageW = static_cast<float>(m->width);
        mp.imageH = static_cast<float>(m->height);
        mp.fillMode = m_noctalia.state().fillMode;
        try {
          m_maskPass.draw(mp);
        } catch (const std::exception& ex) {
          US_ERROR("depth mask pass failed: {}", ex.what());
        }
      }
    }
  }
  w.drewEmpty = !show;
  updateInputRegion(w);
  w.frameCb = wl_surface_frame(w.surface);
  wl_callback_add_listener(w.frameCb, &kFrame, new WidgetCtx{this, &w});
  eglSwapBuffers(m_egl, w.eglSurface);
  w.lastRender = now;
  w.needsRender = false;
  ++w.frames;
  if (now - w.markAt >= 1.0) {
    w.fpsMeasured = (w.frames - w.framesAtMark) / (now - w.markAt);
    w.framesAtMark = w.frames;
    w.markAt = now;
  }
}

// ── edit mode ───────────────────────────────────────────────────────────────
// While editing, a transparent fullscreen surface on the overlay layer takes
// the pointer and keyboard. Pointer positions on it are output coordinates,
// so dragging never depends on where the (moving) widget surface is.

void App::setEditMode(bool on) {
  if (on == m_edit) return;
  m_edit = on;
  m_drag = Drag::None;
  m_pointerWidget = nullptr;
  m_pointerEdit = nullptr;
  m_repeatKey = 0;
  m_galleryOpen = false;
  m_guidesV.clear();
  m_guidesH.clear();
  if (on && !m_selected)
    for (auto& w : m_widgets)
      if (w->impl && !w->impl->fullscreen()) {
        m_selected = w.get();
        break;
      }
  if (on) {
    for (auto& o : m_outputs) createEditSurface(o.get());
  } else {
    for (auto& e : m_editSurfaces) destroyEditSurface(*e);
    m_editSurfaces.clear();
  }
  US_INFO("edit mode {}", on ? "on" : "off");
}

void App::createEditSurface(Output* o) {
  auto e = std::make_unique<EditSurface>();
  e->output = o;
  e->scale = o->scale;
  e->surface = wl_compositor_create_surface(m_compositor);
  wl_surface_set_buffer_scale(e->surface, e->scale);
  e->layer = zwlr_layer_shell_v1_get_layer_surface(m_layerShell, e->surface, o->wl, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                                                   "undershell-editor");
  zwlr_layer_surface_v1_add_listener(e->layer, &kEditLayer, new EditCtx{this, e.get()});
  zwlr_layer_surface_v1_set_anchor(e->layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  zwlr_layer_surface_v1_set_size(e->layer, 0, 0);
  zwlr_layer_surface_v1_set_exclusive_zone(e->layer, -1);
  zwlr_layer_surface_v1_set_keyboard_interactivity(e->layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
  wl_surface_commit(e->surface);
  m_editSurfaces.push_back(std::move(e));
}

void App::destroyEditSurface(EditSurface& e) {
  if (e.eglSurface != EGL_NO_SURFACE) {
    eglMakeCurrent(m_egl, EGL_NO_SURFACE, EGL_NO_SURFACE, m_eglContext);
    eglDestroySurface(m_egl, e.eglSurface);
    e.eglSurface = EGL_NO_SURFACE;
  }
  if (e.eglWindow) wl_egl_window_destroy(e.eglWindow);
  e.eglWindow = nullptr;
  if (e.frameCb) {
    delete static_cast<EditCtx*>(wl_proxy_get_user_data(reinterpret_cast<wl_proxy*>(e.frameCb)));
    wl_callback_destroy(e.frameCb);
    e.frameCb = nullptr;
  }
  if (e.layer) zwlr_layer_surface_v1_destroy(e.layer);
  e.layer = nullptr;
  if (e.surface) wl_surface_destroy(e.surface);
  e.surface = nullptr;
}

void App::onEditConfigure(EditSurface* e, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t width, uint32_t height) {
  zwlr_layer_surface_v1_ack_configure(ls, serial);
  const int nw = width ? static_cast<int>(width) : static_cast<int>(e->output->logicalW());
  const int nh = height ? static_cast<int>(height) : static_cast<int>(e->output->logicalH());
  if (!e->eglWindow) {
    e->eglWindow = wl_egl_window_create(e->surface, nw * e->scale, nh * e->scale);
    e->eglSurface = eglCreatePlatformWindowSurface(m_egl, m_eglConfig, e->eglWindow, nullptr);
    eglMakeCurrent(m_egl, e->eglSurface, e->eglSurface, m_eglContext);
    eglSwapInterval(m_egl, 0);
  } else if (nw != e->w || nh != e->h) {
    wl_egl_window_resize(e->eglWindow, nw * e->scale, nh * e->scale, 0, 0);
  }
  e->w = nw;
  e->h = nh;
  e->configured = true;
  e->needsRender = true;
}

void App::onEditFrameDone(EditSurface*) {}  // redraws are driven by markEditDirty()

void App::markEditDirty() {
  for (auto& e : m_editSurfaces) e->needsRender = true;
}

void App::renderEdit(EditSurface& e) {
  if (!e.configured || e.eglSurface == EGL_NO_SURFACE || e.frameCb) return;
  std::vector<EditRect> rects;
  int hover = -1, active = -1, selected = -1;
  Color accent = Color::fromHex("#e2342a");
  for (auto& w : m_widgets) {
    if (w->output != e.output || !w->impl || w->impl->fullscreen() || !w->surface) continue;
    const int idx = static_cast<int>(rects.size());
    if (w.get() == m_pointerWidget) (m_drag != Drag::None ? active : hover) = idx;
    if (w.get() == m_selected) selected = idx;
    rects.push_back({static_cast<float>(w->cfg.x), static_cast<float>(w->cfg.y), static_cast<float>(w->cfg.width),
                     static_cast<float>(w->cfg.height)});
    accent = w->impl->accent();
  }
  eglMakeCurrent(m_egl, e.eglSurface, e.eglSurface, m_eglContext);
  glViewport(0, 0, e.w * e.scale, e.h * e.scale);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  try {
    m_overlay.draw(static_cast<float>(e.w), static_cast<float>(e.h), rects, hover, active, selected, accent,
                   static_cast<float>(m_config.gridSize), m_guidesV, m_guidesH);
    drawEditorText(e);
  } catch (const std::exception& ex) {
    US_ERROR("editor canvas failed: {}", ex.what());
  }
  e.frameCb = wl_surface_frame(e.surface);
  wl_callback_add_listener(e.frameCb, &kEditFrame, new EditCtx{this, &e});
  eglSwapBuffers(m_egl, e.eglSurface);
  e.needsRender = false;
}

EditSurface* App::editBySurface(wl_surface* s) {
  for (auto& e : m_editSurfaces)
    if (e->surface == s) return e.get();
  return nullptr;
}

Widget* App::widgetBySurface(wl_surface* s) {
  for (auto& w : m_widgets)
    if (w->surface == s) return w.get();
  return nullptr;
}

void App::setCursor(const char* name) {
  if (!m_pointer || !m_cursorTheme || !m_cursorSurface) return;
  wl_cursor* c = wl_cursor_theme_get_cursor(m_cursorTheme, name);
  if (!c) c = wl_cursor_theme_get_cursor(m_cursorTheme, "left_ptr");
  if (!c || c->image_count == 0) return;
  wl_cursor_image* img = c->images[0];
  wl_surface_attach(m_cursorSurface, wl_cursor_image_get_buffer(img), 0, 0);
  wl_surface_damage(m_cursorSurface, 0, 0, static_cast<int>(img->width), static_cast<int>(img->height));
  wl_surface_commit(m_cursorSurface);
  wl_pointer_set_cursor(m_pointer, m_pointerSerial, m_cursorSurface, static_cast<int>(img->hotspot_x),
                        static_cast<int>(img->hotspot_y));
}

// ── commands ────────────────────────────────────────────────────────────────

std::string App::handleCommand(const std::string& cmd) {
  if (cmd == "ping") return "pong";
  if (cmd == "edit") {
    setEditMode(!m_edit);
    return m_edit ? "edit on" : "edit off";
  }
  if (cmd == "edit-on") {
    setEditMode(true);
    return "edit on";
  }
  if (cmd == "edit-off") {
    setEditMode(false);
    return "edit off";
  }
  if (cmd.rfind("set ", 0) == 0) {
    // set <id|all> <key> <value>
    std::istringstream in(cmd.substr(4));
    std::string id, key, value;
    in >> id >> key;
    std::getline(in, value);
    value.erase(0, value.find_first_not_of(' '));
    if (id.empty() || key.empty() || value.empty()) return "error: usage: set <id|all> <key> <value>";
    auto isNumber = [](const std::string& v) {
      char* end = nullptr;
      std::strtod(v.c_str(), &end);
      return end && *end == '\0';
    };
    const std::string toml = (value == "true" || value == "false" || isNumber(value) || value.front() == '"')
                                 ? value
                                 : "\"" + value + "\"";
    int n = 0;
    for (auto& c : m_config.widgets)
      if (id == "all" || c.id == id) n += Config::setKey(m_configPath, c.id, key, toml) ? 1 : 0;
    if (n == 0) return "error: no widget '" + id + "'";
    m_reloadConfigAt = nowSeconds() + 0.05;
    return std::format("set {} = {} on {} widget(s)", key, toml, n);
  }
  if (cmd.rfind("add ", 0) == 0) {
    // add <type> [look]: a new widget centred on the first output
    std::istringstream in(cmd.substr(4));
    std::string type, look;
    in >> type >> look;
    if (!createWidget(type)) return "error: unknown widget type '" + type + "'";
    const std::string id = addWidget(type, look, INT32_MIN, INT32_MIN, false);
    return id.empty() ? "error: could not write the config" : "added " + id;
  }
  if (cmd.rfind("remove ", 0) == 0) {
    const std::string id = cmd.substr(7);
    for (auto& w : m_widgets)
      if (w->cfg.id == id) {
        removeWidget(*w);
        return "removed " + id;
      }
    // disabled widgets have no instance: remove the block directly
    return Config::removeBlock(m_configPath, id) ? "removed " + id : "error: no widget '" + id + "'";
  }
  if (cmd.rfind("select ", 0) == 0) {
    const std::string id = cmd.substr(7);
    for (auto& w : m_widgets)
      if (w->cfg.id == id) {
        m_selected = w.get();
        m_inspScroll = 0;
        markEditDirty();
        return "selected " + id;
      }
    return "error: no widget '" + id + "'";
  }
  if (cmd == "gallery") {
    if (!m_edit) setEditMode(true);
    m_galleryOpen = !m_galleryOpen;
    markEditDirty();
    return m_galleryOpen ? "gallery open" : "gallery closed";
  }
  if (cmd == "reset") {
    // bring every widget back to the bottom-centre of its output
    for (auto& w : m_widgets) {
      if (!w->output || w->impl->fullscreen()) continue;
      const int ow = static_cast<int>(w->output->logicalW()), oh = static_cast<int>(w->output->logicalH());
      w->cfg.width = std::min(w->cfg.width, ow);
      w->cfg.height = std::min(w->cfg.height, oh);
      zwlr_layer_surface_v1_set_size(w->layer, static_cast<uint32_t>(w->cfg.width), static_cast<uint32_t>(w->cfg.height));
      moveWidget(*w, (ow - w->cfg.width) / 2, oh - w->cfg.height - 64);
      wl_surface_commit(w->surface);
      persist(*w);
      w->needsRender = true;
    }
    return "reset";
  }
  if (cmd == "reload") {
    loadConfig();
    m_refreshNoctAt = nowSeconds();
    return "reloaded";
  }
  if (cmd == "demo" || cmd == "demo-on" || cmd == "demo-off") {
    m_demo = cmd == "demo" ? !m_demo : cmd == "demo-on";
    for (auto& w : m_widgets) w->needsRender = true;
    return m_demo ? "demo on" : "demo off";
  }
  if (cmd == "quit") {
    m_running = false;
    return "bye";
  }
  if (cmd == "status") {
    const MediaState& ms = m_media.state();
    std::string s = std::format("media: {}{}{}\n", ms.present ? (ms.playing ? "playing " : "paused ") : "none",
                                ms.present ? ms.title + " — " + ms.artist : std::string(),
                                ms.present ? std::format(" (cover {}x{}, lyrics {})", ms.coverW, ms.coverH,
                                                         ms.lyrics == MediaState::Lyrics::Synced ? "synced"
                                                         : ms.lyrics == MediaState::Lyrics::Plain ? "plain"
                                                         : ms.lyrics == MediaState::Lyrics::Searching ? "searching" : "none")
                                           : std::string());
    s += std::format("widgets={} edit={} audio={} palette_roles={} fill_mode={}\n", m_widgets.size(),
                                m_edit, m_audioOk ? (m_audio.idle() ? "idle" : "active") : "off",
                                m_noctalia.state().palette.size(), m_noctalia.state().fillMode);
    for (auto& w : m_widgets) {
      const DepthMask* m = w->output ? m_depth.get(w->output->name) : nullptr;
      const bool stale = nowSeconds() - w->markAt > 1.5;
      const std::string look = w->cfg.type == "visualizer" ? w->cfg.options["style"].value_or(std::string("bars"))
                                     : w->cfg.type == "clock" ? "clock:" + w->cfg.options["face"].value_or(std::string("digital"))
                                                              : w->cfg.type;
      s += std::format("  {} {} on {} at {},{} {}x{} depth={} frames={} fps={:.0f}\n", w->cfg.id, look,
                       w->output ? w->output->name : "-", w->cfg.x, w->cfg.y, w->cfg.width, w->cfg.height,
                       m ? fs::path(m->maskPath).filename().string().substr(0, 12) : "none", w->frames,
                       stale ? 0.0 : w->fpsMeasured);
    }
    return s;
  }
  return "error: unknown command (edit, edit-on, edit-off, demo, set, add, remove, select, gallery, reset, reload, status, quit)";
}

// ── file watching ───────────────────────────────────────────────────────────

void App::handleInotify() {
  alignas(inotify_event) char buf[8192];
  for (;;) {
    ssize_t n = read(m_inotify, buf, sizeof(buf));
    if (n <= 0) break;
    for (char* p = buf; p < buf + n;) {
      auto* ev = reinterpret_cast<inotify_event*>(p);
      std::string name = ev->len ? ev->name : "";
      const double now = nowSeconds();
      if (ev->wd == m_wdConfig && name == fs::path(m_configPath).filename().string()) m_reloadConfigAt = now + 0.15;
      if (ev->wd == m_wdNoctState && name == "settings.toml") m_refreshNoctAt = now + 0.5;
      if (ev->wd == m_wdNoctConfig && name.ends_with(".toml")) m_refreshNoctAt = now + 0.5;
      if (ev->wd == m_wdMasks && name.ends_with(".png")) m_refreshDepthAt = now + 0.3;
      p += sizeof(inotify_event) + ev->len;
    }
  }
}

int App::computeTimeout() {
  const double now = nowSeconds();
  double next = 1e9;
  for (double t : {m_reloadConfigAt, m_refreshNoctAt, m_refreshDepthAt})
    if (t > 0) next = std::min(next, t);
  for (auto& w : m_widgets) {
    if (!w->configured || !w->impl) continue;
    if (!w->needsRender) {
      next = std::min(next, w->impl->nextWakeup(now));
      continue;
    }
    if (w->frameCb) continue;
    const double due = w->lastRender + 1.0 / std::max(1, w->impl->fps());
    next = std::min(next, due);
  }
  for (auto& e : m_editSurfaces)
    if (e->needsRender && !e->frameCb) next = now;
  if (m_edit && m_repeatKey) next = std::min(next, m_repeatNext);
  int timeout = next >= 1e8 ? -1 : std::max(0, static_cast<int>(std::ceil((next - now) * 1000)));
  if (m_demo || (m_edit && (!m_audioOk || m_audio.idle()))) timeout = timeout < 0 ? 16 : std::min(timeout, 16);
  if (const int mt = m_media.pollTimeoutMs(now); mt >= 0) timeout = timeout < 0 ? mt : std::min(timeout, mt);
  if (m_audioOk) {
    int a = m_audio.pollTimeoutMs();
    if (a >= 0) timeout = timeout < 0 ? a : std::min(timeout, a);
  }
  return timeout;
}

// ── main loop ───────────────────────────────────────────────────────────────

int App::run() {
  std::signal(SIGINT, [](int) { g_quit = 1; });
  std::signal(SIGTERM, [](int) { g_quit = 1; });
  if (!m_ipc.listen([this](const std::string& c) { return handleCommand(c); })) return 1;
  if (!initWayland() || !initEgl()) return 1;

  m_configPath = Config::defaultPath();
  m_noctalia.refresh();
  m_depth.update(m_noctalia.state(), outputNames());
  loadConfig();
  m_audioOk = m_audio.start();
  m_media.start();

  m_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  const uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE;
  m_wdConfig = inotify_add_watch(m_inotify, fs::path(m_configPath).parent_path().c_str(), mask);
  m_wdNoctState = inotify_add_watch(m_inotify, fs::path(Noctalia::settingsPath()).parent_path().c_str(), mask);
  m_wdNoctConfig = inotify_add_watch(m_inotify, Noctalia::configDir().c_str(), mask);
  m_wdMasks = inotify_add_watch(m_inotify, DepthMasks::maskDir().c_str(), mask);

  while (m_running && !g_quit) {
    while (wl_display_prepare_read(m_display) != 0) wl_display_dispatch_pending(m_display);
    wl_display_flush(m_display);

    pollfd fds[6] = {{wl_display_get_fd(m_display), POLLIN, 0},
                     {m_audioOk ? m_audio.fd() : -1, POLLIN, 0},
                     {m_inotify, POLLIN, 0},
                     {m_ipc.fd(), POLLIN, 0},
                     {m_media.fd(), POLLIN, 0},
                     {m_jobs.fd(), POLLIN, 0}};
    int r = poll(fds, 6, computeTimeout());
    if (r < 0 && errno != EINTR) {
      wl_display_cancel_read(m_display);
      break;
    }
    if (r > 0 && (fds[0].revents & POLLIN)) {
      if (wl_display_read_events(m_display) < 0) break;
    } else {
      wl_display_cancel_read(m_display);
    }
    if (fds[0].revents & (POLLERR | POLLHUP)) break;
    wl_display_dispatch_pending(m_display);

    if (r > 0 && (fds[1].revents & POLLIN)) m_audio.dispatch();
    if (r > 0 && (fds[2].revents & POLLIN)) handleInotify();
    if (r > 0 && (fds[3].revents & POLLIN)) m_ipc.dispatch();
    if (r > 0 && (fds[4].revents & POLLIN)) m_media.dispatch();
    if (r > 0 && (fds[5].revents & POLLIN)) m_jobs.dispatch();

    const double now = nowSeconds();
    if (m_reloadConfigAt > 0 && now >= m_reloadConfigAt) {
      m_reloadConfigAt = 0;
      loadConfig();
    }
    if (m_refreshNoctAt > 0 && now >= m_refreshNoctAt) {
      m_refreshNoctAt = 0;
      refreshNoctalia();
    }
    if (m_refreshDepthAt > 0 && now >= m_refreshDepthAt) {
      m_refreshDepthAt = 0;
      updateDepth();
    }
    // safety net: a mask generated while an event was missed is found anyway
    if (m_depth.missing() && m_refreshDepthAt == 0) m_refreshDepthAt = now + 15;

    if (m_audioOk && m_audio.tick()) {
      for (auto& w : m_widgets)
        if (w->impl && w->impl->usesAudio()) w->needsRender = true;
    }
    // the media feed runs only while a widget shows it
    bool wantMedia = false, wantLyrics = false;
    for (auto& w : m_widgets)
      if (w->impl) {
        wantMedia = wantMedia || w->impl->wantsMedia();
        wantLyrics = wantLyrics || w->impl->wantsLyrics();
      }
    m_media.setWanted(wantMedia, wantLyrics);
    m_media.poll(now);
    m_media.dispatch();
    if (m_media.generation() != m_mediaGen) {
      m_mediaGen = m_media.generation();
      for (auto& w : m_widgets)
        if (w->impl && w->impl->wantsMedia()) {
          w->needsRender = true;
          w->drewEmpty = false;
        }
    }
    if (m_demo || m_edit) {
      // a synthetic spectrum: a bass beat under drifting mids and a little noise
      m_demoT = now;
      const int n = 64;
      m_demoBands.resize(n);
      const double beat = std::pow(std::max(0.0, std::sin(now * 2 * M_PI * 2.0)), 8.0);
      for (int i = 0; i < n; ++i) {
        const double x = i / double(n - 1);
        double v = 0.55 * std::exp(-x * 2.2) * (0.6 + 0.4 * beat) +
                   0.25 * (0.5 + 0.5 * std::sin(now * 3 + i * 0.35)) * (1 - x * 0.5) +
                   0.05 * (0.5 + 0.5 * std::sin(now * 17.0 + i * 2.1));
        m_demoBands[static_cast<size_t>(i)] = static_cast<float>(std::clamp(v, 0.0, 1.0) * 0.9);
      }
      for (auto& w : m_widgets) w->needsRender = true;
    }
    for (auto& w : m_widgets) {
      if (w->impl && !w->needsRender && now >= w->impl->nextWakeup(now)) w->needsRender = true;
      if (!w->needsRender) continue;
      if (now + 0.0005 < w->lastRender + 1.0 / std::max(1, w->impl->fps())) continue;
      render(*w);
    }
    if (m_edit) editorTick(now);
    for (auto& e : m_editSurfaces)
      if (e->needsRender) renderEdit(*e);
  }
  US_INFO("exiting");
  return 0;
}

}  // namespace undershell
