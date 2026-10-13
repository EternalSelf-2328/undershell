// SPDX-License-Identifier: GPL-3.0-or-later
// The image widget: the picture laid in its box by one shader, which also
// cuts the corners or the shape, draws the sticker outline (the silhouette
// grown by sampling its alpha round each pixel) and the shadow (its alpha,
// blurred by reading a smaller mipmap, a little lower).
#include "image.hpp"

#include "common.hpp"
#include "m3shapes.hpp"

#include <EGL/egl.h>
#include <algorithm>
#include <cmath>

namespace undershell {

namespace {
template <typename T>
T get(const toml::table& t, std::string_view key, T def) {
  if (auto v = t[key].value<T>()) return *v;
  return def;
}

const char* kFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_tex;
uniform vec2 u_size;      // the box, logical px
uniform vec4 u_rect;      // where the picture's frame is, logical px (x, y, w, h)
uniform vec4 u_uv;        // the part of the texture in it (u0, v0, u1, v1)
uniform float u_px;       // one device pixel, logical px
uniform float u_corner;   // px
uniform int u_cut;        // 0 none, 1 a radial table
uniform float u_radii[64];
uniform float u_outline;  // px
uniform vec4 u_outlineC;
uniform float u_shadow;
uniform float u_lod;      // the shadow's blur, as a mipmap level
uniform float u_opacity;
uniform float u_mirror;

float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
// the frame's mask (corners or a cut shape): 1 inside
float frameMask(vec2 px) {
    vec2 c = u_rect.xy + u_rect.zw * 0.5, h = u_rect.zw * 0.5;
    vec2 p = px - c;
    float d;
    if (u_cut == 1) {
        float mn = min(h.x, h.y);
        vec2 q = p * vec2(mn / h.x, mn / h.y);
        float a = atan(q.y, q.x);
        if (a < 0.0) a += 6.2831853;
        float f = a / 6.2831853 * 64.0;
        int i0 = int(floor(f)) % 64, i1 = (i0 + 1) % 64;
        d = (length(q) - mix(u_radii[i0], u_radii[i1], fract(f)) * mn) * 0.9;
    } else {
        d = sdRoundBox(p, h, min(u_corner, min(h.x, h.y)));
    }
    return clamp(0.5 - d / u_px, 0.0, 1.0);
}
vec2 uvAt(vec2 px) {
    vec2 t = (px - u_rect.xy) / u_rect.zw;
    if (u_mirror > 0.5) t.x = 1.0 - t.x;
    return mix(u_uv.xy, u_uv.zw, t);
}
bool inTex(vec2 uv) { return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))); }
float alphaAt(vec2 px, float lod) {
    vec2 uv = uvAt(px);
    if (!inTex(uv)) return 0.0;
    return textureLod(u_tex, uv, lod).a * frameMask(px);
}

void main() {
    vec2 px = v_uv * u_size;
    vec2 uv = uvAt(px);
    vec4 tex = inTex(uv) ? texture(u_tex, uv) : vec4(0.0);
    float a = tex.a * frameMask(px);
    vec4 col = vec4(0.0);
    if (u_shadow > 0.0) {  // the silhouette blurred, a little lower
        float s = alphaAt(px - vec2(0.0, u_outline + 3.0 + u_lod * 2.0), u_lod);
        if (u_outline > 0.0) s = max(s, alphaAt(px - vec2(0.0, 3.0 + u_lod * 2.0), u_lod + 1.0));
        col = vec4(0.0, 0.0, 0.0, s * u_shadow * 0.6);
    }
    if (u_outline > 0.0) {  // the silhouette grown: the most alpha round this pixel
        // 24 directions at its width and 12 at half of it: round at corners and tips
        float grown = a;
        for (int k = 0; k < 24; ++k) {
            float an = float(k) * 0.2617994;
            vec2 o = vec2(cos(an), sin(an));
            grown = max(grown, alphaAt(px + o * u_outline, 1.0));  // a mipmap softer: no teeth at tips
            if ((k & 1) == 0) grown = max(grown, alphaAt(px + o * u_outline * 0.55, 0.0));
        }
        float oa = clamp(grown * 1.4 - 0.2, 0.0, 1.0) * u_outlineC.a;
        col = vec4(u_outlineC.rgb * oa, oa) + col * (1.0 - oa);
    }
    col = vec4(tex.rgb * a, a) + col * (1.0 - a);
    fragColor = col * u_opacity;
}
)";

