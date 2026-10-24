// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Lens fringe", "es": "Aberración"},
  "stage": "colour",
  "uses": ["audio"],
  "audio_when": "beat",
  "params": [
    {"key": "beat", "kind": "bool", "default": false, "label": {"en": "On the beat", "es": "Al golpe"}}
  ]
}
*/
// A cheap lens: red and blue land a little apart, more towards the edges
// (or only when the beat hits).
vec3 color(vec3 c, vec2 uv) {
    vec2 r = uv - 0.5;
    float k = beat ? 0.3 + 2.5 * u_audio.w : 1.0;
    vec2 o = r * length(r) * 0.03 * k;
    return vec3(wall(uv + o).r, c.g, wall(uv - o).b);
}
)FX"
