// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "audio.hpp"
#include "config.hpp"
#include "depth.hpp"
#include "ipc.hpp"
#include "noctalia.hpp"
#include "overlay.hpp"
#include "visualizer.hpp"

#include <EGL/egl.h>
#include <memory>
#include <string>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_output;
struct wl_seat;
struct wl_pointer;
struct wl_keyboard;
struct wl_shm;
struct wl_surface;
struct wl_callback;
struct wl_egl_window;
struct wl_cursor_theme;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

namespace undershell {

struct Output {
  wl_output* wl = nullptr;
  uint32_t global = 0;
  std::string name;
  int modeW = 0, modeH = 0, scale = 1;
  [[nodiscard]] float logicalW() const { return static_cast<float>(modeW) / scale; }
  [[nodiscard]] float logicalH() const { return static_cast<float>(modeH) / scale; }
};

struct Widget {
  WidgetConfig cfg;
  Visualizer viz;
  Output* output = nullptr;
  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  wl_egl_window* eglWindow = nullptr;
  EGLSurface eglSurface = EGL_NO_SURFACE;
  wl_callback* frameCb = nullptr;
  int w = 0, h = 0, scale = 1;  // current logical size
  bool configured = false;
  bool needsRender = true;
  bool drewEmpty = false;
  double lastTick = 0, lastRender = 0;
  bool hovered = false;
  // the surface position pointer events are currently relative to: updated
  // only once the compositor has processed our margin change (wl_display.sync)
  int appliedX = 0, appliedY = 0;
  uint64_t frames = 0;
  uint64_t framesAtMark = 0;
  double markAt = 0;
  double fpsMeasured = 0;
};

// The editor's fullscreen input/canvas surface (overlay layer, one per output).
struct EditSurface {
  Output* output = nullptr;
  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  wl_egl_window* eglWindow = nullptr;
  EGLSurface eglSurface = EGL_NO_SURFACE;
  wl_callback* frameCb = nullptr;
  int w = 0, h = 0, scale = 1;
  bool configured = false;
  bool needsRender = true;
};

class App {
public:
  App();
  ~App();
  int run();

  // wayland callbacks (public for the C listener trampolines)
  void onGlobal(wl_registry* reg, uint32_t name, const char* iface, uint32_t version);
  void onGlobalRemove(uint32_t name);
  void onLayerConfigure(Widget* w, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t width, uint32_t height);
  void onLayerClosed(Widget* w);
  void onFrameDone(Widget* w);
  void onPointerEnter(wl_surface* s, uint32_t serial, double x, double y);
  void onPointerLeave(wl_surface* s);
  void onPointerMotion(double x, double y);
  void onPointerButton(uint32_t serial, uint32_t button, uint32_t state);
  void onOutputDone(Output* o);
  void onMoveApplied(Widget* w, int x, int y);
  void onEditConfigure(EditSurface* e, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t width, uint32_t height);
  void onEditFrameDone(EditSurface* e);
  void onKey(uint32_t key, uint32_t state);
  void onScroll(double value);
  void setupSeat(wl_seat* seat, uint32_t caps);

  Output* outputByWl(wl_output* o);

private:
  bool initWayland();
  bool initEgl();
  void loadConfig();
  void refreshNoctalia();
  void updateDepth();
  void syncWidgets();
  void createSurface(Widget& w);
  void destroySurface(Widget& w);
  void applyInputRegion(Widget& w);
  void render(Widget& w);
  void setEditMode(bool on);
  void createEditSurface(Output* o);
  void destroyEditSurface(EditSurface& e);
  void renderEdit(EditSurface& e);
  EditSurface* editBySurface(wl_surface* s);
  Widget* widgetAt(const Output* o, double x, double y);
  void markEditDirty();
  void moveWidget(Widget& w, int x, int y);
  void clampToOutput(WidgetConfig& c, const Output* o) const;
  void persist(Widget& w);
  void setCursor(const char* name);
  Widget* widgetBySurface(wl_surface* s);
  int computeTimeout();
  void handleInotify();
  std::string handleCommand(const std::string& cmd);
  std::vector<std::string> outputNames() const;

  wl_display* m_display = nullptr;
  wl_registry* m_registry = nullptr;
  wl_compositor* m_compositor = nullptr;
  zwlr_layer_shell_v1* m_layerShell = nullptr;
  wl_shm* m_shm = nullptr;
  wl_seat* m_seat = nullptr;
  wl_pointer* m_pointer = nullptr;
  wl_keyboard* m_keyboard = nullptr;
  wl_cursor_theme* m_cursorTheme = nullptr;
  wl_surface* m_cursorSurface = nullptr;
  uint32_t m_pointerSerial = 0;
  std::vector<std::unique_ptr<Output>> m_outputs;

  EGLDisplay m_egl = EGL_NO_DISPLAY;
  EGLConfig m_eglConfig = nullptr;
  EGLContext m_eglContext = EGL_NO_CONTEXT;

  std::string m_configPath;
  Config m_config;
  std::vector<std::unique_ptr<Widget>> m_widgets;
  Audio m_audio;
  bool m_audioOk = false;
  Noctalia m_noctalia;
  DepthMasks m_depth;
  MaskPass m_maskPass;
  OverlayPass m_overlay;
  IpcServer m_ipc;
  int m_inotify = -1;
  int m_wdConfig = -1, m_wdNoctState = -1, m_wdNoctConfig = -1, m_wdMasks = -1;
  double m_reloadConfigAt = 0, m_refreshNoctAt = 0, m_refreshDepthAt = 0;

  bool m_edit = false;
  std::vector<std::unique_ptr<EditSurface>> m_editSurfaces;
  EditSurface* m_pointerEdit = nullptr;  // editor surface under the pointer
  Widget* m_pointerWidget = nullptr;     // widget hovered / being dragged
  double m_px = 0, m_py = 0;             // pointer, output coordinates
  enum class Drag { None, Move, Resize } m_drag = Drag::None;
  double m_pressX = 0, m_pressY = 0;     // pointer at press, output coordinates
  int m_startX = 0, m_startY = 0, m_dragW = 0, m_dragH = 0;
  double m_scrollAcc = 0;
  bool m_running = true;
  bool m_demo = false;
  double m_demoT = 0;
  std::vector<float> m_demoBands;
};

// Offscreen render of every look to PNGs (for previews and testing).
int snapshotLooks(const std::string& dir);

}  // namespace undershell
