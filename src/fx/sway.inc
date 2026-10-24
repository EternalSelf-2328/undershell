// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Wind", "es": "Viento"},
  "stage": "warp",
  "uses": ["time"],
  "params": [
    {"key": "speed", "kind": "number", "min": 0, "max": 3, "step": 0.05, "default": 1, "label": {"en": "Speed", "es": "Velocidad"}},
    {"key": "gusts", "kind": "number", "min": 0, "max": 1, "step": 0.05, "default": 0.5, "label": {"en": "Gusts", "es": "Ráfagas"}},
    {"key": "size", "kind": "number", "min": 0.2, "max": 4, "step": 0.1, "default": 1, "label": {"en": "Leaf size", "es": "Tamaño"}}
  ]
}
*/
// Leaves and grass in the wind: a soft sideways sway, faster flutter on
// top, and gusts that sweep across the picture now and then.
vec2 warp(vec2 uv) {
    vec2 q = vec2(uv.x * u_aspect, uv.y) * 8.0 / size;
    float t = u_time * speed;
    float g = 1.0 - gusts + gusts * (0.5 + 0.5 * sin(t * 0.7 - q.x * 0.4) * sin(t * 0.31 + 1.3));
    float n = noise(q + vec2(t * 0.8, 0.0)) - 0.5;
    float m = noise(q * 2.7 + vec2(t * 1.9, 4.0)) - 0.5;
    return uv + pictureOffset(vec2(n + 0.45 * m, (m - n) * 0.3) * 0.006 * size * g);
}
)FX"
