// SPDX-License-Identifier: GPL-3.0-or-later
// The text-with-effects engine (see textfx.hpp).
#include "textfx.hpp"

#include <EGL/egl.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace undershell {

namespace {
const char* kVert = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
uniform vec4 u_rect;     // the quad, logical px
uniform vec2 u_surface;
uniform vec3 u_turn;     // the centre it turns about and the angle
uniform vec4 u_rest;     // the quad where it rests, in shares of the text's ink box
out vec2 v_uv;
out vec2 v_block;
void main() {
    v_uv = a_pos;
    v_block = u_rest.xy + a_pos * u_rest.zw;
    vec2 q = u_rect.xy + a_pos * u_rect.zw - u_turn.xy;
    float c = cos(u_turn.z), s = sin(u_turn.z);
    vec2 p = (u_turn.xy + vec2(c * q.x - s * q.y, s * q.x + c * q.y)) / u_surface;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
)";

const char* kFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
in vec2 v_block;
out vec4 fragColor;
uniform sampler2D u_tex;
uniform vec2 u_quad;      // the quad, logical px
uniform float u_scale;    // device px per logical px
uniform int u_pass;       // 0: shadow, depth, glow, outline   1: the fill
uniform int u_fill;       // 0 solid 1 gradient 2 rainbow 3 none
uniform vec4 u_c1, u_c2;
uniform vec2 u_dir;       // gradient direction
uniform float u_hue;
uniform float u_outW;     // logical px
uniform vec4 u_outC;
uniform float u_glowR, u_glowA;
uniform vec4 u_glowC;
uniform vec2 u_shOff;
uniform float u_shR;
uniform vec4 u_shC;
uniform vec2 u_depth;     // the extrusion's far end, logical px
uniform vec4 u_depthC;
uniform float u_chroma;
uniform float u_alpha;

const float TAU = 6.2831853;

// the glyphs' alpha; nothing outside the texture
float A(vec2 uv, float lod) {
    vec2 c = clamp(uv, 0.0, 1.0);
    float a = textureLod(u_tex, c, lod).r;
    return c == uv ? a : 0.0;
}
vec2 toUv(vec2 px) { return px / u_quad; }
// a soft copy of the alpha, about r logical px wide
float soft(vec2 uv, float r) {
    float lod = log2(max(r * u_scale * 0.5, 1.0));
    vec2 d = toUv(vec2(r * 0.5));
    return A(uv, lod) * 0.4 + (A(uv + d, lod) + A(uv - d, lod) + A(uv + vec2(d.x, -d.y), lod) + A(uv + vec2(-d.x, d.y), lod)) * 0.15;
}
// the alpha grown by w logical px: the most of it on rings round the pixel
float grow(vec2 uv, float w) {
    float lod = w * u_scale > 10.0 ? 1.0 : 0.0;
    float m = A(uv, 0.0);
    for (int i = 0; i < 24; i++) {
        float a = float(i) * TAU / 24.0;
        m = max(m, A(uv + toUv(vec2(cos(a), sin(a)) * w), lod));
    }
    for (int i = 0; i < 12; i++) {
        float a = (float(i) + 0.5) * TAU / 12.0;
        m = max(m, A(uv + toUv(vec2(cos(a), sin(a)) * w * 0.55), 0.0));
    }
    return clamp(m * 1.15, 0.0, 1.0);
}
vec4 over(vec4 top, vec4 under) { return top + under * (1.0 - top.a); }
vec3 hsv(float h, float s, float v) {
    vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0, 1.0) / 3.0) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return v * mix(vec3(1.0), k, s);
}

