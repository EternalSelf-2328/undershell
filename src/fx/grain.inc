// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Film", "es": "Película"},
  "category": "retro",
  "stage": "colour",
  "uses": ["time"],
  "params": [
    {"key": "amount", "kind": "number", "min": 0, "max": 1, "step": 0.05, "default": 0.5, "label": {"en": "Grain", "es": "Grano"}},
    {"key": "flicker", "kind": "number", "min": 0, "max": 1, "step": 0.05, "default": 0.3, "label": {"en": "Flicker", "es": "Parpadeo"}}
  ]
}
*/
// Moving film grain, new 24 times a second, and the exposure flickering a
// little from frame to frame.
vec3 color(vec3 c, vec2 uv) {
    float f = floor(u_time * 24.0);
    float n = hash(screenPx + vec2(f * 37.0, f * 11.0)) - 0.5;
    float fl = 1.0 + (hash(vec2(f, 3.0)) - 0.5) * flicker * 0.12;
    return (c + n * 0.16 * amount) * fl;
}
)FX"
