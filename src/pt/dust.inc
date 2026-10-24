// SPDX-License-Identifier: GPL-3.0-or-later
R"PT({
  "name": {"en": "Floating dust", "es": "Polvo en el aire"},
  "max": 160, "rate": 12, "prewarm": 12, "sprite": "dot", "blend": "add",
  "emitter": {"shape": "box", "origin": [0.5, 0.5], "size": [1.0, 1.0]},
  "init": {"lifetime": [8, 14], "size": [4, 8], "speed": [2, 9], "direction": [0, 360],
           "color": ["#fff6e0", "#ffffff"], "alpha": [0.3, 0.7], "depth": [0.2, 0.9]},
  "operators": [
    {"type": "turbulence", "scale": 0.003, "speed": 0.15, "strength": 10},
    {"type": "oscillate_alpha", "freq": [0.2, 0.7], "amount": 0.6},
    {"type": "fade", "in": 0.2, "out": 0.25}
  ]
})PT"
