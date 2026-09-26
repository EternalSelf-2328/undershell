// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas.hpp"

#include <cmath>
#include <numbers>

namespace undershell {

static const char* kShapeVert = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
uniform vec4 u_bounds;   // x0 y0 x1 y1, surface px
uniform vec2 u_surface;
out vec2 v_px;
void main() {
    v_px = mix(u_bounds.xy, u_bounds.zw, a_pos);
    vec2 p = v_px / u_surface;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
)";

static const char* kShapeFrag = R"(#version 300 es
precision highp float;
in vec2 v_px;
out vec4 fragColor;
uniform int u_kind;        // 0 round rect, 1 circle, 2 segment, 3 arc
uniform vec4 u_a;          // kind-specific
uniform vec4 u_b;
uniform vec4 u_fill;       // straight alpha
uniform vec4 u_stroke;
uniform float u_strokeW;
uniform float u_aa;

const float TAU = 6.28318530718;

float roundBox(vec2 p, vec2 b, float r) {
    r = min(r, min(b.x, b.y));
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}
float segDist(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-4), 0.0, 1.0);
    return length(pa - ba * h);
}

void main() {
    vec2 p = v_px;
    float d;
    if (u_kind == 0) {            // a: x y w h   b: radius
        vec2 c = u_a.xy + u_a.zw * 0.5;
        d = roundBox(p - c, u_a.zw * 0.5, u_b.x);
    } else if (u_kind == 1) {     // a: cx cy r
        d = length(p - u_a.xy) - u_a.z;
    } else if (u_kind == 2) {     // a: x1 y1 x2 y2   b: halfwidth roundcap
        if (u_b.y > 0.5) {
            d = segDist(p, u_a.xy, u_a.zw) - u_b.x;
        } else {
            vec2 dir = u_a.zw - u_a.xy;
            float len = max(length(dir), 1e-4);
            vec2 t = dir / len;
            vec2 q = p - u_a.xy;
            vec2 local = vec2(dot(q, t) - len * 0.5, dot(q, vec2(-t.y, t.x)));
            d = roundBox(local, vec2(len * 0.5 + u_b.x, u_b.x), 0.0);
        }
    } else {                      // a: cx cy r halfwidth   b: a0 a1 roundcap
        vec2 q = p - u_a.xy;
        float ang = atan(q.x, -q.y);            // 0 at 12 o'clock, clockwise
        if (ang < 0.0) ang += TAU;
        float a0 = u_b.x, a1 = u_b.y;
        bool inside = a1 - a0 >= TAU - 1e-4 || (ang >= a0 && ang <= a1);
        if (inside) {
            d = abs(length(q) - u_a.z) - u_a.w;
        } else if (u_b.z > 0.5) {
            vec2 e0 = u_a.z * vec2(sin(a0), -cos(a0));
            vec2 e1 = u_a.z * vec2(sin(a1), -cos(a1));
            d = min(length(q - e0), length(q - e1)) - u_a.w;
        } else {
            d = 1e3;
        }
    }
    float fillA = (1.0 - smoothstep(-u_aa, u_aa, d)) * u_fill.a;
    vec3 pm = u_fill.rgb * fillA;   // premultiplied
    float a = fillA;
    if (u_strokeW > 0.0) {
        // an inner stroke of width u_strokeW, composited over the fill
        float s = 1.0 - smoothstep(-u_aa, u_aa, abs(d + u_strokeW * 0.5) - u_strokeW * 0.5);
        float sa = s * u_stroke.a;
        pm = pm * (1.0 - sa) + u_stroke.rgb * sa;
        a = sa + fillA * (1.0 - sa);
    }
    fragColor = vec4(pm, a);
}
)";

void Canvas::begin(float surfaceW, float surfaceH, int pixelScale, TextRenderer* text) {
  m_w = surfaceW;
  m_h = surfaceH;
  m_pixelScale = std::max(1, pixelScale);
  m_text = text;
  m_scale = 1;
  m_ox = m_oy = 0;
}

void Canvas::setTransform(float scale, float offsetX, float offsetY) {
  m_scale = scale;
  m_ox = offsetX;
  m_oy = offsetY;
}

