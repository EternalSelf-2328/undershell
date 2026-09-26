// SPDX-License-Identifier: GPL-3.0-or-later
#include "overlay.hpp"

namespace undershell {

// calculateWallpaperUV: from Noctalia src/render/programs/wallpaper_sampling_glsl.h (MIT)
static const char* kMaskFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_mask;
uniform vec2 u_surfaceSize;
uniform vec2 u_surfaceOffset;
uniform vec2 u_outputSize;
uniform vec2 u_imageSize;
uniform float u_fillMode;

vec2 calculateWallpaperUV(vec2 uv, float imgWidth, float imgHeight) {
    float sw = u_outputSize.x, sh = u_outputSize.y;
    vec2 t = uv;
    if (u_fillMode < 0.5) {
        vec2 screenPixel = uv * vec2(sw, sh);
        vec2 imageOffset = (vec2(sw, sh) - vec2(imgWidth, imgHeight)) * 0.5;
        t = (screenPixel - imageOffset) / vec2(imgWidth, imgHeight);
    } else if (u_fillMode < 1.5 || u_fillMode > 4.5) {
        float scale = max(sw / imgWidth, sh / imgHeight);
        vec2 scaled = vec2(imgWidth, imgHeight) * scale;
        vec2 offset = (scaled - vec2(sw, sh)) / scaled;
        t = uv * (vec2(1.0) - offset) + offset * 0.5;
    } else if (u_fillMode < 2.5) {
        float scale = min(sw / imgWidth, sh / imgHeight);
        vec2 scaled = vec2(imgWidth, imgHeight) * scale;
        vec2 offset = (vec2(sw, sh) - scaled) * 0.5;
        t = ((uv * vec2(sw, sh) - offset) / scale) / vec2(imgWidth, imgHeight);
    } else if (u_fillMode < 3.5) {
        // stretch: output UV unchanged
    } else {
        t = uv * vec2(sw, sh) / vec2(imgWidth, imgHeight);
    }
    return t;
}

void main() {
    vec2 outputPixel = u_surfaceOffset + v_uv * u_surfaceSize;
    vec2 maskUV = calculateWallpaperUV(outputPixel / u_outputSize, u_imageSize.x, u_imageSize.y);
    float coverage = 0.0;
    if (u_fillMode > 3.5 && u_fillMode < 4.5) {
        coverage = texture(u_mask, fract(maskUV)).r;
    } else if (!(maskUV.x < 0.0 || maskUV.x > 1.0 || maskUV.y < 0.0 || maskUV.y > 1.0)) {
        coverage = texture(u_mask, maskUV).r;
    }
    fragColor = vec4(0.0, 0.0, 0.0, coverage);
}
)";

void MaskPass::draw(const MaskParams& p) {
  if (!p.texture || p.surfaceW <= 0 || p.outputW <= 0 || p.imageW <= 0) return;
  if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kMaskFrag, "mask");
  glUseProgram(m_prog.id());
  glUniform2f(m_prog.uniform("u_surfaceSize"), p.surfaceW, p.surfaceH);
  glUniform2f(m_prog.uniform("u_surfaceOffset"), p.offsetX, p.offsetY);
  glUniform2f(m_prog.uniform("u_outputSize"), p.outputW, p.outputH);
  glUniform2f(m_prog.uniform("u_imageSize"), p.imageW, p.imageH);
  glUniform1f(m_prog.uniform("u_fillMode"), static_cast<float>(p.fillMode));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, p.texture);
  glUniform1i(m_prog.uniform("u_mask"), 0);
  // DestinationOut on premultiplied colour: dst *= (1 - coverage)
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
  drawUnitQuad();
  glDisable(GL_BLEND);
}

static const char* kOverlayFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 u_size;
uniform vec4 u_accent;
uniform vec4 u_rects[16];
uniform int u_count;
uniform int u_hover;
uniform int u_active;
uniform int u_selected;
uniform float u_grid;
uniform float u_vg[4];
uniform float u_hg[4];
uniform int u_nvg;
uniform int u_nhg;

float roundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec2 px = v_uv * u_size;
    float a = 0.0;
    // faint snapping grid, a stronger line every 4 cells
    if (u_grid > 1.0) {
        vec2 g = mod(px, u_grid);
        vec2 G = mod(px, u_grid * 4.0);
        float fine = (g.x < 1.0 || g.y < 1.0) ? 0.035 : 0.0;
        float major = (G.x < 1.0 || G.y < 1.0) ? 0.07 : 0.0;
        a = max(fine, major);
    }
    vec3 col = vec3(1.0);
    for (int i = 0; i < 16; i++) {
        if (i >= u_count) break;
        vec4 r = u_rects[i];
        vec2 c = r.xy + r.zw * 0.5;
        float d = roundBox(px - c, r.zw * 0.5 - 1.5, 10.0);
        bool hot = i == u_hover || i == u_active || i == u_selected;
        float border = 1.0 - smoothstep(0.0, 1.2, abs(d) - (hot ? 1.3 : 0.7));
        float inside = 1.0 - smoothstep(-0.5, 0.5, d);
        vec2 g = (r.xy + r.zw) - px;
        float grip = 0.0;
        if (g.x >= 0.0 && g.y >= 0.0 && g.x < 24.0 && g.y < 24.0) {
            float diag = g.x + g.y;
            for (int k = 0; k < 3; k++)
                grip = max(grip, 1.0 - smoothstep(0.6, 1.6, abs(diag - (8.0 + float(k) * 7.0))));
        }
        float wa = inside * (i == u_active ? 0.16 : (i == u_selected ? 0.10 : (hot ? 0.07 : 0.04)))
                 + border * (i == u_selected ? 1.0 : (hot ? 0.85 : 0.5)) + grip * 0.95;
        if (wa > 0.0) {
            col = mix(col, u_accent.rgb, clamp(wa * 2.0, 0.0, 1.0));
            a = max(a, clamp(wa, 0.0, 1.0));
        }
    }
    // alignment guides: crisp accent lines across the whole output
    for (int i = 0; i < 4; i++) {
        if (i < u_nvg && abs(px.x - u_vg[i]) < 0.75) { col = u_accent.rgb; a = 0.9; }
        if (i < u_nhg && abs(px.y - u_hg[i]) < 0.75) { col = u_accent.rgb; a = 0.9; }
    }
    fragColor = vec4(col * a, a);
}
)";

void OverlayPass::draw(float w, float h, const std::vector<EditRect>& rects, int hover, int active, int selected,
                       Color accent, float grid, const std::vector<float>& vguides, const std::vector<float>& hguides) {
  if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kOverlayFrag, "overlay");
  glUseProgram(m_prog.id());
  glUniform2f(m_prog.uniform("u_size"), w, h);
  glUniform4f(m_prog.uniform("u_accent"), accent.r, accent.g, accent.b, 1.0F);
  std::vector<float> packed;
  const int n = std::min<int>(kMaxRects, static_cast<int>(rects.size()));
  for (int i = 0; i < n; ++i) {
    packed.insert(packed.end(), {rects[static_cast<size_t>(i)].x, rects[static_cast<size_t>(i)].y,
                                 rects[static_cast<size_t>(i)].w, rects[static_cast<size_t>(i)].h});
  }
  if (n > 0) glUniform4fv(m_prog.uniform("u_rects"), n, packed.data());
  glUniform1i(m_prog.uniform("u_count"), n);
  glUniform1i(m_prog.uniform("u_hover"), hover);
  glUniform1i(m_prog.uniform("u_active"), active);
  glUniform1f(m_prog.uniform("u_grid"), grid);
  glUniform1i(m_prog.uniform("u_selected"), selected);
  const int nv = std::min<int>(kMaxGuides, static_cast<int>(vguides.size()));
  const int nh = std::min<int>(kMaxGuides, static_cast<int>(hguides.size()));
  if (nv) glUniform1fv(m_prog.uniform("u_vg"), nv, vguides.data());
  if (nh) glUniform1fv(m_prog.uniform("u_hg"), nh, hguides.data());
  glUniform1i(m_prog.uniform("u_nvg"), nv);
  glUniform1i(m_prog.uniform("u_nhg"), nh);
  glDisable(GL_BLEND);
  drawUnitQuad();
}

static const char* kPillVert = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
uniform vec4 u_rect;
uniform vec2 u_surface;
out vec2 v_local;
void main() {
    v_local = a_pos * u_rect.zw;
    vec2 p = (u_rect.xy + v_local) / u_surface;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
)";

static const char* kPillFrag = R"(#version 300 es
precision highp float;
in vec2 v_local;
out vec4 fragColor;
uniform vec4 u_rect;
uniform vec4 u_color;
uniform float u_radius;
void main() {
    vec2 hs = u_rect.zw * 0.5;
    vec2 q = abs(v_local - hs) - hs + u_radius;
    float d = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - u_radius;
    float a = (1.0 - smoothstep(-0.5, 0.5, d)) * u_color.a;
    fragColor = vec4(u_color.rgb * a, a);
}
)";

void OverlayPass::drawPill(float x, float y, float w, float h, float radius, Color color, float surfaceW, float surfaceH) {
  if (!m_pill.valid()) m_pill.create(kPillVert, kPillFrag, "pill");
  glUseProgram(m_pill.id());
  glUniform4f(m_pill.uniform("u_rect"), x, y, w, h);
  glUniform2f(m_pill.uniform("u_surface"), surfaceW, surfaceH);
  glUniform4f(m_pill.uniform("u_color"), color.r, color.g, color.b, color.a);
  glUniform1f(m_pill.uniform("u_radius"), std::min(radius, std::min(w, h) / 2));
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  drawUnitQuad();
  glDisable(GL_BLEND);
}

}  // namespace undershell
