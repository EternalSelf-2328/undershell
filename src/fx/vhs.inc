// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "VHS tape", "es": "Cinta VHS"},
  "stage": "colour",
  "uses": ["time"],
  "params": [
    {"key": "snow", "kind": "number", "min": 0, "max": 1, "step": 0.05, "default": 0.5, "label": {"en": "Tape noise", "es": "Ruido"}}
  ]
}
*/
// An old tape: rows that shiver, colour that bleeds sideways, snow, faint
// scanlines and a band of tracking noise rolling down now and then.
vec3 color(vec3 c, vec2 uv) {
    float t = u_time;
    float row = floor(screenPx.y / 3.0);
    float band = fract(t * 0.07) * 1.3 - 0.15;
    float inBand = smoothstep(0.05, 0.0, abs(uv.y - band));
    float jit = (hash(vec2(row, floor(t * 20.0))) - 0.5) * (0.0015 + inBand * 0.02);
    vec2 q = uv + vec2(jit * snow * 2.0, 0.0);
    vec3 s = vec3(wall(q + vec2(0.0025, 0.0)).r, wall(q).g, wall(q - vec2(0.0025, 0.0)).b);
    s = mix(s, vec3(luma(s)), 0.25);
    s += (hash(screenPx + floor(t * 30.0) * 7.0) - 0.5) * 0.12 * snow;
    s += inBand * 0.2 * snow * hash(vec2(floor(screenPx.x * 0.2), floor(t * 30.0)));
    s *= 0.94 + 0.06 * sin(screenPx.y * 3.14159);
    return s;
}
)FX"
