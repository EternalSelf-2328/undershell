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
uniform sampler2D u_field;
uniform float u_useField;
uniform float u_level;
uniform float u_feather;
uniform vec4 u_tint;
uniform vec3 u_view;      // centre (output px) and zoom of the brush view
uniform sampler2D u_wall;
uniform float u_wallMode;

float coverageAt(vec2 uv) {
    if (u_useField > 0.5) {
        // depth_helper.py: smoothstep(depth, threshold - feather/2, threshold + feather/2)
        float d = texture(u_field, uv).r;
        float lo = u_level - u_feather * 0.5, hi = u_level + u_feather * 0.5;
        if (hi <= lo) return d >= hi ? 1.0 : 0.0;
        return smoothstep(lo, hi, d);
    }
    return texture(u_mask, uv).r;
}

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
    if (u_view.z > 1.0001) outputPixel = u_view.xy + (outputPixel - u_outputSize * 0.5) / u_view.z;
    vec2 maskUV = calculateWallpaperUV(outputPixel / u_outputSize, u_imageSize.x, u_imageSize.y);
    if (u_wallMode > 0.5) {
        bool inside = (u_fillMode > 3.5 && u_fillMode < 4.5) || !(maskUV.x < 0.0 || maskUV.x > 1.0 || maskUV.y < 0.0 || maskUV.y > 1.0);
        vec2 uv = (u_fillMode > 3.5 && u_fillMode < 4.5) ? fract(maskUV) : maskUV;
        fragColor = vec4(inside ? texture(u_wall, uv).rgb : vec3(0.0), 1.0);
        return;
    }
    float coverage = 0.0;
    if (u_fillMode > 3.5 && u_fillMode < 4.5) {
        coverage = coverageAt(fract(maskUV));
    } else if (!(maskUV.x < 0.0 || maskUV.x > 1.0 || maskUV.y < 0.0 || maskUV.y > 1.0)) {
        coverage = coverageAt(maskUV);
    }
    if (u_tint.a > 0.0) {
        fragColor = vec4(u_tint.rgb, 1.0) * u_tint.a * coverage;  // premultiplied preview
        return;
    }
    fragColor = vec4(0.0, 0.0, 0.0, coverage);
}
)";

void MaskPass::setup(const MaskParams& p) {
  if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kMaskFrag, "mask");
  glUseProgram(m_prog.id());
  glUniform2f(m_prog.uniform("u_surfaceSize"), p.surfaceW, p.surfaceH);
  glUniform2f(m_prog.uniform("u_surfaceOffset"), p.offsetX, p.offsetY);
  glUniform2f(m_prog.uniform("u_outputSize"), p.outputW, p.outputH);
  glUniform2f(m_prog.uniform("u_imageSize"), p.imageW, p.imageH);
  glUniform1f(m_prog.uniform("u_fillMode"), static_cast<float>(p.fillMode));
  glUniform3f(m_prog.uniform("u_view"), p.viewX, p.viewY, p.zoom);
  glUniform1f(m_prog.uniform("u_level"), p.level);
  glUniform1f(m_prog.uniform("u_feather"), p.feather);
  glUniform1i(m_prog.uniform("u_mask"), 0);
  glUniform1i(m_prog.uniform("u_field"), 0);
  glUniform1i(m_prog.uniform("u_wall"), 1);
  glUniform1f(m_prog.uniform("u_wallMode"), 0.0F);
  glUniform4f(m_prog.uniform("u_tint"), 0, 0, 0, 0);
}

void MaskPass::draw(const MaskParams& p) {
  const bool useField = p.field && p.level > 0;
  if ((!p.texture && !useField) || p.surfaceW <= 0 || p.outputW <= 0 || p.imageW <= 0) return;
  setup(p);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, useField ? p.field : p.texture);
  glUniform1f(m_prog.uniform("u_useField"), useField ? 1.0F : 0.0F);
  // DestinationOut on premultiplied colour: dst *= (1 - coverage)
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
  drawUnitQuad();
  glDisable(GL_BLEND);
}

void MaskPass::drawTint(const MaskParams& p, Color tint) {
  if (!p.field || p.surfaceW <= 0 || p.outputW <= 0 || p.imageW <= 0) return;
  setup(p);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, p.field);
  glUniform1f(m_prog.uniform("u_useField"), 1.0F);
  glUniform4f(m_prog.uniform("u_tint"), tint.r, tint.g, tint.b, tint.a);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  drawUnitQuad();
  glDisable(GL_BLEND);
}

void MaskPass::drawWallpaper(const MaskParams& p, GLuint wallpaper) {
  if (!wallpaper || p.surfaceW <= 0 || p.outputW <= 0 || p.imageW <= 0) return;
  setup(p);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, wallpaper);
  glActiveTexture(GL_TEXTURE0);
  glUniform1f(m_prog.uniform("u_useField"), 0.0F);
  glUniform1f(m_prog.uniform("u_wallMode"), 1.0F);
  glDisable(GL_BLEND);  // opaque: it stands in for the real wallpaper
  drawUnitQuad();
}

