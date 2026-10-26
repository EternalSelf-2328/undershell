// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Twinkle", "es": "Destellos"},
  "category": "light",
  "stage": "colour",
  "uses": ["time"],
  "params": [
    {"key": "amount", "kind": "number", "min": 0.2, "max": 3, "step": 0.1, "default": 1, "label": {"en": "Density", "es": "Cantidad"}},
    {"key": "speed", "kind": "number", "min": 0, "max": 3, "step": 0.05, "default": 1, "label": {"en": "Speed", "es": "Velocidad"}},
    {"key": "threshold", "kind": "number", "min": 0, "max": 0.95, "step": 0.05, "default": 0.6, "label": {"en": "From brightness", "es": "Desde el brillo"}},
    {"key": "tint", "kind": "color", "default": "#ffffff", "label": {"en": "Colour", "es": "Color"}}
  ]
}
*/
// Small four-pointed sparkles that come and go on the bright spots of the
// picture (stars, city lights, water in the sun). One cell per sparkle.
vec3 color(vec3 c, vec2 uv) {
    float k = 90.0 * amount;
    vec2 g = vec2(uv.x * u_aspect, uv.y) * k;
    vec2 id = floor(g);
    float h = hash(id);
    vec2 at = id + 0.25 + 0.5 * vec2(h, hash(id + 7.1));
    vec2 f = g - at;
    float tw = 0.5 + 0.5 * sin(u_time * speed * (1.5 + 3.0 * h) + h * 40.0);
    tw *= tw * tw;
    float star = smoothstep(0.3, 0.0, length(f)) * 0.6
               + max(0.0, 1.0 - abs(f.x) * 9.0) * max(0.0, 1.0 - abs(f.y) * 1.6) * 0.5
               + max(0.0, 1.0 - abs(f.y) * 9.0) * max(0.0, 1.0 - abs(f.x) * 1.6) * 0.5;
    vec2 spot = vec2(at.x / k / u_aspect, at.y / k);
    float here = smoothstep(threshold, threshold + 0.15, luma(wallLod(spot, 1.0))) * step(0.55, h);
    return c + tint * star * tw * here;
}
)FX"
