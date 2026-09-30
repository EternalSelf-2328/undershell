// SPDX-License-Identifier: GPL-3.0-or-later
// The vortex: the halo's ring as an event horizon, with arms of gaseous
// light wound in a logarithmic spiral around it, streaming inward like water
// down a drain; the centre stays empty. Cheap on purpose (a few sines, no
// noise textures) and it stops early outside its reach: it may cover the
// whole screen on a small GPU. Same light model as the halo: summed glow,
// tone mapped. Music (from the CPU): the energy speeds the spin (on a
// spring), the bass pulse lights the arms, a kick flashes and gives a spin
// burst that brakes by itself.
static const char* kVortexFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 res;
uniform vec3 u_primary;
uniform vec3 u_secondary;
uniform float u_radius;     // the horizon ring, in units of the half size
uniform float u_width;      // its core line width
uniform float u_reach;      // how far the arms reach, as a factor of the radius
uniform float u_arms;       // number of arms
uniform float u_twist;      // how tightly they wind
uniform float u_turb;       // 0..1, how gaseous the streaks are
uniform float u_phase;      // rotation (radians), integrated on the CPU
uniform float u_flow;       // inward streaming, integrated on the CPU
uniform float u_dir;        // 1 counter-clockwise, -1 clockwise
uniform float u_inside;     // 1: the swirl lives inside the ring (a tunnel)
uniform float u_flowSign;   // +1 toward the ring's inside, -1 away from it
uniform float u_intensity;
uniform float u_ring;       // 0..1: the horizon ring's light (0 = arms only)
uniform float u_breath;
uniform float u_pump;
uniform float u_hit;
uniform float u_tone;
uniform float u_waves[4];
uniform float u_waveGain[4];
uniform float u_opacity;
uniform float u_px;

float g(float x, float s) { return exp(-(x * x) / (s * s)); }

void main() {
    vec2 px = v_uv * res;
    vec2 q = (px - res * 0.5) / (min(res.x, res.y) * 0.5);
    float d = length(q);
    float r = u_radius * (1.0 + 0.04 * u_breath + 0.012 * u_pump);
    float outer = r * u_reach;
    if (d > outer * 1.08) {  // nothing out here: stop early
        fragColor = vec4(0.0);
        return;
    }
    float theta = atan(q.y, q.x) * u_dir;
    float lr = log(max(d, 1e-4) / r);          // 0 on the horizon, > 0 outside
    float span = log(u_reach);                 // lr at the arms' end
    float t = d - r;

    // --- the horizon: the halo's ring ---
    float w = u_width * (1.0 + 0.35 * u_pump + 0.25 * u_hit);
    float core = 1.0 - smoothstep(w * 0.5 - u_px, w * 0.5 + u_px, abs(t));
    float side = t > 0.0 ? 1.0 : 0.45;
    float ringGlow = g(t, w * 1.6 + u_px) * 0.9 + g(t, r * 0.08 * side) * 0.5 + g(t, r * 0.25 * side) * 0.22;

    // --- the arms: a logarithmic spiral ---
    // x: 0 on the horizon, growing away from it on the arms' side (outside
    // the ring, or inside it for the tunnel)
    float x = u_inside > 0.5 ? -lr : lr;
    float arms = 0.0;
    if (x > -0.05) {
        float s = u_arms * (theta + u_twist * lr - u_phase);
        float band = 0.5 + 0.5 * cos(s);
        band = pow(band, 3.0 - 1.8 * u_turb);                   // thin filaments .. broad gas
        band = band * 0.85 + 0.15;                             // a haze between the arms
        // gaseous streaks along the spiral; +flow moves them toward x = 0
        float f = u_flow * u_flowSign * (u_inside > 0.5 ? -1.0 : 1.0);
        // whole multiples of the angle only, so the pattern closes on itself
        // all the way round (no seam)
        float n = 0.55 * sin(s + x * 9.0 + f) + 0.3 * sin(2.0 * s - x * 15.0 + f * 1.6 + 1.3) +
                  0.15 * sin(theta * 11.0 + x * 23.0 + f * 2.2 + 4.0);
        float streak = clamp(0.62 + u_turb * 0.45 * n, 0.0, 1.4);
        // brightest by the horizon, thinning out toward the reach (outside)
        // or toward the empty centre (inside)
        float along = clamp(x / span, 0.0, 1.0);
        float fall = exp(-along * 2.2) * smoothstep(1.0, 0.6, along) * smoothstep(-0.05, 0.08, x);
        arms = band * streak * fall * 2.2;
    }

    // --- the other side of the horizon: dark, a faint glow ---
    float innerGlow = (u_inside < 0.5 && d < r) ? exp((d - r) / (r * 0.12)) * 0.35 : 0.0;

    float level = 0.45 + 0.2 * u_breath + 0.75 * u_pump + 0.8 * u_hit;
    vec3 second = max(u_secondary, u_primary * 0.6);
    float far = clamp(x / span, 0.0, 1.0);  // toward the arms' far end, on either side
    vec3 armC = mix(u_primary, second, clamp(far * 1.3 + u_tone * 0.3, 0.0, 1.0));
    vec3 hot = mix(u_primary, vec3(1.0), 0.5);
    vec3 light = u_primary * (ringGlow * u_intensity * level + innerGlow * level * 0.6) * u_ring +
                 armC * arms * u_intensity * (0.35 + 0.65 * level) + hot * core * (1.2 + 1.2 * u_hit) * u_ring;

    // shock waves rolling out along the arms' reach
    for (int i = 0; i < 4; ++i) {
        float age = u_waves[i];
        if (age < 0.0) continue;
        float ease = 1.0 - (1.0 - age) * (1.0 - age);
        float wr = u_inside > 0.5 ? r - (r - r / u_reach) * 0.8 * ease : r + (outer - r) * 0.8 * ease;
        float life = 1.0 - age;
        light += armC * g(d - wr, (0.03 + 0.08 * age) * r) * life * life * life * 0.35 * u_waveGain[i];
    }

    light *= 1.0 - smoothstep(outer * 0.85, outer * 1.08, d);
    // and never cut by the widget's edges: fade within the last stretch of the box
    vec2 edge = min(px, res - px) / (min(res.x, res.y) * 0.5);
    light *= smoothstep(0.0, 0.25, min(edge.x, edge.y));
    vec3 c = vec3(1.0) - exp(-light * 1.4);
    float a = max(c.r, max(c.g, c.b));
    float nz = fract(sin(dot(px, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    a = clamp(a + nz * (1.5 / 255.0) * step(0.004, a), 0.0, 1.0);
    c = min(c, vec3(a));
    fragColor = vec4(c, a) * u_opacity;
}
)";
