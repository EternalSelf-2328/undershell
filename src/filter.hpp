// SPDX-License-Identifier: GPL-3.0-or-later
// A colour filter over the wallpaper. A layer-shell surface cannot change
// what lies under it, so the filter draws the wallpaper itself -- the same
// picture, fitted to the output with the same maths as the shell (crop, fit,
// center, stretch, repeat) -- through brightness, contrast, saturation, hue,
// warmth, a tint or two-tone, a vignette, blur, grain, posterize or
// pixelate. It sits at the bottom of the stack; with Depth on it is cut where
// the wallpaper's subject stands, so only the background is filtered. One
// texture and one draw per wallpaper or option change, nothing while still.
// A video or scene wallpaper has no still picture: then it draws nothing.
#pragma once

#include "gl.hpp"
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
  ~FilterWidget() override;
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] bool fullscreen() const override { return true; }
  [[nodiscard]] double nextWakeup(double now) const override { return m_loading ? now + 0.05 : 1e18; }
  [[nodiscard]] Color accent() const override { return m_tintC; }

  static const std::vector<std::string>& styles();
  static const std::map<std::string, std::string>& preset(const std::string& style);

private:
  struct Load;  // a decode on its own thread
  void startLoad(const std::string& path, int maxSide);

  FilterConfig m_cfg;
  Color m_tintC, m_darkC, m_lightC;
  Program m_prog;
  GLuint m_texture = 0;
  int m_texW = 0, m_texH = 0;  // the picture's own size
  int m_pxW = 0, m_pxH = 0;    // the texture's
  std::string m_path;  // the picture in the texture (or being decoded)
  std::shared_ptr<Load> m_load;
  bool m_loading = false;
};

}  // namespace undershell
