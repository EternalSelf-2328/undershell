// SPDX-License-Identifier: GPL-3.0-or-later
// A Wallpaper Engine wallpaper as undershell understands it: where the
// person's Steam keeps Wallpaper Engine and its Workshop items, what a
// wallpaper folder is (scene, video, web), and a scene read from the folder
// undershell-scene unpacked (plain JSON and converted textures) into layers
// to draw. Only reads JSON with nlohmann (no binary parsing here: that is
// the confined helper's job). Nothing of Wallpaper Engine ships with
// undershell; its assets are read from the person's own install.
#pragma once

#include "common.hpp"
#include "particles.hpp"

#include <toml++/toml.hpp>

#include <array>
#include <map>
#include <string>
#include <vector>

namespace undershell {

// Steam libraries (libraryfolders.vdf) with Wallpaper Engine's Workshop
// folder (…/workshop/content/431960) and its assets (…/common/wallpaper_engine/assets).
[[nodiscard]] std::vector<std::string> weWorkshopDirs();
[[nodiscard]] std::string weAssetsDir();
// a Workshop id ("1234567") or a folder: the wallpaper's folder, or ""
[[nodiscard]] std::string weWallpaperDir(const std::string& idOrPath);

struct WeProject {
  std::string type;   // scene, video, web, application (lowercase)
  std::string title;
  std::string file;   // video: the file; scene: scene.json
  std::string preview;  // its preview picture (absolute), when there is one
  // user properties: name -> the value as text (colours "r g b" 0..1, bools "true"/"false")
  std::map<std::string, std::string> properties;
};
[[nodiscard]] bool readWeProject(const std::string& folder, WeProject& out, std::string& why);

struct WeLayer {
  int id = -1, parent = -1;
  std::string name;
  float ox = 0, oy = 0;          // scene units, y up (the parent's space for a child)
  float sx = 1, sy = 1;
  float angle = 0;               // radians, about z
  float w = 0, h = 0;            // 0: the texture's own size
  std::string align = "center";
  bool visible = true;
  float alpha = 1, brightness = 1;
  Color color{1, 1, 1, 1};
  bool additive = false;
  std::string texture;           // "materials/foo" (no extension): in the scene, else in the assets
  bool compose = false;          // an effect-only layer (Wallpaper Engine's compose layers): nothing of its own to draw
  // Wallpaper Engine's effects on it, in undershell's terms:
  // what never moves is baked into its pixels when it loads…
  float opacity = 1;             // "opacity": alpha times its mask
  std::string opacityMask;
  Color tint{1, 1, 1, 1};        // "tint": towards a colour where its mask says
  float tintAmount = 0;
  std::string tintMask;
  // …and what moves runs through undershell's own effects (effects.hpp)
  struct Fx {
    toml::table options;  // fx_<id> = true and its options (the mask filled in when drawn)
    std::string mask;     // the Wallpaper Engine mask texture ("materials/…"), or ""
    bool flowMap = false; // the mask is a flow map: direction and amount come from it
  };
  std::vector<Fx> fx;
  int unmapped = 0;              // effects with no counterpart yet
  int particle = -1;             // a particle system's object: its index in WeScene::particles
};

struct WeScene {
  float width = 1920, height = 1080;  // the orthographic projection
  Color clear{0, 0, 0, 1};
  std::vector<WeLayer> layers;        // in drawing order
  // files the scene refers to that are not in its folder (asked from the assets)
  std::vector<std::string> needs;
  int skipped = 0;                    // objects not drawn yet (text, models, sounds, particles)
  int effects = 0, mapped = 0;        // Wallpaper Engine effects met, and drawn with undershell's
  // particle systems, in undershell's terms (particles.hpp): units of a
  // 1080-high scene, y down; Texture sprites keyed by their texture path
  std::vector<ParticleDef> particles;
};

// Reads an unpacked scene. `assets` is where files missing from the scene
// are looked up (an unpacked copy of the needed assets); what is still
// missing is listed in `needs`.
[[nodiscard]] bool readWeScene(const std::string& unpacked, const std::string& assets, const WeProject& project, WeScene& out,
                               std::string& why);

// A layer's corners on the scene (y up), its parents' transforms applied:
// [x0, y0, x1, y1, x2, y2, x3, y3] for its top-left, top-right,
// bottom-left, bottom-right (texture u,v 0,0 / 1,0 / 0,1 / 1,1).
[[nodiscard]] std::array<float, 8> weLayerQuad(const WeScene& scene, const WeLayer& layer, float texW, float texH);

}  // namespace undershell
