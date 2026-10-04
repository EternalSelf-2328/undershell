// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "anchor.hpp"
#include "audio.hpp"
#include "config.hpp"
#include "depth.hpp"
#include "geom.hpp"
#include "i18n.hpp"
#include "ipc.hpp"
#include "jobs.hpp"
#include "media.hpp"
#include "noctalia.hpp"
#include "overlay.hpp"
#include "schema.hpp"
#include "canvas.hpp"
#include "text.hpp"
#include "widget.hpp"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <map>
#include <set>
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
struct zxdg_output_manager_v1;
struct zxdg_output_v1;
struct wp_viewporter;
struct wp_viewport;
struct wp_fractional_scale_manager_v1;
struct wp_fractional_scale_v1;
struct zwlr_layer_surface_v1;

namespace undershell {


struct Output {
  wl_output* wl = nullptr;
  uint32_t global = 0;
  std::string name;
  int modeW = 0, modeH = 0, scale = 1;
  // the size surfaces and pointers live in, from xdg-output: under a
  // fractional scale (1366x768 at 0.98 is 1393x783) mode / scale is wrong
  zxdg_output_v1* xdg = nullptr;
  int logW = 0, logH = 0;
  [[nodiscard]] float logicalW() const { return logW > 0 ? static_cast<float>(logW) : static_cast<float>(modeW) / scale; }
  [[nodiscard]] float logicalH() const { return logH > 0 ? static_cast<float>(logH) : static_cast<float>(modeH) / scale; }
};

struct Widget {
  WidgetConfig cfg;  // geometry mapped to the output (anchor.hpp)
  SpaceMap toScreen;  // the block's space -> this output, through the wallpaper
  std::unique_ptr<WidgetImpl> impl;
  Output* output = nullptr;
  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  wl_egl_window* eglWindow = nullptr;
  EGLSurface eglSurface = EGL_NO_SURFACE;
  wl_callback* frameCb = nullptr;
  int w = 0, h = 0;  // current logical size
  // the buffer behind it: scale is device px per logical px, fractional when
  // the compositor offers fractional-scale + viewporter (else the output's)
  float scale = 1;
  int bufW = 0, bufH = 0;
  wp_viewport* viewport = nullptr;
  wp_fractional_scale_v1* fraction = nullptr;
  bool configured = false;
  bool needsRender = true;
  bool drewEmpty = false;
  bool surfaceFullscreen = false;  // how the current surface was created
  int idleState = 0;               // 0 normal, 1 hidden, 2 demo (nothing playing)
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
  int w = 0, h = 0;
  float scale = 1;
  int bufW = 0, bufH = 0;
  wp_viewport* viewport = nullptr;
  wp_fractional_scale_v1* fraction = nullptr;
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
  // moving wallpapers turn depth off (wallkind.hpp)
  void checkWallpaperKind();
  void setMotion(bool on);
  [[nodiscard]] bool depthLocked() const { return m_motion; }
  // wallpaper profiles (profiles.cpp)
  void checkProfile();
  std::string profileDir() const;
  std::string profileKeyFor(const std::string& wallpaper);
  std::string profileWallpaper() const;
  std::string profileStatus() const;
  // saved layouts (saves.cpp)
  std::string savesDir() const;
  void refreshSaves();
  std::string saveLayout(const std::string& name);  // "" = "Profile N"
  bool overwriteSave(const std::string& id);
  // A saved layout. With `output` it goes onto that monitor ("all": a copy
  // on each) and only that monitor's widgets are replaced; without, it
  // replaces the whole layout as saved.
  bool loadSave(const std::string& id, const std::string& output = {});
  // the monitor a widget block shows on (its own if present, else the first)
  [[nodiscard]] std::string blockOutput(const std::string& block) const;
  void replaceLayout(const std::string& blocks, bool record);
  std::string savesJson();
  bool textKey(uint32_t key);  // typing a save's name
  bool fontKey(uint32_t key);  // typing in the font picker's search
  std::vector<std::string> filteredFonts() const;
  // the depth brush (depthpaint.cpp)
  float previewPlane() const;
  bool paintMap(const Output* o, double x, double y, double& fx, double& fy, double& fr);
  void paintDab(double x, double y);
  void paintBegin(double x, double y);
  void paintMove(double x, double y);
  void paintEnd();
  void paintUndo();
  void paintClear();
  void setPaintMode(bool on);
  void paintWorld(const Output* o, double sx, double sy, double& wx, double& wy) const;  // screen -> wallpaper view
  void paintScreen(const Output* o, double wx, double wy, double& sx, double& sy) const;
  void paintZoom(double factor, double sx, double sy);  // about a screen point
  void clampPaintView(const Output* o);
  void applySelection(DepthMask& m, std::vector<float>& mask, PixelBox box, float matchValue);
  void wandAt(double wx, double wy);
  void lassoClick(double sx, double sy);
  void lassoClose();
  bool fieldAt(const Output* o, double wx, double wy, int& fx, int& fy);
  void commitRename();
  void syncWidgets();
  void createSurface(Widget& w);
  void destroySurface(Widget& w);
  void placeLayer(Widget& w);  // layer size + margin from the (turned) box
  void restack();              // surfaces in stacking order: nearer widgets on top
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
    std::string kind = "geom";  // geom | prop | add | remove | layout
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
  void snapBox(const Widget& w, int& x, int& y, int& width, int& height, bool moving, int corner = 3);
  void setStyle(Widget& w, const std::string& look);
  void editorTick(double now);
  void validateEditPointers();
  bool widgetPointer(PointerEvent::Type type, wl_surface* s, double x, double y, uint32_t button);
  void updateInputRegion(Widget& w);
  void drawEditorText(EditSurface& e);
  // inspector + gallery (inspector.cpp)
  struct UiControl {
    enum Type { Prev, Next, Slider, Toggle, Swatch, Duplicate, Delete, Plus, GalleryItem, Panel, Undo, Redo, Magnet, Grid, Help,
                Done, Chip, Saves, SaveNew, SaveLoad, SaveOverwrite, SaveRename, SaveDelete, SaveRow, Paint, PaintTool,
                PaintSize, PaintSmart, PaintUndo, PaintClear, PaintSelect, PaintZoomIn, PaintZoomOut, PaintZoomReset,
                ElemExpand, ElemShow, ElemUp, ElemDown, FontPick, FontItem, FontClose, PanelGrab, Language, Collapse } type = Panel;
    Rect r;       // output coordinates
    int prop = -1;
    std::string value;  // swatch colour / gallery type
  };

private:
  void layoutUi(const EditSurface& e);
  // m_ui is laid out for whichever editor surface drew last; with several
  // monitors, lay it out again for the one under the pointer before hit-testing
  void uiFor(const EditSurface* e) {
    if (e && m_uiOutput != e->output) layoutUi(*e);
  }
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
  int pinAt(const Widget& w, double x, double y) const;  // a pinned widget's corner under the pointer
  void commitPin(Widget& w);                            // store the points (one undo step) and fit the box
  void setRotation(Widget& w, double degrees);  // live, not persisted
  void clampToOutput(WidgetConfig& c, const Output* o) const;
  // wallpaper anchoring (anchor.hpp): the map for a block on an output, and
  // a runtime box back to the block's space for writing
  SpaceMap spaceMapFor(const WidgetConfig& c, const Output* o);
  [[nodiscard]] static WidgetConfig toStored(const Widget& w) {
    WidgetConfig c = w.cfg;
    mapWidget(c, w.toScreen.inverse());
    return c;
  }
  std::map<std::string, std::pair<int, int>> m_imageSizes;  // wallpaper -> size (0: not an image)
  int m_saveRows = 5;  // saved-layout rows that fit the screen
  std::string m_anchorKey;  // what the maps were made from (fill mode + wallpapers)
  std::string anchorKey() const;
  void persist(Widget& w);
  void setCursor(const char* name);
  Widget* widgetBySurface(wl_surface* s);
  int computeTimeout();
  AudioFrame audioFrame(const Widget* w = nullptr);
  std::string idleModeOf(const Widget& w) const;  // show | hide | demo
  void updateIdle(double now);
  void handleInotify();
  std::string handleCommand(const std::string& cmd);
  std::vector<std::string> outputNames() const;

