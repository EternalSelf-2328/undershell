// SPDX-License-Identifier: GPL-3.0-or-later
// The lyrics of the song playing, line by line and in time (the synced
// lyrics the music card already fetches from LRCLIB), drawn with the text
// widget's styles and effects: one big line that changes with a fade, a
// slide, a pop or typed out; three lines that scroll, the one being sung in
// the middle; or karaoke, the line lighting up as it is sung. Between lines
// it sleeps until the next one is due.
#pragma once

#include "media.hpp"
#include "textfx.hpp"
#include "textwidget.hpp"
#include "widget.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace undershell {

struct LyricsConfig {
  TextConfig look;                   // style, font, effects, animation, music: the text widget's keys
  std::string layout = "lines";      // line lines karaoke
  std::string transition = "slide";  // fade slide pop typewriter: how one line gives way to the next
  std::string fallback = "title";    // title note hide: without synced lyrics
  double offset = 0;                 // seconds; more shows each line later

  static LyricsConfig fromTable(const toml::table& t);
};

class LyricsWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void configure(const LyricsConfig& cfg, const NoctaliaState& noct);
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] bool animating(const TickContext& ctx) const override;
  [[nodiscard]] double nextWakeup(double now) const override;
  [[nodiscard]] int fps() const override;
  [[nodiscard]] bool usesAudio() const override { return m_cfg.look.react != "none"; }
  [[nodiscard]] bool wantsMedia() const override { return true; }
  [[nodiscard]] bool wantsLyrics() const override { return true; }
  [[nodiscard]] Color accent() const override { return m_accent; }

private:
  enum class Mode { None, Synced, Single, Sample };
  [[nodiscard]] std::string lineText(int i) const;
  TextFx& fxFor(int i);
  void drawLine(const DrawContext& ctx, int i, float dy, float scale, float alpha, bool current, float reveal, float wipe);

  LyricsConfig m_cfg;
  TextFxStyle m_style;
  Color m_accent;
  Mode m_mode = Mode::None;
  std::vector<LyricLine> m_lines;  // Synced and Sample: the lines with their times
  std::string m_single;            // Single: the title, or a note
  std::string m_track;
  MediaState::Lyrics m_lyricsState = MediaState::Lyrics::None;
  bool m_playing = false;
  double m_now = 0, m_pos = 0;     // m_pos: seconds into the song (offset applied)
  int m_idx = -1, m_prev = -1;     // the line being sung, and the one before the change
  double m_changedAt = -100;
  std::map<int, std::unique_ptr<TextFx>> m_fx;  // by line
  std::vector<TextFxMotion> m_motion;
  std::vector<float> m_bands;
  double m_beat = 0;
  uint64_t m_kicks = 0;
  float m_size = 0;
};

}  // namespace undershell
