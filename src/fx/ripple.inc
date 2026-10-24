// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Rippling water", "es": "Agua rizada"},
  "stage": "warp",
  "uses": ["time"],
  "params": [
    {"key": "speed", "kind": "number", "min": 0, "max": 3, "step": 0.05, "default": 1, "label": {"en": "Speed", "es": "Velocidad"}},
    {"key": "size", "kind": "number", "min": 0.2, "max": 4, "step": 0.1, "default": 1, "label": {"en": "Ripple size", "es": "Tamaño"}}
  ]
}
*/
// A wind-ruffled surface: two layers of smooth random slopes drifting
// across each other bend the picture under them, like light through water.
vec2 warp(vec2 uv) {
    vec2 q = vec2(uv.x * u_aspect, uv.y) * 60.0 / size;
    float t = u_time * speed;
    vec2 a = flow(q + vec2(t * 0.9, t * 0.35));
    vec2 b = flow(q * 1.7 + vec2(-t * 0.6, t * 1.1) + 17.0);
    return uv + pictureOffset((a + 0.6 * b) * 0.0018 * size);
}
)FX"
