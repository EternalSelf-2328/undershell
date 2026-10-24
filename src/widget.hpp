// SPDX-License-Identifier: GPL-3.0-or-later
// The contract every widget type implements. The App owns the surface, the
// frame pacing, depth masking and the editor; a widget only turns its config
// and the shared inputs (time, audio, palette) into pixels.
#pragma once

#include "common.hpp"
#include "config.hpp"
#include "noctalia.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace undershell {

class TextRenderer;
class MediaService;
class WeatherService;

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
  [[nodiscard]] bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct PointerEvent {
  enum Type { Enter, Leave, Motion, Press, Release } type = Motion;
  float x = 0, y = 0;  // surface logical px
  uint32_t button = 0;
};

struct AudioFrame {
  const std::vector<float>* bands = nullptr;  // 0..1, empty when silent
  double energy = 0;
  bool silent = true;
  // kicks from the analyser's own detector (Audio::kickCount): a counter that
  // steps per kick; onsets is false for the demo spectrum, which has none
  bool onsets = false;
  uint64_t kicks = 0;
  double kickStrength = 0, kickAge = 0;
};

struct TickContext {
  double now = 0;  // seconds, steady clock
  double dt = 0;   // since this widget's previous tick
  AudioFrame audio;
  MediaService* media = nullptr;
  WeatherService* weather = nullptr;
};

struct DrawContext {
  float w = 0, h = 0;               // surface, logical px
  float outputW = 0, outputH = 0;   // the monitor, logical px
  float scale = 1;                  // device px per logical px (may be fractional)
  TextRenderer* text = nullptr;
  MediaService* media = nullptr;
  double now = 0;
  // the wallpaper under the widget's output, as the shell fits it there
  std::string wallpaper;
  int fillMode = 1;              // center crop fit stretch repeat span
  bool movingWallpaper = false;  // a video or a scene: no still picture to work with
  // the wallpaper's depth field on this output, for widgets that ask
  // (wantsDepth): a GL texture over the picture (0 = none yet), and the
  // default plane that splits subject from background
  unsigned depthField = 0;
  float depthLevel = 0.5F, depthFeather = 0.1F;
  // the effect masks painted on this wallpaper (RGBA: four channels over the
  // picture, like depthField; 0 = none painted)
  unsigned effectMasks = 0;
};

class WidgetImpl {
public:
  virtual ~WidgetImpl() = default;

  // (Re)applies the widget's [[widget]] table. Called on load, on every config
  // save and when Noctalia's palette changes; must be cheap and idempotent.
  virtual void configure(const WidgetConfig& cfg, const NoctaliaState& noct) = 0;
  virtual void tick(const TickContext& ctx) { (void)ctx; }
  virtual void draw(const DrawContext& ctx) = 0;

  // true while the picture changes every frame (keeps the frame loop alive)
  [[nodiscard]] virtual bool animating(const TickContext& ctx) const {
    (void)ctx;
    return false;
  }
  // false once there is nothing to draw (a silent spectrum faded out)
  [[nodiscard]] virtual bool visible() const { return true; }
  // the next moment (steady seconds) this widget must redraw even when idle,
  // e.g. a clock's next second; a large value means never
  [[nodiscard]] virtual double nextWakeup(double now) const {
    (void)now;
    return 1e18;
  }
  [[nodiscard]] virtual int fps() const { return 60; }
  [[nodiscard]] virtual bool fullscreen() const { return false; }
  [[nodiscard]] virtual bool usesAudio() const { return false; }
  // a still picture this widget shows as the wallpaper of its output (a
  // Wallpaper Engine scene): the app then treats it as that output's
  // wallpaper, for depth and the layouts kept per wallpaper ("" = none)
  [[nodiscard]] virtual std::string stillWallpaper() const { return {}; }
  // DrawContext::depthField is filled in (wallpaper effects)
  [[nodiscard]] virtual bool wantsDepth() const { return false; }
  // drawn at full resolution even when costly: the wallpaper itself, where
  // a softer copy would show at once (the governor lowers its frame rate instead)
  [[nodiscard]] virtual bool keepsResolution() const { return false; }
  [[nodiscard]] virtual Color accent() const { return Color::fromHex("#e2342a"); }
  virtual void rest() {}
  // nothing is playing (after a grace period) and this widget should hide:
  // fade out and stop drawing; false brings it back
  virtual void setHidden(bool hidden) { (void)hidden; }

  // Input. Widgets are click-through except inside inputRects() (surface
  // logical px), where the App forwards pointer events. Return true from
  // onPointer to request a redraw.
  [[nodiscard]] virtual std::vector<Rect> inputRects() const { return {}; }
  virtual bool onPointer(const PointerEvent& ev) {
    (void)ev;
    return false;
  }
  [[nodiscard]] virtual const char* cursor() const { return "default"; }

  // Files the widget shows: the App watches their folders and calls
  // filesChanged() when one of them is written (no polling). A path ending
  // in '/' is a folder: any file written in it counts.
  [[nodiscard]] virtual std::vector<std::string> watchedFiles() const { return {}; }
  virtual void filesChanged() {}

  // Text typed straight on the widget in the editor (a note): what to start
  // from, the draft being typed with its caret (a byte offset; nullptr when
  // typing ends), and keeping it.
  [[nodiscard]] virtual std::string editableText() const { return {}; }
  virtual void setEditing(const std::string* text, size_t caret) {
    (void)text;
    (void)caret;
  }
  virtual void saveEditedText(const std::string& text) { (void)text; }
  [[nodiscard]] bool editable() const { return m_editable; }

  // shared feeds this widget needs (the App only runs what someone wants)
  [[nodiscard]] virtual bool wantsMedia() const { return false; }
  [[nodiscard]] virtual bool wantsLyrics() const { return false; }
  [[nodiscard]] virtual bool wantsWeather() const { return false; }

protected:
  bool m_editable = false;  // its text can be typed on in the editor
};

// Known types: "visualizer", "clock", "now_playing", "shape", "image", "text", "lyrics", "calendar", "notes", "weather", "filter", "scene". Returns nullptr for an unknown type.
std::unique_ptr<WidgetImpl> createWidget(const std::string& type);
[[nodiscard]] std::vector<std::string> widgetTypes();

}  // namespace undershell
