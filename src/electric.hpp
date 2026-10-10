// SPDX-License-Identifier: GPL-3.0-or-later
// Electricity: lightning paths built on the CPU (midpoint displacement, with
// forks) and drawn as narrow strips around each segment -- a white-hot core in
// a glow, or a manga stroke with a black ink outline. The GPU only shades the
// pixels near a bolt, so a big box costs no more than a small one.
#pragma once

#include "gl.hpp"

#include <cstdint>
#include <vector>

namespace undershell {

struct BoltSeg {
  float x0, y0, x1, y1;
  float width;      // the core, logical px
  float intensity;  // 0..1+: how bright (or, in manga, how thick) it is
};

// A small deterministic random source (one per bolt, from its seed).
class BoltRandom {
public:
  explicit BoltRandom(uint32_t seed) : m_s(seed * 0x9E3779B9U + 0x7F4A7C15U) {}
  float next();                                       // 0..1
  float range(float a, float b) { return a + (b - a) * next(); }

private:
  uint32_t m_s;
};

struct BoltShape {
  float rough = 0.22F;     // sideways reach of the zigzag, a share of the length
  float branches = 0.5F;   // 0..1: how readily it forks
  float width = 2.0F;      // core width at the root, logical px
  float intensity = 1.0F;
  int depth = 6;           // subdivisions: 2^depth segments on the main channel
  float reveal = 1.0F;     // 0..1: how far from its start it has reached (a strike grows)
  // where its forks must stay: a rectangle (when x1 > x0) and/or a circle (r > 0)
  float boundX0 = 0, boundY0 = 0, boundX1 = 0, boundY1 = 0;
  float boundCx = 0, boundCy = 0, boundR = 0;
};

// A bolt from (ax, ay) to (bx, by), its forks appended after it.
void lightning(std::vector<BoltSeg>& out, float ax, float ay, float bx, float by, const BoltShape& shape, uint32_t seed);

class BoltRenderer {
public:
  struct Look {
    bool manga = false;
    float glow = 14;      // the glow's reach, logical px
    float ink = 2.2F;     // manga: the outline around the white
    float core[3] = {1, 1, 1};
    float halo[3] = {0.45F, 0.62F, 1.0F};
    float opacity = 1;
  };
  // Draws `segs` on the current framebuffer of `w` x `h` logical px.
  void draw(const std::vector<BoltSeg>& segs, float w, float h, const Look& look);
  void release() { m_prog.destroy(); }

private:
  void pass(const std::vector<BoltSeg>& segs, float w, float h, float reach, int mode, const Look& look);
  Program m_prog;
  std::vector<float> m_verts;
  std::vector<BoltSeg> m_coarse;
};

}  // namespace undershell
