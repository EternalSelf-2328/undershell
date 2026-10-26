// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Scroll", "es": "Desplazar"},
  "category": "motion",
  "stage": "warp",
  "uses": ["time"],
  "params": [
    {"key": "speed_x", "kind": "number", "min": -2, "max": 2, "step": 0.01, "default": 0.02, "label": {"en": "Across", "es": "Horizontal"}},
    {"key": "speed_y", "kind": "number", "min": -2, "max": 2, "step": 0.01, "default": 0, "label": {"en": "Down", "es": "Vertical"}}
  ]
}
*/
// The picture slides on and wraps round: drifting clouds, a moving sky.
vec2 warp(vec2 uv) {
    return fract(uv - vec2(speed_x, speed_y) * u_time);
}
)FX"
