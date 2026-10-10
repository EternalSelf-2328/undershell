// SPDX-License-Identifier: GPL-3.0-or-later
// Onomatopoeia: a sound effect lettered on the kicks, the way comics draw
// them -- English (BOOM!), Japanese (ドン!), Korean (쾅!) or Chinese (轰!) --
// that slams in with a shake, holds, and goes. The harder the kick, the
// bigger the word.
#pragma once

#include "canvas.hpp"
#include "common.hpp"
#include "config.hpp"
#include "noctalia.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace undershell {

struct DrawContext;

class SfxLayer {
public:
  void configure(const toml::table& options, const NoctaliaState& noct);
  void tick(double dt, bool kicked, double strength);
  void draw(const DrawContext& ctx, float opacity);
  [[nodiscard]] bool alive() const;

  enum Lang { English, Japanese, Korean, Chinese };
  // the words a kick of this strength (0..1) may letter, in a language
  static const std::vector<const char*>& words(Lang lang, double strength);
  // a word set top to bottom, one character a line (the way CJK effects are)
  static std::string vertical(const std::string& word);

private:
  struct Word {
    double age = -1;  // < 0: none
    double strength = 0;
    std::string text;
    Lang lang = English;
    float x = 0.5F, y = 0.5F;  // its centre, a share of the box
    float angle = 0;           // radians
    int colour = 0;
    uint32_t seed = 0;
  };
  void spawn(double strength);
  float rnd();
  Lang pickLang();

  std::array<Word, 4> m_words{};
  std::string m_lang = "mix";      // mix english japanese korean chinese
  std::string m_style = "comic";   // comic manga
  std::string m_colors = "classic";
  std::string m_font;              // "" = each language's own
  bool m_burst = true;
  double m_size = 1, m_amount = 0.6, m_preview = -1, m_since = 1;
  std::vector<std::string> m_custom;
  std::string m_last;
  Color m_theme;
  Canvas m_canvas;
  uint32_t m_rng = 0x5F3759DFU;
};

}  // namespace undershell
