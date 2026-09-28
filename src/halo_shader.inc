// SPDX-License-Identifier: GPL-3.0-or-later
// The outer halo of Noctalia's fancy audio visualizer (MIT, (c) 2026
// noctalia-dev, src/render/programs/fancy_audio_visualizer_program.cpp):
// mirrored radial bars and/or a polar wave around an empty centre, graded
// primary -> secondary, with its bloom. The inner rings (ripples, energy
// segments, particles, grid, accent ring) are left out; the base ring is
// optional. Levels come as a uniform array instead of a texture.
static const char* kHaloFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform float u_lv[32];
uniform vec2 res;
uniform vec3 u_primary;
uniform vec3 u_secondary;
uniform float u_turn;        // rotation, radians
uniform float u_sensitivity;
uniform float u_barWidth;
uniform float u_bloom;
uniform float u_inner;       // inner diameter, in units of the half size
uniform float u_waveThick;
uniform float u_shape;       // 0 bars, 1 wave, 2 both
uniform float u_ring;        // the base ring on/off
uniform float u_opacity;
uniform float u_fade;

#define TWOPI 6.28318530718
#define PI 3.14159265359
#define NBARS 32.0
#define MIN_VISUAL_RADIUS 0.35
#define MIN_VISUAL_THICKNESS 0.025
#define MAX_VISUAL_RADIUS 0.95

bool hasBars() { return u_shape < 0.5 || u_shape > 1.5; }
bool hasWave() { return u_shape > 0.5; }

