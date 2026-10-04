// SPDX-License-Identifier: GPL-3.0-or-later
// The editable options of every widget type: one table that drives the
// editor's inspector (and can document the config). Keys and defaults mirror
// what each widget's fromTable() reads.
#pragma once

#include <string>
#include <vector>

namespace undershell {

struct PropSpec {
  // Header, Element and Font rows only appear in the inspector's generated
  // list (clock structures); the config documents the four value kinds
  enum Kind { Enum, Bool, Number, Color, Header, Element, Font } kind = Enum;
  std::string key;
  std::string labelEn;
  std::string labelEs;
  std::vector<std::string> options;  // Enum; Color: its swatches (empty = the standard ones)
  double min = 0, max = 1, step = 0.05, def = 0;  // Number (def also for Bool: 0/1)
  std::string defText;                             // Enum / Color / Font default
  bool integer = false;
  bool indent = false;  // an element's own option, under its row
  std::string face;     // Element: the structure it belongs to
};

inline const std::vector<PropSpec>& schemaFor(const std::string& type) {
  using K = PropSpec::Kind;
  static const std::vector<PropSpec> visualizer = {
      {K::Enum, "style", "Look", "Estilo",
       {"bars", "split", "dots", "segments", "wave", "ribbon", "curtain", "line", "frame", "radial", "orb", "spiral", "halo",
        "vortex", "fire"}, 0, 0, 0,
       0, "bars"},
      {K::Enum, "color_mode", "Colour", "Color", {"theme", "gradient", "custom"}, 0, 0, 0, 0, "theme"},
      {K::Color, "color", "Colour 1", "Color 1", {}, 0, 0, 0, 0, "primary"},
      {K::Color, "color2", "Colour 2", "Color 2", {}, 0, 0, 0, 0, "tertiary"},
      {K::Number, "bars", "Bands", "Bandas", {}, 16, 128, 8, 64, "", true},
      {K::Number, "thickness", "Thickness", "Grosor", {}, 0.1, 1, 0.02, 0.58},
      {K::Number, "glow", "Glow", "Brillo", {}, 0, 1, 0.05, 0.6},
      {K::Number, "reflection", "Reflection", "Reflejo", {}, 0, 0.5, 0.05, 0.1},
      {K::Number, "gain", "Gain", "Ganancia", {}, 0.2, 3, 0.1, 1},
      {K::Number, "smoothing", "Smoothing", "Suavizado", {}, 0, 1, 0.05, 0.5},
      {K::Enum, "grow", "Grow", "Crecer", {"up", "down", "center", "left", "right"}, 0, 0, 0, 0, "up"},
      {K::Enum, "shape", "Shape", "Forma", {"rounded", "square"}, 0, 0, 0, 0, "rounded"},
      {K::Number, "segments", "Segments", "Segmentos", {}, 3, 24, 1, 10, "", true},
      {K::Bool, "peaks", "Peak caps", "Picos", {}, 0, 1, 1, 0},
      {K::Bool, "mirror", "Mirror", "Espejo", {}, 0, 1, 1, 0},
      {K::Bool, "idle_wave", "Idle wave", "Onda en reposo", {}, 0, 1, 1, 0},
      {K::Number, "spin", "Spin °/s", "Giro °/s", {}, 0, 120, 5, 0},
      {K::Number, "opacity", "Opacity", "Opacidad", {}, 0.1, 1, 0.05, 1},
      {K::Number, "fps", "FPS", "FPS", {}, 15, 144, 15, 60, "", true},
      {K::Number, "rotation", "Rotation °", "Rotación °", {}, -180, 180, 0.1, 0},
      {K::Bool, "depth", "Depth", "Profundidad", {}, 0, 1, 1, 1},
      {K::Number, "depth_level", "Depth plane", "Plano de prof.", {}, 0, 100, 1, 0, "", true},
      {K::Number, "layer", "Layer", "Capa", {}, -10, 10, 1, 0, "", true},
  };
  static const std::vector<PropSpec> clock = {
      {K::Enum, "face", "Face", "Estilo",
       {"digital", "minimal", "analog", "flip", "rings", "bighour", "metal", "goodnight", "grand", "column", "outline", "banner",
        "stacked"},
       0, 0, 0, 0, "digital"},
      {K::Enum, "date", "Date", "Fecha", {"none", "inline", "badge", "stacked"}, 0, 0, 0, 0, "inline"},
      {K::Bool, "clock_24h", "24-hour", "24 horas", {}, 0, 1, 1, 1},
      {K::Bool, "seconds", "Seconds", "Segundos", {}, 0, 1, 1, 0},
      {K::Enum, "accent", "Accent", "Acento", {"primary", "secondary", "tertiary", "brand", "mono", "custom"}, 0, 0, 0, 0, "primary"},
      {K::Color, "accent_color", "Custom accent", "Acento propio", {}, 0, 0, 0, 0, "#e2342a"},
      {K::Color, "ink", "Ink", "Tinta", {}, 0, 0, 0, 0, "on_surface"},
      {K::Enum, "language", "Language", "Idioma", {"system", "en", "es"}, 0, 0, 0, 0, "system"},
      {K::Bool, "weather", "Weather (metal)", "Clima (metal)", {}, 0, 1, 1, 1},
      {K::Bool, "fahrenheit", "Fahrenheit", "Fahrenheit", {}, 0, 1, 1, 0},
      {K::Number, "opacity", "Opacity", "Opacidad", {}, 0.1, 1, 0.05, 1},
      {K::Number, "rotation", "Rotation °", "Rotación °", {}, -180, 180, 0.1, 0},
      {K::Bool, "depth", "Depth", "Profundidad", {}, 0, 1, 1, 1},
      {K::Number, "depth_level", "Depth plane", "Plano de prof.", {}, 0, 100, 1, 0, "", true},
      {K::Number, "layer", "Layer", "Capa", {}, -10, 10, 1, 0, "", true},
  };
  static const std::vector<PropSpec> nowPlaying = {
      {K::Enum, "plate", "Plate", "Fondo", {"cover", "glass", "none"}, 0, 0, 0, 0, "cover"},
      {K::Enum, "cover_shape", "Cover shape", "Forma de portada",
       {"rounded", "cycle", "circle", "cookie12Sided", "cookie9Sided", "cookie7Sided", "cookie6Sided", "cookie4Sided", "flower",
        "clover8Leaf", "clover4Leaf", "softBurst", "burst", "sunny", "verySunny", "puffyDiamond", "pentagon", "pill", "square", "oval"},
       0, 0, 0, 0, "rounded"},
      {K::Bool, "show_lyrics", "Lyrics", "Letra", {}, 0, 1, 1, 1},
      {K::Enum, "lyrics_style", "Lyrics style", "Estilo de letra", {"plain", "poster"}, 0, 0, 0, 0, "plain"},
      {K::Enum, "viz", "Spectrum", "Espectro", {"bars", "wave"}, 0, 0, 0, 0, "bars"},
      {K::Enum, "accent_source", "Accent", "Acento", {"album", "theme"}, 0, 0, 0, 0, "album"},
      {K::Color, "ink", "Ink", "Tinta", {}, 0, 0, 0, 0, "on_surface"},
      {K::Number, "opacity", "Opacity", "Opacidad", {}, 0.1, 1, 0.05, 1},
      {K::Number, "fps", "FPS", "FPS", {}, 15, 60, 15, 30, "", true},
      {K::Number, "rotation", "Rotation °", "Rotación °", {}, -180, 180, 0.1, 0},
      {K::Bool, "depth", "Depth", "Profundidad", {}, 0, 1, 1, 1},
      {K::Number, "depth_level", "Depth plane", "Plano de prof.", {}, 0, 100, 1, 0, "", true},
      {K::Number, "layer", "Layer", "Capa", {}, -10, 10, 1, 0, "", true},
  };
  static const std::vector<PropSpec> none;
  if (type == "visualizer") return visualizer;
  if (type == "clock") return clock;
  if (type == "now_playing") return nowPlaying;
  return none;
}

// swatches offered for colour options: palette roles, then a few fixed hues
inline const std::vector<std::string>& colorSwatches() {
  static const std::vector<std::string> s = {"primary", "secondary", "tertiary", "on_surface", "error",
                                             "#e2342a", "#f5c96b", "#7fc8ff", "#b58cff", "#ffffff"};
  return s;
}

}  // namespace undershell
