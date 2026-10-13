// SPDX-License-Identifier: GPL-3.0-or-later
// A text widget with effects: words on the desktop (or the song playing, the
// time) filled with a colour, a gradient or a rainbow, outlined, extruded in
// 3D, glowing, with a shadow; still, or with letters that wave, bounce, type
// themselves, glitch, flicker like neon, shake, or jump with the music.
// Styles set all of that at once; every key can then be changed on its own.
// A still text is drawn once.
#pragma once

#include "textfx.hpp"
#include "widget.hpp"

#include <map>
#include <string>
#include <vector>

namespace undershell {

struct MediaState;

struct TextConfig {
  std::string text = "undershell";  // {title} {artist} {album} {time} {date} {day} are filled in
  std::string style = "plain";      // the preset the keys below start from
  std::string font = "Inter Display";
  int weight = 800;
  bool italic = false;
  double spacing = 0;
  std::string align = "center";  // left center right
  std::string fill = "solid";    // solid gradient rainbow none
  std::string color = "on_surface", color2 = "primary";
  double angle = 90;
  double outline = 0;
  std::string outlineColor = "#111111";
  double shadow = 0.3;
  std::string shadowColor = "#000000";
  double glow = 0;
  std::string glowColor = "primary";
  double extrude = 0;
  std::string extrudeColor = "#1a1020";
  double extrudeAngle = 135;
  std::string animation = "none";  // none wave bounce typewriter glitch flicker shake
  double speed = 1;
  std::string react = "none";      // none pulse letters glow: the music
  double opacity = 1;

  static TextConfig fromTable(const toml::table& t);
};

class TextWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void configure(const TextConfig& cfg, const NoctaliaState& noct);
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] bool animating(const TickContext& ctx) const override;
  [[nodiscard]] double nextWakeup(double now) const override;
  [[nodiscard]] int fps() const override;
  [[nodiscard]] bool usesAudio() const override { return m_cfg.react != "none"; }
  [[nodiscard]] bool wantsMedia() const override { return m_media; }
  [[nodiscard]] Color accent() const override { return m_accent; }

  static const std::vector<std::string>& styles();
  // The keys a style sets, as TOML values (what the inspector writes when it
  // is picked; the widget reads the same table for keys left out).
  static const std::map<std::string, std::string>& preset(const std::string& style);
  // `text` with its placeholders filled in
  static std::string expand(const std::string& text, const MediaState* media, bool* usesTime = nullptr);

private:
  void move(double now, size_t n);

  TextConfig m_cfg;
  TextFxStyle m_style;
  Color m_accent;  // the editor's: the text's own colour when it has one, else the theme's
  TextFx m_fx;
  std::string m_shown;  // the text with its placeholders filled in
  bool m_media = false, m_time = false;
  std::vector<TextFxMotion> m_motion;
  std::vector<float> m_bands;
  double m_now = 0, m_start = 0;
  double m_beat = 0, m_energy = 0;
  uint64_t m_kicks = 0;
  bool m_playing = false;
};

}  // namespace undershell