// what shows before a picture is chosen: a dashed frame and a plus
const char* kHintFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 u_size;
void main() {
    vec2 p = v_uv * u_size, c = u_size * 0.5;
    vec2 q = abs(p - c) - (c - 6.0);
    float sd = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);  // to a frame 6 px in
    float along = q.y > q.x ? p.x : p.y;                        // along the nearer edge
    float border = (1.0 - smoothstep(0.8, 1.8, abs(sd))) * step(0.5, fract(along / 14.0)) * 0.6;
    float r = min(u_size.x, u_size.y) * 0.12;
    vec2 d = abs(p - c);
    float plus = ((d.x < 1.5 && d.y < r) || (d.y < 1.5 && d.x < r)) ? 0.75 : 0.0;
    float a = max(border, plus);
    fragColor = vec4(vec3(a), a) + vec4(0.0, 0.0, 0.0, sd < 0.0 ? 0.16 : 0.0) * (1.0 - a);
}
)";
}  // namespace

ImageConfig ImageConfig::fromTable(const toml::table& t) {
  ImageConfig c;
  c.file = get<std::string>(t, "file", "");
  c.fit = get<std::string>(t, "fit", c.fit);
  auto num = [&](const char* key, double def, double lo, double hi) {
    double v = def;
    if (auto d = t[key].value<double>()) v = *d;
    return std::clamp(v, lo, hi);
  };
  c.corner = num("corner", c.corner, 0, 1);
  c.cut = get<std::string>(t, "cut", c.cut);
  c.outline = num("outline", c.outline, 0, 40);
  c.outlineColor = get<std::string>(t, "outline_color", c.outlineColor);
  c.shadow = num("shadow", c.shadow, 0, 1);
  c.speed = num("speed", c.speed, 0, 4);
  c.react = get<std::string>(t, "react", c.react);
  c.mirror = get<bool>(t, "mirror", false);
  c.opacity = num("opacity", c.opacity, 0, 1);
  return c;
}

ImageWidget::~ImageWidget() {
  if (m_src && eglGetCurrentContext() != EGL_NO_CONTEXT) m_src->releaseGl();
}

void ImageWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) { configure(ImageConfig::fromTable(cfg.options), noct); }

void ImageWidget::configure(const ImageConfig& cfg, const NoctaliaState& noct) {
  m_cfg = cfg;
  m_outlineC = noct.color(cfg.outlineColor, Color{1, 1, 1, 1});
  m_radii.clear();
  if (cfg.cut == "circle") m_radii.assign(64, 1.0F);
  else if (cfg.cut != "none" && !cfg.cut.empty()) {
    m_radii = m3ShapeRadii(cfg.cut, 64);
    float most = 0;
    for (float r : m_radii) most = std::max(most, r);
    if (most > 0)
      for (float& r : m_radii) r /= most;
  }
  if (expandHome(cfg.file) != m_openPath && m_src) {  // another picture: decoded again at the next draw
    if (eglGetCurrentContext() != EGL_NO_CONTEXT) m_src->releaseGl();
    m_src.reset();
    m_openPath.clear();
  }
}

void ImageWidget::tick(const TickContext& ctx) {
  // its own clock: a slow GIF wakes once a frame, longer apart than the App's dt allows
  const double dt = m_lastNow < 0 ? 0 : std::clamp(ctx.now - m_lastNow, 0.0, 2.0);
  m_lastNow = ctx.now;
  if (m_src) m_src->advance(dt, m_cfg.speed);
  m_beat *= std::exp(-dt * 6);
  if (m_cfg.react != "none") {
    if (ctx.audio.kicks != m_kicks) {
      m_kicks = ctx.audio.kicks;
      m_beat = std::max(m_beat, std::clamp(0.55 + ctx.audio.kickStrength * 0.45, 0.55, 1.0));
    } else if (!ctx.audio.onsets && !ctx.audio.silent) {
      m_beat = std::max(m_beat, std::clamp(ctx.audio.energy * 1.4 - 0.3, 0.0, 1.0));
    }
  }
}

bool ImageWidget::animating(const TickContext&) const { return m_beat > 0.004; }

double ImageWidget::nextWakeup(double now) const {
  if (m_cfg.file.empty()) return 1e18;
  if (!m_src) return now + 0.03;  // opened at the next draw
  const double in = m_src->nextFrameIn(m_cfg.speed);
  return in > 1e8 ? 1e18 : now + std::max(0.004, in);
}