void main() {
    vec2 uv = v_uv;
    vec4 col = vec4(0.0);
    if (u_pass == 0) {
        if (u_shC.a > 0.0) {
            float a = soft(uv - toUv(u_shOff), u_shR) * u_shC.a;
            col = vec4(u_shC.rgb * a, a);
        }
        if (u_depthC.a > 0.0) {
            // the alpha stepped back along the depth, the far steps darker
            int n = int(clamp(length(u_depth) * u_scale / 1.2, 2.0, 40.0));
            for (int k = n; k >= 1; k--) {
                float t = float(k) / float(n);
                float a = A(uv - toUv(u_depth * t), 0.0) * u_depthC.a;
                col = over(vec4(u_depthC.rgb * (1.0 - 0.45 * t) * a, a), col);
            }
        }
        if (u_glowA > 0.0) {
            float g = clamp((soft(uv, u_glowR) * 0.6 + soft(uv, u_glowR * 2.4) * 0.55) * u_glowA * 1.5, 0.0, 1.0);
            col = over(vec4(u_glowC.rgb * g, g), col);
        }
        if (u_outW > 0.0) {
            float o = grow(uv, u_outW);
            if (u_fill == 3) o *= 1.0 - A(uv, 0.0);  // hollow: only the ring
            o *= u_outC.a;
            col = over(vec4(u_outC.rgb * o, o), col);
        }
    } else if (u_fill != 3) {
        vec3 c = u_c1.rgb;
        if (u_fill == 1) c = mix(u_c1.rgb, u_c2.rgb, clamp(dot(v_block - 0.5, u_dir) + 0.5, 0.0, 1.0));
        else if (u_fill == 2) c = hsv(fract(v_block.x * 0.85 + v_block.y * 0.15 - u_hue), 0.62, 1.0);
        float a = A(uv, 0.0);
        if (u_chroma > 0.0) {
            // the red and the cyan pulled apart
            float r = A(uv + toUv(vec2(u_chroma, 0.0)), 0.0), b = A(uv - toUv(vec2(u_chroma, 0.0)), 0.0);
            col = vec4(c.r * max(r, a * 0.3), c.g * a, c.b * max(b, a * 0.3), max(a, max(r, b)));
            col.rgb += vec3(r * (1.0 - a), 0.0, b * (1.0 - a)) * 0.8;
        } else {
            col = vec4(c * a, a);
        }
    }
    fragColor = col * u_alpha;
}
)";

}  // namespace

TextFx::TextFx() { m_text.setMipmaps(true); }

TextFx::~TextFx() {
  if (eglGetCurrentContext() != EGL_NO_CONTEXT) releaseGl();
}

void TextFx::releaseGl() {
  m_text.releaseGl();
  m_prog.destroy();
}

float TextFx::spill(const TextFxStyle& st) {
  const double out = st.outline * 0.09;
  const double glow = st.glow > 0 ? 0.05 + st.glow * 0.3 : 0.0;
  const double shadow = st.shadow > 0 ? 0.03 + st.shadow * 0.12 : 0.0;
  const double depth = st.extrude * 0.22;
  return static_cast<float>(out + std::max({glow, shadow, depth}) + 0.02);
}

TextStyle TextFx::textStyle(float size) const {
  TextStyle ts;
  ts.family = m_st.font.empty() ? "Inter Display" : m_st.font;
  ts.size = size;
  ts.weight = m_st.weight;
  ts.italic = m_st.italic;
  ts.letterSpacing = static_cast<float>(m_st.spacing * size);
  return ts;
}

float TextFx::fitSize(const std::string& text, const TextFxStyle& st, float w, float h, float slack) {
  TextStyle ts;
  ts.family = st.font.empty() ? "Inter Display" : st.font;
  ts.size = 100;
  ts.weight = st.weight;
  ts.italic = st.italic;
  ts.letterSpacing = static_cast<float>(st.spacing * 100);
  const auto ink = TextRenderer::measureInk(text, ts);
  if (ink.w <= 0 || ink.h <= 0) return 0;
  const float k = spill(st) + slack;
  // size * (ink / 100 + 2k) must fit the box on both axes
  return std::max(1.0F, std::min(w / (ink.w / 100 + 2 * k), h / (ink.h / 100 + 2 * k)));
}

