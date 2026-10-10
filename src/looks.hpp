// SPDX-License-Identifier: GPL-3.0-or-later
// The looks that keep their own state and draw themselves in strokes: manga
// speed lines, rain (snow, petals), ripples on water, a neon sign and
// fireworks. Each takes the music's beat from the visualizer (its kicks,
// their strength, the groove's energy) and draws into its box.
#pragma once

#include "config.hpp"
#include "electric.hpp"
#include "noctalia.hpp"
#include "strokes.hpp"

#include <memory>
#include <string>

namespace undershell {

struct DrawContext;

// what the music did since the last tick
struct Beat {
  double dt = 0;
  bool kick = false;
  double strength = 0;  // 0..1: how hard the kick was
  double energy = 0;    // 0..1: the groove right now
  bool live = false;    // sound is playing
};

class StrokeLook {
public:
  virtual ~StrokeLook() = default;
  virtual void configure(const toml::table& options, const NoctaliaState& noct) = 0;
  virtual void tick(const Beat& beat) = 0;
  virtual void draw(const DrawContext& ctx, float opacity) = 0;
  // something on screen
  [[nodiscard]] virtual bool visible() const = 0;
  // it changes from frame to frame (else it is drawn once and left)
  [[nodiscard]] virtual bool moving() const = 0;
  // in silence, how soon it wants another frame (seconds); large: never
  [[nodiscard]] virtual double idleFrame() const { return 1e18; }
};

// the look for a style name, or nullptr for the visualizer's own looks
std::unique_ptr<StrokeLook> makeLook(const std::string& style);
bool isStrokeLook(const std::string& style);

// shared by the looks
void setColours(StrokeRenderer::Look& look, Color core, Color halo);
Color lighter(Color c, float towardWhite);

}  // namespace undershell
