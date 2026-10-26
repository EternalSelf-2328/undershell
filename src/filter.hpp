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
//
// It also carries the animated effects (effects.hpp): water, wind, light
// rays, a 2.5D camera… joined into the same single pass as the colour
// filter, so a moving wallpaper costs one full-screen draw per frame, at the
// effects' own frame rate (fx_fps, 30 by default).
#pragma once

#include "effectchain.hpp"
#include "particles.hpp"
#include "gl.hpp"
#include "wallstore.hpp"
#include "widget.hpp"

#include <array>
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

  // nothing would change on screen: the picture as it is (custom style at
  // its defaults, or every stage at zero)
  [[nodiscard]] bool neutral() const;
  static FilterConfig fromTable(const toml::table& t);
};

class FilterWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void tick(const TickContext& ctx) override {
    m_fx.tick(ctx);
    m_pt.tick(ctx);
  }
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] bool fullscreen() const override { return true; }
  [[nodiscard]] bool animating(const TickContext&) const override {
    return !idle() && (m_fade.animating() || m_fx.moving() || !m_pt.empty());
  }
  // a filter that changes nothing and has no effects nor particles draws
  // nothing: the wallpaper shows as its program draws it, with no copy over
  // it (a copy that follows a wallpaper change late, and costs a full-screen
  // draw)
  [[nodiscard]] bool visible() const override { return !idle(); }
  [[nodiscard]] int fps() const override { return m_fx.empty() && m_pt.empty() ? 60 : m_fx.fps(); }
  [[nodiscard]] bool usesAudio() const override { return m_fx.audio() || m_pt.audio(); }
  [[nodiscard]] bool wantsDepth() const override { return m_fx.depth() || m_pt.depth(); }
  [[nodiscard]] bool keepsResolution() const override { return true; }
  // the person's own effects and particles: a new or changed file applies at once
  [[nodiscard]] std::vector<std::string> watchedFiles() const override { return {userEffectsDir() + "/", userParticlesDir() + "/"}; }
  void filesChanged() override {
    m_fx.refresh();
    m_pt.refresh();
  }
  [[nodiscard]] size_t particles() const { return m_pt.count(); }
  // a picture still decoding: look again soon
  [[nodiscard]] double nextWakeup(double now) const override { return !idle() && m_fade.waiting() ? now + 0.05 : 1e18; }
  [[nodiscard]] Color accent() const override { return m_tintC; }

  static const std::vector<std::string>& styles();
  static const std::map<std::string, std::string>& preset(const std::string& style);

private:
  [[nodiscard]] bool idle() const { return m_cfg.neutral() && m_fx.empty() && m_pt.empty(); }
  void drawPicture(const WallPic& pic, const DrawContext& ctx, float opacity);

  FilterConfig m_cfg;
  Color m_tintC, m_darkC, m_lightC;
  EffectChain m_fx;
  ParticleField m_pt;
  WallpaperFade m_fade;
};

}  // namespace undershell
