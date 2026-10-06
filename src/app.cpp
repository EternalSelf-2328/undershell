// SPDX-License-Identifier: GPL-3.0-or-later
#include "app.hpp"

#include "visualizer.hpp"

// the generated header names a parameter `namespace`
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace
#include "xdg-output-unstable-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <cerrno>
#include <cmath>
#include <sstream>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

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

// xdg-output: the output's size in logical (surface) pixels; since v3 it
// lands atomically with wl_output.done
static void xoPosition(void*, zxdg_output_v1*, int32_t, int32_t) {}
static void xoSize(void* d, zxdg_output_v1*, int32_t w, int32_t h) {
  auto* o = static_cast<Output*>(d);
  o->logW = w;
  o->logH = h;
}
static void xoDone(void*, zxdg_output_v1*) {}
static void xoName(void*, zxdg_output_v1*, const char*) {}
static void xoDesc(void*, zxdg_output_v1*, const char*) {}
static const zxdg_output_v1_listener kXdgOutput = {xoPosition, xoSize, xoDone, xoName, xoDesc};

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

// fractional-scale: the scale the compositor wants this surface drawn at
static void widgetScale(void* d, wp_fractional_scale_v1*, uint32_t s120) {
  auto* c = static_cast<WidgetCtx*>(d);
  c->app->onPreferredScale(c->w, nullptr, static_cast<float>(s120) / 120.0F);
}
static const wp_fractional_scale_v1_listener kWidgetScale = {widgetScale};
static void editScale(void* d, wp_fractional_scale_v1*, uint32_t s120) {
  auto* c = static_cast<EditCtx*>(d);
  c->app->onPreferredScale(nullptr, c->e, static_cast<float>(s120) / 120.0F);
}
static const wp_fractional_scale_v1_listener kEditScale = {editScale};
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
  } else if (i == wp_viewporter_interface.name) {
    m_viewporter = static_cast<wp_viewporter*>(wl_registry_bind(reg, name, &wp_viewporter_interface, 1));
  } else if (i == wp_fractional_scale_manager_v1_interface.name) {
    m_fractional = static_cast<wp_fractional_scale_manager_v1*>(
        wl_registry_bind(reg, name, &wp_fractional_scale_manager_v1_interface, 1));
  } else if (i == zxdg_output_manager_v1_interface.name) {
    m_xdgOutputs = static_cast<zxdg_output_manager_v1*>(
        wl_registry_bind(reg, name, &zxdg_output_manager_v1_interface, std::min(version, 3u)));
    for (auto& o : m_outputs) watchLogicalSize(o.get());
  } else if (i == wl_output_interface.name) {
    auto out = std::make_unique<Output>();
    out->global = name;
    out->wl = static_cast<wl_output*>(wl_registry_bind(reg, name, &wl_output_interface, std::min(version, 4u)));
    auto* ctx = new OutputCtx{this, out.get()};
    wl_output_add_listener(out->wl, &kOutput, ctx);
    watchLogicalSize(out.get());
    m_outputs.push_back(std::move(out));
  }
}

void App::watchLogicalSize(Output* o) {
  if (!m_xdgOutputs || o->xdg) return;
  o->xdg = zxdg_output_manager_v1_get_xdg_output(m_xdgOutputs, o->wl);
  zxdg_output_v1_add_listener(o->xdg, &kXdgOutput, o);
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
    if ((*it)->xdg) zxdg_output_v1_destroy((*it)->xdg);
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
  // two widgets sharing an id would be edited as one (older duplicates did that)
  if (const int n = Config::uniquifyIds(m_configPath); n > 0) US_INFO("renamed {} repeated widget id(s)", n);
  try {
    m_config = Config::load(m_configPath);
  } catch (...) {
    US_WARN("keeping the previous configuration");
    return;
  }
  if (m_config.language != uiLanguage()) {
    setUiLanguage(m_config.language);
    markEditDirty();
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
    checkProfile();
    checkWallpaperKind();
    if (anchorKey() != m_anchorKey) syncWidgets();  // another wallpaper or fill: re-anchor the layout
    restack();  // the plugin's plane may have moved
  }
}

// Layer-shell surfaces on one layer stack in the order they were mapped, so
// the widgets are kept bottom-to-top by (layer, depth): a widget nearer the
// viewer is drawn over a farther one where they overlap. Recreating surfaces
// is only done when that order changes.
void App::restack() {
  auto depthOf = [&](const Widget& w) {
    if (!w.cfg.depth || m_motion) return 2.0;  // not behind the scenery at all: in front of every plane
    return w.cfg.depthLevel > 0 ? w.cfg.depthLevel / 100.0 : m_noctalia.state().depthThreshold;
  };
  std::stable_sort(m_widgets.begin(), m_widgets.end(), [&](const auto& a, const auto& b) {
    if (a->cfg.layer != b->cfg.layer) return a->cfg.layer < b->cfg.layer;
    return depthOf(*a) < depthOf(*b);
  });
  std::vector<const Widget*> want;
  for (auto& w : m_widgets)
    if (w->surface) want.push_back(w.get());
  if (want == m_mapped) return;
  for (auto& w : m_widgets) destroySurface(*w);
  for (auto& w : m_widgets)
    if (w->output) createSurface(*w);
  US_DEBUG("restacked widgets");
}

