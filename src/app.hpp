// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "audio.hpp"
#include "config.hpp"
#include "depth.hpp"
#include "geom.hpp"
#include "ipc.hpp"
#include "jobs.hpp"
#include "media.hpp"
#include "noctalia.hpp"
#include "overlay.hpp"
#include "canvas.hpp"
#include "text.hpp"
#include "widget.hpp"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
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
struct xkb_context;
struct xkb_keymap;
struct xkb_state;
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
  std::unique_ptr<WidgetImpl> impl;
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
  bool surfaceFullscreen = false;  // how the current surface was created
  double lastTick = 0, lastRender = 0;
  bool hovered = false;
  // the surface position pointer events are currently relative to: updated
  // only once the compositor has processed our margin change (wl_display.sync)
  int appliedX = 0, appliedY = 0;
  std::vector<Rect> inputRects;  // last input region set (widget px)
  // off-screen target of a turned widget (its own box, at buffer scale)
  GLuint rtFbo = 0, rtTex = 0;
  int rtW = 0, rtH = 0;
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
  void onKeymap(int fd, uint32_t size);
  void onModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
  void onRepeatInfo(int32_t rate, int32_t delay);
  void onKeyboardLeave();
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
  void placeLayer(Widget& w);  // layer size + margin from the (turned) box
  void applyInputRegion(Widget& w);
  void render(Widget& w);
  void setEditMode(bool on);
  void createEditSurface(Output* o);
  void destroyEditSurface(EditSurface& e);
  void renderEdit(EditSurface& e);
  EditSurface* editBySurface(wl_surface* s);
  Widget* widgetAt(const Output* o, double x, double y);
  void markEditDirty();
  // editor internals (editor.cpp)
public:
  struct EditOp {
    std::string id;
    int x = 0, y = 0, w = 0, h = 0;
    std::string style;
    std::string kind = "geom";  // geom | prop | add | remove
    std::string key, value;     // prop: the key and its TOML value before
    std::string block;          // add/remove: the widget's block text
  };
  EditOp snapshot(const Widget& w) const;
  void pushUndo(const Widget& w, const char* kind);
  void applyOp(const EditOp& op);
  void undo();
  void redo();
  void applyEditOp(const EditOp& op, bool undoing);
  [[nodiscard]] EditOp inverseOf(const EditOp& op) const;
  void nudge(int dx, int dy, bool resize);
  bool keyAction(uint32_t key);
  [[nodiscard]] bool modActive(const char* name) const;
  Widget* target();
  void cycleSelection(int step);
  void snapBox(const Widget& w, int& x, int& y, int& width, int& height, bool moving);
  void setStyle(Widget& w, const std::string& look);
  void editorTick(double now);
  void validateEditPointers();
  bool widgetPointer(PointerEvent::Type type, wl_surface* s, double x, double y, uint32_t button);
  void updateInputRegion(Widget& w);
  void drawEditorText(EditSurface& e);
  // inspector + gallery (inspector.cpp)
  struct UiControl {
    enum Type { Prev, Next, Slider, Toggle, Swatch, Duplicate, Delete, Plus, GalleryItem, Panel, Undo, Redo, Magnet, Grid, Help,
                Done, Chip } type = Panel;
    Rect r;       // output coordinates
    int prop = -1;
    std::string value;  // swatch colour / gallery type
  };

private:
  void layoutUi(const EditSurface& e);
  void layoutGallery(float W, float H);
  void drawUi(EditSurface& e);
  int uiHit(double x, double y) const;
  bool uiPress(int index, double x);
  void uiDrag(double x);
  bool uiScroll(double x, double y, int step);
  void setProp(Widget& w, const std::string& key, const std::string& tomlValue);
  void applyProp(Widget& w, const std::string& key, const std::string& tomlValue);
  std::string addWidget(const std::string& type, const std::string& look, int x, int y, bool record);
  void removeWidget(Widget& w);
  void duplicateWidget(Widget& w);
  void moveWidget(Widget& w, int x, int y);
  void resizeWidget(Widget& w, int width, int height);
  bool inRotateHandle(const Widget& w, double x, double y) const;
  void setRotation(Widget& w, double degrees);  // live, not persisted
  void clampToOutput(WidgetConfig& c, const Output* o) const;
  void persist(Widget& w);
  void setCursor(const char* name);
  Widget* widgetBySurface(wl_surface* s);
  int computeTimeout();
  AudioFrame audioFrame();
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
  Jobs m_jobs{2};
  MediaService m_media{m_jobs};
  uint64_t m_mediaGen = 0;
  Widget* m_hoverWidget = nullptr;  // widget surface under the pointer (not editing)
  bool m_audioOk = false;
  Noctalia m_noctalia;
  DepthMasks m_depth;
  MaskPass m_maskPass;
  RotatedBlit m_blit;
  TextRenderer m_text;
  OverlayPass m_overlay;
  IpcServer m_ipc;
  int m_inotify = -1;
  int m_wdConfig = -1, m_wdNoctState = -1, m_wdNoctConfig = -1, m_wdMasks = -1;
  double m_reloadConfigAt = 0, m_refreshNoctAt = 0, m_refreshDepthAt = 0, m_noctRetryAt = 0;

  bool m_edit = false;
  std::vector<std::unique_ptr<EditSurface>> m_editSurfaces;
  EditSurface* m_pointerEdit = nullptr;  // editor surface under the pointer
  Widget* m_pointerWidget = nullptr;     // widget hovered / being dragged
  double m_px = 0, m_py = 0;             // pointer, output coordinates
  enum class Drag { None, Move, Resize, Slider, Rotate } m_drag = Drag::None;
  double m_rotStart = 0, m_rotLast = 0, m_rotAcc = 0;  // rotate drag
  double m_sliderX = 0, m_sliderStart = 0;              // slider drag (Shift: fine)
  bool m_sliderFine = false;
  double m_anchorX = 0, m_anchorY = 0;         // turned resize: fixed corner
  double m_pressX = 0, m_pressY = 0;     // pointer at press, output coordinates
  int m_startX = 0, m_startY = 0, m_dragW = 0, m_dragH = 0;
  double m_scrollAcc = 0;
  Widget* m_selected = nullptr;
  std::vector<EditOp> m_undo, m_redo;
  std::string m_lastOpKind, m_lastOpId;
  double m_lastOpAt = 0;
  std::vector<float> m_guidesV, m_guidesH;  // output coordinates
  xkb_context* m_xkb = nullptr;
  xkb_keymap* m_keymap = nullptr;
  xkb_state* m_xkbState = nullptr;
  int m_repeatRate = 25, m_repeatDelay = 600;
  uint32_t m_repeatKey = 0;
  double m_repeatNext = 0;
  std::vector<UiControl> m_ui;
  const Output* m_uiOutput = nullptr;
  bool m_galleryOpen = false;
  bool m_helpOpen = false;
  bool m_snapOn = true;   // magnet (Shift inverts)
  bool m_gridOn = true;   // grid drawn + snapping on release
  int m_uiHover = -1;
  Canvas m_editCanvas;
  float m_inspScroll = 0;
  int m_sliderControl = -1;
  std::string m_pendingSelect;  // select this id once the reload creates it
  bool m_running = true;
  bool m_demo = false;
  double m_demoT = 0;
  std::vector<float> m_demoBands;
};


}  // namespace undershell