void TextFx::layout(const std::string& text, const TextFxStyle& st, float w, float h, float scale, float slack, bool perLetter,
                    float size, int maxLines) {
  const std::string key = std::format("{}\x1f{}\x1f{}\x1f{}\x1f{:.3f}\x1f{}\x1f{:.1f}\x1f{:.1f}\x1f{:.3f}\x1f{:.3f}\x1f{}\x1f{:.2f}\x1f{}\x1f{}"
                                      "\x1f{:.3f}\x1f{:.3f}\x1f{:.3f}\x1f{:.3f}",
                                      text, st.font, st.weight, st.italic, st.spacing, st.align, w, h, scale, slack, perLetter, size,
                                      maxLines, st.fill, st.outline, st.glow, st.shadow, st.extrude);
  m_st = st;
  if (key == m_key) return;
  m_key = key;
  m_units.clear();
  m_scale = scale;
  m_size = 0;
  if (text.empty() || w < 2 || h < 2) return;
  const bool fit = size <= 0;
  if (fit) size = fitSize(text, st, w, h, slack);
  if (size <= 0) return;
  m_size = size;
  m_pad = (spill(st) + slack) * size;
  TextStyle ts = textStyle(size);
  ts.pad = m_pad;
  if (!fit) {  // wrapped to the box
    ts.maxWidth = std::max(10.0F, w - 2 * m_pad);
    ts.maxLines = std::max(1, maxLines);
  } else {
    // a width just wider than the text: nothing wraps, but lines align
    float lw = 0, lh = 0, base = 0;
    TextRenderer::measure(text, ts, lw, lh, base);
    ts.maxWidth = lw * 1.03F + 4 + std::abs(ts.letterSpacing) * 2;  // letter spacing at a line's end can tip it over
    ts.maxLines = 999;
  }
  ts.align = st.align;
  m_wholeStyle = ts;
  m_glyphStyle = textStyle(size);  // one grapheme: no wrapping
  m_glyphStyle.pad = m_pad;
  const auto ink = TextRenderer::measureInk(text, ts);
  // where the text's logical box goes so that its ink sits in the box
  float ox = (w - ink.w) / 2 - ink.x;
  if (st.align == 0) ox = m_pad - ink.x;
  if (st.align == 2) ox = w - m_pad - ink.w - ink.x;
  if (!fit) ox = m_pad + (st.align == 1 ? (ts.maxWidth - ink.boxW) / 2 : st.align == 2 ? ts.maxWidth - ink.boxW : 0.0F);
  const float oy = (h - ink.h) / 2 - ink.y;
  m_ink = {ox + ink.x, oy + ink.y, std::max(1.0F, ink.w), std::max(1.0F, ink.h)};
  if (!perLetter) {
    Unit u;
    u.text = text;
    u.x = ox;
    u.y = oy;
    u.w = ink.boxW;
    u.h = ink.boxH;
    m_units.push_back(std::move(u));
    m_units.back().line = -1;  // the whole text: drawn with the layout's wrapping
    return;
  }
  for (const auto& c : TextRenderer::clusters(text, ts)) {
    Unit u;
    u.text = c.text;
    u.x = ox + c.x;
    u.y = oy + c.y;
    u.w = c.w;
    u.h = c.h;
    u.line = c.line;
    u.blank = c.blank;
    m_units.push_back(std::move(u));
  }
}

void TextFx::bar(const DrawContext& ctx, float x, float y, float w, float h, Color c) {
  m_canvas.begin(ctx.w, ctx.h, ctx.scale, nullptr);
  m_canvas.roundRect(x, y, w, h, std::min(w, h) * 0.3F, c);
}