  wl_display* m_display = nullptr;
  wl_registry* m_registry = nullptr;
  wl_compositor* m_compositor = nullptr;
  zwlr_layer_shell_v1* m_layerShell = nullptr;
  zxdg_output_manager_v1* m_xdgOutputs = nullptr;
  wp_viewporter* m_viewporter = nullptr;
  wp_fractional_scale_manager_v1* m_fractional = nullptr;
  [[nodiscard]] bool fractional() const { return m_viewporter && m_fractional; }
  float initialScale(const Output* o) const;
  template <class S> void sizeBuffer(S& s, int nw, int nh);
public:
  void onPreferredScale(Widget* w, EditSurface* e, float scale);
private:
  void watchLogicalSize(Output* o);
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
  PerspectiveBlit m_warpBlit;
  TextRenderer m_text;
  OverlayPass m_overlay;
  IpcServer m_ipc;
  int m_inotify = -1;
  int m_wdConfig = -1, m_wdNoctState = -1, m_wdNoctConfig = -1, m_wdMasks = -1, m_wdDepthMaps = -1;
  double m_reloadConfigAt = 0, m_refreshNoctAt = 0, m_refreshDepthAt = 0, m_noctRetryAt = 0;

  std::vector<const Widget*> m_mapped;    // widget surfaces, bottom to top, in the order they were made
  bool m_motion = false;          // a video or scene is on screen: no depth
  bool m_motionCheckBusy = false, m_motionCheckAgain = false;
  std::string m_profileKey, m_profileWall;  // the wallpaper config.toml's layout belongs to
  bool m_edit = false;
  std::vector<std::unique_ptr<EditSurface>> m_editSurfaces;
  EditSurface* m_pointerEdit = nullptr;  // editor surface under the pointer
  Widget* m_pointerWidget = nullptr;     // widget hovered / being dragged
  double m_px = 0, m_py = 0;             // pointer, output coordinates
  enum class Drag { None, Move, Resize, Slider, Rotate, Panel, Pin } m_drag = Drag::None;
  int m_pinCorner = -1;          // corner pin: the corner being moved (or last moved, for the arrows)
  double m_pinStart[8] = {};     // the points when the drag began
  Rect m_loupe;                  // the magnifier while placing a corner (output px)
  std::map<std::string, std::pair<float, float>> m_panelPos;  // panels the user moved (top-left)
  std::set<std::string> m_collapsed;  // panels folded to their title (inspector, paint)
  std::string m_panelDrag;                                    // the panel being dragged
  float m_panelGrabX = 0, m_panelGrabY = 0;
  double m_panelClickAt = 0;
  double m_rotStart = 0, m_rotLast = 0, m_rotAcc = 0;  // rotate drag
  double m_sliderX = 0, m_sliderStart = 0;              // slider drag (Shift: fine)
  bool m_sliderFine = false;
  double m_anchorX = 0, m_anchorY = 0;         // resize: the corner that stays put
  int m_gripCorner = 3;                        // resize: 0 TL, 1 TR, 2 BL, 3 BR
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
  bool m_savesOpen = false;
  std::vector<SavedLayout> m_saves;
  std::string m_saveCurrent;              // the save that matches the screen
  std::string m_renaming, m_renameText;   // save whose name is being typed
  std::string m_confirmDelete;            // save armed for deletion (second click)
  int m_saveScroll = 0;                   // first row shown
  std::vector<PropSpec> m_inspSchema;     // the inspector's rows (generated for clock structures)
  std::string m_elExpanded;               // clock element whose options are open
  std::string m_fontPickFor, m_fontFilter;  // the font picker: key being set, search text
  int m_fontScroll = 0;
  std::vector<std::string> m_fontList;    // installed families (loaded once)
  bool m_paintMode = false, m_painting = false, m_smartBrush = true, m_confirmClear = false;
  int m_paintTool = 0;                    // DepthTool (what a stroke does)
  int m_selectTool = 0;                   // 0 brush, 1 wand, 2 lasso, 3 hand
  float m_wandTolerance = 0.25F;
  double m_zoom = 1, m_viewX = 0, m_viewY = 0;  // brush view: output point at the centre
  bool m_panning = false;
  double m_panX = 0, m_panY = 0, m_panViewX = 0, m_panViewY = 0;
  std::vector<std::pair<double, double>> m_lasso;  // wallpaper-view points
  double m_lastClickAt = 0;
  std::vector<float> m_smoothTarget;      // Smooth: the blurred depth under the stroke
  double m_brush = 60;                    // brush radius, output px
  float m_matchValue = 0;
  double m_lastDabX = 0, m_lastDabY = 0;
  std::string m_paintOutput;
  std::vector<float> m_stroke;            // the stroke being painted (field size)
  PixelBox m_strokeBox;
  std::vector<std::pair<std::string, DepthEdits>> m_editsUndo;
  bool m_snapOn = true;   // magnet (Shift inverts)
  bool m_gridOn = true;   // grid drawn + snapping on release
  int m_uiHover = -1;
  Canvas m_editCanvas;
  float m_inspScroll = 0;
  int m_sliderControl = -1;
  std::string m_pendingSelect;  // select this id once the reload creates it
  bool m_running = true;
  bool m_demo = false;
  bool m_nothingPlaying = false;  // silent for longer than the grace period
  double m_silentSince = -1;
  double m_demoT = 0;
  std::vector<float> m_demoBands;
};


}  // namespace undershell
