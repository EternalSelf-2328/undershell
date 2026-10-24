// SPDX-License-Identifier: GPL-3.0-or-later
// A colour filter over the wallpaper. A layer-shell surface cannot change
// what lies under it, so the filter draws the wallpaper itself -- the same
// picture, fitted to the output with the same maths as the shell (crop, fit,
// center, stretch, repeat) -- through brightness, contrast, saturation, hue,
// warmth, a tint or two-tone, a vignette, blur, grain, posterize or
// pixelate. It sits at the bottom of the stack; with Depth on it is cut where
// the wallpaper's subject stands, so only the background is filtered. The
// picture comes from the shared WallpaperStore (whatever program draws the
// wallpaper); a new wallpaper crossfades in. One draw per wallpaper or option
// change (and the frames of a fade), nothing while still. A video or scene
// wallpaper has no still picture: then it draws nothing.
#pragma once

#include "gl.hpp"
#include "wallstore.hpp"
#include "widget.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace undershell {

struct FilterConfig {
  std::string style = "warm";
  double brightness = 0, contrast = 1, saturation = 1, hue = 0, temperature = 0;
  std::string tint = "primary";
  double tintAmount = 0;
  bool duotone = false;
  std::string dark = "#1b1530", light = "#ffd9a8";
  double vignette = 0, blur = 0, grain = 0;
  int posterize = 0;  // levels; 0 off
  double pixelate = 0;
  double opacity = 1;

  static FilterConfig fromTable(const toml::table& t);
};

class FilterWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] bool fullscreen() const override { return true; }
  [[nodiscard]] bool animating(const TickContext&) const override { return m_fade.animating(); }
  // a picture still decoding: look again soon
  [[nodiscard]] double nextWakeup(double now) const override { return m_fade.waiting() ? now + 0.05 : 1e18; }
  [[nodiscard]] Color accent() const override { return m_tintC; }

  static const std::vector<std::string>& styles();
  static const std::map<std::string, std::string>& preset(const std::string& style);

private:
  void drawPicture(const WallPic& pic, const DrawContext& ctx, float opacity);

  FilterConfig m_cfg;
  Color m_tintC, m_darkC, m_lightC;
  Program m_prog;
  WallpaperFade m_fade;
};

}  // namespace undershell