void ImageWidget::draw(const DrawContext& ctx) {
  const float W = ctx.w, H = ctx.h;
  if (m_cfg.file.empty()) {
    if (!m_hint.valid()) m_hint.create(kQuadVertexShader, kHintFrag, "image-hint");
    glUseProgram(m_hint.id());
    glUniform2f(m_hint.uniform("u_size"), W, H);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawUnitQuad();
    glDisable(GL_BLEND);
    return;
  }
  const int devW = std::max(16, static_cast<int>(std::lround(W * ctx.scale)));
  const int devH = std::max(16, static_cast<int>(std::lround(H * ctx.scale)));
  // decoded to the box's size: again when it grows well past it
  const std::string path = expandHome(m_cfg.file);
  if (!m_src || (m_src->still() && (devW > m_src->wantWidth() * 1.4 || devH > m_src->wantHeight() * 1.4))) {
    if (m_src && eglGetCurrentContext() != EGL_NO_CONTEXT) m_src->releaseGl();
    m_src = std::make_unique<VideoSource>();
    m_src->open(path, devW, devH, true);
    m_openPath = path;
    m_src->advance(0, m_cfg.speed);
  }
  if (!m_src->texture() || m_src->width() <= 0) return;  // still loading
  if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kFrag, "image");
  const float tw = static_cast<float>(m_src->width()), th = static_cast<float>(m_src->height());
  // room for the outline and the shadow, then the frame by the fit
  const float pad = static_cast<float>(m_cfg.outline + (m_cfg.shadow > 0 ? 4 + 6 * m_cfg.shadow : 0) + 1);
  float bw = std::max(4.0F, W - 2 * pad), bh = std::max(4.0F, H - 2 * pad);
  if (m_cfg.react == "pulse") {
    const float k = static_cast<float>(0.92 + 0.08 * m_beat);
    bw *= k, bh *= k;
  }
  float rx = (W - bw) / 2, ry = (H - bh) / 2, rw = bw, rh = bh;
  float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  const float ta = tw / th, ba = bw / bh;
  if (m_cfg.fit == "fit") {
    if (ta > ba) rh = bw / ta, ry = (H - rh) / 2;
    else rw = bh * ta, rx = (W - rw) / 2;
  } else if (m_cfg.fit == "fill") {
    if (ta > ba) {
      const float f = ba / ta;
      u0 = (1 - f) / 2, u1 = 1 - u0;
    } else {
      const float f = ta / ba;
      v0 = (1 - f) / 2, v1 = 1 - v0;
    }
  }
  if (m_cfg.react == "bounce") ry -= static_cast<float>(m_beat * std::min(H, W) * 0.08);
  glUseProgram(m_prog.id());
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_src->texture());
  glUniform1i(m_prog.uniform("u_tex"), 0);
  glUniform2f(m_prog.uniform("u_size"), W, H);
  glUniform4f(m_prog.uniform("u_rect"), rx, ry, rw, rh);
  glUniform4f(m_prog.uniform("u_uv"), u0, v0, u1, v1);
  glUniform1f(m_prog.uniform("u_px"), 1.0F / std::max(0.25F, ctx.scale));
  glUniform1f(m_prog.uniform("u_corner"), static_cast<float>(m_cfg.corner * std::min(rw, rh) / 2));
  glUniform1i(m_prog.uniform("u_cut"), m_radii.empty() ? 0 : 1);
  if (!m_radii.empty()) glUniform1fv(m_prog.uniform("u_radii"), 64, m_radii.data());
  glUniform1f(m_prog.uniform("u_outline"), static_cast<float>(m_cfg.outline));
  glUniform4f(m_prog.uniform("u_outlineC"), m_outlineC.r, m_outlineC.g, m_outlineC.b, 1);
  glUniform1f(m_prog.uniform("u_shadow"), static_cast<float>(m_cfg.shadow));
  // the shadow's softness: a mipmap a few texels across per logical px of blur
  const float texPerPx = tw / std::max(1.0F, rw * (u1 - u0 > 0 ? 1.0F / (u1 - u0) : 1.0F));
  glUniform1f(m_prog.uniform("u_lod"), static_cast<float>(std::log2(std::max(1.0F, texPerPx * (2 + 6 * static_cast<float>(m_cfg.shadow))))));
  glUniform1f(m_prog.uniform("u_opacity"), static_cast<float>(m_cfg.opacity));
  glUniform1f(m_prog.uniform("u_mirror"), m_cfg.mirror ? 1.0F : 0.0F);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  drawUnitQuad();
  glDisable(GL_BLEND);
}

}  // namespace undershell