static const char* kOverlayFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 u_size;
uniform vec4 u_accent;
uniform vec4 u_rects[16];
uniform float u_angles[16];
uniform int u_count;
uniform int u_handle;
uniform float u_handleGap;
uniform float u_handleR;
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
        // into the rect's own frame, so turned widgets get turned outlines
        float an = radians(u_angles[i]);
        vec2 q = px - c;
        vec2 p = vec2(q.x * cos(an) + q.y * sin(an), -q.x * sin(an) + q.y * cos(an));
        float d = roundBox(p, r.zw * 0.5 - 1.5, 10.0);
        bool hot = i == u_hover || i == u_active || i == u_selected;
        float border = 1.0 - smoothstep(0.0, 1.2, abs(d) - (hot ? 1.3 : 0.7));
        float inside = 1.0 - smoothstep(-0.5, 0.5, d);
        vec2 g = r.zw * 0.5 - p;
        float grip = 0.0;
        if (g.x >= 0.0 && g.y >= 0.0 && g.x < 24.0 && g.y < 24.0) {
            float diag = g.x + g.y;
            for (int k = 0; k < 3; k++)
                grip = max(grip, 1.0 - smoothstep(0.6, 1.6, abs(diag - (8.0 + float(k) * 7.0))));
        }
        float wa = inside * (i == u_active ? 0.16 : (i == u_selected ? 0.10 : (hot ? 0.07 : 0.04)))
                 + border * (i == u_selected ? 1.0 : (hot ? 0.85 : 0.5)) + grip * 0.95;
        if (i == u_handle) {
            // rotation knob on a short stem above the top edge
            vec2 k = vec2(0.0, -r.w * 0.5 - u_handleGap);
            float kd = length(p - k);
            float ring = 1.0 - smoothstep(0.0, 1.2, abs(kd - u_handleR) - 0.9);
            float dot_ = 1.0 - smoothstep(-0.6, 0.6, kd - u_handleR + 3.0);
            float stem = (p.y < -r.w * 0.5 && p.y > k.y + u_handleR)
                       ? 1.0 - smoothstep(0.0, 1.0, abs(p.x) - 0.6) : 0.0;
            wa += max(ring, max(dot_ * 0.9, stem * 0.8));
        }
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
                       Color accent, float grid, const std::vector<float>& vguides, const std::vector<float>& hguides,
                       int handle) {
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
  std::vector<float> angles;
  for (int i = 0; i < n; ++i) angles.push_back(rects[static_cast<size_t>(i)].angle);
  if (n > 0) {
    glUniform4fv(m_prog.uniform("u_rects"), n, packed.data());
    glUniform1fv(m_prog.uniform("u_angles"), n, angles.data());
  }
  glUniform1i(m_prog.uniform("u_handle"), handle < n ? handle : -1);
  glUniform1f(m_prog.uniform("u_handleGap"), kHandleGap);
  glUniform1f(m_prog.uniform("u_handleR"), kHandleR);
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

static const char* kBlitVert = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
uniform vec2 u_surface;
uniform vec2 u_centre;
uniform vec2 u_box;
uniform float u_angle;
out vec2 v_local;
void main() {
    // one pixel of margin all round, for the edge to fade in
    v_local = a_pos * (u_box + 2.0) - 1.0;
    vec2 q = v_local - u_box * 0.5;
    vec2 p = u_centre + vec2(q.x * cos(u_angle) - q.y * sin(u_angle), q.x * sin(u_angle) + q.y * cos(u_angle));
    p /= u_surface;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
)";

static const char* kBlitFrag = R"(#version 300 es
precision highp float;
in vec2 v_local;
out vec4 fragColor;
uniform sampler2D u_tex;
uniform vec2 u_box;
void main() {
    vec2 uv = v_local / u_box;
    // the off-screen target is stored bottom-up
    vec4 c = texture(u_tex, vec2(uv.x, 1.0 - uv.y));
    float edge = min(min(v_local.x, u_box.x - v_local.x), min(v_local.y, u_box.y - v_local.y));
    fragColor = c * clamp(edge + 0.5, 0.0, 1.0);
}
)";

void RotatedBlit::draw(GLuint texture, float surfaceW, float surfaceH, float centreX, float centreY, float boxW,
                       float boxH, float degrees) {
  if (!texture || surfaceW <= 0 || surfaceH <= 0) return;
  if (!m_prog.valid()) m_prog.create(kBlitVert, kBlitFrag, "rotated-blit");
  glUseProgram(m_prog.id());
  glUniform2f(m_prog.uniform("u_surface"), surfaceW, surfaceH);
  glUniform2f(m_prog.uniform("u_centre"), centreX, centreY);
  glUniform2f(m_prog.uniform("u_box"), boxW, boxH);
  glUniform1f(m_prog.uniform("u_angle"), degrees * 3.14159265F / 180.0F);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glUniform1i(m_prog.uniform("u_tex"), 0);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  drawUnitQuad();
  glDisable(GL_BLEND);
}

}  // namespace undershell
