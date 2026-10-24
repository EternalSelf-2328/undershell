// SPDX-License-Identifier: GPL-3.0-or-later
R"PT({
  "name": {"en": "Fireflies", "es": "Luciérnagas"},
  "max": 70, "rate": 7, "prewarm": 10, "sprite": "glow", "blend": "add",
  "emitter": {"shape": "box", "origin": [0.5, 0.65], "size": [1.0, 0.6]},
  "init": {"lifetime": [6, 12], "size": [16, 30], "speed": [4, 16], "direction": [0, 360],
           "color": ["#d8ff7a", "#fff2a0"], "alpha": [0.7, 1], "depth": [0.1, 0.9]},
  "operators": [
    {"type": "turbulence", "scale": 0.004, "speed": 0.25, "strength": 22},
    {"type": "oscillate_alpha", "freq": [0.3, 1.1], "amount": 0.9},
    {"type": "fade", "in": 0.2, "out": 0.25}
  ]
})PT"
