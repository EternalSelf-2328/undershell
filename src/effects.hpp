// SPDX-License-Identifier: GPL-3.0-or-later
// Wallpaper effects: small pieces of GLSL that move or light the wallpaper
// (water, wind, light rays, a 2.5D camera…). Each one starts with a JSON
// header that declares its options, so the inspector builds their rows by
// itself, and anyone can add their own as
// ~/.config/undershell/effects/<id>.glsl (format: docs/EFFECTS.md).
//
// The effects that are on are joined into ONE fragment shader: the warps
// bend where the wallpaper is read, then the colour stages change what was
// read. One pass and one read of the wallpaper, plus what an effect reads on
// its own (light rays, lens fringe); each effect's names are prefixed so two
// of them never clash.
#pragma once

#include "schema.hpp"

#include <toml++/toml.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace undershell {

struct EffectParam {
  enum Kind { Number, Bool, Choice, Color } kind = Number;
  std::string key;
  std::string labelEn, labelEs;
  double min = 0, max = 1, step = 0.05, def = 0;
  bool integer = false;
  std::string defText;               // Choice and Color
  std::vector<std::string> options;  // Choice
  std::vector<std::pair<std::string, std::string>> optionNames;  // Choice: en, es per option
};

struct EffectSpec {
  std::string id;  // the file's name: [a-z0-9_]{1,24}
  std::string nameEn, nameEs;
  // warp: bends where the picture is read; colour: changes what was read;
  // light: something soft laid over it (rays, fog), drawn at a fraction of
  // the resolution in a pass of its own and stretched over the sharp picture
  enum Stage { Warp, Colour, Light } stage = Warp;
  bool time = false;        // moves by itself: frames keep coming
  bool depth = false;       // reads the wallpaper's depth
  bool audio = false;       // follows the music…
  std::string audioWhen;    // …only while this bool option is on ("" = always)
  std::vector<EffectParam> params;
  std::string body;   // the GLSL, comments stripped
  std::string file;   // where it came from ("" = built in)
  std::string error;  // why it cannot be used ("" = fine)
  uint64_t hash = 0;  // of the source text
};

// Reads an effect's text (header + GLSL). On a problem the spec comes back
// with `error` set and is not used.
[[nodiscard]] EffectSpec parseEffect(const std::string& id, const std::string& text);

// The effects undershell comes with, in order (warps first).
[[nodiscard]] std::vector<EffectSpec> builtinEffects();

// The built-in effects and the person's own (an own effect with a built-in's
// id replaces it). Effects are listed warps first, in a fixed order.
class EffectLibrary {
public:
  void load(const std::string& userDir);
  // reads the folder again if a file in it changed; true when something did
  bool refresh();
  [[nodiscard]] const std::vector<EffectSpec>& all() const { return m_all; }
  [[nodiscard]] const EffectSpec* find(const std::string& id) const;
  [[nodiscard]] const std::string& dir() const { return m_dir; }
  // a GL compile error found while drawing: kept until the file changes
  void markBroken(const std::string& id, uint64_t hash, const std::string& why);
  [[nodiscard]] std::string brokenWhy(const EffectSpec& e) const;

private:
  [[nodiscard]] std::string stamp() const;
  std::string m_dir, m_stamp;
  std::vector<EffectSpec> m_all;
  std::map<std::string, std::pair<uint64_t, std::string>> m_broken;
};

// the shared library, loaded from the config folder on first use
EffectLibrary& effectLibrary();
// $XDG_CONFIG_HOME/undershell/effects
std::string userEffectsDir();

// The effects a [[widget]] table turns on (fx_<id> = true), in library order,
// without broken ones.
[[nodiscard]] std::vector<const EffectSpec*> enabledEffects(const toml::table& opts);

// GLSL for those effects: their declarations (renamed fx<i>_…), and the
// statements that apply them in main(). Declarations come after
// kEffectApi; the statements read and write `vec2 uv` (warps), `vec3 c`
// (colours) and `vec4 L` (lights: colour laid on, premultiplied, and how
// much it covers).
struct EffectCode {
  std::string decls, warps, colours, lights;
};
[[nodiscard]] EffectCode effectCode(const std::vector<const EffectSpec*>& fx);
// What an effect can use: uniforms, wall(), depth(), noise()… (docs/EFFECTS.md).
// It declares u_tex (the wallpaper) and the other inputs; the host sets them
// (EffectInputs below) and assigns `screenPx` at the top of main().
extern const char* kEffectApi;

// The option keys of one effect in a widget table.
inline std::string effectKey(const std::string& id) { return "fx_" + id; }
inline std::string effectKey(const std::string& id, const std::string& param) { return "fx_" + id + "_" + param; }

// The inspector rows: a header, a switch per effect, and the options of
// those that are on (all of them when `all`, for default lookups).
[[nodiscard]] std::vector<PropSpec> effectRows(const toml::table& opts, bool all);

// The values of an effect's `mask` (its where): all, background, subject,
// above, below, paint1..paint4 (the painted effect masks); the index is the
// shader's mode.
[[nodiscard]] const std::vector<std::string>& effectMaskModes();

// A 256×256 RGBA tile of random bytes (the noise() texture), the same every run.
[[nodiscard]] std::vector<uint8_t> effectNoise();

}  // namespace undershell
