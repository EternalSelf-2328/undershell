// SPDX-License-Identifier: GPL-3.0-or-later
R"PT({
  "name": {"en": "Petals", "es": "Pétalos"},
  "category": "nature",
  "max": 160, "rate": 14, "prewarm": 14, "sprite": "petal",
  "emitter": {"shape": "box", "origin": [0.6, -0.05], "size": [1.4, 0.02]},
  "init": {"lifetime": [11, 18], "size": [9, 16], "speed": [35, 70], "direction": [70, 100],
           "color": ["#ffc4d6", "#ffe0ea"], "alpha": [0.8, 1], "rotation": [0, 360], "spin": [-90, 90], "depth": [0.1, 0.95]},
  "operators": [
    {"type": "oscillate_position", "freq": [0.2, 0.45], "amount": 40},
    {"type": "fade", "in": 0.04, "out": 0.1}
  ]
})PT"
