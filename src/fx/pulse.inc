// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Beat pulse", "es": "Pulso"},
  "stage": "colour",
  "uses": ["audio"],
  "params": [
    {"key": "source", "kind": "choice", "options": ["bass", "mid", "treble", "beat"], "default": "bass",
     "option_names": {"en": ["Bass", "Mids", "Treble", "Beat"], "es": ["Graves", "Medios", "Agudos", "Golpe"]},
     "label": {"en": "Follows", "es": "Sigue"}},
    {"key": "mode", "kind": "choice", "options": ["brighten", "glow"], "default": "glow",
     "option_names": {"en": ["Brighten", "Glow"], "es": ["Aclarar", "Resplandor"]},
     "label": {"en": "Does", "es": "Hace"}},
    {"key": "tint", "kind": "color", "default": "primary", "label": {"en": "Glow colour", "es": "Color"}}
  ]
}
*/
// The picture breathes with the music: lighter all over, or a coloured
// glow on its bright parts.
vec3 color(vec3 c, vec2 uv) {
    float v = source == 0 ? u_audio.x : source == 1 ? u_audio.y : source == 2 ? u_audio.z : u_audio.w;
    if (mode == 0) return c * (1.0 + v * 0.6);
    return c + tint * v * (0.15 + smoothstep(0.35, 1.0, luma(c))) * 0.5;
}
)FX"
