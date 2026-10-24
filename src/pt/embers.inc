// SPDX-License-Identifier: GPL-3.0-or-later
R"PT({
  "name": {"en": "Embers", "es": "Brasas"},
  "max": 220, "rate": 26, "prewarm": 6, "sprite": "glow", "blend": "add",
  "emitter": {"shape": "box", "origin": [0.5, 1.03], "size": [1.1, 0.04]},
  "init": {"lifetime": [4, 8], "size": [7, 15], "speed": [40, 110], "direction": [255, 285],
           "color": ["#ffb347", "#ff6a1a"], "alpha": [0.7, 1], "depth": [0.1, 0.9]},
  "operators": [
    {"type": "turbulence", "scale": 0.006, "speed": 0.5, "strength": 40},
    {"type": "color_change", "end": "#a81c08"},
    {"type": "size_change", "end": 0.4},
    {"type": "oscillate_alpha", "freq": [2, 6], "amount": 0.35},
    {"type": "fade", "in": 0.05, "out": 0.35}
  ]
})PT"
