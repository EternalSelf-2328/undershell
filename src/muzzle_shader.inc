// SPDX-License-Identifier: GPL-3.0-or-later
// Muzzle flash: the burst at a gun's muzzle, one per kick of the music. The
// muzzle sits at the box's left edge and the shot goes right (turn, pin or
// mesh the widget onto the barrel). A shot is a white-hot core, a plume
// forward and shorter spikes to the sides, each ragged with noise and
// different per shot; it grows in ~30 ms and is gone in ~150. Sparks fly out
// in a fan and die in half a second; smoke stays, swelling and rising, for
// about a second and a half. Realistic: light (white -> yellow -> orange, or
// the theme's colours), tone mapped like the fire. Manga: flat white with a
// black ink outline, for black-and-white artwork. Each shot's shape (angles,
// lengths, sparks, puffs) is drawn at random once, on the CPU, when it is
// fired; here every pixel only measures itself against it.
static const char* kMuzzleFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 res;
uniform float u_age[4];   // seconds since each shot (< 0: no shot)
uniform float u_seed[4];
uniform float u_gain[4];  // 0..1, how hard the kick was
uniform vec4 u_lobe[36];  // per shot, 3 tongues then 6 spikes: cos, sin, length (share of the reach), width
uniform vec4 u_spark[56]; // per shot, 14 sparks: direction, speed, life (< 0: none)
uniform vec4 u_puff[16];  // per shot, 4 smoke puffs: along (share of the reach), across, swell
uniform float u_pump;     // the bass envelope: an ember between shots
uniform float u_manga;    // 0 realistic light, 1 manga ink
uniform float u_theme;    // 1: the theme's colours instead of a flame's
uniform vec3 u_c1;
uniform vec3 u_c2;
uniform float u_length;   // plume reach, fraction of the box ahead of the muzzle
uniform float u_spikes;   // side spikes per shot (0..6)
uniform float u_sparks;   // 0..1
uniform float u_smoke;    // 0..1
uniform float u_opacity;

// a hash without sin (Dave Hoskins'): cheaper on small GPUs
float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
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

// one tongue of the flash, along direction cs: pointed at its tip, ragged
// at its sides
float lobe(vec2 p, vec2 cs, float width, float len, float seed) {
    if (len < 1e-3) return 0.0;
    vec2 q = vec2(cs.x * p.x + cs.y * p.y, -cs.y * p.x + cs.x * p.y);
    float d = q.x / len;
    if (q.x < -0.06 || d > 1.15) return 0.0;
    float taper = width * len * pow(clamp(1.0 - d, 0.0, 1.0), 0.8) * smoothstep(-0.05, 0.22, d);
    // even at its most ragged (x 1.35) it is gone 2.7 widths off its axis:
    // most pixels stop here, before the noise
    if (abs(q.y) > 2.7 * (taper * 1.35 + 0.012)) return 0.0;
    float rag = 0.65 + 0.7 * fbm(vec2(q.x / len * 5.0, q.y * 9.0) + seed * 13.7);
    float w = taper * rag + 0.012;
    float across = exp(-(q.y * q.y) / (w * w));
    float along = (1.0 - smoothstep(0.75, 1.1, d)) * (1.0 - 0.55 * clamp(d, 0.0, 1.0));
    return across * along;
}

