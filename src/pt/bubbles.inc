// SPDX-License-Identifier: GPL-3.0-or-later
R"PT({
  "name": {"en": "Bubbles", "es": "Burbujas"},
  "max": 90, "rate": 7, "prewarm": 8, "sprite": "bubble",
  "emitter": {"shape": "box", "origin": [0.5, 1.04], "size": [1.0, 0.03]},
  "init": {"lifetime": [8, 14], "size": [8, 26], "speed": [40, 90], "direction": [262, 278],
           "color": ["#e8f6ff", "#ffffff"], "alpha": [0.45, 0.8], "depth": [0.1, 0.9]},
  "operators": [
    {"type": "oscillate_position", "freq": [0.4, 0.9], "amount": 14},
    {"type": "fade", "in": 0.05, "out": 0.08}
  ]
})PT"
