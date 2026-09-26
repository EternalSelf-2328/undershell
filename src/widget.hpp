// SPDX-License-Identifier: GPL-3.0-or-later
// The contract every widget type implements. The App owns the surface, the
// frame pacing, depth masking and the editor; a widget only turns its config
// and the shared inputs (time, audio, palette) into pixels.
#pragma once

#include "common.hpp"
#include "config.hpp"
#include "noctalia.hpp"

#include <memory>
#include <string>
#include <vector>

namespace undershell {

class TextRenderer;

struct AudioFrame {
  const std::vector<float>* bands = nullptr;  // 0..1, empty when silent
  double energy = 0;
  bool silent = true;
};

struct TickContext {
  double now = 0;  // seconds, steady clock
  double dt = 0;   // since this widget's previous tick
  AudioFrame audio;
};

struct DrawContext {
  float w = 0, h = 0;               // surface, logical px
  float outputW = 0, outputH = 0;   // the monitor, logical px
  int scale = 1;
  TextRenderer* text = nullptr;
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
  [[nodiscard]] virtual Color accent() const { return Color::fromHex("#e2342a"); }
  virtual void rest() {}
};

// Known types: "visualizer". Returns nullptr for an unknown type.
std::unique_ptr<WidgetImpl> createWidget(const std::string& type);
[[nodiscard]] std::vector<std::string> widgetTypes();

}  // namespace undershell
