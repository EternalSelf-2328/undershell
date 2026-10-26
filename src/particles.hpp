// SPDX-License-Identifier: GPL-3.0-or-later
// Particles over the wallpaper: snow, rain, fireflies, embers, petals,
// dust, bubbles, sparks to the beat. Each system is a JSON description (an
// emitter, the ranges a particle starts from, operators that change it as it
// lives, a sprite drawn in the shader); the built-in ones are in src/pt/,
// anyone can add their own as ~/.config/undershell/particles/<id>.json
// (docs/PARTICLES.md). The structure follows the usual emitter /
// initializer / operator split, so other formats can be read into it.
//
// Cost: the simulation runs on the CPU at a fixed step (the performance
// level's simHz, drawn interpolated), into arrays sized once; each system
// is one instanced draw of procedural sprites. Counts scale with the level
// (perfKnobs().particles). Each particle lives on a depth plane: with
// "behind" on, the wallpaper's subject hides the ones beyond it.
#pragma once

#include "common.hpp"
#include "effectchain.hpp"
#include "gl.hpp"
#include "noctalia.hpp"
#include "schema.hpp"
#include "widget.hpp"

#include <toml++/toml.hpp>

#include <array>
#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace undershell {

struct ParticleOp {
  enum Type { Gravity, Drag, Turbulence, Fade, OscAlpha, OscPosition, SizeChange, ColorChange, Vortex, Audio } type = Gravity;
  std::array<float, 4> v{};  // the numbers each type takes (see parseParticles)
  Color color;               // ColorChange
};

struct ParticleDef {
  std::string id, nameEn, nameEs, file, error;
  std::string category;  // weather nature fire ("" = the person's own); the picker's groups
  uint64_t hash = 0;
  int max = 200;          // at "high", amount 1
  float rate = 20;        // per second
  float prewarm = 0;      // seconds simulated before the first frame
  enum Sprite { Dot, Glow, Flake, Streak, Petal, Bubble, Spark, Leaf, Texture } sprite = Dot;
  bool additive = false;
  float streak = 0;       // stretched along the velocity by this many seconds of travel
  float react = 0;        // default response to music
  bool sphere = false;    // emitter: a disc (origin, radius) or a box (origin, size)
  float ox = 0.5F, oy = 0.5F, sx = 1, sy = 1, radius = 0.1F;  // fractions of the screen (radius of its height)
  // initial ranges [lo, hi]
  std::array<float, 2> life{4, 8}, size{3, 6}, speed{20, 60}, dir{90, 90}, alpha{1, 1}, rot{0, 0}, spin{0, 0}, depth{0.5F, 0.5F};
  Color c0, c1;
  std::vector<ParticleOp> ops;
  // read from other formats (a Wallpaper Engine scene's particles):
  std::string texture;        // Texture: the picture's key for the drawer
  int frames = 1, cols = 1, rows = 1;  // a sprite sheet: frames in a grid
  int cellW = 0, cellH = 0;            // its cell size, when known (the grid follows from the picture)
  float sheetTime = 1;        // seconds for the whole sequence
  bool velBox = false;        // velocity drawn from a box (min x, min y, max x, max y; y down)
  std::array<float, 4> vel{};
  std::array<float, 2> radial{0, 0};  // speed away from the emitter's centre
  std::array<float, 2> jitter{0, 0};  // speed in a random direction, added
  float radiusMin = 0;        // a disc emitter's inner edge (fraction of the height)
};

[[nodiscard]] ParticleDef parseParticles(const std::string& id, const std::string& text);
[[nodiscard]] std::vector<ParticleDef> builtinParticles();

class ParticleLibrary {
public:
  void load(const std::string& userDir);
  bool refresh();  // the folder changed: read again
  [[nodiscard]] const std::vector<ParticleDef>& all() const { return m_all; }
  [[nodiscard]] const ParticleDef* find(const std::string& id) const;

private:
  [[nodiscard]] std::string stamp() const;
  std::string m_dir, m_stamp;
  std::vector<ParticleDef> m_all;
};
ParticleLibrary& particleLibrary();
std::string userParticlesDir();  // $XDG_CONFIG_HOME/undershell/particles

inline std::string particleKey(const std::string& id) { return "pt_" + id; }
inline std::string particleKey(const std::string& id, const std::string& opt) { return "pt_" + id + "_" + opt; }

// The inspector rows: a header, a switch per system, the options of those on.
[[nodiscard]] std::vector<PropSpec> particleRows(const toml::table& opts, bool all);

