#version 300 es
// Ryoku's desktop spectrum (ryoku/ui/shaders/spectrum.frag, GPL-3.0), ported
// from Qt's GLSL 440 + qsb to GLSL ES 3.00 for undershell. The looks are
// unchanged; levels/peaks/ramp now arrive as uniform arrays instead of packed
// mat4 blocks, and the frame look exits early away from the screen edge.
precision highp float;
precision highp int;

in vec2 v_uv;
out vec4 fragColor;

uniform float u_opacity;
uniform float u_lv[128];
uniform float u_pk[128];
uniform vec4 c0; uniform vec4 c1; uniform vec4 c2; uniform vec4 c3;
uniform vec4 c4; uniform vec4 c5; uniform vec4 c6; uniform vec4 c7;
uniform vec2 res;
uniform vec2 origin;
uniform float style;     // 0 bars 1 split 2 dots 3 segments 4 wave 5 ribbon
                         // 6 curtain 7 line 8 frame 9 radial 10 orb 11 spiral
uniform float posMode;   // 0 bottom 1 top 2 center 3 left 4 right
uniform float bands;
uniform float maxLen;
uniform float minLen;
uniform float thickness;
uniform float shapeW;
uniform float capR;
uniform float segN;
uniform float segGap;
uniform float gapPx;
uniform float glowAmt;
uniform float glowPx;
uniform float reflectPx;
uniform float pad;
uniform float peakOn;
uniform float r0;
uniform float rMax;
uniform float spinRad;
uniform float energy;
uniform float fade;
uniform float aa;

const float TAU = 6.28318530718;
const float PI = 3.14159265359;

float lvAt(int i) { return u_lv[clamp(i, 0, 127)]; }
float pkAt(int i) { return u_pk[clamp(i, 0, 127)]; }

int bandAt(float t) {
    return int(clamp(floor(t * bands), 0.0, bands - 1.0));
}

// Catmull-Rom, so the smooth looks read as one curve rather than a fan of steps.
float lvSmooth(float t) {
    float x = clamp(t, 0.0, 1.0) * (bands - 1.0);
    float i1 = floor(x);
    float f = x - i1;
    float p0 = lvAt(int(max(i1 - 1.0, 0.0)));
    float p1 = lvAt(int(i1));
    float p2 = lvAt(int(min(i1 + 1.0, bands - 1.0)));
    float p3 = lvAt(int(min(i1 + 2.0, bands - 1.0)));
    float f2 = f * f;
    float f3 = f2 * f;
    return max(0.0, 0.5 * ((2.0 * p1) + (-p0 + p2) * f
        + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * f2
        + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * f3));
}

vec3 rampAt(float t) {
    float x = clamp(t, 0.0, 0.99999) * 7.0;
    float i = floor(x);
    float f = x - i;
    vec3 a = i < 0.5 ? c0.rgb : (i < 1.5 ? c1.rgb : (i < 2.5 ? c2.rgb : (i < 3.5 ? c3.rgb
           : (i < 4.5 ? c4.rgb : (i < 5.5 ? c5.rgb : (i < 6.5 ? c6.rgb : c7.rgb))))));
    vec3 b = i < 0.5 ? c1.rgb : (i < 1.5 ? c2.rgb : (i < 2.5 ? c3.rgb : (i < 3.5 ? c4.rgb
           : (i < 4.5 ? c5.rgb : (i < 5.5 ? c6.rgb : c7.rgb)))));
    return mix(a, b, f);
}

float roundBox(vec2 p, vec2 b, float r) {
    r = min(r, min(b.x, b.y));
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float segDist(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-4), 0.0, 1.0);
    return length(pa - ba * h);
}

// The scope keeps its trace in the level slots, centred on 0.5.
float scopeAt(float t) {
    return (lvSmooth(t) - 0.5) * 2.0;
}

