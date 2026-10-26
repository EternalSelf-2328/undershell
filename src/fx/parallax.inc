// SPDX-License-Identifier: GPL-3.0-or-later
R"FX(/* undershell-effect
{
  "name": {"en": "2.5D camera", "es": "Cámara 2.5D"},
  "category": "motion",
  "stage": "warp",
  "uses": ["time", "depth", "audio"],
  "audio_when": "beat",
  "params": [
    {"key": "motion", "kind": "choice", "options": ["orbit", "sway", "zoom"], "default": "orbit",
     "option_names": {"en": ["Orbit", "Side to side", "Push in"], "es": ["Órbita", "Vaivén", "Acercar"]},
     "label": {"en": "Camera", "es": "Cámara"}},
    {"key": "speed", "kind": "number", "min": 0.05, "max": 2, "step": 0.05, "default": 0.4, "label": {"en": "Speed", "es": "Velocidad"}},
    {"key": "focus", "kind": "number", "min": 0, "max": 1, "step": 0.05, "default": 0.5, "label": {"en": "Still plane", "es": "Plano quieto"}},
    {"key": "beat", "kind": "bool", "default": false, "label": {"en": "Bounce on beat", "es": "Salto al golpe"}}
  ]
}
*/
// The picture seen from a camera that drifts: what the depth map puts near
// moves more than what lies far, the plane at `focus` stays put. A few
// steps read the depth where the shifted ray lands, not where it starts,
// so edges tear less.
vec2 warp(vec2 uv) {
    float t = u_time * speed;
    vec2 cam = vec2(0.0);
    float zoom = 0.0;
    if (motion == 0) cam = vec2(sin(t), cos(t) * 0.6);
    else if (motion == 1) cam = vec2(sin(t), 0.0);
    else zoom = sin(t * 0.7);
    if (beat) zoom += u_audio.w * 0.8;
    vec2 p = uv;
    int steps = 1 + u_quality;
    for (int i = 0; i < steps; i++) {
        float d = depth(p) - focus;
        p = uv - pictureOffset(cam * d * 0.012) + (uv - 0.5) * zoom * d * 0.03;
    }
    // a touch closer, so the borders never show what lies past the picture
    return 0.5 + (p - 0.5) * 0.985;
}
)FX"
