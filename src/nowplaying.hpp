// SPDX-License-Identifier: GPL-3.0-or-later
// Ryoku's now-playing sheet (modules/desktop/music): the sleeve leads, this
// song's lyrics run beside it under the line being sung (or a live spectrum
// when there are none), and the track, its clock, a wavy seek rail and the
// three transport moves close it out. The card wears the sleeve's colour.
#pragma once

#include "canvas.hpp"
#include "widget.hpp"

#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace undershell {

struct MediaState;

struct NowPlayingConfig {
  std::string layout = "sheet";  // sheet vinyl tile poster strip portrait
  std::string plate = "cover";   // cover glass none
  bool showLyrics = true;
  // the pieces, each of which can be left out
  bool showCover = true, showArtist = true, showTime = true, showRail = true;
  bool showTransport = true, showOpen = true, showPulse = true, showViz = true;
  std::string viz = "bars";      // bars wave (when there are no lyrics)
  std::string musicApp = "spotify";
  std::string accentSource = "album";  // album theme
  std::string ink = "on_surface";
  double opacity = 1.0;
  int fps = 30;
  std::string coverShape = "rounded";
  std::string railStyle = "wave";  // wave line dots bars ring
  // the track's type: a scale on the design's size, and a font of your own
  // ("" keeps the one the card was drawn with)
  double titleSize = 1, artistSize = 1, timeSize = 1;
  std::string titleFont, artistFont, timeFont;
  std::string lyricsStyle = "plain";   // plain | focus (the sung line set well apart)

  static NowPlayingConfig fromTable(const toml::table& t);
};

// The size a layout is drawn at, so a new widget starts at its own shape.
void nowPlayingDesignSize(const std::string& layout, float& w, float& h);

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
  // the design size of the layout in use
  [[nodiscard]] std::pair<float, float> designSize() const;

  static constexpr float kW = 560, kH = 302;  // Ryoku's design size

private:
  struct Target {
    std::string id;
    Rect r;  // design units
  };
  Canvas::Size measure(const std::string& text, const TextStyle& st);

  // Where each piece of the card goes, for the layout in use: the drawing and
  // the hit targets both read it. A rect of no width is a piece this layout
  // does not have.
  struct Places {
    float w = 560, h = 302;   // the design size this layout is drawn at
    Rect plate;               // the card itself
    float radius = 22;
    Rect cover;               // the sleeve
    float coverRadius = 10;
    Rect side;                // lyrics, or the spectrum when there are none
    Rect text;                // title and artist
    int align = 0;            // 0 left, 1 centre, 2 right
    Rect rail;                // the seek rail
    Rect time;                // the clock, when it does not follow the text
    Rect open;                // the corner button
    float prevX = 0, playX = 0, nextX = 0, ctrlY = 0;
    float playR = 19, sideR = 15;
    bool clock = true;        // the elapsed / total stamp
    bool coverPlate = false;  // the sleeve is the card itself
    float record = 0;         // > 0: the sleeve is a record of this radius
  };
  [[nodiscard]] Places places() const;
  [[nodiscard]] float textBandHeight() const;
  [[nodiscard]] TextStyle titleStyle() const;
  [[nodiscard]] TextStyle artistStyle() const;
  [[nodiscard]] TextStyle clockStyle() const;
  void layoutTargets(const Places& p, bool seekable);
  [[nodiscard]] Color tint(Color col, float a = 1) const;
  void drawSleeve(Canvas& c, const MediaState& s, const Places& p);
  void drawRecord(Canvas& c, const MediaState& s, const Places& p);
  void drawSide(Canvas& c, const MediaState& s, const Places& p);
  void drawText(Canvas& c, const MediaState& s, const Places& p);
  void drawRail(Canvas& c, const MediaState& s, const Places& p);
  void drawTransport(Canvas& c, const MediaState& s, const Places& p);
  // what every piece draws with, for the frame in hand
  struct Paint {
    Color ink, dim, surface;
    float op = 1;
    double now = 0;
    bool es = false;
  };
  Paint m_paint;

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
  double m_spin = 0;  // the record's turn, radians
};

}  // namespace undershell