// What a person sets on one system.
struct ParticleSettings {
  float amount = 1, speed = 1, size = 1, wind = 0, react = 0, split = 0.5F;
  Color tint{1, 1, 1, 1};
  bool behind = false;
  int mask = 0;  // effectMaskModes(): all, above, below, paint1..4 are offered
};

// One running system: the simulation and what it hands to the GPU.
class ParticleSystem {
public:
  ParticleSystem(const ParticleDef& def, const ParticleSettings& s, uint64_t seed);
  void settle(const ParticleSettings& s);  // new settings, keeping the particles
  // advance to `now` in fixed steps; `aspect` = screen width / height
  void advance(double dt, float aspect, const MusicLevels& music);
  // the particles at this instant into `out`: 12 floats each (x, y in
  // 1080-high units, size, rotation | rgba premultiplied | vx, vy, depth, seed)
  size_t fill(std::vector<float>& out, float aspect, const MusicLevels& music) const;
  [[nodiscard]] const ParticleDef& def() const { return m_def; }
  [[nodiscard]] const ParticleSettings& settings() const { return m_set; }
  [[nodiscard]] size_t count() const { return m_n; }

private:
  void spawn(float aspect);
  void step(float dt, float aspect, const MusicLevels& music);
  void resize(size_t cap);
  [[nodiscard]] size_t wantedCap() const;
  [[nodiscard]] float rnd() {
    m_rng ^= m_rng << 13, m_rng ^= m_rng >> 7, m_rng ^= m_rng << 17;
    return static_cast<float>(m_rng >> 40) / static_cast<float>(1ULL << 24);
  }
  [[nodiscard]] float range(const std::array<float, 2>& r) { return r[0] + (r[1] - r[0]) * rnd(); }

  ParticleDef m_def;
  ParticleSettings m_set;
  uint64_t m_rng;
  size_t m_n = 0, m_cap = 0;
  // one array per field, sized once to the cap
  std::vector<float> m_x, m_y, m_px, m_py, m_vx, m_vy, m_age, m_life, m_size, m_rot, m_spin, m_alpha, m_depth, m_seed, m_fa, m_fp;
  std::vector<std::array<float, 3>> m_col;
  double m_acc = 0, m_time = 0;
  float m_emit = 0;
  float m_blend = 1;  // between the last two steps
};

// The systems a [[widget]] block turns on (pt_<id> = true), drawn over the
// wallpaper by the wallpaper widget.
class ParticleField {
public:
  ParticleField() = default;
  ParticleField(const ParticleField&) = delete;
  ParticleField& operator=(const ParticleField&) = delete;
  ~ParticleField();
  void configure(const toml::table& opts, const NoctaliaState& noct);
  // systems given directly (a scene's own) instead of pt_<id> options
  void configureDefs(const std::vector<ParticleDef>& defs);
  // where their units land on the surface: px = pos * unit + offset, in a
  // space of this width / height (else the surface's 1080-high units)
  void setPlacement(float unit, float offX, float offY, float aspect);
  // a Texture sprite's picture by its key: the GL texture, and whether its
  // red is the coverage (a one-channel picture)
  using TextureLookup = std::function<std::pair<GLuint, bool>(const std::string&)>;
  void setTextures(TextureLookup lookup) { m_textures = std::move(lookup); }
  [[nodiscard]] size_t systems() const { return m_sys.size(); }
  bool refresh();
  [[nodiscard]] bool empty() const { return m_sys.empty(); }
  [[nodiscard]] bool audio() const;
  [[nodiscard]] bool depth() const;  // some system hides behind the subject or follows a painted mask
  void tick(const TickContext& ctx);
  // over what is drawn, in the surface's logical px; `img` and `fill` place
  // the picture (for the depth field and the masks)
  void draw(const DrawContext& ctx, float imgW, float imgH, int only = -1);  // only: one system by index
  [[nodiscard]] size_t count() const;

private:
  toml::table m_opts;
  NoctaliaState m_noct;
  std::vector<std::unique_ptr<ParticleSystem>> m_sys;
  MusicLevels m_music;
  float m_aspect = 16.0F / 9;
  bool m_placed = false;
  float m_unit = 1, m_offX = 0, m_offY = 0;
  TextureLookup m_textures;
  double m_last = -1;
  Program m_prog;
  GLuint m_vbo = 0;
  std::vector<float> m_buf;
};

}  // namespace undershell
