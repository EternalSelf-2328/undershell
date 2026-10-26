// SPDX-License-Identifier: GPL-3.0-or-later
R"PT({
  "name": {"en": "Snow", "es": "Nieve"},
  "category": "weather",
  "max": 700, "rate": 70, "prewarm": 14, "sprite": "flake",
  "emitter": {"shape": "box", "origin": [0.5, -0.04], "size": [1.3, 0.02]},
  "init": {"lifetime": [12, 20], "size": [4, 11], "speed": [35, 80], "direction": [82, 98],
           "color": ["#ffffff", "#e6eeff"], "alpha": [0.55, 1], "rotation": [0, 360], "spin": [-40, 40], "depth": [0.05, 0.95]},
  "operators": [
    {"type": "oscillate_position", "freq": [0.15, 0.5], "amount": 18},
    {"type": "fade", "in": 0.04, "out": 0.08}
  ]
})PT"
