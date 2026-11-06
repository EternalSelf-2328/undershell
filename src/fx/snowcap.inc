// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Snow on things", "es": "Nieve encima"},
  "category": "light",
  "stage": "colour",
  "uses": ["depth"],
  "params": [
    {"key": "amount", "kind": "number", "min": 0, "max": 1, "step": 0.05, "default": 0.7, "label": {"en": "Amount", "es": "Cantidad"}},
    {"key": "thickness", "kind": "number", "min": 0.2, "max": 3, "step": 0.1, "default": 1, "label": {"en": "Thickness", "es": "Grosor"}},
    {"key": "tint", "kind": "color", "default": "#f4f8ff", "label": {"en": "Colour", "es": "Color"}}
  ]
}
*/
// Snow lying on what stands out in the picture: where something is nearer
// than what is just above it (the top of a head, a branch, a roof against
// the sky), the depth map says it has a top, and snow settles there, thicker
// right at the edge and ragged as snow lies. Needs the wallpaper's depth.
vec3 color(vec3 c, vec2 uv) {
    float d = depth(uv);
    float h = 0.0028 * thickness;
    float cap = 0.0;
    for (int i = 1; i <= 7; i++) {  // close steps, fading with the distance: a soft layer, not stripes
        float above = depth(uv - vec2(0.0, h * float(i)));
        cap = max(cap, smoothstep(0.05, 0.14, d - above) * (1.0 - float(i - 1) / 7.0));
    }
    float n = noise(vec2(uv.x * u_aspect, uv.y) * 110.0);
    cap = smoothstep(0.15, 0.6, cap) * smoothstep(0.15, 0.45, n * 0.6 + amount * 0.6) * amount;
    return mix(c, tint * (0.84 + 0.16 * n), clamp(cap, 0.0, 1.0));
}
)FX"
