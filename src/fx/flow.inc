// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Flowing water", "es": "Corriente"},
  "category": "water",
  "stage": "warp",
  "uses": ["time"],
  "params": [
    {"key": "speed", "kind": "number", "min": 0, "max": 3, "step": 0.05, "default": 1, "label": {"en": "Speed", "es": "Velocidad"}},
    {"key": "size", "kind": "number", "min": 0.2, "max": 4, "step": 0.1, "default": 1, "label": {"en": "Streak size", "es": "Tamaño"}},
    {"key": "direction", "kind": "number", "min": -180, "max": 180, "step": 5, "default": 90, "integer": true,
     "label": {"en": "Direction °", "es": "Dirección °"}}
  ]
}
*/
// Water running one way (a river, a waterfall): a random bend stretched
// along the stream and sliding down it.
vec2 warp(vec2 uv) {
    float a = radians(direction);
    vec2 d = vec2(cos(a), sin(a));
    vec2 n = vec2(-d.y, d.x);
    vec2 q = vec2(uv.x * u_aspect, uv.y) * 25.0 / size;
    float t = u_time * speed * 1.5;
    vec2 s = d * dot(q, d) * 0.35 + n * dot(q, n);
    vec2 o = flow(s - d * t) + 0.5 * flow(s * 2.1 - d * t * 1.7 + 5.0);
    return uv + pictureOffset(o * 0.0025 * size);
}
)FX"
