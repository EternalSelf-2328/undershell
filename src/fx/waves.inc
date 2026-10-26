// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Water waves", "es": "Olas"},
  "category": "water",
  "stage": "warp",
  "uses": ["time"],
  "params": [
    {"key": "speed", "kind": "number", "min": 0, "max": 3, "step": 0.05, "default": 1, "label": {"en": "Speed", "es": "Velocidad"}},
    {"key": "size", "kind": "number", "min": 0.2, "max": 4, "step": 0.1, "default": 1, "label": {"en": "Wave size", "es": "Tamaño"}},
    {"key": "direction", "kind": "number", "min": -180, "max": 180, "step": 5, "default": 90, "integer": true,
     "label": {"en": "Direction °", "es": "Dirección °"}}
  ]
}
*/
// Long waves rolling one way: two trains of slightly different length and
// speed, so the crests never line up for long, and a slow bend across them.
vec2 warp(vec2 uv) {
    float a = radians(direction);
    vec2 d = vec2(cos(a), sin(a));
    vec2 n = vec2(-d.y, d.x);
    vec2 q = vec2(uv.x * u_aspect, uv.y) * 40.0 / size;
    float t = u_time * speed;
    float x = dot(q, d), y = dot(q, n);
    float s = sin(x - t * 2.0 + sin(y * 0.35 + t * 0.5) * 0.8) + 0.5 * sin(x * 1.9 + y * 0.6 - t * 3.1);
    return uv + pictureOffset((d * 0.8 + n * 0.3) * s * 0.0022 * size);
}
)FX"
