// SPDX-License-Identifier: GPL-3.0-or-later
// Material 3's shape library (cookies, flower, clovers, bursts…), ported from
// Sung's src/m3shape.cpp (MIT, (c) yappologistic), itself a port of
// androidx.graphics.shapes' RoundedPolygon and MaterialShapes.kt. Every shape
// is star-shaped about its centre, so it is fully described by its radius at
// each angle: that is what the widgets use (shaders sample it).
#pragma once

#include <string>
#include <vector>

namespace undershell {

// the shapes, in the order Material lists them
const std::vector<std::string>& m3ShapeNames();
// The outline's radius at `count` even angles from 0 (+x, growing clockwise
// on screen), in units of half the box (the longer side spans -1..1). An
// unknown name, or "none", is a circle.
std::vector<float> m3ShapeRadii(const std::string& name, int count);

}  // namespace undershell
