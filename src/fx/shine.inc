// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Shine", "es": "Brillo que pasa"},
  "stage": "colour",
  "uses": ["time"],
  "params": [
    {"key": "every", "kind": "number", "min": 1, "max": 30, "step": 0.5, "default": 6, "label": {"en": "Every (s)", "es": "Cada (s)"}},
    {"key": "width", "kind": "number", "min": 0.02, "max": 0.6, "step": 0.02, "default": 0.12, "label": {"en": "Width", "es": "Ancho"}},
    {"key": "angle", "kind": "number", "min": -90, "max": 90, "step": 5, "default": 30, "integer": true, "label": {"en": "Angle °", "es": "Ángulo °"}},
    {"key": "tint", "kind": "color", "default": "#ffffff", "label": {"en": "Colour", "es": "Color"}}
  ]
}
*/
// A band of light sweeping across, then a pause; it catches the lighter
// parts of the picture more than the dark ones.
vec3 color(vec3 c, vec2 uv) {
    float a = radians(angle);
    vec2 d = vec2(cos(a), sin(a));
    vec2 q = vec2(uv.x * u_aspect, uv.y);
    float lo = min(0.0, d.x * u_aspect) + min(0.0, d.y);
    float span = abs(d.x) * u_aspect + abs(d.y);
    float ph = fract(u_time / every) * 1.6;  // the sweep takes 1/1.6 of the time
    float pos = lo - width + ph * (span + 2.0 * width);
    float band = 1.0 - smoothstep(0.0, width, abs(dot(q, d) - pos));
    return c + tint * band * (0.25 + 0.75 * smoothstep(0.15, 0.85, luma(c))) * 0.6;
}
)FX"