float segDist(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

void main() {
    vec2 px = v_uv * res;
    float H = res.y * 0.5;                              // unit: half the box's height
    vec2 O = vec2(min(res.y * 0.12, res.x * 0.1), res.y * 0.5);
    vec2 p = (px - O) / H;                              // +x is forward
    float reach = max(0.2, (res.x - O.x) / H * u_length);
    vec3 hot = u_theme > 0.5 ? mix(u_c1, vec3(1.0), 0.35) : vec3(1.0, 0.86, 0.45);
    vec3 warm = u_theme > 0.5 ? u_c2 : vec3(1.0, 0.42, 0.06);

    float flashSum = 0.0;  // the flash's density (all shots)
    vec3 light = vec3(0.0);
    float sparkMass = 0.0;
    float smoke = 0.0, smokeBody = 0.0, tornAge = 9.0, tornSeed = 0.0;
    for (int i = 0; i < 4; ++i) {
        float a = u_age[i];
        if (a < 0.0) continue;
        float sd = u_seed[i], g = u_gain[i];
        float grow = smoothstep(0.0, 0.03, a);
        float flash = g * exp(-max(a - 0.02, 0.0) / 0.055);
        // manga fills only past 0.75, which a flash this faint cannot reach
        if (flash > (u_manga > 0.5 ? 0.07 : 0.003)) {
            // forward tongues, then the side spikes
            float f = 0.0;
            int n = 3 + int(u_spikes + 0.5);
            for (int l = 0; l < 9; ++l) {
                if (l >= n) break;
                vec4 L = u_lobe[i * 9 + l];
                f += (l < 3 ? 1.0 : 0.9) * lobe(p, L.xy, L.w, reach * L.z * grow, sd + float(l) * 1.7);
            }
            vec2 cq = p * vec2(0.7, 1.2);
            f += 3.0 * exp(-dot(cq, cq) / 0.018);           // the white-hot core
            f *= flash * 1.5;
            flashSum += f;
            light += mix(warm, hot, clamp(f * 0.8, 0.0, 1.0)) * f;
            light += mix(warm, hot, clamp(f - 1.0, 0.0, 1.0)) * max(f - 1.0, 0.0);  // whiter where densest
            light += warm * 0.05 * flash * exp(-length(p) / 0.7);  // light thrown around
        }
        // sparks: a fan of streaks flying out
        if (u_sparks > 0.001 && a < 0.7) {
            float spread = 0.4 + 0.6 * reach;
            for (int j = 0; j < 14; ++j) {
                vec4 S = u_spark[i * 14 + j];
                if (a > S.w) continue;  // burnt out, or never there
                float speed = S.z * spread;
                vec2 head = S.xy * (0.08 + speed * a) + vec2(0.0, 0.25 * a * a);  // a touch of gravity
                float streak = (0.05 + 0.04 * speed) * (1.0 - a / S.w);
                vec2 off = p - head;
                if (dot(off, off) > (streak + 0.04) * (streak + 0.04)) continue;  // nowhere near it
                vec2 tail = head - S.xy * streak;
                float dd = segDist(p, tail, head);
                sparkMass += exp(-(dd * dd) / 0.00012) * (1.0 - a / S.w) * g;
            }
        }
        // smoke: a few puffs that swell, drift ahead and rise
        if (u_smoke > 0.001 && a < 1.6) {
            float body = 0.0;
            for (int k = 0; k < 4; ++k) {
                vec4 P = u_puff[i * 4 + k];
                vec2 c = vec2(P.x * reach + a * 0.3, P.y - a * 0.25);
                float rad = 0.1 + a * P.z;
                vec2 dp = (p - c) / (rad * vec2(1.6, 1.0));  // drawn out along the shot
                body += exp(-dot(dp, dp) * 0.8);
            }
            smokeBody += body * pow(1.0 - a / 1.6, 1.5) * smoothstep(0.0, 0.08, a) * u_smoke * 0.75 * (0.5 + 0.5 * g);
            if (a < tornAge) {
                tornAge = a;
                tornSeed = sd;
            }
        }
    }
    // the smoke torn into wisps by one noise, the youngest shot's (its puffs
    // the thickest): only where it can show - manga draws none under 0.12,
    // the light under about 1/255
    if (smokeBody > (u_manga > 0.5 ? 0.06 : 0.004))
        smoke = smokeBody * smoothstep(0.3, 0.8, fbm(p * 3.2 + tornSeed + vec2(-tornAge * 0.5, tornAge * 0.6)));
    // an ember with the bass between shots (realistic only)
    float ember = u_pump * 0.22 * exp(-dot(p, p) / 0.01) * (1.0 - u_manga);
    light += warm * ember + hot * sparkMass * 1.6;
    smoke = clamp(smoke, 0.0, 0.85);

    vec3 col;
    float alpha;
    if (u_manga > 0.5) {
        // manga: flat white flash with a black ink line of constant width
        // round its edge (found by the density's slope, so it never smears
        // as the flash fades), white sparks, flat grey smoke
        float F = flashSum + sparkMass * 0.6;
        float thr = 0.75;
        float slope = max(fwidth(F), 1e-4);
        float fill = clamp((F - thr) / slope + 0.5, 0.0, 1.0);
        float inkBand = clamp(1.0 - abs(F - thr) / (slope * 1.6), 0.0, 1.0) * step(0.004, flashSum + sparkMass);
        float sm = smoothstep(0.12, 0.2, smoke) * 0.55;
        alpha = max(fill, inkBand);
        col = vec3(fill * (1.0 - inkBand));  // ink is black: colour 0 at its alpha
        col += vec3(0.82) * sm * (1.0 - alpha);
        alpha += sm * (1.0 - alpha);
    } else {
        vec3 c = vec3(1.0) - exp(-light * 1.6);
        float a = max(c.r, max(c.g, c.b));
        vec3 sc = vec3(0.62, 0.62, 0.64) * smoke;  // smoke behind the light
        col = c + sc * (1.0 - a);
        alpha = a + smoke * (1.0 - a);
        float nz = hash(px) - 0.5;
        alpha = clamp(alpha + nz * (1.5 / 255.0) * step(0.004, alpha), 0.0, 1.0);
        col = min(col, vec3(alpha));
    }
    fragColor = vec4(col, alpha) * u_opacity;
}
)";