void TextFx::draw(const DrawContext& ctx, const std::vector<TextFxMotion>& motion, float hue, float opacity) {
  if (m_units.empty() || m_size <= 0) return;
  if (!m_prog.valid()) m_prog.create(kVert, kFrag, "textfx");
  const float S = m_size;
  glUseProgram(m_prog.id());
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(m_prog.uniform("u_tex"), 0);
  glUniform2f(m_prog.uniform("u_surface"), ctx.w, ctx.h);
  glUniform1f(m_prog.uniform("u_scale"), ctx.scale);
  glUniform1i(m_prog.uniform("u_fill"), m_st.fill);
  glUniform4f(m_prog.uniform("u_c1"), m_st.c1.r, m_st.c1.g, m_st.c1.b, 1);
  glUniform4f(m_prog.uniform("u_c2"), m_st.c2.r, m_st.c2.g, m_st.c2.b, 1);
  const double ga = m_st.angle * M_PI / 180;
  glUniform2f(m_prog.uniform("u_dir"), static_cast<float>(std::cos(ga)), static_cast<float>(std::sin(ga)));
  glUniform1f(m_prog.uniform("u_hue"), hue);
  const float outW = static_cast<float>(m_st.outline * 0.09) * S;
  glUniform1f(m_prog.uniform("u_outW"), m_st.outline > 0 ? std::max(outW, 0.8F) : 0.0F);
  glUniform4f(m_prog.uniform("u_outC"), m_st.outlineC.r, m_st.outlineC.g, m_st.outlineC.b, 1);
  const float glowR = static_cast<float>(0.03 + m_st.glow * 0.1) * S;
  glUniform1f(m_prog.uniform("u_glowR"), glowR);
  glUniform4f(m_prog.uniform("u_glowC"), m_st.glowC.r, m_st.glowC.g, m_st.glowC.b, 1);
  const float sh = static_cast<float>(0.02 + m_st.shadow * 0.07) * S;
  glUniform2f(m_prog.uniform("u_shOff"), sh * 0.5F, sh);
  glUniform1f(m_prog.uniform("u_shR"), sh * 1.2F + 1);
  glUniform4f(m_prog.uniform("u_shC"), m_st.shadowC.r, m_st.shadowC.g, m_st.shadowC.b,
              m_st.shadow > 0 ? static_cast<float>(0.45 + 0.4 * m_st.shadow) : 0.0F);
  const double da = m_st.extrudeAngle * M_PI / 180;
  const float dl = static_cast<float>(m_st.extrude * 0.2) * S;
  glUniform2f(m_prog.uniform("u_depth"), static_cast<float>(std::cos(da)) * dl, static_cast<float>(std::sin(da)) * dl);
  glUniform4f(m_prog.uniform("u_depthC"), m_st.extrudeC.r, m_st.extrudeC.g, m_st.extrudeC.b, m_st.extrude > 0 ? 1.0F : 0.0F);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  const bool whole = m_units.size() == 1 && m_units[0].line == -1;
  for (int pass = 0; pass < 2; ++pass) {
    glUniform1i(m_prog.uniform("u_pass"), pass);
    if (pass == 0 && m_st.outline <= 0 && m_st.glow <= 0 && m_st.shadow <= 0 && m_st.extrude <= 0) continue;
    for (size_t i = 0; i < m_units.size(); ++i) {
      const Unit& u = m_units[i];
      if (u.blank || u.text.empty()) continue;
      const TextFxMotion mo = i < motion.size() ? motion[i] : TextFxMotion{};
      if (!mo.shown || mo.alpha <= 0.003F) continue;
      const TextImage& img = m_text.get(u.text, whole ? m_wholeStyle : m_glyphStyle, ctx.scale);
      if (!img.texture) continue;
      glBindTexture(GL_TEXTURE_2D, img.texture);
      const float cx = u.x + img.w / 2, cy = u.y + img.h / 2;
      float qx = cx + (u.x + img.quadX - cx) * mo.scale + mo.dx, qy = cy + (u.y + img.quadY - cy) * mo.scale + mo.dy;
      if (mo.scale == 1 && mo.turn == 0 && mo.dx == 0 && mo.dy == 0) {
        qx = std::round(qx * ctx.scale) / ctx.scale;  // texels on device pixels
        qy = std::round(qy * ctx.scale) / ctx.scale;
      }
      glUniform4f(m_prog.uniform("u_rect"), qx, qy, img.quadW * mo.scale, img.quadH * mo.scale);
      glUniform3f(m_prog.uniform("u_turn"), cx + mo.dx, cy + mo.dy, mo.turn);
      glUniform4f(m_prog.uniform("u_rest"), (u.x + img.quadX - m_ink.x) / m_ink.w, (u.y + img.quadY - m_ink.y) / m_ink.h,
                  img.quadW / m_ink.w, img.quadH / m_ink.h);
      glUniform2f(m_prog.uniform("u_quad"), img.quadW, img.quadH);
      glUniform1f(m_prog.uniform("u_glowA"), m_st.glow > 0 ? static_cast<float>(m_st.glow) * mo.glow : 0.0F);
      glUniform1f(m_prog.uniform("u_chroma"), mo.chroma);
      glUniform1f(m_prog.uniform("u_alpha"), opacity * mo.alpha);
      drawUnitQuad();
    }
  }
  glDisable(GL_BLEND);
}

}  // namespace undershell
