// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Reflection", "es": "Reflejo"},
  "category": "water",
  "stage": "colour",
  "uses": ["time"],
  "time_when": "water",
  "params": [
    {"key": "left", "min": 0, "max": 1, "step": 0.01, "default": 0, "label": {"en": "Left", "es": "Izquierda"}},
    {"key": "top", "min": 0, "max": 1, "step": 0.01, "default": 0.5, "label": {"en": "Top / mirror line", "es": "Arriba / línea del espejo"}},
    {"key": "width", "min": 0, "max": 1, "step": 0.01, "default": 1, "label": {"en": "Width", "es": "Ancho"}},
    {"key": "height", "min": 0, "max": 1, "step": 0.01, "default": 0.5, "label": {"en": "Height", "es": "Alto"}},
    {"key": "water", "kind": "bool", "default": false, "label": {"en": "Water waves", "es": "Ondas de agua"}},
    {"key": "bend", "min": 0, "max": 2, "step": 0.05, "default": 0.5, "label": {"en": "Wave strength", "es": "Fuerza de las ondas"}},
    {"key": "speed", "min": 0, "max": 3, "step": 0.05, "default": 1, "label": {"en": "Speed", "es": "Velocidad"}},
    {"key": "size", "min": 0.2, "max": 4, "step": 0.1, "default": 1, "label": {"en": "Wave size", "es": "Tamaño de las ondas"}},
    {"key": "stretch", "min": 0.25, "max": 4, "step": 0.05, "default": 1, "label": {"en": "Vertical stretch", "es": "Estiramiento vertical"}},
    {"key": "fade", "min": 0, "max": 1, "step": 0.05, "default": 0, "label": {"en": "Fade towards bottom", "es": "Desvanecer hacia abajo"}},
    {"key": "feather", "min": 0, "max": 0.1, "step": 0.005, "default": 0, "label": {"en": "Soft edges", "es": "Bordes suaves"}}
  ]
}
*/
// Rectangle and reflection axis follow the picture, including its crop/fit.
// Outside the rectangle, no extra texture read. Strength and painted masks
// blend colours, rather than interpolating the reflected coordinates.
vec4 pixel(vec4 c, vec2 uv) {
    vec2 q = uv - vec2(left, top);
    vec2 extent = min(vec2(width, height), vec2(1.0 - left, 1.0 - top));
    if (any(lessThanEqual(extent, vec2(0.0))) || any(lessThan(q, vec2(0.0))) || any(greaterThanEqual(q, extent))) return c;
    float down = q.y / extent.y;
    vec2 src = vec2(uv.x, top - q.y / stretch);
    if (water && bend > 0.0) {
        float t = u_time * speed;
        float phase = q.y * 180.0 / size;
        float wave = sin(phase - t * 2.2) + 0.4 * sin(phase * 1.73 + uv.x * 13.0 + t * 1.4);
        // Calm at the shoreline; longer horizontal ripples farther away.
        src += pictureOffset(vec2(wave, sin(phase * 0.8 - t * 1.7) * 0.2)
                             * (0.002 + 0.006 * down) * bend * smoothstep(0.0, 0.05, down));
    }
    // There is no image above its top: leave the original there, never smear
    // the first row or wrap to the opposite edge of the wallpaper.
    if (any(lessThan(src, vec2(0.0))) || any(greaterThan(src, vec2(1.0)))) return c;
    float coverage = 1.0;
    if (feather > 0.0) {
        vec2 edge = min(q, extent - q) * vec2(u_aspect, 1.0);
        coverage = smoothstep(0.0, feather, min(edge.x, edge.y));
    }
    // Explicit LOD avoids undefined implicit derivatives at rectangle edges
    // (neighbouring fragments can have returned before this texture read).
    return mix(c, wallPixel(src), coverage * (1.0 - fade * down));
}
)FX"
