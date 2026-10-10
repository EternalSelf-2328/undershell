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
uniform int u_kind;        // 0 round rect, 1 circle, 2 segment, 3 arc, 4 triangle, 5 wave
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
    } else if (u_kind == 4) {     // a: x1 y1 x2 y2   b: x3 y3
        vec2 p0 = u_a.xy, p1 = u_a.zw, p2 = u_b.xy;
        vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
        vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
        vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
        vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
        vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
        float sgn = sign(e0.x * e2.y - e0.y * e2.x);
        vec2 dd = min(min(vec2(dot(pq0, pq0), sgn * (v0.x * e0.y - v0.y * e0.x)),
                          vec2(dot(pq1, pq1), sgn * (v1.x * e1.y - v1.y * e1.x))),
                      vec2(dot(pq2, pq2), sgn * (v2.x * e2.y - v2.y * e2.x)));
        d = -sqrt(dd.x) * sign(dd.y);
    } else if (u_kind == 5) {     // a: x0 x1 baseline amplitude   b: k(2pi/wl) phase halfthick
        float x = clamp(p.x, u_a.x, u_a.y);
        float yv = u_a.z + u_a.w * sin(x * u_b.x + u_b.y);
        float slope = u_a.w * u_b.x * cos(x * u_b.x + u_b.y);
        d = abs(p.y - yv) / sqrt(1.0 + slope * slope) - u_b.z;
        d = max(d, max(u_a.x - p.x, p.x - u_a.y));   // flat ends
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

void Canvas::begin(float surfaceW, float surfaceH, float pixelScale, TextRenderer* text) {
  m_w = surfaceW;
  m_h = surfaceH;
  m_pixelScale = std::max(0.25F, pixelScale);
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

void Canvas::triangle(float x1, float y1, float x2, float y2, float x3, float y3, Color color) {
  auto X = [&](float v) { return m_ox + v * m_scale; };
  auto Y = [&](float v) { return m_oy + v * m_scale; };
  const float p[8] = {X(x1), Y(y1), X(x2), Y(y2), X(x3), Y(y3), 0, 0};
  shape(4, std::min({p[0], p[2], p[4]}), std::min({p[1], p[3], p[5]}), std::max({p[0], p[2], p[4]}),
        std::max({p[1], p[3], p[5]}), p, color, 0, {});
}

void Canvas::wave(float x0, float x1, float y, float amplitude, float wavelength, float phase, float thickness,
                  Color color) {
  if (x1 <= x0) return;
  const float X0 = m_ox + x0 * m_scale, X1 = m_ox + x1 * m_scale, Yb = m_oy + y * m_scale;
  const float A = amplitude * m_scale, k = 2 * std::numbers::pi_v<float> / (wavelength * m_scale);
  const float hw = thickness * m_scale / 2;
  // the sine starts at x0 and travels right as `phase` (design px) grows
  const float p[8] = {X0, X1, Yb, A, k, -(phase * m_scale + X0) * k, hw, 0};
  shape(5, X0 - hw, Yb - A - hw, X1 + hw, Yb + A + hw, p, color, 0, {});
}

static const char* kImageFrag = R"(#version 300 es
precision highp float;
in vec2 v_px;
out vec4 fragColor;
uniform sampler2D u_tex;
uniform vec4 u_rect;      // x y w h (surface px)
uniform vec4 u_uv;        // uv origin + size (cover crop)
uniform float u_radius;
uniform float u_opacity;
uniform vec2 u_blur;      // uv radius
uniform float u_shapeR[128];
uniform float u_shaped;
float shapeAt(float a01) {
    float idx = fract(a01) * 128.0;
    int i = int(floor(idx));
    return mix(u_shapeR[i & 127], u_shapeR[(i + 1) & 127], fract(idx));
}
float roundBox(vec2 p, vec2 b, float r) {
    r = min(r, min(b.x, b.y));
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}
void main() {
    vec2 local = (v_px - u_rect.xy) / u_rect.zw;
    vec2 uv = u_uv.xy + local * u_uv.zw;
    vec4 c;
    if (u_blur.x > 0.0) {
        vec4 sum = vec4(0.0);
        float wsum = 0.0;
        for (int j = -4; j <= 4; j++)
            for (int i = -4; i <= 4; i++) {
                float w = exp(-float(i * i + j * j) / 10.0);
                sum += textureLod(u_tex, uv + vec2(float(i), float(j)) * u_blur / 4.0, 3.0) * w;
                wsum += w;
            }
        c = sum / wsum;
    } else {
        c = texture(u_tex, uv);
    }
    vec2 pc = v_px - (u_rect.xy + u_rect.zw * 0.5);
    float d = roundBox(pc, u_rect.zw * 0.5, u_radius);
    if (u_shaped > 0.5) {
        // a Material shape: distance to its outline, square to the edge
        float halfSide = min(u_rect.z, u_rect.w) * 0.5;
        float a01 = atan(pc.y, pc.x) / 6.28318530718;
        float k = shapeAt(a01);
        float dk = (shapeAt(a01 + 1.0 / 512.0) - shapeAt(a01 - 1.0 / 512.0)) / (2.0 * 6.28318530718 / 512.0);
        float slope = dk / max(k, 1e-3);
        d = (length(pc) - halfSide * k) / sqrt(1.0 + slope * slope);
    }
    float a = (1.0 - smoothstep(-0.6, 0.6, d)) * c.a * u_opacity;
    fragColor = vec4(c.rgb * a, a);
}
)";

void Canvas::image(GLuint texture, int texW, int texH, float x, float y, float w, float h, float radius, float opacity,
                   float blur, const float* shape) {
  if (!texture || texW <= 0 || texH <= 0 || w <= 0 || h <= 0) return;
  if (!m_image.valid()) m_image.create(kShapeVert, kImageFrag, "canvas-image");
  const float X = m_ox + x * m_scale, Y = m_oy + y * m_scale, W = w * m_scale, H = h * m_scale;
  // "cover": crop the texture to the rect's aspect ratio, centred
  const float ta = static_cast<float>(texW) / texH, ra = W / H;
  float uw = 1, uh = 1;
  if (ta > ra) uw = ra / ta;
  else uh = ta / ra;
  glUseProgram(m_image.id());
  glUniform4f(m_image.uniform("u_bounds"), X, Y, X + W, Y + H);
  glUniform2f(m_image.uniform("u_surface"), m_w, m_h);
  glUniform4f(m_image.uniform("u_rect"), X, Y, W, H);
  glUniform4f(m_image.uniform("u_uv"), (1 - uw) / 2, (1 - uh) / 2, uw, uh);
  glUniform1f(m_image.uniform("u_radius"), radius * m_scale);
  glUniform1f(m_image.uniform("u_opacity"), opacity);
  glUniform1f(m_image.uniform("u_shaped"), shape ? 1.0F : 0.0F);
  if (shape) glUniform1fv(m_image.uniform("u_shapeR"), 128, shape);
  glUniform2f(m_image.uniform("u_blur"), blur > 0 ? blur * m_scale / W * uw : 0.0F, blur > 0 ? blur * m_scale / H * uh : 0.0F);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glUniform1i(m_image.uniform("u_tex"), 0);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  drawUnitQuad();
  glDisable(GL_BLEND);
}

void Canvas::clip(float x, float y, float w, float h) {
  // GL scissor is in framebuffer pixels, origin bottom-left
  const float X = (m_ox + x * m_scale) * m_pixelScale, Y = (m_oy + y * m_scale) * m_pixelScale;
  const float W = w * m_scale * m_pixelScale, H = h * m_scale * m_pixelScale;
  glEnable(GL_SCISSOR_TEST);
  glScissor(static_cast<GLint>(std::floor(X)), static_cast<GLint>(std::floor(m_h * m_pixelScale - Y - H)),
            static_cast<GLsizei>(std::ceil(W)), static_cast<GLsizei>(std::ceil(H)));
}

void Canvas::clip() { glDisable(GL_SCISSOR_TEST); }

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
  st.maxWidth = style.maxWidth * m_scale;
  const TextImage& img = m_text->get(text, st, m_pixelScale);
  m_text->drawEx(img, m_ox + x * m_scale, m_oy + y * m_scale, color, m_w, m_h, 1, sy, blur * m_scale, opacity);
}

void Canvas::textAt(const std::string& text, const TextStyle& style, float cx, float cy, Color color, float opacity, float scale,
                    float angle) {
  if (!m_text || text.empty()) return;
  TextStyle st = style;
  st.size = style.size * m_scale;
  st.letterSpacing = style.letterSpacing * m_scale;
  st.stroke = style.stroke * m_scale;
  st.maxWidth = style.maxWidth * m_scale;
  const TextImage& img = m_text->get(text, st, m_pixelScale);
  const float x = m_ox + cx * m_scale - img.w / 2, y = m_oy + cy * m_scale - img.h / 2;
  m_text->drawEx(img, x, y, color, m_w, m_h, scale, scale, 0, opacity, angle);
}

}  // namespace undershell
