// SPDX-License-Identifier: GPL-3.0-or-later
// A picture on the desktop: a photo, a sticker, a GIF, an animated WebP or
// APNG, or a video, with its transparency. Fitted into its box (all of it,
// filling it or stretched), with rounded corners or cut to a shape, and as a sticker:
// an outline round its silhouette and a soft shadow under it. It can bounce or
// pulse with the music's kicks.
//
// A still picture is decoded once and drawn once. An animation is decoded on
// its own thread, kept whole when short, and the widget sleeps until its next
// frame is due, so a 10 fps GIF is drawn 10 times a second, not 60.
#pragma once

#include "gl.hpp"
#include "video.hpp"
#include "widget.hpp"

#include <memory>
#include <string>
#include <vector>

namespace undershell {

struct ImageConfig {
  std::string file;              // a path (~/ allowed)
  std::string fit = "fit";       // fit (all of it) fill (covers the box) stretch
  double corner = 0;             // rounded corners, a share of the picture's shorter side
  std::string cut = "none";      // none circle, or a Material 3 shape
  double outline = 0;            // sticker outline round the silhouette, px
  std::string outlineColor = "#ffffff";
  double shadow = 0;             // 0..1
  double speed = 1;              // an animation's speed
  std::string react = "none";    // none bounce pulse: the music's kicks
  bool mirror = false;
  double opacity = 1;

  static ImageConfig fromTable(const toml::table& t);
};

class ImageWidget final : public WidgetImpl {
public:
  ~ImageWidget() override;
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void configure(const ImageConfig& cfg, const NoctaliaState& noct);
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] bool animating(const TickContext& ctx) const override;
  [[nodiscard]] double nextWakeup(double now) const override;
  [[nodiscard]] bool usesAudio() const override { return m_cfg.react != "none"; }
  [[nodiscard]] Color accent() const override { return m_outlineC.a > 0 && m_cfg.outline > 0 ? m_outlineC : Color::fromHex("#7fc8ff"); }
  // why nothing shows ("" when it does, or while it loads)
  [[nodiscard]] std::string error() const { return m_src ? m_src->error() : std::string(); }

private:
  ImageConfig m_cfg;
  Color m_outlineC;
  std::vector<float> m_radii;  // the cut shape's outline (64 angles)
  std::unique_ptr<VideoSource> m_src;
  std::string m_openPath;
  Program m_prog;
  Program m_hint;
  double m_lastNow = -1, m_beat = 0;
  uint64_t m_kicks = 0;
};

}  // namespace undershell
