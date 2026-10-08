// SPDX-License-Identifier: GPL-3.0-or-later
// Ryoku's now-playing sheet (modules/desktop/music): the sleeve leads, this
// song's lyrics run beside it under the line being sung (or a live spectrum
// when there are none), and the track, its clock, a wavy seek rail and the
// three transport moves close it out. The card wears the sleeve's colour.
#pragma once

#include "canvas.hpp"
#include "widget.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace undershell {

struct NowPlayingConfig {
  std::string plate = "cover";   // cover glass none
  bool showLyrics = true;
  std::string viz = "bars";      // bars wave (when there are no lyrics)
  std::string musicApp = "spotify";
  std::string accentSource = "album";  // album theme
  std::string ink = "on_surface";
  double opacity = 1.0;
  int fps = 30;
  std::string coverShape = "rounded";
  std::string lyricsStyle = "plain";   // plain | poster (Google Sans Flex, every word its own shape)  // rounded | cycle (a new Material shape each song) | a shape name

  static NowPlayingConfig fromTable(const toml::table& t);
};

class NowPlayingWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void configure(const NowPlayingConfig& cfg, const NoctaliaState& noct);
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] bool animating(const TickContext& ctx) const override;
  [[nodiscard]] int fps() const override { return m_cfg.fps; }
  [[nodiscard]] Color accent() const override { return m_accent; }
  [[nodiscard]] std::vector<Rect> inputRects() const override;
  bool onPointer(const PointerEvent& ev) override;
  [[nodiscard]] const char* cursor() const override { return m_hover.empty() ? "default" : "pointer"; }
  [[nodiscard]] bool wantsMedia() const override { return true; }
  [[nodiscard]] bool wantsLyrics() const override { return m_cfg.showLyrics; }

  static constexpr float kW = 560, kH = 302;  // Ryoku's design size

private:
  struct Target {
    std::string id;
    Rect r;  // design units
  };
  void layoutTargets(bool seekable);
  Canvas::Size measure(const std::string& text, const TextStyle& st);

  NowPlayingConfig m_cfg;
  NoctaliaState m_noct;
  Color m_ink, m_accent, m_theme;
  Canvas m_canvas;
  MediaService* m_media = nullptr;
  std::vector<Target> m_targets;
  std::string m_hover;
  float m_k = 1, m_ox = 0, m_oy = 0;   // design → surface
  double m_now = 0;
  float m_glide = 0;                  // lyric column offset, eased
  int m_lyricIndex = -1;
  std::vector<float> m_viz;           // 40 eased bands
  std::unordered_map<std::string, Canvas::Size> m_measures;
  bool m_wasPlaying = false;
  // the cover's Material shape and its morph (a spring) between songs
  std::vector<float> m_shapeFrom, m_shapeTo, m_shapeNow;
  double m_morph = 1, m_morphVel = 0;
  std::string m_shapeTrack, m_shapeName;
  void setCoverShape(const std::string& name, bool animate);
  // the poster lyric line
  struct Poster {
    struct Word {
      std::string text;
      TextStyle style;
      float x, y;
    };
    std::vector<Word> words;
    float h = 0;
  };
  // Setting a poster costs hundreds of text measurements, so the one on
  // screen is kept until its line, its column or its stage in the ease
  // changes — a line that has arrived is set once and then only drawn.
  const Poster& poster(const std::string& text, float width, float base, int plainWeight);
  Poster posterFor(const std::string& text, float width, float base, int plainWeight);
  Poster m_poster;
  std::string m_posterKey;
  double m_posterP = 1;
};

}  // namespace undershell