float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 px = v_uv * res;
    int st = int(style + 0.5);
    if (st == 8) {
        float edge = min(min(px.x, res.x - px.x), min(px.y, res.y - px.y));
        if (edge > maxLen + glowPx * 3.5 + 2.0) {
            fragColor = vec4(0.0);
            return;
        }
    }
    int pm = int(posMode + 0.5);

    float along;
    float across;
    float axisLen;
    // The pass is the look's box plus `pad` on every side for the bloom to fall
    // off in, so the geometry works in box-local pixels.
    vec2 cpx = px - vec2(pad);
    float cw = max(1.0, res.x - 2.0 * pad);
    float ch = max(1.0, res.y - 2.0 * pad);
    if (pm == 3) {
        along = cpx.y / ch;
        across = cpx.x;
        axisLen = ch;
    } else if (pm == 4) {
        along = cpx.y / ch;
        across = cw - cpx.x;
        axisLen = ch;
    } else if (pm == 1) {
        along = cpx.x / cw;
        across = cpx.y;
        axisLen = cw;
    } else {
        along = cpx.x / cw;
        across = ch - reflectPx - cpx.y;
        axisLen = cw;
    }

    bool polar = st >= 9;
    bool frame = st == 8;
    bool centred = (pm == 2 && st != 6) || st == 1;
    float signedAcross = polar ? 0.0
        : ((pm == 3 || pm == 4) ? cpx.x - cw * 0.5 : ch * 0.5 - cpx.y);
    if (pm == 4) signedAcross = -signedAcross;
    if (centred) across = abs(signedAcross);

    float mirrorFade = 1.0;
    // below the root: only the reflection, for looks that grow from it (the
    // line floats in its box, so its glow falls off downwards as well)
    if (!polar && !frame && !centred && st != 7 && across < 0.0) {
        if (reflectPx <= 0.0) {
            fragColor = vec4(0.0);
            return;
        }
        across = -across;
        mirrorFade = 0.42 * max(0.0, 1.0 - across / reflectPx);
    }

    float slot = axisLen / max(bands, 1.0);
    float barW = max(1.5, slot * thickness);
    float alongPx = along * axisLen;

    float sd = 1e9;
    float extraA = 0.0;   // accents: crests, stems, caps, rings
    float tRamp = along;
    float lift = 0.0;     // 0 at the root of the shape, 1 at its tip
    float hot = 0.0;      // how hard this band is hitting, for the tip highlight
    float fillA = 1.0;    // how solid the body reads; a rimmed look wants less

    if (st == 0 || st == 1) {
        int i = bandAt(along);
        float lvv = lvAt(i);
        float len = max(minLen, maxLen * lvv);
        float ci = (float(i) + 0.5) * slot;
        tRamp = float(i) / max(bands - 1.0, 1.0);
        hot = lvv;
        if (st == 1) {
            float arm = max(minLen, len * 0.5);
            float base = gapPx * 0.5;
            sd = roundBox(vec2(alongPx - ci, across - (base + arm * 0.5)),
                          vec2(barW * 0.5, arm * 0.5), capR);
            lift = clamp((across - base) / max(arm, 1.0), 0.0, 1.0);
        } else {
            sd = roundBox(vec2(alongPx - ci, across - len * 0.5),
                          vec2(barW * 0.5, len * 0.5), capR);
            lift = clamp(across / max(len, 1.0), 0.0, 1.0);
        }
        if (peakOn > 0.5) {
            float ph = maxLen * pkAt(i);
            float capH = max(2.0, min(capR * 1.1, barW * 0.34));
            float sdp = roundBox(vec2(alongPx - ci, across - (ph + capH * 1.6)),
                                 vec2(barW * 0.46, capH * 0.5), capH * 0.5);
            extraA += (1.0 - smoothstep(-aa, aa, sdp)) * 0.85;
        }
    } else if (st == 2) {
        // a disc on the band's tip over a hairline stem, so it still reads as a
        // spectrum and not as scattered pills.
        int i = bandAt(along);
        float lvv = lvAt(i);
        float len = max(minLen, maxLen * lvv);
        float ci = (float(i) + 0.5) * slot;
        float rad = max(1.2, barW * (0.30 + 0.28 * lvv));
        tRamp = float(i) / max(bands - 1.0, 1.0);
        hot = lvv;
        sd = length(vec2(alongPx - ci, across - len)) - rad;
        lift = 1.0;
        float stem = roundBox(vec2(alongPx - ci, across - len * 0.5),
                              vec2(max(0.6, barW * 0.07), len * 0.5), 0.0);
        extraA += (1.0 - smoothstep(-aa, aa, stem)) * 0.16;
    } else if (st == 3) {
        int i = bandAt(along);
        float lvv = lvAt(i);
        float len = max(minLen, maxLen * lvv);
        float ci = (float(i) + 0.5) * slot;
        float pitch = maxLen / max(segN, 1.0);
        float lit = ceil(len / max(pitch, 1.0));
        float cell = floor(across / max(pitch, 1.0));
        tRamp = float(i) / max(bands - 1.0, 1.0);
        if (cell >= 0.0 && cell < lit) {
            float ch = max(1.5, pitch - segGap);
            sd = roundBox(vec2(alongPx - ci, across - (cell + 0.5) * pitch),
                          vec2(barW * 0.5, ch * 0.5), min(capR, ch * 0.4));
            lift = lit > 1.0 ? cell / (lit - 1.0) : 1.0;
            // the topmost cell is the leading edge of the meter and runs hot.
            hot = lvv * (cell >= lit - 1.0 ? 1.0 : 0.25);
        }
        if (peakOn > 0.5) {
            float ph = maxLen * pkAt(i);
            float capH = max(2.0, pitch * 0.20);
            float sdp = roundBox(vec2(alongPx - ci, across - (ph + capH * 1.5)),
                                 vec2(barW * 0.46, capH * 0.5), capH * 0.5);
            extraA += (1.0 - smoothstep(-aa, aa, sdp)) * 0.85;
        }
    } else if (st == 4 || st == 6) {
        // wave fills up to the curve; curtain hangs from the baseline down to
        // it, so it reads as light spilling out from under the bar above.
        float lvv = lvSmooth(along);
        float h = max(minLen, maxLen * lvv);
        if (centred) h = max(minLen, h * 0.5);
        sd = across - h;
        hot = lvv;
        lift = st == 4 ? clamp(across / max(h, 1.0), 0.0, 1.0)
                       : 1.0 - clamp(across / max(h, 1.0), 0.0, 1.0);
        // a lit crest on the moving edge, and for the curtain a hairline sealing
        // it to the edge it hangs from.
        extraA += exp(-abs(sd) / max(1.5, aa * 2.5)) * 0.55;
        if (st == 6)
            extraA += (1.0 - smoothstep(-aa, aa, across - max(1.0, aa * 1.4))) * 0.5;
    } else if (st == 5) {
        // three translucent bands of light at different depths: an aurora, not
        // three stacked fills, so the wallpaper still shows between them.
        // Small phase offsets: shifting a layer far along the spectrum makes the
        // three disagree and spike, instead of drifting past each other.
        float a = 0.0;
        float front = maxLen * lvSmooth(along);
        for (int k = 0; k < 3; k++) {
            float h = maxLen * (1.0 - float(k) * 0.24) * lvSmooth(along + float(k) * 0.03);
            if (centred) h = h * 0.5;
            float bandW = max(1.5, maxLen * (0.055 - 0.010 * float(k)));
            a += (1.0 - smoothstep(-aa * 1.5, bandW * 1.1, max(abs(across - h) - bandW, 0.0)))
                 * (0.50 - 0.12 * float(k));
        }
        if (centred) front = front * 0.5;
        sd = abs(across - front) - max(1.5, maxLen * 0.055);
        extraA += a;
        hot = lvSmooth(along);
        lift = clamp(across / max(front, 1.0), 0.0, 1.0);
    } else if (st == 7) {
        // the scope: the live waveform, windowed at the ends so it melts into
        // the wallpaper instead of stopping dead.
        // kept inside the box (the bloom has the pad around it to fall off in)
        float amp = maxLen * 0.46;
        float base = centred ? 0.0 : maxLen * 0.5;
        float win = pow(sin(PI * clamp(along, 0.0, 1.0)), 0.45);
        float dt = 1.0 / max(axisLen * 0.25, 8.0);
        float y0 = base + scopeAt(along - dt) * amp * win;
        float y1 = base + scopeAt(along) * amp * win;
        float y2 = base + scopeAt(along + dt) * amp * win;
        float w = max(1.5, maxLen * 0.013);
        vec2 p = vec2(alongPx, centred ? signedAcross : across);
        sd = min(segDist(p, vec2(alongPx - dt * axisLen, y0), vec2(alongPx, y1)),
                 segDist(p, vec2(alongPx, y1), vec2(alongPx + dt * axisLen, y2))) - w;
        lift = 1.0;
        hot = abs(scopeAt(along));
    } else if (st == 8) {
        // the frame: a full ring of bars around the whole screen's edge, packed
        // with a small gap and each grown inward to its own height, so it reads
        // as a bar spectrum wrapped around the display.
        float dL = cpx.x;
        float dR = cw - cpx.x;
        float dT = cpx.y;
        float dB = ch - cpx.y;
        float perim = 2.0 * (cw + ch);
        float ed;   // inward distance from the nearest edge
        float s;    // arc length clockwise from the top-left corner
        float mE = min(min(dL, dR), min(dT, dB));
        if (mE == dT)      { ed = dT; s = cpx.x; }
        else if (mE == dR) { ed = dR; s = cw + cpx.y; }
        else if (mE == dB) { ed = dB; s = cw + ch + (cw - cpx.x); }
        else               { ed = dL; s = 2.0 * cw + ch + (ch - cpx.y); }
        float al = s / perim;
        // Many bars, packed: each samples the smoothed spectrum at its own spot,
        // so the ring fills with bars of uneven height rather than sparse ticks.
        float ticks = max(bands, 1.0) * 3.0;
        float slotP = perim / ticks;
        float ti = floor(s / slotP);
        float ci = (ti + 0.5) * slotP;
        float tAl = (ti + 0.5) / ticks;
        float lvv = lvSmooth(tAl);
        float len = max(minLen, maxLen * lvv);
        float barWp = max(1.5, slotP * thickness);
        tRamp = tAl;
        hot = lvv;
        sd = roundBox(vec2(s - ci, ed - len * 0.5), vec2(barWp * 0.5, len * 0.5), capR);
        lift = clamp(ed / max(len, 1.0), 0.0, 1.0);
        if (peakOn > 0.5) {
            float ph = maxLen * pkAt(bandAt(tAl));
            float capH = max(2.0, min(capR * 1.1, barWp * 0.34));
            float sdp = roundBox(vec2(s - ci, ed - (ph + capH * 1.6)),
                                 vec2(barWp * 0.46, capH * 0.5), capH * 0.5);
            extraA += (1.0 - smoothstep(-aa, aa, sdp)) * 0.85;
        }
    } else if (st == 9) {
        vec2 q = px - origin;
        float d = length(q);
        float a01 = fract((atan(q.y, q.x) + spinRad + PI * 0.5) / TAU + 1.0);
        int i = bandAt(a01);
        float lvv = lvAt(i);
        float len = max(minLen, rMax * lvv);
        float ac = (float(i) + 0.5) / max(bands, 1.0);
        float off = (a01 - ac) - floor((a01 - ac) + 0.5);
        tRamp = a01;
        hot = lvv;
        // the angular offset becomes an arc length at this radius, so a bar
        // keeps its width whatever the ring size.
        sd = roundBox(vec2(off * TAU * max(d, 1.0), d - (r0 + len * 0.5)),
                      vec2(max(1.0, shapeW * 0.5), len * 0.5), capR);
        lift = clamp((d - r0) / max(len, 1.0), 0.0, 1.0);
        // the ring the bars stand on, breathing with the bass so the centre is
        // never a dead hole.
        float ringW = max(1.2, shapeW * 0.22);
        float ring = abs(d - r0 * (1.0 + 0.08 * energy)) - ringW;
        extraA += (1.0 - smoothstep(-aa, aa, ring)) * 0.45;
    } else if (st == 10) {
        vec2 q = px - origin;
        float d = length(q);
        float a01 = fract((atan(q.y, q.x) + spinRad + PI * 0.5) / TAU + 1.0);
        // folded around the vertical axis so both seams meet, and averaged over
        // five angles: a bass-heavy spectrum wrapped raw reads as a flower.
        float fold = a01 < 0.5 ? a01 * 2.0 : (1.0 - a01) * 2.0;
        float w = 2.0 / max(bands, 8.0);
        float lvl = (lvSmooth(fold - w) + lvSmooth(fold - w * 0.5) + lvSmooth(fold)
                   + lvSmooth(fold + w * 0.5) + lvSmooth(fold + w)) * 0.2;
        float rr = r0 + rMax * lvl;
        // A glass sphere, not a disc of colour: the body is barely there, the
        // wobbling rim carries the shape, and two ripples inside give it depth so
        // the wallpaper reads through the middle.
        sd = d - rr;
        lift = clamp(d / max(rr, 1.0), 0.0, 1.0);
        hot = lvl;
        fillA = 0.05 + 0.25 * lift;
        tRamp = 0.28 + 0.50 * lift;
        float rim = abs(sd) - max(1.3, aa * 1.5);
        extraA += (1.0 - smoothstep(-aa, aa, rim)) * 0.95;
        extraA += exp(-max(abs(sd), 0.0) / max(rMax * 0.22, 2.0)) * 0.22;
        for (int rg = 1; rg < 3; rg++) {
            float rr2 = rr * (0.34 + 0.24 * float(rg)) * (1.0 + 0.06 * energy);
            float ripple = abs(d - rr2) - max(1.0, aa);
            extraA += (1.0 - smoothstep(-aa, aa, ripple)) * (0.30 - 0.08 * float(rg));
        }
        extraA += exp(-d / max(r0 * 0.30, 2.0)) * (0.10 + 0.25 * energy);
    } else {
        vec2 q = px - origin;
        float d = length(q);
        float a01 = fract((atan(q.y, q.x) + spinRad + PI * 0.5) / TAU + 1.0);
        float turns = 1.5;
        float k = rMax / (turns * TAU);
        float best = 1e9;
        float bestT = 0.0;
        float bestL = 0.0;
        for (int n = 0; n < 2; n++) {
            float th = (a01 + float(n)) * TAU;
            if (th <= turns * TAU) {
                float t = th / (turns * TAU);
                float lvv = lvAt(bandAt(t));
                // the arm tapers in at both ends, so it reads as drawn rather
                // than cut off.
                float taper = smoothstep(0.0, 0.10, t) * (1.0 - smoothstep(0.90, 1.0, t));
                float w = max(1.5, rMax * 0.13 * (0.35 + 0.65 * lvv) * max(taper, 0.06));
                float dd = abs(d - (r0 + k * th)) - w * 0.5;
                if (dd < best) {
                    best = dd;
                    bestT = t;
                    bestL = lvv;
                }
            }
        }
        sd = best;
        tRamp = bestT;
        hot = bestL;
        lift = clamp((d - r0) / max(rMax, 1.0), 0.0, 1.0);
    }

    float body = 1.0 - smoothstep(-aa, aa, sd);
    // Roots read as light rather than paint: the shape fades toward where it
    // grows from, and only the tip carries full weight.
    float cover = clamp(body * fillA * mix(0.66, 1.0, lift) + extraA, 0.0, 1.0);

    // Two-term bloom outside the shape: a tight bright core and a wide soft
    // skirt. It must not reach inside, or a filled look (the orb, the wave)
    // takes a flat wash of light across its whole body and turns milky.
    float g = max(sd, 0.0);
    float halo = glowAmt > 0.0
        ? (exp(-g / max(glowPx * 0.5, 0.8)) * 0.42 + exp(-g / max(glowPx * 3.0, 3.0)) * 0.16)
          * glowAmt * (0.35 + 0.65 * energy) * (1.0 - body)
        : 0.0;
    // the soft skirt reaches past the pad: let it die out before the surface
    // edge, or a bright glow ends in a hard rectangle around the widget
    if (!frame && pad > 0.0) {
        float edge = min(min(px.x, res.x - px.x), min(px.y, res.y - px.y));
        halo *= smoothstep(0.0, pad, edge);
    }

    float a = clamp(cover + halo, 0.0, 1.0) * fade * mirrorFade * u_opacity;
    // Edge looks melt into the wallpaper at both ends instead of being cut off;
    // the frame wraps, so it has no ends to fade.
    if (!polar && !frame)
        a *= smoothstep(0.0, 0.035, along) * (1.0 - smoothstep(0.965, 1.0, along));
    if (a <= 0.002) {
        fragColor = vec4(0.0);
        return;
    }

    vec3 col = rampAt(tRamp);
    // A loud band's tip runs hot, which is what makes a spectrum look lit. Kept
    // gentle: a hard push to white drains the accent the rest of the shell uses.
    col = mix(col, min(col * 1.22 + 0.10, vec3(1.0)),
              smoothstep(0.62, 1.0, lift) * clamp(hot, 0.0, 1.0) * 0.55);
    if (mirrorFade < 1.0) col *= 0.85;
    if (st == 1 && signedAcross < 0.0) col *= 0.62;
    // A touch of noise keeps the wide soft skirts from banding.
    a = clamp(a + (hash12(px) - 0.5) * 0.006, 0.0, 1.0);
    fragColor = vec4(col * a, a);
}
