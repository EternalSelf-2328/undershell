// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Light rays", "es": "Rayos de luz"},
  "category": "light",
  "stage": "light",
  "uses": ["time"],
  "params": [
    {"key": "x", "kind": "number", "min": 0, "max": 1, "step": 0.01, "default": 0.5, "label": {"en": "Light across", "es": "Luz: horizontal"}},
    {"key": "y", "kind": "number", "min": -0.5, "max": 1, "step": 0.01, "default": 0.1, "label": {"en": "Light down", "es": "Luz: vertical"}},
    {"key": "reach", "kind": "number", "min": 0.1, "max": 1, "step": 0.05, "default": 0.6, "label": {"en": "Length", "es": "Largo"}},
    {"key": "threshold", "kind": "number", "min": 0, "max": 0.95, "step": 0.05, "default": 0.55, "label": {"en": "From brightness", "es": "Desde el brillo"}},
    {"key": "flicker", "kind": "number", "min": 0, "max": 2, "step": 0.05, "default": 0.5, "label": {"en": "Shimmer", "es": "Titileo"}},
    {"key": "tint", "kind": "color", "default": "#fff1d6", "label": {"en": "Colour", "es": "Color"}}
  ]
}
*/
// Light pouring from a point: each pixel gathers the bright parts of the
// picture on the way to the light (blurred, from a smaller mipmap), fading
// with distance; a random start per pixel turns the steps into fine grain
// instead of bands. The rays breathe with a slow noise around the light.
vec4 light(vec2 uv) {
    vec2 src = vec2(x, y);
    int n = 12 + 8 * u_quality;
    vec2 stepv = (uv - src) * reach / float(n);
    vec2 q = uv - stepv * hash(screenPx);
    vec3 acc = vec3(0.0);
    float w = 1.0, wsum = 0.0;
    for (int i = 0; i < n; i++) {
        q -= stepv;
        vec3 s = wallLod(q, 2.0);
        acc += s * max(luma(s) - threshold, 0.0) * w;
        wsum += w;
        w *= 0.93;
    }
    acc /= wsum * max(1.0 - threshold, 0.05);
    vec2 r = uv - src;
    r.x *= u_aspect;
    float shimmer = 1.0 - flicker * 0.3 + flicker * 0.3 * noise(normalize(r + 1e-5) * 5.0 + u_time * 0.4);
    return vec4(acc * tint * shimmer, 0.0);
}
)FX"
