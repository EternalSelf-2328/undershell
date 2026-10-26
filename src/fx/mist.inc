// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Drifting mist", "es": "Niebla"},
  "category": "light",
  "stage": "light",
  "uses": ["time"],
  "params": [
    {"key": "look", "kind": "choice", "options": ["fog", "shadows"], "default": "fog",
     "option_names": {"en": ["Fog", "Cloud shadows"], "es": ["Niebla", "Sombra de nubes"]},
     "label": {"en": "Kind", "es": "Tipo"}},
    {"key": "speed", "kind": "number", "min": 0, "max": 3, "step": 0.05, "default": 0.5, "label": {"en": "Speed", "es": "Velocidad"}},
    {"key": "size", "kind": "number", "min": 0.2, "max": 4, "step": 0.1, "default": 1, "label": {"en": "Size", "es": "Tamaño"}},
    {"key": "density", "kind": "number", "min": 0, "max": 1, "step": 0.05, "default": 0.5, "label": {"en": "Density", "es": "Densidad"}},
    {"key": "tint", "kind": "color", "default": "#dfe6ee", "label": {"en": "Colour", "es": "Color"}}
  ]
}
*/
// Soft fog, or the shadows of clouds, drifting slowly across the picture:
// layered noise bent by more noise, so it rolls instead of sliding.
vec4 light(vec2 uv) {
    vec2 q = vec2(uv.x * u_aspect, uv.y) * 3.0 / size;
    float t = u_time * speed * 0.06;
    float f = fbm(q + vec2(t, t * 0.3) + 0.35 * fbm(q * 1.5 - vec2(t * 0.7, 0.0)));
    float cover = clamp(smoothstep(0.62 - density * 0.4, 0.95, f) * (0.4 + density), 0.0, 1.0);
    if (look == 1) return vec4(0.0, 0.0, 0.0, cover * 0.5);
    return vec4(tint * cover * 0.75, cover * 0.75);
}
)FX"