void App::updateDepth() {
  if (m_depth.update(m_noctalia.state(), outputNames())) {
    for (auto& w : m_widgets) w->needsRender = true;
  }
}

// Keeps m_widgets in step with the config: reconfigures existing widgets in
// place (so a saved file or an editor drop never flickers), adds new ones and
// drops removed ones.
std::string App::anchorKey() const {
  const NoctaliaState& st = m_noctalia.state();
  std::string k = std::to_string(st.fillMode);
  for (auto& o : m_outputs) k += std::format("|{}:{}x{}:{}", o->name, o->logicalW(), o->logicalH(), st.wallpaperFor(o->name));
  return k;
}

SpaceMap App::spaceMapFor(const WidgetConfig& c, const Output* o) {
  if (!o || o->logicalW() <= 0 || o->logicalH() <= 0) return {};
  const NoctaliaState& st = m_noctalia.state();
  const std::string wall = st.wallpaperFor(o->name);
  auto it = m_imageSizes.find(wall);
  if (it == m_imageSizes.end()) {
    int iw = 0, ih = 0;
    if (!wall.empty() && !gdk_pixbuf_get_file_info(wall.c_str(), &iw, &ih)) iw = ih = 0;  // a video, a scene
    it = m_imageSizes.emplace(wall, std::make_pair(iw, ih)).first;
  }
  return spaceMap(blockSpaceW(c, o->logicalW()), blockSpaceH(c, o->logicalH()), o->logicalW(), o->logicalH(),
                  it->second.first, it->second.second, st.fillMode);
}