float band(int i) { return u_lv[clamp(i, 0, 31)]; }
float getAudio(float pos) {
    float idx = clamp(pos, 0.0, 1.0) * (NBARS - 1.0);
    int i = int(floor(idx));
    return mix(band(i), band(i + 1), fract(idx));
}
float smoothAudio(float pos) { return getAudio(pos) * u_sensitivity; }
float catmullAudio(float pos) {
    float bp = pos * (NBARS - 1.0);
    int b1 = int(floor(bp));
    float t = fract(bp);
    float p0 = band(max(b1 - 1, 0)) * u_sensitivity, p1 = band(b1) * u_sensitivity;
    float p2 = band(min(b1 + 1, 31)) * u_sensitivity, p3 = band(min(b1 + 2, 31)) * u_sensitivity;
    float t2 = t * t, t3 = t2 * t;
    return max(0.5 * ((2.0 * p1) + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                      (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3), 0.0);
}
float visualBaseRadius(float innerRadius) { return max(MIN_VISUAL_RADIUS, innerRadius + MIN_VISUAL_THICKNESS); }
float mirrored(float theta) {
    float a = mod(theta + PI + u_turn, TWOPI) / TWOPI;
    return a < 0.5 ? a * 2.0 : (1.0 - a) * 2.0;
}

void main() {
    vec2 px = v_uv * res;
    vec2 centered = (px - res * 0.5) / (min(res.x, res.y) * 0.5);
    float theta = atan(centered.y, centered.x);
    float d = length(centered);
    float innerRadius = u_inner / 2.0;
    float baseRadius = visualBaseRadius(innerRadius);
    float bass = smoothAudio(0.05), mid = smoothAudio(0.3), treble = smoothAudio(0.9);
    vec4 color = vec4(0.0);

    if (u_ring > 0.5) {  // the circle the bars stand on
        float outerRad = innerRadius + bass * 0.05;
        if (abs(d - outerRad) < 0.008) {
            vec3 c = u_primary + vec3(0.2, 0.3, 0.4) * clamp(treble * 0.5, 0.0, 0.3);
            color = max(color, vec4(c * (1.0 + bass * 0.4), 0.8));
        }
    }

    if (hasWave()) {
        float sa = catmullAudio(mirrored(theta));
        float waveRadius = min(baseRadius + sa * 0.5, MAX_VISUAL_RADIUS);
        if (d >= innerRadius && d <= waveRadius) {
            float f = (d - innerRadius) / max(waveRadius - innerRadius, 0.001);
            vec3 fc = mix(u_primary * 0.8, u_secondary, f) * (1.0 + bass * 0.3);
            color = max(color, vec4(fc, clamp(mix(0.4, 1.0, f) * u_waveThick, 0.0, 1.0)));
        }
        float edgeT = u_waveThick * 0.025, dist = abs(d - waveRadius);
        if (dist < edgeT) {
            vec3 ec = u_secondary * (1.2 + sa * 0.5);
            if (sa > 0.5) ec += vec3(0.3, 0.4, 0.5) * (sa - 0.5);
            color = max(color, vec4(ec, 1.0 - smoothstep(0.0, edgeT, dist)));
        }
    }

    if (hasBars() && d > innerRadius) {
        float section = TWOPI / (NBARS * 2.0), center = section / 2.0;
        float m = mod(theta + PI + u_turn, section);
        float ym = d * sin(center - m);
        float barW = u_barWidth * 0.015;
        if (abs(ym) < barW) {
            float v = smoothAudio(mirrored(theta));
            v = max(v + sin(theta * 3.0 + mid * 5.0) * clamp(mid * 0.03, 0.0, 0.05), 0.0);
            float barEnd = min(baseRadius + v * 0.5, MAX_VISUAL_RADIUS);
            if (d >= innerRadius && d <= barEnd) {
                float h = (d - innerRadius) / max(barEnd - innerRadius, 0.001);
                vec3 bc = h < 0.5 ? mix(u_primary * 0.6, u_primary, h * 2.0) : mix(u_primary, u_secondary, (h - 0.5) * 2.0);
                bc *= 1.0 + bass * 0.4;
                if (h > 0.85) bc += vec3(0.3, 0.4, 0.5) * clamp(treble * 0.8, 0.0, 0.5);
                color = max(color, vec4(bc, 1.0 - smoothstep(barW * 0.7, barW, abs(ym))));
            }
        }
    }

    // bloom around what is lit, outside it
    if (u_bloom > 0.01 && color.a < 0.01 && d > innerRadius * 0.8) {
        float glowAmount = 0.0;
        vec3 glowColor = vec3(0.0);
        float v = smoothAudio(mirrored(theta));
        if (u_ring > 0.5) {
            float g = exp(-abs(d - (innerRadius + bass * 0.05)) * 8.0 / u_bloom) * (1.0 + bass * 0.5);
            glowColor += u_primary * g;
            glowAmount = max(glowAmount, g);
        }
        if (hasWave()) {
            float sa = catmullAudio(mirrored(theta));
            float wr = min(baseRadius + sa * 0.5, MAX_VISUAL_RADIUS);
            float g = exp(-abs(d - wr) * 8.0 / u_bloom) * sa * 2.5;
            glowColor += mix(u_primary, u_secondary, clamp(sa, 0.0, 1.0)) * g;
            glowAmount = max(glowAmount, g);
        }
        if (hasBars()) {
            float section = TWOPI / (NBARS * 2.0);
            float m = mod(theta + PI + u_turn, section), center = section / 2.0;
            float ad = min(abs(m - center), section - abs(m - center));
            float barEnd = min(baseRadius + v * 0.5, MAX_VISUAL_RADIUS);
            float rd = d < innerRadius ? innerRadius - d : (d > barEnd ? d - barEnd : 0.0);
            float g = exp(-length(vec2(ad * d, rd)) * 15.0 / u_bloom) * v * 2.0;
            float h = clamp((d - innerRadius) / max(barEnd - innerRadius, 0.001), 0.0, 1.0);
            glowColor += mix(u_primary, u_secondary, h) * g;
            glowAmount = max(glowAmount, g);
        }
        float bm = u_bloom * (1.0 + bass * 0.5);
        color = vec4(min(glowColor * bm, vec3(1.5)), min(glowAmount * bm * 0.6, 0.8));
    }

    color *= 1.0 - smoothstep(MAX_VISUAL_RADIUS, 1.0, d);  // fade before the box edge
    // Noctalia lets bright parts run past 1.0; premultiplied output must not
    float a = clamp(color.a, 0.0, 1.0) * u_opacity * u_fade;
    fragColor = vec4(clamp(color.rgb, 0.0, 1.0) * a, a);
}
)";
