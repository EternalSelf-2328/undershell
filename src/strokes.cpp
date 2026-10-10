// SPDX-License-Identifier: GPL-3.0-or-later
#include "strokes.hpp"

#include <algorithm>
#include <cmath>

namespace undershell {

namespace {

const char* kStrokeVertex = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_seg;
layout(location = 2) in vec3 a_wi;
uniform vec2 u_res;
out vec2 v_px;
flat out vec4 v_seg;
flat out vec3 v_wi;
void main() {
    v_px = a_pos;
    v_seg = a_seg;
    v_wi = a_wi;
    gl_Position = vec4(a_pos.x / u_res.x * 2.0 - 1.0, 1.0 - a_pos.y / u_res.y * 2.0, 0.0, 1.0);
}
)";

// modes 0 and 3: light, its glow and its white-hot core, blended by max so
// crossings and joints never double up; 1: ink, the outline; 2: ink, the
// white on top; 4: flat colour
const char* kStrokeFragment = R"(#version 300 es
precision highp float;
in vec2 v_px;
flat in vec4 v_seg;
flat in vec3 v_wi;
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
    float width = mix(v_wi.x, v_wi.y, h);  // it may taper
    float I = v_wi.z;
    float aa = max(fwidth(d), 0.35);
    if (u_mode == 0) {
        // the glow alone, on a coarser copy of the path (it is soft anyway)
        float g = u_glow;
        float glow = I * (0.55 * exp(-d * d / (g * g * 0.18)) + 0.22 * exp(-d / (g * 0.55)));
        float a = clamp(glow * 0.9, 0.0, 1.0);
        fragColor = vec4(min(u_halo * glow, vec3(a)), a) * u_opacity;
    } else if (u_mode == 3) {
        // the white-hot core, on the fine path, in narrow strips; it stays
        // white as the stroke fades, the glow is what dims first
        float r = width * 0.5;
        float core = (1.0 - smoothstep(r - aa, r + aa, d)) * clamp(I * 1.7, 0.0, 1.0);
        fragColor = vec4(u_core * core, core) * u_opacity;
    } else if (u_mode == 4) {
        float r = width * 0.5;
        float a = (1.0 - smoothstep(r - aa, r + aa, d)) * clamp(I, 0.0, 1.0);
        fragColor = vec4(u_core * a, a) * u_opacity;
    } else {
        // in ink a fading stroke thins rather than dims, and its outline thins
        // with it: a thin one stays white inside, never a bare black line
        float core = width * 0.5 * clamp(I, 0.25, 1.4);
        if (I < 0.3 || core < 0.45) discard;  // ink has no greys: too faint to draw is not drawn
        float r = core + (u_mode == 1 ? min(u_ink, 0.3 + core * 0.7) : 0.0);
        float a = 1.0 - smoothstep(r - aa, r + aa, d);
        fragColor = (u_mode == 1 ? vec4(0.0, 0.0, 0.0, a) : vec4(a)) * u_opacity;
    }
}
)";

}  // namespace

void StrokeRenderer::pass(const std::vector<Stroke>& strokes, float reach, int mode, const Look& look) {
  m_verts.clear();
  m_verts.reserve(strokes.size() * 6 * 9);
  const bool ink = mode == 1 || mode == 2;
  for (const Stroke& s : strokes) {
    const float w1 = s.widthEnd < 0 ? s.width : s.widthEnd;
    const float widest = std::max(s.width, w1) * (ink ? std::clamp(s.intensity, 0.25F, 1.4F) : 1.0F);  // only ink thins
    const float R = (mode == 0 ? reach : mode == 1 ? widest * 0.5F + look.ink : widest * 0.5F) + 1.5F;
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
      m_verts.insert(m_verts.end(), {c[k][0], c[k][1], s.x0, s.y0, s.x1, s.y1, s.width, w1, s.intensity});
  }
  if (m_verts.empty()) return;
  glUniform1i(m_prog.uniform("u_mode"), mode);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  for (GLuint a = 0; a < 3; ++a) glEnableVertexAttribArray(a);
  const GLsizei stride = 9 * sizeof(float);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, m_verts.data());
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, m_verts.data() + 2);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, m_verts.data() + 6);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_verts.size() / 9));
  for (GLuint a = 0; a < 3; ++a) glDisableVertexAttribArray(a);
}

void StrokeRenderer::draw(const std::vector<Stroke>& strokes, float w, float h, const Look& look) {
  if (strokes.empty()) return;
  if (!m_prog.valid()) m_prog.create(kStrokeVertex, kStrokeFragment, "strokes");
  glUseProgram(m_prog.id());
  glUniform2f(m_prog.uniform("u_res"), w, h);
  glUniform1f(m_prog.uniform("u_glow"), std::max(1.0F, look.glow));
  glUniform1f(m_prog.uniform("u_ink"), look.ink);
  glUniform1f(m_prog.uniform("u_opacity"), look.opacity);
  glUniform3fv(m_prog.uniform("u_core"), 1, look.core);
  glUniform3fv(m_prog.uniform("u_halo"), 1, look.halo);
  glEnable(GL_BLEND);
  if (look.mode == Mode::Ink) {
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    pass(strokes, 0, 1, look);  // every outline first, so the white lies over all of them
    pass(strokes, 0, 2, look);
  } else if (look.mode == Mode::Flat) {
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    pass(strokes, 0, 4, look);
  } else {
    // The glow is what costs: a strip some 4 glows wide per segment. It is
    // drawn on each run joined `k` segments at a time (each about a glow
    // long), which a blur this soft cannot tell from the fine one.
    float total = 0;
    for (const Stroke& sg : strokes) total += std::hypot(sg.x1 - sg.x0, sg.y1 - sg.y0);
    const float avg = total / static_cast<float>(strokes.size());
    const int k = std::clamp(static_cast<int>(look.glow / std::max(1.0F, avg)), 1, 8);
    m_coarse.clear();
    for (size_t i = 0; i < strokes.size();) {
      Stroke c = strokes[i];
      size_t j = i + 1;
      // only along one unbroken run: a fork starts its own
      while (j < strokes.size() && static_cast<int>(j - i) < k && strokes[j].x0 == strokes[j - 1].x1 &&
             strokes[j].y0 == strokes[j - 1].y1)
        ++j;
      c.x1 = strokes[j - 1].x1;
      c.y1 = strokes[j - 1].y1;
      m_coarse.push_back(c);
      i = j;
    }
    glBlendEquation(GL_MAX);
    pass(m_coarse, look.glow * 2.2F, 0, look);
    pass(strokes, 0, 3, look);
    glBlendEquation(GL_FUNC_ADD);
  }
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_BLEND);
}

}  // namespace undershell