void Canvas::shape(int kind, float x0, float y0, float x1, float y1, const float* params, Color fill, float strokeW,
                   Color stroke) {
  if (!m_prog.valid()) m_prog.create(kShapeVert, kShapeFrag, "canvas");
  glUseProgram(m_prog.id());
  const float pad = 2 + strokeW;
  glUniform4f(m_prog.uniform("u_bounds"), x0 - pad, y0 - pad, x1 + pad, y1 + pad);
  glUniform2f(m_prog.uniform("u_surface"), m_w, m_h);
  glUniform1i(m_prog.uniform("u_kind"), kind);
  glUniform4f(m_prog.uniform("u_a"), params[0], params[1], params[2], params[3]);
  glUniform4f(m_prog.uniform("u_b"), params[4], params[5], params[6], params[7]);
  glUniform4f(m_prog.uniform("u_fill"), fill.r, fill.g, fill.b, fill.a);
  glUniform4f(m_prog.uniform("u_stroke"), stroke.r, stroke.g, stroke.b, stroke.a);
  glUniform1f(m_prog.uniform("u_strokeW"), strokeW);
  glUniform1f(m_prog.uniform("u_aa"), 0.6F);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  drawUnitQuad();
  glDisable(GL_BLEND);
}

void Canvas::roundRect(float x, float y, float w, float h, float radius, Color fill, float strokeW, Color stroke) {
  const float X = m_ox + x * m_scale, Y = m_oy + y * m_scale, W = w * m_scale, H = h * m_scale;
  const float p[8] = {X, Y, W, H, radius * m_scale, 0, 0, 0};
  shape(0, X, Y, X + W, Y + H, p, fill, strokeW * m_scale, stroke);
}

void Canvas::circle(float cx, float cy, float r, Color fill, float strokeW, Color stroke) {
  const float X = m_ox + cx * m_scale, Y = m_oy + cy * m_scale, R = r * m_scale;
  const float p[8] = {X, Y, R, 0, 0, 0, 0, 0};
  shape(1, X - R, Y - R, X + R, Y + R, p, fill, strokeW * m_scale, stroke);
}

void Canvas::segment(float x1, float y1, float x2, float y2, float width, Color color, bool roundCap) {
  const float X1 = m_ox + x1 * m_scale, Y1 = m_oy + y1 * m_scale, X2 = m_ox + x2 * m_scale, Y2 = m_oy + y2 * m_scale;
  const float hw = width * m_scale / 2;
  const float p[8] = {X1, Y1, X2, Y2, hw, roundCap ? 1.0F : 0.0F, 0, 0};
  shape(2, std::min(X1, X2) - hw, std::min(Y1, Y2) - hw, std::max(X1, X2) + hw, std::max(Y1, Y2) + hw, p, color, 0, {});
}

void Canvas::arc(float cx, float cy, float r, float width, float a0, float a1, Color color, bool roundCap) {
  const float X = m_ox + cx * m_scale, Y = m_oy + cy * m_scale, R = r * m_scale, hw = width * m_scale / 2;
  const float p[8] = {X, Y, R, hw, a0, a1, roundCap ? 1.0F : 0.0F, 0};
  shape(3, X - R - hw, Y - R - hw, X + R + hw, Y + R + hw, p, color, 0, {});
}

Canvas::Size Canvas::measure(const std::string& text, const TextStyle& style) {
  Size s;
  TextRenderer::measure(text, style, s.w, s.h, s.baseline);
  return s;
}

void Canvas::text(const std::string& text, const TextStyle& style, float x, float y, Color color, float opacity, float sy,
                  float blur) {
  if (!m_text || text.empty()) return;
  TextStyle st = style;
  st.size = style.size * m_scale;
  st.letterSpacing = style.letterSpacing * m_scale;
  st.stroke = style.stroke * m_scale;
  const TextImage& img = m_text->get(text, st, m_pixelScale);
  m_text->drawEx(img, m_ox + x * m_scale, m_oy + y * m_scale, color, m_w, m_h, 1, sy, blur * m_scale, opacity);
}

}  // namespace undershell
