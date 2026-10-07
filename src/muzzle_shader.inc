// SPDX-License-Identifier: GPL-3.0-or-later
// Muzzle flash: the burst at a gun's muzzle, one per kick of the music. The
// muzzle sits at the box's left edge and the shot goes right (turn, pin or
// mesh the widget onto the barrel). A shot is a white-hot core, a plume
// forward and shorter spikes to the sides, each ragged with noise and
// different per shot; it grows in ~30 ms and is gone in ~150. Sparks fly out
// in a fan and die in half a second; smoke stays, swelling and rising, for
// about a second and a half. Realistic: light (white -> yellow -> orange, or
// the theme's colours), tone mapped like the fire. Manga: flat white with a
// black ink outline, for black-and-white artwork.
static const char* kMuzzleFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 res;
uniform float u_age[4];   // seconds since each shot (< 0: no shot)
uniform float u_seed[4];
uniform float u_gain[4];  // 0..1, how hard the kick was
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

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float h1(float n) { return fract(sin(n * 91.3458 + 7.13) * 47453.5453); }
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

// one tongue of the flash: pointed at its tip, ragged at its sides
float lobe(vec2 p, float ang, float width, float len, float seed) {
    float c = cos(ang), s = sin(ang);
    vec2 q = vec2(c * p.x + s * p.y, -s * p.x + c * p.y);
    if (q.x < -0.06 || len < 1e-3) return 0.0;
    float d = q.x / len;
    if (d > 1.15) return 0.0;
    float rag = 0.65 + 0.7 * fbm(vec2(q.x / len * 5.0, q.y * 9.0) + seed * 13.7);
    float w = width * len * pow(clamp(1.0 - d, 0.0, 1.0), 0.8) * smoothstep(-0.05, 0.22, d) * rag + 0.012;
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
    float smoke = 0.0;
    for (int i = 0; i < 4; ++i) {
        float a = u_age[i];
        if (a < 0.0) continue;
        float sd = u_seed[i], g = u_gain[i];
        float grow = smoothstep(0.0, 0.03, a);
        float flash = g * exp(-max(a - 0.02, 0.0) / 0.055);
        if (flash > 0.003) {
            // forward: three tongues fanned a little, the middle one longest
            float f = 0.0;
            for (int t = 0; t < 3; ++t) {
                float ft = float(t) - 1.0;
                float ang = ft * mix(0.1, 0.24, h1(sd + 1.0 + float(t))) + (h1(sd + 5.0) - 0.5) * 0.06;
                float lt = reach * (t == 1 ? mix(0.8, 1.0, h1(sd)) : mix(0.45, 0.75, h1(sd + 2.0 + float(t)))) * grow;
                f += lobe(p, ang, t == 1 ? 0.13 : 0.1, lt, sd + float(t) * 1.7);
            }
            // sideways: short, sharp spikes
            int n = int(u_spikes + 0.5);
            for (int k = 0; k < 6; ++k) {
                if (k >= n) break;
                float side = (k % 2 == 0) ? 1.0 : -1.0;
                float ang = side * mix(0.75, 1.45, h1(sd + 3.0 + float(k)));
                float lk = reach * mix(0.14, 0.32, h1(sd + 9.0 + float(k))) * grow;
                f += 0.9 * lobe(p, ang, 0.065, lk, sd + float(k) * 2.3);
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
            for (int j = 0; j < 14; ++j) {
                float fj = float(j);
                if (h1(sd + 20.0 + fj) > u_sparks) continue;
                float life = 0.3 + 0.35 * h1(sd + 30.0 + fj);
                if (a > life) continue;
                float ang = (h1(sd + 40.0 + fj) - 0.5) * 1.3;
                vec2 dir = vec2(cos(ang), sin(ang));
                float speed = mix(1.2, 3.6, h1(sd + 60.0 + fj)) * (0.4 + 0.6 * reach);
                vec2 head = dir * (0.08 + speed * a) + vec2(0.0, 0.25 * a * a);  // a touch of gravity
                vec2 tail = head - dir * (0.05 + 0.04 * speed) * (1.0 - a / life);
                float dd = segDist(p, tail, head);
                sparkMass += exp(-(dd * dd) / 0.00012) * (1.0 - a / life) * g;
            }
        }
        // smoke: a few puffs that swell, drift ahead and rise
        if (u_smoke > 0.001 && a < 1.6) {
            float fade = pow(1.0 - a / 1.6, 1.5) * smoothstep(0.0, 0.08, a);
            // a cloud along the shot's path, torn into wisps by noise
            for (int k = 0; k < 4; ++k) {
                float fk = float(k);
                vec2 c = vec2(mix(0.08, 0.6, (fk + h1(sd + 70.0 + fk)) / 4.0) * reach + a * 0.3,
                              (h1(sd + 80.0 + fk) - 0.5) * 0.18 - a * 0.25);
                float rad = 0.1 + a * mix(0.3, 0.5, h1(sd + 90.0 + fk));
                vec2 dp = (p - c) / (rad * vec2(1.6, 1.0));  // drawn out along the shot
                float body = exp(-dot(dp, dp) * 0.8);
                float wisp = smoothstep(0.3, 0.8, fbm(p * 3.2 + sd + vec2(-a * 0.5, a * 0.6)));
                smoke += body * wisp * fade * u_smoke * 0.75 * (0.5 + 0.5 * g);
            }
        }
    }
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
