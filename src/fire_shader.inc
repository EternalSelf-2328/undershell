// SPDX-License-Identifier: GPL-3.0-or-later
// Fire: procedural flames rising from the bottom of the box, torn into
// tongues by domain-warped value noise (three octaves, cheap), shaped by the
// music — the energy sets their height (the halo's damped spring), the
// spectrum shapes it across the width (bass in the middle), the bass pulse
// stokes the core, a kick throws a flare and a burst of sparks. Colour is a
// black-body ramp (deep red -> orange -> yellow -> white) or the theme's two
// colours; tone mapped like the halo.
static const char* kFireFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 res;
uniform float u_lv[32];      // smoothed levels, bass first
uniform vec3 u_c1;           // theme colours (when u_theme)
uniform vec3 u_c2;
uniform float u_theme;
uniform float u_shape;       // 0 bonfire (a central pile), 1 wall (full width)
uniform float u_height;      // flame height at full energy, fraction of the box
uniform float u_turb;        // 0..1
uniform float u_spectrum;    // 0..1, how much the spectrum shapes the height
uniform float u_sparks;      // 0..1
uniform float u_time;        // flame time, integrated on the CPU (faster with energy)
uniform float u_sparkTime;
uniform float u_breath, u_pump, u_hit, u_flare;
uniform float u_opacity;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.5 * vnoise(p);
    p = p * 2.03 + 17.1;
    v += 0.28 * vnoise(p);
    p = p * 2.01 + 31.7;
    v += 0.14 * vnoise(p);
    return v / 0.92;
}
float level(float pos) {  // 0 centre .. 1 sides, mirrored: bass in the middle
    float idx = clamp(pos, 0.0, 1.0) * 31.0;
    int i = int(floor(idx));
    return mix(u_lv[clamp(i, 0, 31)], u_lv[clamp(i + 1, 0, 31)], fract(idx));
}

vec3 blackbody(float t) {  // 0 .. 1.3
    vec3 c = vec3(0.0);
    c = mix(c, vec3(0.55, 0.04, 0.01), smoothstep(0.0, 0.25, t));
    c = mix(c, vec3(1.0, 0.32, 0.03), smoothstep(0.2, 0.5, t));
    c = mix(c, vec3(1.0, 0.72, 0.18), smoothstep(0.45, 0.8, t));
    c = mix(c, vec3(1.0, 0.95, 0.75), smoothstep(0.8, 1.25, t));
    return c;
}

void main() {
    vec2 px = v_uv * res;
    float aspect = res.x / res.y;
    float x = v_uv.x, y = 1.0 - v_uv.y;           // y up from the bottom
    float cx = abs(x - 0.5) * 2.0;                // 0 centre .. 1 sides

    // the flame's height here: energy, the spectrum across the width
    float spec = mix(1.0, 0.5 + 1.0 * level(cx), u_spectrum);
    float h = u_height * (0.33 + 0.38 * u_breath + 0.28 * u_flare) * spec;
    h = max(h, 0.05);
    float top = min(1.0, h * 2.9);  // the flame has faded out by here
    if (y > top * 1.8 && u_sparks <= 0.001) {
        fragColor = vec4(0.0);
        return;
    }

    vec3 light = vec3(0.0);
    float rel = y / h;
    // the silhouette, eroded by the noise more and more as it rises: a
    // campfire is a cone (narrowing with height), a wall a flat front
    float shapeD = u_shape < 0.5 ? length(vec2(cx * 1.2 * (1.0 + rel * 1.1), rel)) : rel * 1.15 + 0.12 * smoothstep(0.88, 1.0, cx);
    // the erosion can push the edge out to about shapeD 2.4: beyond, no flame
    // and no glow worth the noise
    if (y < top && shapeD < 2.6) {
        // noise rising through the flame, warped by a slower one (single
        // octaves: the warp only needs to be smooth)
        vec2 p = vec2(x * aspect * 3.4, y * 2.6 - u_time * 1.6);
        vec2 warp = vec2(vnoise(p * 0.6 + vec2(0.0, -u_time * 0.5)), vnoise(p * 0.6 + vec2(5.2, 1.3)));
        float n = fbm(p + (warp - 0.5) * (0.6 + 1.2 * u_turb));
        // (the erosion is capped and the flame fades with height, or the
        // noise would light stray blobs far above the fire)
        float c = 1.0 - 1.7 * pow(max(0.0, shapeD - n * clamp(rel + 0.25, 0.0, 1.5) * (0.75 + 0.7 * u_turb)), 1.2);
        c *= 1.0 - smoothstep(1.7, 2.9, rel);
        // the colour is the noise inside the shape: textured, never a flat blob
        float c1 = clamp(n * c * (1.25 + 0.3 * u_pump + 0.45 * u_hit), 0.0, 1.2);
        vec3 col = u_theme > 0.5 ? mix(u_c2 * c1, mix(u_c1, vec3(1.0), smoothstep(0.9, 1.25, c1)), smoothstep(0.35, 1.0, c1))
                                 : vec3(1.5 * c1, 1.5 * c1 * c1 * c1, c1 * c1 * c1 * c1 * c1 * c1);
        light += col;
        // a warm glow around the fire
        float glow = exp(-max(shapeD - 0.6, 0.0) / 0.5) * (1.0 - smoothstep(0.0, top, y) * 0.7);
        light += (u_theme > 0.5 ? u_c2 : vec3(1.0, 0.3, 0.04)) * glow * 0.07 * (0.6 + u_pump + u_hit);
    }

    // sparks: one candidate per cell, rising and fading, swaying
    if (u_sparks > 0.001 && y < 0.98) {
        vec2 cellSize = vec2(0.035, 0.06);
        vec2 sp = vec2(x * aspect, y - u_sparkTime * 0.35) / cellSize;
        vec2 cell = floor(sp);
        float rnd = hash(cell);
        float density = u_sparks * (0.035 + 0.25 * u_flare);
        if (rnd < density) {
            vec2 centre = cell + vec2(hash(cell + 3.1), hash(cell + 7.7));
            centre.x += 0.35 * sin(u_sparkTime * 2.0 + rnd * 40.0);
            vec2 dd = (sp - centre) * cellSize / vec2(aspect, 1.0) * res.y;  // pixels
            // born in the fire, dying as they climb above it
            float life = smoothstep(h * 0.3, h * 0.8, y) * (1.0 - clamp((y - h * 0.8) / (h * 2.0 + 0.1), 0.0, 1.0));
            float pileHere = mix(smoothstep(0.6, 0.05, cx), 1.0, u_shape);
            float s = exp(-dot(dd, dd) / 3.0) * life * pileHere;
            vec3 sc = u_theme > 0.5 ? mix(u_c2, mix(u_c1, vec3(1.0), 0.4), rnd) : blackbody(0.6 + 0.35 * rnd);
            light += sc * s * 2.2;
        }
    }

    vec3 c = vec3(1.0) - exp(-light * 1.2);
    float a = max(c.r, max(c.g, c.b));
    float nz = hash(px) - 0.5;
    a = clamp(a + nz * (1.5 / 255.0) * step(0.004, a), 0.0, 1.0);
    c = min(c, vec3(a));
    fragColor = vec4(c, a) * u_opacity;
}
)";