void App::syncWidgets() {
  m_anchorKey = anchorKey();
  std::vector<std::unique_ptr<Widget>> next;
  bool stamped = false;
  for (auto& stored : m_config.widgets) {
    if (!stored.enabled) continue;
    std::unique_ptr<Widget> w;
    for (auto& old : m_widgets) {
      if (old && old->cfg.id == stored.id) {
        w = std::move(old);
        break;
      }
    }
    if (!w) w = std::make_unique<Widget>();
    if (!w->impl || w->cfg.type != stored.type) {
      destroySurface(*w);
      w->impl = createWidget(stored.type);
      if (!w->impl) {
        US_WARN("widget {}: unknown type '{}'", stored.id, stored.type);
        continue;
      }
    }
    Output* out = nullptr;
    for (auto& o : m_outputs)
      if (stored.output.empty() ? true : o->name == stored.output) {
        out = o.get();
        break;
      }
    // a monitor this machine does not have (a layout from another setup, an
    // unplugged screen): show it on the first one, mapped to its size
    if (!out && !m_outputs.empty()) out = m_outputs.front().get();
    if ((stored.spaceW <= 0 || stored.spaceH <= 0) && out && out->logicalW() > 0 && out->logicalH() > 0) {
      // no space recorded (an older or hand-written block): it was made for
      // this screen; write that down so another screen can follow the wallpaper
      stored.spaceW = std::round(out->logicalW());
      stored.spaceH = std::round(out->logicalH());
      stamped |= Config::setKey(m_configPath, stored.id, "space",
                                std::format("[{}, {}]", static_cast<int>(stored.spaceW), static_cast<int>(stored.spaceH)));
    }
    // the block's box, moved to where its part of the wallpaper is on this output
    WidgetConfig wc = stored;
    w->toScreen = spaceMapFor(stored, out);
    mapWidget(wc, w->toScreen);
    if (out) clampToOutput(wc, out);
    const bool geomChanged = w->cfg.x != wc.x || w->cfg.y != wc.y || w->cfg.width != wc.width ||
                             w->cfg.height != wc.height || w->cfg.output != wc.output ||
                             std::abs(w->cfg.rotation - wc.rotation) > 1e-6 || std::abs(w->cfg.tiltX - wc.tiltX) > 1e-6 ||
                             std::abs(w->cfg.tiltY - wc.tiltY) > 1e-6 || std::abs(w->cfg.skewX - wc.skewX) > 1e-6 ||
                             std::abs(w->cfg.perspective - wc.perspective) > 1e-6 || w->cfg.pinned != wc.pinned ||
                             !std::equal(std::begin(wc.pin), std::end(wc.pin), std::begin(w->cfg.pin));
    w->cfg = wc;
    w->impl->configure(w->cfg, m_noctalia.state());
    w->needsRender = true;
    // a look that switches between boxed and fullscreen (frame) needs a new
    // surface; compare against how the surface was made, not the previous
    // config (the inspector reconfigures the widget before the reload lands)
    if (out != w->output || !w->surface || w->surfaceFullscreen != w->impl->fullscreen()) {
      destroySurface(*w);
      w->output = out;
      if (out) createSurface(*w);
    } else if (geomChanged && w->layer) {
      if (w->impl->fullscreen()) {
        destroySurface(*w);
        createSurface(*w);
      } else {
        placeLayer(*w);
      }
    }
    next.push_back(std::move(w));
  }
  if (stamped) US_INFO("recorded the screen size of older widget blocks (space = [w, h])");
  for (auto& old : m_widgets)
    if (old) destroySurface(*old);
  m_widgets = std::move(next);
  restack();
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
  m_mapped.push_back(&w);
  w.surface = wl_compositor_create_surface(m_compositor);
  if (fractional()) {
    // drawn at the output's real pixels and shown 1:1 (a 0.98 scale would
    // otherwise resample every surface, softening text)
    w.scale = initialScale(w.output);
    w.viewport = wp_viewporter_get_viewport(m_viewporter, w.surface);
    w.fraction = wp_fractional_scale_manager_v1_get_fractional_scale(m_fractional, w.surface);
    wp_fractional_scale_v1_add_listener(w.fraction, &kWidgetScale, new WidgetCtx{this, &w});
  } else {
    w.scale = static_cast<float>(w.output->scale);
    wl_surface_set_buffer_scale(w.surface, w.output->scale);
  }
  // Bottom layer, like Noctalia's own desktop widgets: above the wallpaper,
  // below every window.
  w.layer = zwlr_layer_shell_v1_get_layer_surface(m_layerShell, w.surface, w.output->wl,
                                                  ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "undershell");
  auto* ctx = new WidgetCtx{this, &w};
  zwlr_layer_surface_v1_add_listener(w.layer, &kLayer, ctx);
  w.surfaceFullscreen = w.impl->fullscreen();
  if (w.surfaceFullscreen) {
    zwlr_layer_surface_v1_set_anchor(w.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(w.layer, 0, 0);
  } else {
    zwlr_layer_surface_v1_set_anchor(w.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    const Box b = surfaceBox(w.cfg);
    zwlr_layer_surface_v1_set_size(w.layer, static_cast<uint32_t>(b.w), static_cast<uint32_t>(b.h));
    zwlr_layer_surface_v1_set_margin(w.layer, b.y, 0, 0, b.x);
    w.appliedX = b.x;
    w.appliedY = b.y;
  }
  // -1: position against the output edge, ignoring bars' reserved space, so
  // coordinates match the wallpaper (and the depth mask) exactly.
  zwlr_layer_surface_v1_set_exclusive_zone(w.layer, -1);
  zwlr_layer_surface_v1_set_keyboard_interactivity(w.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
  applyInputRegion(w);
  wl_surface_commit(w.surface);
  w.configured = false;
}

void App::placeLayer(Widget& w) {
  if (!w.layer || (w.impl && w.impl->fullscreen())) return;
  const Box b = surfaceBox(w.cfg);
  zwlr_layer_surface_v1_set_size(w.layer, static_cast<uint32_t>(b.w), static_cast<uint32_t>(b.h));
  zwlr_layer_surface_v1_set_margin(w.layer, b.y, 0, 0, b.x);
  applyInputRegion(w);
  wl_surface_commit(w.surface);
  w.appliedX = b.x;
  w.appliedY = b.y;
  w.needsRender = true;
  w.drewEmpty = false;
}

void App::destroySurface(Widget& w) {
  std::erase(m_mapped, &w);
  if (w.eglSurface != EGL_NO_SURFACE || w.rtFbo) {
    eglMakeCurrent(m_egl, EGL_NO_SURFACE, EGL_NO_SURFACE, m_eglContext);
    if (w.rtFbo) glDeleteFramebuffers(1, &w.rtFbo);
    if (w.rtTex) glDeleteTextures(1, &w.rtTex);
    w.rtFbo = w.rtTex = 0;
    w.rtW = w.rtH = 0;
  }
  if (w.eglSurface != EGL_NO_SURFACE) {
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
  if (w.fraction) {
    delete static_cast<WidgetCtx*>(wl_proxy_get_user_data(reinterpret_cast<wl_proxy*>(w.fraction)));
    wp_fractional_scale_v1_destroy(w.fraction);
    w.fraction = nullptr;
  }
  if (w.viewport) wp_viewport_destroy(w.viewport);
  w.viewport = nullptr;
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
  if (w.impl && !w.impl->fullscreen()) {
    const Box b = surfaceBox(w.cfg);
    for (const Rect& r : w.inputRects) {
      // widget px -> surface px: the bounding box of the turned rect
      double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
      for (auto [lx, ly] : {std::pair{r.x, r.y}, {r.x + r.w, r.y}, {r.x, r.y + r.h}, {r.x + r.w, r.y + r.h}}) {
        double ox = 0, oy = 0;
        toOutput(w.cfg, lx, ly, ox, oy);
        x0 = std::min(x0, ox - b.x), y0 = std::min(y0, oy - b.y);
        x1 = std::max(x1, ox - b.x), y1 = std::max(y1, oy - b.y);
      }
      wl_region_add(region, static_cast<int>(std::floor(x0)), static_cast<int>(std::floor(y0)),
                    static_cast<int>(std::ceil(x1 - x0)), static_cast<int>(std::ceil(y1 - y0)));
    }
  }
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
    if (!w->impl->fullscreen()) {
      // surface px -> the widget's own coordinates (identity unless turned)
      const Box b = surfaceBox(w->cfg);
      toLocal(w->cfg, x + b.x, y + b.y, x, y);
    }
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
  const int nw = width ? static_cast<int>(width) : w->cfg.width;
  const int nh = height ? static_cast<int>(height) : w->cfg.height;
  sizeBuffer(*w, nw, nh);
  w->configured = true;
  w->needsRender = true;
  w->drewEmpty = false;
}

float App::initialScale(const Output* o) const {
  // until the compositor says: the output's pixels per logical px
  if (o && o->logW > 0 && o->modeW > 0) return static_cast<float>(o->modeW) / static_cast<float>(o->logW);
  return o ? static_cast<float>(o->scale) : 1.0F;
}

// (re)sizes the buffer behind a surface of nw x nh logical px
template <class S> void App::sizeBuffer(S& x, int nw, int nh) {
  const int bw = std::max(1, static_cast<int>(std::lround(nw * x.scale)));
  const int bh = std::max(1, static_cast<int>(std::lround(nh * x.scale)));
  if (x.viewport) wp_viewport_set_destination(x.viewport, nw, nh);
  if (!x.eglWindow) {
    x.eglWindow = wl_egl_window_create(x.surface, bw, bh);
    x.eglSurface = eglCreatePlatformWindowSurface(m_egl, m_eglConfig, x.eglWindow, nullptr);
    eglMakeCurrent(m_egl, x.eglSurface, x.eglSurface, m_eglContext);
    eglSwapInterval(m_egl, 0);  // pacing comes from frame callbacks
  } else if (bw != x.bufW || bh != x.bufH) {
    wl_egl_window_resize(x.eglWindow, bw, bh, 0, 0);
  }
  x.w = nw;
  x.h = nh;
  x.bufW = bw;
  x.bufH = bh;
}

void App::onPreferredScale(Widget* w, EditSurface* e, float scale) {
  if (scale <= 0) return;
  US_DEBUG("preferred scale {:.4f} for {}", scale, w ? w->cfg.id : std::string("editor"));
  if (w && std::abs(w->scale - scale) > 1e-4F) {
    w->scale = scale;
    if (w->eglWindow) sizeBuffer(*w, w->w, w->h);
    w->needsRender = true;
    w->drewEmpty = false;
  }
  if (e && std::abs(e->scale - scale) > 1e-4F) {
    e->scale = scale;
    if (e->eglWindow) sizeBuffer(*e, e->w, e->h);
    e->needsRender = true;
  }
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
AudioFrame App::audioFrame(const Widget* w) {
  static std::vector<float> bands;
  AudioFrame f;
  // while editing in silence the looks move on the demo spectrum; so does a
  // visualizer whose "nothing playing" mode is demo
  const bool audioIdle = !m_audioOk || m_audio.idle();
  const bool idleDemo = w && m_nothingPlaying && idleModeOf(*w) == "demo";
  const bool demo = m_demo || (m_edit && audioIdle) || idleDemo;
  const bool silent = !demo && audioIdle;
  const auto& vals = demo ? m_demoBands : m_audio.values();
  bands.resize(vals.size());
  for (size_t i = 0; i < vals.size(); ++i) bands[i] = silent ? 0.0F : vals[i] / 0.9F;
  f.bands = &bands;
  f.silent = silent;
  f.energy = silent ? 0.0 : (demo ? 0.3 : m_audio.energy());
  return f;
}

// a visualizer's behaviour with nothing playing: its own "idle" option, or
// [general] idle when that is "auto" (or unset)
static constexpr double kIdleGrace = 0.35;

std::string App::idleModeOf(const Widget& w) const {
  if (!w.impl || !w.impl->usesAudio()) return "show";
  const std::string own = w.cfg.options["idle"].value_or(std::string("auto"));
  return own == "show" || own == "hide" || own == "demo" ? own : m_config.idle;
}

// "Nothing playing" = no sound for kIdleGrace; sound brings everything
// back at once. Hidden visualizers fade out and
// stop drawing; editing always shows them.
void App::updateIdle(double now) {
  // silence straight from the level (the analyser itself waits a second
  // before calling itself idle), with a third of a second of grace
  const bool silentNow = !m_audioOk || m_audio.idle() || m_audio.energy() < 0.004;
  if (!silentNow) m_silentSince = -1;
  else if (m_silentSince < 0) m_silentSince = now;
  const bool nothing = silentNow && now - m_silentSince > kIdleGrace;
  if (nothing != m_nothingPlaying) {
    m_nothingPlaying = nothing;
    US_DEBUG("{}", nothing ? "nothing playing" : "music back");
  }
  for (auto& w : m_widgets) {
    if (!w->impl || !w->impl->usesAudio()) continue;
    const std::string mode = idleModeOf(*w);
    const bool hide = m_nothingPlaying && mode == "hide" && !m_edit && !m_demo;
    const int state = hide ? 1 : (m_nothingPlaying && mode == "demo") ? 2 : 0;
    w->impl->setHidden(hide);
    if (state != w->idleState) {
      // a nudge on the change only: the fade (or the demo) then keeps the
      // frames coming by itself, and a faded-out widget stops drawing
      w->idleState = state;
      w->needsRender = true;
      w->drewEmpty = false;
    }
  }
}

void App::render(Widget& w) {
  if (!w.configured || w.eglSurface == EGL_NO_SURFACE || w.frameCb || !w.impl) return;
  const double now = nowSeconds();
  const double dt = w.lastTick > 0 ? std::min(0.1, now - w.lastTick) : 1.0 / 60;
  w.lastTick = now;

  TickContext tctx;
  tctx.now = now;
  tctx.dt = dt;
  tctx.audio = audioFrame(&w);
  tctx.media = &m_media;
  w.impl->tick(tctx);

  const bool show = w.impl->visible();
  if (!show && w.drewEmpty) {
    w.needsRender = false;
    return;
  }

  eglMakeCurrent(m_egl, w.eglSurface, w.eglSurface, m_eglContext);
  // A turned widget draws its own box off-screen, then that picture is laid
  // on the surface at its angle; the depth mask comes after, in output space.
  const bool turn = show && !w.impl->fullscreen() && rotated(w.cfg);
  if (turn) {
    const int tw = std::max(1, static_cast<int>(std::lround(w.cfg.width * w.scale)));
    const int th = std::max(1, static_cast<int>(std::lround(w.cfg.height * w.scale)));
    if (!w.rtFbo || w.rtW != tw || w.rtH != th) {
      if (!w.rtTex) glGenTextures(1, &w.rtTex);
      glBindTexture(GL_TEXTURE_2D, w.rtTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      if (!w.rtFbo) glGenFramebuffers(1, &w.rtFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, w.rtFbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, w.rtTex, 0);
      w.rtW = tw;
      w.rtH = th;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, w.rtFbo);
    glViewport(0, 0, tw, th);
  } else {
    glViewport(0, 0, w.bufW, w.bufH);
  }
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  const float ow = w.output ? w.output->logicalW() : 1920.0F;
  const float oh = w.output ? w.output->logicalH() : 1080.0F;
  if (show) {
    DrawContext dctx;
    dctx.w = static_cast<float>(turn ? w.cfg.width : w.w);
    dctx.h = static_cast<float>(turn ? w.cfg.height : w.h);
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
    if (turn) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, w.bufW, w.bufH);
      glClear(GL_COLOR_BUFFER_BIT);
      const Box b = surfaceBox(w.cfg);
      try {
        if (w.cfg.meshed) {
          m_meshBlit.draw(w.rtTex, static_cast<float>(w.w), static_cast<float>(w.h), meshVertices(w.cfg, b.x, b.y),
                          meshSteps(w.cfg));
        } else if (warped(w.cfg)) {
          const Homography toUv = surfaceToUv(w.cfg);
          m_warpBlit.draw(w.rtTex, static_cast<float>(w.w), static_cast<float>(w.h), toUv.m);
        } else
        m_blit.draw(w.rtTex, static_cast<float>(w.w), static_cast<float>(w.h),
                    static_cast<float>(w.cfg.x + w.cfg.width / 2.0 - b.x), static_cast<float>(w.cfg.y + w.cfg.height / 2.0 - b.y),
                    static_cast<float>(w.cfg.width), static_cast<float>(w.cfg.height), static_cast<float>(w.cfg.rotation));
      } catch (const std::exception& ex) {
        US_ERROR("widget {}: rotated blit failed: {}", w.cfg.id, ex.what());
      }
    }
    if (w.cfg.depth && w.output && !m_motion) {  // a moving wallpaper has no mask that fits
      if (const DepthMask* m = m_depth.get(w.output->name)) {
        MaskParams mp;
        mp.texture = m->texture;
        mp.surfaceW = static_cast<float>(w.w);
        mp.surfaceH = static_cast<float>(w.h);
        const Box b = surfaceBox(w.cfg);
        mp.offsetX = w.impl->fullscreen() ? 0.0F : static_cast<float>(b.x);
        mp.offsetY = w.impl->fullscreen() ? 0.0F : static_cast<float>(b.y);
        mp.outputW = ow;
        mp.outputH = oh;
        mp.imageW = static_cast<float>(m->texture ? m->width : m->imageW);
        mp.imageH = static_cast<float>(m->texture ? m->height : m->imageH);
        mp.fillMode = m_noctalia.state().fillMode;
        if ((w.cfg.depthLevel > 0 || m->hasEdits) && m->field) {
          // its own plane (or the plugin's, over hand-corrected depth): the
          // refined field cut at this widget's level
          mp.field = m->field;
          mp.level = static_cast<float>(w.cfg.depthLevel > 0 ? w.cfg.depthLevel / 100.0 : m_noctalia.state().depthThreshold);
          mp.feather = static_cast<float>(m_noctalia.state().depthFeather);
          mp.imageW = static_cast<float>(m->imageW);
          mp.imageH = static_cast<float>(m->imageH);
        }
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
  m_helpOpen = false;
  m_savesOpen = false;
  m_fontPickFor.clear();
  m_confirmDelete.clear();
  if (!on) {
    commitRename();
    setPaintMode(false);
  }
  m_uiHover = -1;
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
  e->surface = wl_compositor_create_surface(m_compositor);
  if (fractional()) {
    e->scale = initialScale(o);
    e->viewport = wp_viewporter_get_viewport(m_viewporter, e->surface);
    e->fraction = wp_fractional_scale_manager_v1_get_fractional_scale(m_fractional, e->surface);
    wp_fractional_scale_v1_add_listener(e->fraction, &kEditScale, new EditCtx{this, e.get()});
  } else {
    e->scale = static_cast<float>(o->scale);
    wl_surface_set_buffer_scale(e->surface, o->scale);
  }
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
  if (e.fraction) {
    delete static_cast<EditCtx*>(wl_proxy_get_user_data(reinterpret_cast<wl_proxy*>(e.fraction)));
    wp_fractional_scale_v1_destroy(e.fraction);
    e.fraction = nullptr;
  }
  if (e.viewport) wp_viewport_destroy(e.viewport);
  e.viewport = nullptr;
  if (e.layer) zwlr_layer_surface_v1_destroy(e.layer);
  e.layer = nullptr;
  if (e.surface) wl_surface_destroy(e.surface);
  e.surface = nullptr;
}

void App::onEditConfigure(EditSurface* e, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t width, uint32_t height) {
  zwlr_layer_surface_v1_ack_configure(ls, serial);
  const int nw = width ? static_cast<int>(width) : static_cast<int>(e->output->logicalW());
  const int nh = height ? static_cast<int>(height) : static_cast<int>(e->output->logicalH());
  sizeBuffer(*e, nw, nh);
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
  int hover = -1, active = -1, selected = -1, handle = -1;
  Color accent = Color::fromHex("#e2342a");
  for (auto& w : m_widgets) {
    if (w->output != e.output || !w->impl || !w->surface) continue;
    if (w->impl->fullscreen()) {
      // a frame owns the screen: outline the whole output when it is selected
      if (w.get() == m_selected) {
        selected = static_cast<int>(rects.size());
        rects.push_back({6, 6, static_cast<float>(e.w) - 12, static_cast<float>(e.h) - 12});
      }
      continue;
    }
    if (warped(w->cfg)) continue;  // drawn as its quad by drawUi (perspective)
    const int idx = static_cast<int>(rects.size());
    if (w.get() == m_pointerWidget) (m_drag != Drag::None ? active : hover) = idx;
    if (w.get() == m_selected) selected = handle = idx;
    rects.push_back({static_cast<float>(w->cfg.x), static_cast<float>(w->cfg.y), static_cast<float>(w->cfg.width),
                     static_cast<float>(w->cfg.height), static_cast<float>(w->cfg.rotation)});
    accent = w->impl->accent();
  }
  eglMakeCurrent(m_egl, e.eglSurface, e.eglSurface, m_eglContext);
  glViewport(0, 0, e.bufW, e.bufH);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  try {
    m_overlay.draw(static_cast<float>(e.w), static_cast<float>(e.h), rects, hover, active, selected, accent,
                   m_gridOn ? static_cast<float>(m_config.gridSize) : 0.0F, m_guidesV, m_guidesH, handle);
    if (m_paintMode && e.output) {
      // depth mode: tint what would cover the selected widget
      m_depth.get(e.output->name);
      if (DepthMask* m = m_depth.paintable(e.output->name); m && m->field) {
        MaskParams mp;
        mp.surfaceW = static_cast<float>(e.w);
        mp.surfaceH = static_cast<float>(e.h);
        mp.outputW = e.output->logicalW();
        mp.outputH = e.output->logicalH();
        mp.imageW = static_cast<float>(m->imageW);
        mp.imageH = static_cast<float>(m->imageH);
        mp.fillMode = m_noctalia.state().fillMode;
        mp.field = m->field;
        mp.level = previewPlane();
        mp.feather = static_cast<float>(m_noctalia.state().depthFeather);
        if (m_zoom > 1.0001) {
          // zoomed: draw the wallpaper ourselves, magnified, under the tint
          mp.viewX = static_cast<float>(m_viewX);
          mp.viewY = static_cast<float>(m_viewY);
          mp.zoom = static_cast<float>(m_zoom);
          m_maskPass.drawWallpaper(mp, m_depth.wallpaperTexture(*m));
        }
        m_maskPass.drawTint(mp, Color{accent.r, accent.g, accent.b, 0.42F});
      }
    }
    // placing a pinned corner: a loupe of the wallpaper under it
    m_loupe = {};
    if (m_drag == Drag::Pin && m_selected && m_selected->cfg.pinned && m_pinCorner >= 0 && e.output == m_selected->output) {
      m_depth.get(e.output->name);  // uploads the field and image even when no widget uses depth
      if (DepthMask* m = m_depth.paintable(e.output->name); m && m->field) {
        const float cx = static_cast<float>(m_selected->cfg.pin[2 * m_pinCorner]);
        const float cy = static_cast<float>(m_selected->cfg.pin[2 * m_pinCorner + 1]);
        const float L = 190;
        // beside the pointer, away from the screen edge
        float lx = cx + 40, ly = cy - 40 - L;
        if (lx + L > e.w - 8) lx = cx - 40 - L;
        if (ly < 8) ly = cy + 40;
        m_loupe = {lx, ly, L, L};
        MaskParams mp;
        mp.surfaceW = static_cast<float>(e.w);
        mp.surfaceH = static_cast<float>(e.h);
        mp.outputW = e.output->logicalW();
        mp.outputH = e.output->logicalH();
        mp.imageW = static_cast<float>(m->imageW);
        mp.imageH = static_cast<float>(m->imageH);
        mp.fillMode = m_noctalia.state().fillMode;
        mp.viewX = cx;
        mp.viewY = cy;
        mp.zoom = 5;
        mp.viewAtX = lx + L / 2;
        mp.viewAtY = ly + L / 2;
        glEnable(GL_SCISSOR_TEST);
        glScissor(static_cast<GLint>(std::lround(lx * e.scale)), static_cast<GLint>(std::lround((e.h - ly - L) * e.scale)),
                  static_cast<GLsizei>(std::lround(L * e.scale)), static_cast<GLsizei>(std::lround(L * e.scale)));
        m_maskPass.drawWallpaper(mp, m_depth.wallpaperTexture(*m));
        glDisable(GL_SCISSOR_TEST);
      }
    }
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
    if (m_motion && (key == "depth" || key == "depth_level"))
      return "error: depth is locked while a video or scene is the wallpaper";
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
        if (!m_edit) setEditMode(true);  // selecting is for editing
        m_selected = w.get();
        m_inspScroll = 0;
        markEditDirty();
        return "selected " + id;
      }
    return "error: no widget '" + id + "'";
  }
  if (cmd == "card fold inspector" || cmd == "card fold paint") {  // fold a panel to its title, or unfold it
    const std::string panel = cmd.substr(10);
    if (!m_collapsed.erase(panel)) m_collapsed.insert(panel);
    markEditDirty();
    return panel + (m_collapsed.count(panel) ? " folded" : " unfolded");
  }
  if (cmd == "card help" || cmd == "card saves" || cmd == "card paint") {  // the editor's cards, from a script
    // ("saves" alone is the saved-layout list the bar plugin reads)
    if (!m_edit) setEditMode(true);
    const std::string card = cmd.substr(5);
    if (card == "help") m_helpOpen = !m_helpOpen;
    else if (card == "saves") m_savesOpen = !m_savesOpen;
    else setPaintMode(!m_paintMode);
    markEditDirty();
    return card + " toggled";
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
      moveWidget(*w, (ow - w->cfg.width) / 2, oh - w->cfg.height - 64);
      placeLayer(*w);
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
  if (cmd == "saves") return savesJson();
  if (cmd == "wallpaper-check") {  // skwd's hooks call this after each change
    checkWallpaperKind();
    return "checking";
  }
  if (cmd == "save" || cmd.rfind("save ", 0) == 0) {
    const std::string id = saveLayout(cmd.size() > 5 ? cmd.substr(5) : std::string());
    return id.empty() ? "error: could not save" : "saved " + id;
  }
  if (cmd.rfind("save-load ", 0) == 0) {  // save-load <id> [output]
    std::istringstream in(cmd.substr(10));
    std::string id, output;
    in >> id >> output;
    if (!output.empty() && output != "all" &&
        std::none_of(m_outputs.begin(), m_outputs.end(), [&](auto& o) { return o->name == output; }))
      return "error: no output '" + output + "'";
    return loadSave(id, output) ? "loaded" : "error: no such save";
  }
  if (cmd.rfind("save-overwrite ", 0) == 0) return overwriteSave(cmd.substr(15)) ? "overwritten" : "error: no such save";
  if (cmd.rfind("save-delete ", 0) == 0) {
    const bool ok = Config::deleteSave(savesDir(), cmd.substr(12));
    refreshSaves();
    markEditDirty();
    return ok ? "deleted" : "error: no such save";
  }
  if (cmd.rfind("save-rename ", 0) == 0) {
    const std::string rest = cmd.substr(12);
    const size_t sp = rest.find(' ');
    const bool ok = sp != std::string::npos && Config::renameSave(savesDir(), rest.substr(0, sp), rest.substr(sp + 1));
    refreshSaves();
    markEditDirty();
    return ok ? "renamed" : "error: usage save-rename <id> <name>";
  }
  if (cmd == "json") {
    // machine-readable state for front ends (the Noctalia bar plugin)
    auto q = [](const std::string& v) {
      std::string o = "\"";
      for (char c : v) {
        if (c == '"' || c == '\\') o += '\\';
        if (static_cast<unsigned char>(c) < 0x20) {
          o += std::format("\\u{:04x}", static_cast<int>(c));
          continue;
        }
        o += c;
      }
      return o + "\"";
    };
    std::error_code ec;
    size_t saved = 0;
    for (const auto& e : fs::directory_iterator(profileDir(), ec))
      if (e.path().extension() == ".toml" && e.path().stem() != m_profileKey) ++saved;
    bool field = false, mask = false;
    for (auto& o : m_outputs)
      if (const DepthMask* m = m_depth.get(o->name)) {
        field = field || m->field != 0;
        mask = mask || m->texture != 0;
      }
    std::string s = std::format(
        "{{\"motion\":{},\"edit\":{},\"demo\":{},\"profiles\":{},\"profile\":{{\"key\":{},\"wallpaper\":{},\"saved\":{}}},"
        "\"depth\":{{\"mask\":{},\"field\":{},\"plugin_threshold\":{:.0f}}},\"outputs\":[",
        m_motion, m_edit, m_demo, m_config.profiles, q(m_profileKey), q(fs::path(m_profileWall).filename().string()), saved, mask, field,
        m_noctalia.state().depthThreshold * 100);
    // the monitors (logical size), and how many widgets each shows
    for (size_t i = 0; i < m_outputs.size(); ++i) {
      const Output& o = *m_outputs[i];
      const auto n = std::count_if(m_widgets.begin(), m_widgets.end(), [&](auto& w) { return w->output == &o; });
      s += std::format("{}{{\"name\":{},\"width\":{:.0f},\"height\":{:.0f},\"widgets\":{}}}", i ? "," : "", q(o.name),
                       o.logicalW(), o.logicalH(), n);
    }
    s += "],\"widgets\":[";
    bool first = true;
    for (auto& w : m_widgets) {
      const std::string look = w->cfg.type == "visualizer" ? w->cfg.options["style"].value_or(std::string("bars"))
                               : w->cfg.type == "clock"    ? w->cfg.options["face"].value_or(std::string("digital"))
                                                           : w->cfg.type;
      s += std::format("{}{{\"id\":{},\"type\":{},\"look\":{},\"output\":{},\"x\":{},\"y\":{},\"width\":{},\"height\":{},"
                       "\"rotation\":{:.1f},\"depth\":{},\"depth_level\":{:.0f},\"fps\":{:.0f}}}",
                       first ? "" : ",", q(w->cfg.id), q(w->cfg.type), q(look), q(w->output ? w->output->name : ""), w->cfg.x,
                       w->cfg.y, w->cfg.width, w->cfg.height,
                       w->cfg.rotation, w->cfg.depth, w->cfg.depthLevel, nowSeconds() - w->markAt > 1.5 ? 0.0 : w->fpsMeasured);
      first = false;
    }
    return s + "]}";
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
    s += profileStatus() + "\n";
    if (m_motion) s += "wallpaper: moving (video or scene) — depth off and locked\n";
    for (auto& w : m_widgets) {
      const DepthMask* m = w->output ? m_depth.get(w->output->name) : nullptr;
      const bool stale = nowSeconds() - w->markAt > 1.5;
      const std::string look = w->cfg.type == "visualizer" ? w->cfg.options["style"].value_or(std::string("bars"))
                                     : w->cfg.type == "clock" ? "clock:" + w->cfg.options["face"].value_or(std::string("digital"))
                                                              : w->cfg.type;
      s += std::format("  {} {} on {} at {},{} {}x{} buffer={}x{}@{:.3f} depth={} frames={} fps={:.0f}\n", w->cfg.id, look,
                       w->output ? w->output->name : "-", w->cfg.x, w->cfg.y, w->cfg.width, w->cfg.height, w->bufW,
                       w->bufH, w->scale, m ? fs::path(m->maskPath).filename().string().substr(0, 12) : "none",
                       w->frames, stale ? 0.0 : w->fpsMeasured);
    }
    return s;
  }
  return "error: unknown command (edit, edit-on, edit-off, demo, set, add, remove, select, gallery, reset, reload, status, json, saves, save, save-load, save-overwrite, save-rename, save-delete, quit)";
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
      if (ev->wd == m_wdDepthMaps && name.ends_with(".npy")) m_refreshDepthAt = now + 0.3;
      p += sizeof(inotify_event) + ev->len;
    }
  }
}

int App::computeTimeout() {
  const double now = nowSeconds();
  double next = 1e9;
  for (double t : {m_reloadConfigAt, m_refreshNoctAt, m_refreshDepthAt})
    if (t > 0) next = std::min(next, t);
  if (!m_noctalia.ready()) next = std::min(next, m_noctRetryAt);
  // wake when the silence becomes "nothing playing", if a visualizer cares
  if (m_silentSince >= 0 && !m_nothingPlaying)
    for (auto& w : m_widgets)
      if (idleModeOf(*w) != "show") {
        next = std::min(next, m_silentSince + kIdleGrace + 0.01);
        break;
      }
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
  bool idleDemo = false;
  if (m_nothingPlaying)
    for (auto& w : m_widgets) idleDemo = idleDemo || idleModeOf(*w) == "demo";
  if (m_demo || idleDemo || (m_edit && (!m_audioOk || m_audio.idle()))) timeout = timeout < 0 ? 16 : std::min(timeout, 16);
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
  m_depth.setEditsDir((fs::path(m_configPath).parent_path() / "depth-edits").string());
  m_depth.setJobs(&m_jobs, [this] {
    for (auto& w : m_widgets) w->needsRender = true;
  });
  m_noctalia.refresh();
  m_depth.update(m_noctalia.state(), outputNames());
  loadConfig();
  checkProfile();  // the wallpaper may have changed while undershell was not running
  checkWallpaperKind();
  m_audioOk = m_audio.start();
  m_media.start();

  m_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  const uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE;
  m_wdConfig = inotify_add_watch(m_inotify, fs::path(m_configPath).parent_path().c_str(), mask);
  m_wdNoctState = inotify_add_watch(m_inotify, fs::path(Noctalia::settingsPath()).parent_path().c_str(), mask);
  m_wdNoctConfig = inotify_add_watch(m_inotify, Noctalia::configDir().c_str(), mask);
  m_wdMasks = inotify_add_watch(m_inotify, DepthMasks::maskDir().c_str(), mask);
  // the depth maps (.npy) the per-widget planes are cut from; only finished
  // writes, never a half-written file
  m_wdDepthMaps = inotify_add_watch(m_inotify, (fs::path(DepthMasks::maskDir()).parent_path() / "depth").c_str(),
                                    IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE);

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
    // until Noctalia's config has been read once, retry every few seconds
    if (!m_noctalia.ready() && now >= m_noctRetryAt) {
      m_noctRetryAt = now + 3;
      refreshNoctalia();
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
    updateIdle(now);
    bool idleDemo = false;
    if (m_nothingPlaying)
      for (auto& w : m_widgets) idleDemo = idleDemo || idleModeOf(*w) == "demo";
    if (m_demo || m_edit || idleDemo) {
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
      // only the widgets that are playing the demo
      for (auto& w : m_widgets)
        if (m_demo || m_edit || w->idleState == 2) w->needsRender = true;
    }
    for (auto& w : m_widgets) {
      // a live option change (inspector) may turn a boxed look fullscreen
      if (w->impl && w->surface && w->surfaceFullscreen != w->impl->fullscreen()) {
        destroySurface(*w);
        createSurface(*w);
      }
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
