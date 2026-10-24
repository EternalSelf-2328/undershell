// SPDX-License-Identifier: GPL-3.0-or-later
// The animated effects that are on in one [[widget]] block (effects.hpp),
// ready to draw: the values of their options, the music they follow, the
// programs built from them (with a broken effect found and left out), the
// noise texture and the lights pass. The wallpaper widget (filter.hpp) and
// every other widget (FxWidget, fxwidget.hpp) draw through one of these;
// each brings its own shader head and body around the effects.
#pragma once

#include "effects.hpp"
#include "gl.hpp"
#include "noctalia.hpp"
#include "widget.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace undershell {

// The music as four slow numbers 0..1: bass, mids, treble (fast up, slower
// down) and the beat (the analyser's kick, fading). Effects and particles
// read it.
struct MusicLevels {
  std::array<float, 4> v{};
  bool live = false;  // sounding, or still fading out
  void tick(const TickContext& ctx);
  void clear() { v = {}, live = false; }
};

class EffectChain {
public:
  EffectChain() = default;
  EffectChain(const EffectChain&) = delete;
  EffectChain& operator=(const EffectChain&) = delete;
  ~EffectChain();

  // the effects the block turns on (fx_<id> = true) and their options;
  // colours through the palette
  void configure(const toml::table& opts, const NoctaliaState& noct);
  // the person's effects folder changed: read again; true if anything did
  bool refresh();
  [[nodiscard]] bool empty() const { return m_fx.empty(); }
  [[nodiscard]] bool moving() const { return m_moving; }  // frames keep coming
  [[nodiscard]] bool audio() const { return m_audio; }
  [[nodiscard]] bool depth() const { return m_depth; }  // needs the depth field (or the painted masks)
  [[nodiscard]] int fps() const { return m_fps; }
  void tick(const TickContext& ctx);

  // The programs: head + kEffectApi + the effects + fit + body (with
  // //@WARPS and //@COLOURS) and, when a light is on, + lights (//@LIGHTS).
  void build(const char* head, const char* fit, const char* body, const char* lights);
  [[nodiscard]] const Program& program() const { return m_prog; }
  [[nodiscard]] bool hasLights() const { return m_lightProg.valid(); }
  // Uses `prog` (0 main, 1 lights) and sets what the effects read: time,
  // music, quality, depth (unit 1), noise (2), the lights (3), the painted
  // masks (4), each effect's options. `flipY`: the picture is an off-screen
  // render (its rows bottom up).
  void use(int which, const DrawContext& ctx, bool flipY);
  // The lights pass into its smaller texture (bound on unit 3 after);
  // `picture` sets the picture's own uniforms and binds it on unit 0.
  void drawLights(const DrawContext& ctx, bool flipY, const std::function<void(const Program&)>& picture);

private:
  struct Fx {
    std::string id;
    uint64_t hash = 0;
    std::vector<std::array<float, 3>> values;  // per option (a colour uses all three)
    float strength = 1, split = 0.5F;
    int mask = 0;              // effectMaskModes()
    std::vector<GLint> loc[2];  // per option, then strength, mask, split; main and lights program
  };
  toml::table m_opts;
  NoctaliaState m_noct;
  std::vector<Fx> m_fx;
  Program m_prog, m_lightProg;
  std::string m_built;  // the effects the programs were built for
  GLuint m_noise = 0;
  GLuint m_lightTex = 0, m_lightFbo = 0;
  int m_lightW = 0, m_lightH = 0;
  int m_fps = 30;
  bool m_time = false, m_audio = false, m_depth = false, m_moving = false;
  MusicLevels m_level;
  double m_start = -1;
};

}  // namespace undershell
