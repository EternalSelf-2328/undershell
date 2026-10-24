// SPDX-License-Identifier: GPL-3.0-or-later
R"PT({
  "name": {"en": "Rain", "es": "Lluvia"},
  "max": 900, "rate": 520, "prewarm": 2, "sprite": "streak", "streak": 0.03,
  "emitter": {"shape": "box", "origin": [0.55, -0.06], "size": [1.4, 0.02]},
  "init": {"lifetime": [1.4, 2.2], "size": [1.2, 2.4], "speed": [850, 1250], "direction": [97, 103],
           "color": ["#c8d6e6", "#e9f1fa"], "alpha": [0.18, 0.45], "depth": [0.05, 0.95]},
  "operators": [{"type": "fade", "in": 0.05, "out": 0.05}]
})PT"
