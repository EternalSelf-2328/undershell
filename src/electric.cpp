// SPDX-License-Identifier: GPL-3.0-or-later
#include "electric.hpp"

#include <algorithm>
#include <cmath>

namespace undershell {

float BoltRandom::next() {
  // xorshift32, then the top 24 bits
  m_s ^= m_s << 13;
  m_s ^= m_s >> 17;
  m_s ^= m_s << 5;
  return static_cast<float>(m_s >> 8) / 16777216.0F;
}

namespace {

struct Pt {
  float x, y;
};

// How far from (x, y) along (dx, dy) the bounds allow (a large number if none).
float roomAlong(const BoltShape& s, float x, float y, float dx, float dy) {
  float t = 1e9F;
  if (s.boundX1 > s.boundX0) {
    if (dx > 1e-4F) t = std::min(t, (s.boundX1 - x) / dx);
    if (dx < -1e-4F) t = std::min(t, (s.boundX0 - x) / dx);
    if (dy > 1e-4F) t = std::min(t, (s.boundY1 - y) / dy);
    if (dy < -1e-4F) t = std::min(t, (s.boundY0 - y) / dy);
  }
  if (s.boundR > 0) {
    const float ox = x - s.boundCx, oy = y - s.boundCy;
    const float b = ox * dx + oy * dy, c = ox * ox + oy * oy - s.boundR * s.boundR;
    const float disc = b * b - c;
    t = std::min(t, disc > 0 ? -b + std::sqrt(disc) : 0.0F);
  }
  return std::max(0.0F, t);
}

// One channel and, recursively, its forks. `from` is how far along the
// parent's reveal this channel starts (0 for the main one).
void channel(std::vector<Stroke>& out, Pt a, Pt b, const BoltShape& s, BoltRandom& rnd, int level) {
  const float len = std::hypot(b.x - a.x, b.y - a.y);
  if (len < 1 || s.reveal <= 0) return;
  // midpoint displacement: each pass halves the segments and the reach
  std::vector<Pt> pts = {a, b};
  float reach = s.rough * len;
  for (int d = 0; d < s.depth; ++d) {
    std::vector<Pt> next;
    next.reserve(pts.size() * 2);
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
      const Pt p = pts[i], q = pts[i + 1];
      const float dx = q.x - p.x, dy = q.y - p.y, l = std::max(1e-3F, std::hypot(dx, dy));
      const float off = rnd.range(-1, 1) * reach;
      next.push_back(p);
      next.push_back({(p.x + q.x) / 2 - dy / l * off, (p.y + q.y) / 2 + dx / l * off});
    }
    next.push_back(pts.back());
    pts.swap(next);
    reach *= 0.5F;
  }
  const size_t n = pts.size() - 1;  // segments
  const float shown = s.reveal * static_cast<float>(n);
  for (size_t i = 0; i < n; ++i) {
    if (static_cast<float>(i) >= shown) break;
    const float t = static_cast<float>(i) / static_cast<float>(n);
    Pt p = pts[i], q = pts[i + 1];
    const float part = std::min(1.0F, shown - static_cast<float>(i));
    q = {p.x + (q.x - p.x) * part, p.y + (q.y - p.y) * part};
    // thinner and dimmer toward its end, as the charge spends itself
    out.push_back({p.x, p.y, q.x, q.y, s.width * (1 - 0.45F * t), s.intensity * (1 - 0.3F * t)});
  }
  // forks: off the main run (not its first or last few points), short, bent
  // away from the channel, thinner and dimmer, forking again less readily
  if (level >= 2 || s.branches <= 0.001F) return;
  const float chance = s.branches * (level == 0 ? 0.11F : 0.06F) * 64.0F / static_cast<float>(n);
  for (size_t i = 2; i + 3 < pts.size(); ++i) {
    if (rnd.next() > chance) continue;
    const float t = static_cast<float>(i) / static_cast<float>(n);
    if (t >= s.reveal) break;
    const Pt p = pts[i], q = pts[i + 1];
    const float dir = std::atan2(q.y - p.y, q.x - p.x) + (rnd.next() < 0.5F ? -1 : 1) * rnd.range(0.35F, 0.85F);
    // short enough to stay in its bounds, its zigzag included
    const float fl = std::min(len * (1 - t) * rnd.range(0.22F, 0.55F), roomAlong(s, p.x, p.y, std::cos(dir), std::sin(dir)) * 0.8F);
    if (fl < 4) continue;
    BoltShape f = s;
    f.width = s.width * (1 - 0.45F * t) * 0.55F;
    f.intensity = s.intensity * 0.62F;
    f.branches = s.branches * 0.6F;
    f.depth = std::max(2, s.depth - 2);
    f.rough = s.rough * 1.1F;
    f.reveal = std::clamp((s.reveal - t) / std::max(1e-3F, 1 - t), 0.0F, 1.0F);
    channel(out, p, {p.x + std::cos(dir) * fl, p.y + std::sin(dir) * fl}, f, rnd, level + 1);
  }
}

}  // namespace

void lightning(std::vector<Stroke>& out, float ax, float ay, float bx, float by, const BoltShape& shape, uint32_t seed) {
  BoltRandom rnd(seed);
  channel(out, {ax, ay}, {bx, by}, shape, rnd, 0);
}

}  // namespace undershell
