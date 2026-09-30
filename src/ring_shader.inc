// SPDX-License-Identifier: GPL-3.0-or-later
// The halo: one perfectly round ring of light. The glow is built like real
// bloom — a tight, a medium and a wide falloff summed as light — then tone
// mapped, so the core turns white-hot on its own instead of clipping. Its
// size breathes on a damped spring driven by the music's energy (never the
// raw bass, which jitters); a kick, found by spectral flux, flashes the
// light and sends a soft shock wave out. The tone drifts between the two
// colours with how bright the music sounds; an optional aurora varies the
// glow slowly along the ring without bending it.
static const char* kRingFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 res;
uniform vec3 u_primary;
uniform vec3 u_secondary;
uniform float u_radius;     // ring radius, in units of the half size
uniform float u_width;      // core line width, same units
uniform float u_spread;     // glow reach
uniform float u_intensity;  // glow strength (the "bloom" option)
uniform float u_breath;     // spring-smoothed energy 0..1
uniform float u_hit;        // kick flash 0..1, decaying
uniform float u_tone;       // 0 dark music .. 1 bright music
uniform float u_aurora;     // 0..1
uniform float u_time;
uniform float u_waves[4];   // shock wave ages 0..1, < 0 = none
uniform float u_waveGain[4];
uniform float u_opacity;
uniform float u_px;         // one pixel, same units

float g(float x, float s) { return exp(-(x * x) / (s * s)); }

void main() {
    vec2 px = v_uv * res;
    vec2 q = (px - res * 0.5) / (min(res.x, res.y) * 0.5);
    float d = length(q);
    float theta = atan(q.y, q.x);

    float r = u_radius * (1.0 + 0.035 * u_breath);
    float w = u_width * (1.0 + 0.25 * u_hit);
    float t = d - r;
    float reach = u_spread * (0.7 + 0.45 * u_breath + 0.35 * u_hit);

    // the aurora: a slow, smooth brightness drift along the ring (the shape stays round)
    float aur = 1.0;
    if (u_aurora > 0.001) {
        float n = sin(theta * 2.0 + u_time * 0.21) * 0.5 + sin(theta * 3.0 - u_time * 0.17 + 1.7) * 0.3 +
                  sin(theta * 5.0 + u_time * 0.11 + 4.1) * 0.2;
        aur = 1.0 + u_aurora * 0.55 * n;
    }

    // light: a crisp core, then three bloom layers; the outside reaches
    // further than the inside
    float core = 1.0 - smoothstep(w * 0.5 - u_px, w * 0.5 + u_px, abs(t));
    float side = t > 0.0 ? 1.0 : 0.45;
    float bloom = g(t, w * 1.6 + u_px) * 0.9 + g(t, reach * 0.45 * side) * 0.55 + g(t, reach * 1.6 * side) * 0.28;
    float level = 0.55 + 0.35 * u_breath + 0.9 * u_hit;  // how bright the light burns
    float glowI = bloom * u_intensity * level * aur;

    // colour: the tone drifts toward the secondary with bright music; far
    // glow leans a little further; a dark secondary is lifted so it never
    // draws a dark band
    vec3 second = max(u_secondary, u_primary * 0.75);
    float far = clamp(abs(t) / max(reach * 2.0, 1e-3), 0.0, 1.0);
    vec3 glowC = mix(u_primary, second, clamp(u_tone * 0.6 + far * 0.3, 0.0, 1.0));
    vec3 light = glowC * glowI + mix(u_primary, vec3(1.0), 0.5) * core * (1.2 + 1.2 * u_hit);

    // shock waves: thin, soft, fading as they widen
    for (int i = 0; i < 4; ++i) {
        float age = u_waves[i];
        if (age < 0.0) continue;
        float wr = r + (1.0 - r) * 0.85 * (1.0 - (1.0 - age) * (1.0 - age));  // eases out
        // wide and soft: a breath of light rolling out, not a drawn circle
        float life = 1.0 - age;
        float k = g(d - wr, 0.03 + 0.08 * age) * life * life * life * 0.32 * u_waveGain[i];
        light += glowC * k;
    }

    // tone map (filmic-ish): bright light rolls to white, nothing clips
    light *= 1.0 - smoothstep(0.7, 1.0, d);          // fades out well before the box edge
    vec3 c = vec3(1.0) - exp(-light * 1.4);
    float a = max(c.r, max(c.g, c.b));
    // a whisper of noise: soft glow in 8 bits otherwise shows steps
    float n = fract(sin(dot(px, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    a = clamp(a + n * (1.5 / 255.0) * step(0.004, a), 0.0, 1.0);
    c = min(c, vec3(a));                               // valid premultiplied colour
    fragColor = vec4(c, a) * u_opacity;
}
)";
