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
void channel(std::vector<BoltSeg>& out, Pt a, Pt b, const BoltShape& s, BoltRandom& rnd, int level) {
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

const char* kBoltVertex = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_seg;
layout(location = 2) in vec2 a_wi;
uniform vec2 u_res;
out vec2 v_px;
flat out vec4 v_seg;
flat out vec2 v_wi;
void main() {
    v_px = a_pos;
    v_seg = a_seg;
    v_wi = a_wi;
    gl_Position = vec4(a_pos.x / u_res.x * 2.0 - 1.0, 1.0 - a_pos.y / u_res.y * 2.0, 0.0, 1.0);
}
)";

// modes 0 and 3: light, its glow and its white-hot core, blended by max so
// crossings and joints never double up; 1: manga ink, the outline; 2: manga
// white, on top
const char* kBoltFragment = R"(#version 300 es
precision highp float;
in vec2 v_px;
flat in vec4 v_seg;
flat in vec2 v_wi;
out vec4 fragColor;
uniform int u_mode;
uniform float u_glow;
uniform float u_ink;
uniform float u_opacity;
uniform vec3 u_core;
uniform vec3 u_halo;
void main() {
    vec2 pa = v_px - v_seg.xy, ba = v_seg.zw - v_seg.xy;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    float d = length(pa - ba * h);
    float I = v_wi.y;
    float aa = max(fwidth(d), 0.35);
    if (u_mode == 0) {
        // the glow alone, on a coarser copy of the path (it is soft anyway)
        float g = u_glow;
        float glow = I * (0.55 * exp(-d * d / (g * g * 0.18)) + 0.22 * exp(-d / (g * 0.55)));
        float a = clamp(glow * 0.9, 0.0, 1.0);
        fragColor = vec4(min(u_halo * glow, vec3(a)), a) * u_opacity;
    } else if (u_mode == 3) {
        // the white-hot core, on the fine path, in narrow strips
        float r = v_wi.x * 0.5;
        // it stays white-hot as the bolt fades; the glow is what dims first
        float core = (1.0 - smoothstep(r - aa, r + aa, d)) * clamp(I * 1.7, 0.0, 1.0);
        fragColor = vec4(u_core * core, core) * u_opacity;
    } else {
        // in ink a fading bolt thins rather than dims, and its outline thins
        // with it: a thin fork stays white inside, never a bare black line
        float core = v_wi.x * 0.5 * clamp(I, 0.25, 1.4);
        if (I < 0.3 || core < 0.45) discard;  // ink has no greys: too faint to draw is not drawn
        float r = core + (u_mode == 1 ? min(u_ink, 0.3 + core * 0.7) : 0.0);
        float a = 1.0 - smoothstep(r - aa, r + aa, d);
        fragColor = (u_mode == 1 ? vec4(0.0, 0.0, 0.0, a) : vec4(a)) * u_opacity;
    }
}
)";

}  // namespace

void lightning(std::vector<BoltSeg>& out, float ax, float ay, float bx, float by, const BoltShape& shape, uint32_t seed) {
  BoltRandom rnd(seed);
  channel(out, {ax, ay}, {bx, by}, shape, rnd, 0);
}

void BoltRenderer::pass(const std::vector<BoltSeg>& segs, float w, float h, float reach, int mode, const Look& look) {
  m_verts.clear();
  m_verts.reserve(segs.size() * 6 * 8);
  for (const BoltSeg& s : segs) {
    const float width = mode == 0 || mode == 3 ? s.width : s.width * std::clamp(s.intensity, 0.25F, 1.4F);  // only ink thins
    const float R = (mode == 0 ? reach : mode == 3 ? width * 0.5F : width * 0.5F + look.ink) + 1.5F;
    float dx = s.x1 - s.x0, dy = s.y1 - s.y0;
    const float l = std::hypot(dx, dy);
    if (l < 1e-4F) {
      dx = 1;
      dy = 0;
    } else {
      dx /= l;
      dy /= l;
    }
    const float nx = -dy, ny = dx;
    const float c[4][2] = {{s.x0 - dx * R + nx * R, s.y0 - dy * R + ny * R},
                           {s.x0 - dx * R - nx * R, s.y0 - dy * R - ny * R},
                           {s.x1 + dx * R + nx * R, s.y1 + dy * R + ny * R},
                           {s.x1 + dx * R - nx * R, s.y1 + dy * R - ny * R}};
    for (int k : {0, 1, 2, 2, 1, 3})
      m_verts.insert(m_verts.end(), {c[k][0], c[k][1], s.x0, s.y0, s.x1, s.y1, s.width, s.intensity});
  }
  if (m_verts.empty()) return;
  glUniform1i(m_prog.uniform("u_mode"), mode);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  for (GLuint a = 0; a < 3; ++a) glEnableVertexAttribArray(a);
  const GLsizei stride = 8 * sizeof(float);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, m_verts.data());
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, m_verts.data() + 2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, m_verts.data() + 6);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_verts.size() / 8));
  for (GLuint a = 0; a < 3; ++a) glDisableVertexAttribArray(a);
  (void)w;
  (void)h;
}

void BoltRenderer::draw(const std::vector<BoltSeg>& segs, float w, float h, const Look& look) {
  if (segs.empty()) return;
  if (!m_prog.valid()) m_prog.create(kBoltVertex, kBoltFragment, "bolt");
  glUseProgram(m_prog.id());
  glUniform2f(m_prog.uniform("u_res"), w, h);
  glUniform1f(m_prog.uniform("u_glow"), std::max(1.0F, look.glow));
  glUniform1f(m_prog.uniform("u_ink"), look.ink);
  glUniform1f(m_prog.uniform("u_opacity"), look.opacity);
  glUniform3fv(m_prog.uniform("u_core"), 1, look.core);
  glUniform3fv(m_prog.uniform("u_halo"), 1, look.halo);
  glEnable(GL_BLEND);
  if (look.manga) {
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    pass(segs, w, h, 0, 1, look);  // every outline first, so the white lies over all of them
    pass(segs, w, h, 0, 2, look);
  } else {
    // The glow is what costs: a strip some 4 glows wide per segment. It is
    // drawn on the path joined `k` segments at a time (each about a glow
    // long), which a blur this soft cannot tell from the fine one.
    float total = 0;
    for (const BoltSeg& sg : segs) total += std::hypot(sg.x1 - sg.x0, sg.y1 - sg.y0);
    const float avg = total / static_cast<float>(segs.size());
    const int k = std::clamp(static_cast<int>(look.glow / std::max(1.0F, avg)), 1, 8);
    m_coarse.clear();
    for (size_t i = 0; i < segs.size();) {
      BoltSeg c = segs[i];
      size_t j = i + 1;
      // only along one unbroken run: a fork starts its own
      while (j < segs.size() && static_cast<int>(j - i) < k && segs[j].x0 == segs[j - 1].x1 && segs[j].y0 == segs[j - 1].y1) ++j;
      c.x1 = segs[j - 1].x1;
      c.y1 = segs[j - 1].y1;
      m_coarse.push_back(c);
      i = j;
    }
    glBlendEquation(GL_MAX);
    pass(m_coarse, w, h, look.glow * 2.2F, 0, look);
    pass(segs, w, h, 0, 3, look);
    glBlendEquation(GL_FUNC_ADD);
  }
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_BLEND);
}

}  // namespace undershell
