// SPDX-License-Identifier: GPL-3.0-or-later
// The editable options of every widget type: one table that drives the
// editor's inspector (and can document the config). Keys and defaults mirror
// what each widget's fromTable() reads.
#pragma once

#include <string>
#include <vector>

namespace undershell {

struct WidgetConfig;

struct PropSpec {
  // Header, Element and Font rows only appear in the inspector's generated
  // list (clock structures); the config documents the four value kinds
  // Action: a button row (two half buttons when it has options); File: one
  // entry of a list of paths (def = its index), with a button to drop it;
  // Text: words typed in the row (Shift+Enter for a new line)
  enum Kind { Enum, Bool, Number, Color, Header, Element, Font, Action, File, Text } kind = Enum;
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

// Whether an option does anything for a widget as it is configured now (its
// look, face, layout, colour mode…); the inspector shows only those.
bool optionApplies(const WidgetConfig& c, const std::string& key);

inline const std::vector<PropSpec>& schemaFor(const std::string& type) {
  using K = PropSpec::Kind;
  static const std::vector<PropSpec> visualizer = {
      {K::Enum, "style", "Look", "Estilo",
       {"bars", "split", "dots", "segments", "wave", "ribbon", "curtain", "line", "frame", "radial", "orb", "spiral", "halo",
        "vortex", "fire", "muzzle", "electric", "sfx", "speedlines", "rain", "ripples", "neon", "fireworks", "terminal", "tv"}, 0, 0, 0,
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
      {K::Number, "layer", "Layer", "Capa", {}, -999, 999, 1, 0, "", true},
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
      {K::Number, "layer", "Layer", "Capa", {}, -999, 999, 1, 0, "", true},
  };
  static const std::vector<PropSpec> nowPlaying = {
      {K::Enum, "layout", "Layout", "Disposición", {"sheet", "vinyl", "tile", "poster", "strip", "portrait", "text"}, 0, 0, 0, 0, "sheet"},
      {K::Enum, "text_form", "Text form", "Forma del texto", {"stacked", "line"}, 0, 0, 0, 0, "stacked"},
      {K::Enum, "text_align", "Align", "Alinear", {"left", "center", "right"}, 0, 0, 0, 0, "left"},
      {K::Enum, "text_valign", "Vertical", "Vertical", {"top", "center", "bottom"}, 0, 0, 0, 0, "center"},
      {K::Enum, "plate", "Plate", "Fondo", {"cover", "glass", "none"}, 0, 0, 0, 0, "cover"},
      {K::Enum, "cover_shape", "Cover shape", "Forma de portada",
       {"rounded", "cycle", "circle", "cookie12Sided", "cookie9Sided", "cookie7Sided", "cookie6Sided", "cookie4Sided", "flower",
        "clover8Leaf", "clover4Leaf", "softBurst", "burst", "sunny", "verySunny", "puffyDiamond", "pentagon", "pill", "square", "oval"},
       0, 0, 0, 0, "rounded"},
      {K::Bool, "show_lyrics", "Lyrics", "Letra", {}, 0, 1, 1, 1},
      {K::Enum, "lyrics_style", "Lyrics style", "Estilo de letra", {"plain", "focus"}, 0, 0, 0, 0, "plain"},
      {K::Bool, "show_viz", "Spectrum", "Espectro", {}, 0, 1, 1, 1},
      {K::Enum, "viz", "Spectrum look", "Estilo de espectro", {"bars", "wave"}, 0, 0, 0, 0, "bars"},
      {K::Bool, "show_cover", "Sleeve", "Portada", {}, 0, 1, 1, 1},
      {K::Bool, "show_artist", "Artist", "Artista", {}, 0, 1, 1, 1},
      {K::Number, "title_size", "Title size ×", "Tamaño de título ×", {}, 0.4, 2.5, 0.05, 1},
      {K::Font, "title_font", "Title font", "Fuente del título", {}, 0, 0, 0, 0, "Fraunces 144pt"},
      {K::Number, "artist_size", "Artist size ×", "Tamaño artista ×", {}, 0.4, 2.5, 0.05, 1},
      {K::Font, "artist_font", "Artist font", "Fuente del artista", {}, 0, 0, 0, 0, "Space Grotesk"},
      {K::Number, "time_size", "Clock size ×", "Tamaño del reloj ×", {}, 0.4, 2.5, 0.05, 1},
      {K::Font, "time_font", "Clock font", "Fuente del reloj", {}, 0, 0, 0, 0, "JetBrains Mono"},
      {K::Bool, "show_time", "Clock", "Reloj", {}, 0, 1, 1, 1},
      {K::Bool, "show_rail", "Seek rail", "Barra de avance", {}, 0, 1, 1, 1},
      {K::Enum, "rail_style", "Rail look", "Estilo de barra", {"wave", "line", "dots", "bars", "ring"}, 0, 0, 0, 0, "wave"},
      {K::Bool, "show_transport", "Transport", "Controles", {}, 0, 1, 1, 1},
      {K::Bool, "show_open", "Open button", "Botón de abrir", {}, 0, 1, 1, 1},
      {K::Bool, "show_pulse", "Playing pulse", "Pulso al sonar", {}, 0, 1, 1, 1},
      {K::Enum, "accent_source", "Accent", "Acento", {"album", "theme"}, 0, 0, 0, 0, "album"},
      {K::Color, "ink", "Ink", "Tinta", {}, 0, 0, 0, 0, "on_surface"},
      {K::Number, "opacity", "Opacity", "Opacidad", {}, 0.1, 1, 0.05, 1},
      {K::Number, "fps", "FPS", "FPS", {}, 15, 60, 15, 30, "", true},
      {K::Number, "rotation", "Rotation °", "Rotación °", {}, -180, 180, 0.1, 0},
      {K::Bool, "depth", "Depth", "Profundidad", {}, 0, 1, 1, 1},
      {K::Number, "depth_level", "Depth plane", "Plano de prof.", {}, 0, 100, 1, 0, "", true},
      {K::Number, "layer", "Layer", "Capa", {}, -999, 999, 1, 0, "", true},
  };
  static const std::vector<PropSpec> shape = {
      {K::Enum, "shape", "Shape", "Forma",
       {"rect", "ellipse", "triangle", "polygon", "star", "heart", "ring", "line", "pill", "sunny", "verySunny", "cookie4Sided",
        "cookie6Sided", "cookie7Sided", "cookie9Sided", "cookie12Sided", "clover4Leaf", "clover8Leaf", "burst", "softBurst", "flower",
        "puffyDiamond"},
       0, 0, 0, 0, "rect"},
      {K::Number, "sides", "Sides / points", "Lados / puntas", {}, 3, 16, 1, 5, "", true},
      {K::Number, "sharpness", "Sharpness", "Agudeza", {}, 0, 1, 0.05, 0.5},
      {K::Number, "corner", "Rounding", "Redondeo", {}, 0, 1, 0.05, 0.15},
      {K::Number, "thickness", "Thickness", "Grosor", {}, 0.02, 1, 0.02, 0.25},
      {K::Number, "arc", "Arc °", "Arco °", {}, 10, 360, 5, 360, "", true},
      {K::Enum, "fill", "Fill", "Relleno", {"solid", "gradient", "radial", "none"}, 0, 0, 0, 0, "solid"},
      {K::Color, "color", "Colour 1", "Color 1", {}, 0, 0, 0, 0, "primary"},
      {K::Color, "color2", "Colour 2", "Color 2", {}, 0, 0, 0, 0, "tertiary"},
      {K::Number, "angle", "Gradient angle", "Ángulo", {}, 0, 360, 15, 90, "", true},
      {K::Number, "stroke", "Outline px", "Borde px", {}, 0, 48, 1, 0, "", true},
      {K::Color, "stroke_color", "Outline colour", "Color del borde", {}, 0, 0, 0, 0, "on_surface"},
      {K::Number, "glow", "Glow", "Brillo", {}, 0, 1, 0.05, 0},
      {K::Number, "shadow", "Shadow", "Sombra", {}, 0, 1, 0.05, 0},
      {K::Enum, "react", "With the music", "Con la música", {"none", "pulse", "glow"}, 0, 0, 0, 0, "none"},
      {K::Number, "spin", "Spin °/s", "Giro °/s", {}, -120, 120, 5, 0},
      {K::Number, "opacity", "Opacity", "Opacidad", {}, 0.05, 1, 0.05, 1},
      {K::Number, "rotation", "Rotation °", "Rotación °", {}, -180, 180, 0.1, 0},
      {K::Bool, "depth", "Depth", "Profundidad", {}, 0, 1, 1, 1},
      {K::Number, "depth_level", "Depth plane", "Plano de prof.", {}, 0, 100, 1, 0, "", true},
      {K::Number, "layer", "Layer", "Capa", {}, -999, 999, 1, 0, "", true},
  };
  // the defaults are the "plain" style's; another style brings its own (TextWidget::preset)
  static const std::vector<PropSpec> text = {
      {K::Text, "text", "Text", "Texto", {}, 0, 0, 0, 0, "undershell"},
      {K::Enum, "style", "Style", "Estilo",
       {"plain", "neon", "retro", "comic", "outline", "sticker", "chrome", "glitch", "rainbow", "typewriter"}, 0, 0, 0, 0, "plain"},
      {K::Font, "font", "Font", "Fuente", {}, 0, 0, 0, 0, "Inter Display"},
      {K::Number, "weight", "Weight", "Grosor", {}, 100, 900, 100, 800, "", true},
      {K::Bool, "italic", "Italic", "Cursiva", {}, 0, 1, 1, 0},
      {K::Number, "spacing", "Letter spacing", "Espaciado", {}, -0.1, 0.6, 0.01, 0},
      {K::Enum, "align", "Align", "Alineación", {"left", "center", "right"}, 0, 0, 0, 0, "center"},
      {K::Enum, "fill", "Fill", "Relleno", {"solid", "gradient", "rainbow", "none"}, 0, 0, 0, 0, "solid"},
      {K::Color, "color", "Colour 1", "Color 1", {}, 0, 0, 0, 0, "on_surface"},
      {K::Color, "color2", "Colour 2", "Color 2", {}, 0, 0, 0, 0, "primary"},
      {K::Number, "angle", "Gradient angle", "Ángulo", {}, 0, 360, 15, 90, "", true},
      {K::Number, "outline", "Outline", "Borde", {}, 0, 1, 0.05, 0},
      {K::Color, "outline_color", "Outline colour", "Color del borde", {}, 0, 0, 0, 0, "#111111"},
      {K::Number, "shadow", "Shadow", "Sombra", {}, 0, 1, 0.05, 0.3},
      {K::Color, "shadow_color", "Shadow colour", "Color de sombra", {}, 0, 0, 0, 0, "#000000"},
      {K::Number, "glow", "Glow", "Brillo", {}, 0, 1, 0.05, 0},
      {K::Color, "glow_color", "Glow colour", "Color del brillo", {}, 0, 0, 0, 0, "primary"},
      {K::Number, "extrude", "3D depth", "Relieve 3D", {}, 0, 1, 0.05, 0},
      {K::Color, "extrude_color", "3D colour", "Color del 3D", {}, 0, 0, 0, 0, "#1a1020"},
      {K::Number, "extrude_angle", "3D direction °", "Dirección 3D °", {}, 0, 360, 15, 135, "", true},
      {K::Enum, "animation", "Animation", "Animación", {"none", "wave", "bounce", "typewriter", "glitch", "flicker", "shake"}, 0, 0, 0, 0,
       "none"},
      {K::Number, "speed", "Speed", "Velocidad", {}, 0.1, 4, 0.1, 1},
      {K::Enum, "react", "With the music", "Con la música", {"none", "pulse", "letters", "glow"}, 0, 0, 0, 0, "none"},
      {K::Number, "opacity", "Opacity", "Opacidad", {}, 0.05, 1, 0.05, 1},
      {K::Number, "rotation", "Rotation °", "Rotación °", {}, -180, 180, 0.1, 0},
      {K::Bool, "depth", "Depth", "Profundidad", {}, 0, 1, 1, 1},
      {K::Number, "depth_level", "Depth plane", "Plano de prof.", {}, 0, 100, 1, 0, "", true},
      {K::Number, "layer", "Layer", "Capa", {}, -999, 999, 1, 0, "", true},
  };
  // the text widget's look, after how the lines come and go
  static const std::vector<PropSpec> lyrics = [] {
    std::vector<PropSpec> v = {
        {K::Enum, "layout", "Layout", "Disposición", {"lines", "line", "karaoke"}, 0, 0, 0, 0, "lines"},
        {K::Enum, "transition", "Change", "Cambio", {"slide", "fade", "pop", "typewriter"}, 0, 0, 0, 0, "slide"},
        {K::Enum, "fallback", "Without lyrics", "Sin letra", {"title", "note", "hide"}, 0, 0, 0, 0, "title"},
        {K::Number, "offset", "Delay s", "Retraso s", {}, -5, 5, 0.1, 0},
    };
    for (const auto& p : text)
      if (p.key != "text") v.push_back(p);
    return v;
  }();
  static const std::vector<PropSpec> calendar = {
      {K::Enum, "layout", "Layout", "Disposición", {"month", "week", "day"}, 0, 0, 0, 0, "month"},
      {K::Enum, "plate", "Plate", "Fondo", {"glass", "solid", "none"}, 0, 0, 0, 0, "glass"},
      {K::Font, "font", "Font", "Fuente", {}, 0, 0, 0, 0, "Space Grotesk"},
      {K::Color, "accent", "Accent", "Acento", {}, 0, 0, 0, 0, "primary"},
      {K::Color, "ink", "Ink", "Tinta", {}, 0, 0, 0, 0, "on_surface"},
      {K::Enum, "first_day", "Week starts", "Semana empieza", {"monday", "sunday"}, 0, 0, 0, 0, "monday"},
      {K::Bool, "weekends", "Tint weekends", "Resaltar finde", {}, 0, 1, 1, 1},
      {K::Bool, "week_numbers", "Week numbers", "Nº de semana", {}, 0, 1, 1, 0},
      {K::Enum, "language", "Language", "Idioma", {"system", "en", "es"}, 0, 0, 0, 0, "system"},
      {K::Number, "opacity", "Opacity", "Opacidad", {}, 0.05, 1, 0.05, 1},
      {K::Number, "rotation", "Rotation °", "Rotación °", {}, -180, 180, 0.1, 0},
      {K::Bool, "depth", "Depth", "Profundidad", {}, 0, 1, 1, 1},
      {K::Number, "depth_level", "Depth plane", "Plano de prof.", {}, 0, 100, 1, 0, "", true},
      {K::Number, "layer", "Layer", "Capa", {}, -999, 999, 1, 0, "", true},
  };
  static const std::vector<PropSpec> notes = {
      {K::Enum, "plate", "Plate", "Fondo", {"paper", "glass", "none"}, 0, 0, 0, 0, "paper"},
      {K::Color, "paper", "Paper", "Papel", {"#ffe58a", "#ffb3c7", "#b8f0c8", "#a8d8ff", "#e0c8ff", "#ffffff", "#2b2b30"}, 0, 0, 0, 0,
       "#ffe58a"},
      {K::Color, "ink", "Ink", "Tinta", {}, 0, 0, 0, 0, "auto"},
      {K::Color, "accent", "Accent", "Acento", {}, 0, 0, 0, 0, "primary"},
      {K::Font, "font", "Font", "Fuente", {}, 0, 0, 0, 0, "Space Grotesk"},
      {K::Number, "size", "Text size", "Tamaño", {}, 8, 72, 1, 18, "", true},
      {K::Number, "opacity", "Opacity", "Opacidad", {}, 0.05, 1, 0.05, 1},
      {K::Number, "rotation", "Rotation °", "Rotación °", {}, -180, 180, 0.1, 0},
      {K::Bool, "depth", "Depth", "Profundidad", {}, 0, 1, 1, 1},
      {K::Number, "depth_level", "Depth plane", "Plano de prof.", {}, 0, 100, 1, 0, "", true},
      {K::Number, "layer", "Layer", "Capa", {}, -999, 999, 1, 0, "", true},
  };
  static const std::vector<PropSpec> image = {
      {K::Enum, "fit", "Fit", "Ajuste", {"fit", "fill", "stretch"}, 0, 0, 0, 0, "fit"},
      {K::Enum, "cut", "Cut to", "Recorte", {"none", "circle", "cookie12Sided", "cookie9Sided", "cookie7Sided", "cookie6Sided",
       "cookie4Sided", "flower", "clover8Leaf", "clover4Leaf", "softBurst", "burst", "sunny", "verySunny", "puffyDiamond", "pentagon",
       "pill", "oval"}, 0, 0, 0, 0, "none"},
      {K::Number, "corner", "Rounding", "Redondeo", {}, 0, 1, 0.05, 0},
      {K::Number, "outline", "Sticker outline px", "Borde de sticker px", {}, 0, 40, 1, 0, "", true},
      {K::Color, "outline_color", "Outline colour", "Color del borde", {}, 0, 0, 0, 0, "#ffffff"},
      {K::Number, "shadow", "Shadow", "Sombra", {}, 0, 1, 0.05, 0},
      {K::Bool, "mirror", "Mirror", "Espejo", {}, 0, 1, 1, 0},
      {K::Number, "speed", "Speed", "Velocidad", {}, 0, 4, 0.05, 1},
      {K::Enum, "react", "With the music", "Con la música", {"none", "bounce", "pulse"}, 0, 0, 0, 0, "none"},
      {K::Number, "opacity", "Opacity", "Opacidad", {}, 0.05, 1, 0.05, 1},
      {K::Number, "rotation", "Rotation °", "Rotación °", {}, -180, 180, 0.1, 0},
      {K::Bool, "depth", "Depth", "Profundidad", {}, 0, 1, 1, 1},
      {K::Number, "depth_level", "Depth plane", "Plano de prof.", {}, 0, 100, 1, 0, "", true},
      {K::Number, "layer", "Layer", "Capa", {}, -999, 999, 1, 0, "", true},
  };
  static const std::vector<PropSpec> none;
  if (type == "shape") return shape;
  if (type == "image") return image;
  if (type == "text") return text;
  if (type == "lyrics") return lyrics;
  if (type == "calendar") return calendar;
  if (type == "notes") return notes;
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
