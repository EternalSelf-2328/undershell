// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Aurora", "es": "Aurora"},
  "category": "light",
  "stage": "light",
  "uses": ["time", "audio"],
  "audio_when": "music",
  "params": [
    {"key": "height", "kind": "number", "min": 0, "max": 0.8, "step": 0.02, "default": 0.15, "label": {"en": "Height", "es": "Altura"}},
    {"key": "speed", "kind": "number", "min": 0, "max": 3, "step": 0.05, "default": 0.6, "label": {"en": "Speed", "es": "Velocidad"}},
    {"key": "glow", "kind": "number", "min": 0, "max": 2, "step": 0.05, "default": 1, "label": {"en": "Brightness", "es": "Brillo"}},
    {"key": "tint", "kind": "color", "default": "#3dffa8", "label": {"en": "Colour", "es": "Color"}},
    {"key": "tint2", "kind": "color", "default": "#8a5cff", "label": {"en": "Top colour", "es": "Color de arriba"}},
    {"key": "music", "kind": "bool", "default": false, "label": {"en": "With the music", "es": "Con la música"}}
  ]
}
*/
// Northern lights: three curtains, each a line that waves slowly across the
// sky with light falling from it in long, flickering rays, green at the
// foot and turning to the top colour above. Added over the picture; keep it
// to the sky with "Where" (above a line, or the background).
vec4 light(vec2 uv) {
    float t = u_time * speed * 0.07;
    float x = uv.x * u_aspect;
    float lift = music ? 0.75 + 0.6 * u_audio.x + 0.4 * u_audio.w : 1.0;
    vec3 col = vec3(0.0);
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        // the curtain's foot: broad folds that drift, and smaller ones on them
        float fold = fbm(vec2(x * 0.55 + t * (0.8 + fi * 0.3), fi * 4.1));
        float foot = height + 0.12 + fi * 0.07 + 0.22 * (fold - 0.5) + 0.04 * sin(x * 3.1 + t * 5.0 + fi * 2.0);
        float d = foot - uv.y;  // above the foot: positive (the rays rise from it)
        if (d < -0.03) continue;
        // rays: thin and upright, bent a little where the curtain folds
        float rays = 0.35 + 0.65 * noise(vec2(x * 34.0 + fold * 9.0 + t * 2.0, fi * 13.0 + t * 0.3));
        rays *= rays;
        float band = smoothstep(-0.03, 0.005, d) * exp(-max(d, 0.0) * (3.2 - fi * 0.6)) * rays;
        col += mix(tint, tint2, clamp(d * 3.0, 0.0, 1.0)) * band * (0.7 - fi * 0.16);
    }
    return vec4(col * glow * lift, 0.0);
}
)FX"
