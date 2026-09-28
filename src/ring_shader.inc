// SPDX-License-Identifier: GPL-3.0-or-later
// The halo's "ring" shape: one even circle whose glow spreads outward with
// the music — the same at every angle. The core line swells and brightens
// with the bass envelope; the outer glow (an exponential falloff, wider
// with energy) and a faint inner glow give it depth; each strong beat sends
// a soft shock wave out that widens and fades.
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
uniform float u_bass;       // bass envelope 0..1
uniform float u_energy;     // overall level 0..1
uniform float u_waves[4];   // shock wave ages 0..1, < 0 = none
uniform float u_waveOn;
uniform float u_opacity;
uniform float u_fade;
uniform float u_px;         // one pixel, same units

void main() {
    vec2 px = v_uv * res;
    float d = length(px - res * 0.5) / (min(res.x, res.y) * 0.5);
    float r = u_radius * (1.0 + 0.07 * u_bass);
    float w = u_width * (1.0 + 0.6 * u_bass);
    float t = d - r;                                  // outward distance from the core
    // the core: a crisp antialiased line
    float core = 1.0 - smoothstep(w * 0.5 - u_px, w * 0.5 + u_px, abs(t));
    // outer glow: reaches further and burns brighter with the music
    float reach = u_spread * (0.35 + 0.9 * u_energy + 0.6 * u_bass);
    float outer = t > 0.0 ? exp(-t / max(reach, 1e-3)) : 0.0;
    float inner = t < 0.0 ? exp(t / max(reach * 0.35, 1e-3)) * 0.5 : 0.0;
    float glowA = (outer + inner * 0.8) * (0.36 + 0.45 * u_energy + 0.3 * u_bass);
    // colour: the core near white-hot primary; the glow stays in the primary,
    // only a touch of the secondary at its far edge (a dark secondary must
    // never draw a dark band)
    float far = clamp(abs(t) / max(reach * 3.0, 1e-3), 0.0, 1.0);
    vec3 glowC = mix(u_primary, max(u_secondary, u_primary * 0.8), far * 0.35);
    vec3 coreC = mix(u_primary, vec3(1.0), 0.35 + 0.35 * u_bass);
    vec3 col = glowC * glowA;
    float a = glowA;
    // shock waves: thin, soft, fading as they widen
    if (u_waveOn > 0.5) {
        for (int i = 0; i < 4; ++i) {
            float age = u_waves[i];
            if (age < 0.0) continue;
            float wr = r + age * (1.0 - r) * 0.9;
            float k = exp(-pow((d - wr) / (0.012 + 0.05 * age), 2.0)) * (1.0 - age) * (1.0 - age) * 0.55;
            col += mix(u_primary, max(u_secondary, u_primary * 0.8), age * 0.4) * k;
            a += k;
        }
    }
    col = mix(col, coreC, core);
    a = max(a, core);
    a *= 1.0 - smoothstep(0.72, 1.0, d);             // fades out well before the box edge
    // a whisper of noise: a soft glow in 8 bits otherwise shows steps
    float n = fract(sin(dot(px, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    a = clamp(a + n * (1.5 / 255.0) * step(0.004, a), 0.0, 1.0) * u_opacity * u_fade;
    fragColor = vec4(clamp(col, 0.0, 1.0) * a, a);
}
)";
