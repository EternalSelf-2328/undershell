// SPDX-License-Identifier: GPL-3.0-or-later
R"PT({
  "name": {"en": "Sparks to the beat", "es": "Chispas al ritmo"},
  "category": "fire",
  "max": 400, "rate": 18, "prewarm": 0, "sprite": "spark", "blend": "add", "streak": 0.04, "react": 0.8,
  "emitter": {"shape": "sphere", "origin": [0.5, 1.02], "radius": 0.25},
  "init": {"lifetime": [0.9, 1.8], "size": [2.5, 5], "speed": [380, 760], "direction": [235, 305],
           "color": ["#ffe7a0", "#ffa040"], "alpha": [0.8, 1], "depth": [0.3, 0.9]},
  "operators": [
    {"type": "gravity", "value": [0, 520]},
    {"type": "drag", "value": 0.4},
    {"type": "color_change", "end": "#ff4010"},
    {"type": "fade", "in": 0.0, "out": 0.4},
    {"type": "audio", "band": "beat", "rate": 14, "size": 0.6}
  ]
})PT"
