// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "Rain on the glass", "es": "Gotas en el vidrio"},
  "category": "water",
  "stage": "warp",
  "uses": ["time"],
  "params": [
    {"key": "amount", "kind": "number", "min": 0, "max": 1, "step": 0.05, "default": 0.6, "label": {"en": "Amount", "es": "Cantidad"}},
    {"key": "size", "kind": "number", "min": 0.3, "max": 3, "step": 0.1, "default": 1, "label": {"en": "Drop size", "es": "Tamaño"}},
    {"key": "speed", "kind": "number", "min": 0, "max": 3, "step": 0.05, "default": 1, "label": {"en": "Speed", "es": "Velocidad"}},
    {"key": "bend", "kind": "number", "min": 0, "max": 2, "step": 0.05, "default": 1, "label": {"en": "Refraction", "es": "Refracción"}}
  ]
}
*/
// Rain on a window in front of the picture. Beads sit on the glass, each on
// its own clock (it forms, stays, dries); a few drops run down, wobbling, and
// leave a wet line behind. Every drop is a small lens: the picture behind it
// shows through bent, as through water.
vec2 warp(vec2 uv) {
    vec2 p = vec2(uv.x * u_aspect, uv.y);
    vec2 off = vec2(0.0);
    float t = u_time * speed;
    for (int layer = 0; layer < 2; layer++) {  // small beads and bigger ones
        float k = (layer == 0 ? 30.0 : 15.0) / size;
        vec2 id = floor(p * k) + float(layer) * 101.0;
        vec2 f = fract(p * k) - 0.5;
        vec4 rnd = noise4(id);  // four random numbers in one read: the noise texture at a whole cell
        if (rnd.x > amount * 0.9) continue;
        vec2 c = (rnd.yz - 0.5) * 0.5;
        float life = fract(t * 0.03 + rnd.x * 7.0);
        float r = (0.1 + 0.17 * rnd.w) * smoothstep(0.0, 0.04, life) * smoothstep(1.0, 0.75, life);
        vec2 d = f - c;
        off -= d * smoothstep(r, r * 0.35, length(d)) * 2.4 / k;
    }
    // drops that run: one lane each, sliding faster and slower
    float lanes = 16.0 / size;
    float lane = floor(p.x * lanes);
    vec4 lr = noise4(vec2(lane, 31.0));
    float hl = lr.x;
    if (hl < amount * 0.8) {
        float y = fract(t * (0.05 + 0.08 * lr.y) + hl * 9.0);  // its head, top to bottom
        float x = fract(p.x * lanes) - 0.5 - (lr.z - 0.5) * 0.4 + sin(uv.y * 31.0 + hl * 40.0) * 0.06;
        float dy = (uv.y - y) * lanes;  // in the lane's own units, below the head is positive
        vec2 d = vec2(x, dy * 0.75);
        off -= vec2(x, dy) * smoothstep(0.2, 0.06, length(d)) * 2.2 / lanes;
        float trail = smoothstep(0.05, 0.0, abs(x)) * step(dy, 0.0) * smoothstep(-5.0, 0.0, dy);
        off.x -= x * trail * 0.8 / lanes;
    }
    return uv + pictureOffset(off * bend);
}
)FX"
