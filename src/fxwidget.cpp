// SPDX-License-Identifier: GPL-3.0-or-later
// Animated effects on any widget (see fxwidget.hpp).
#include "fxwidget.hpp"

namespace undershell {

namespace {

const char* kHead = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_lights;  // the light effects, drawn smaller (lights pass)
uniform int u_hasLights;
uniform vec4 u_layerPose; // xy offset, brightness, opacity
)";

// the widget's picture is premultiplied: the effects see its colour, and
// colour-only effects keep coverage; pixel effects can resample it too
const char* kBody = R"(
void main() {
    screenPx = v_uv * u_screen;
    vec2 uv = v_uv - u_layerPose.xy;
//@WARPS
    vec4 t = any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) ? vec4(0.0) : texture(u_tex, vec2(uv.x, 1.0 - uv.y));
    float coverage = t.a;
    vec3 c = t.a > 0.0 ? t.rgb / t.a : vec3(0.0);
//@COLOURS
    if (u_hasLights == 1) {
        vec4 L = texture(u_lights, vec2(v_uv.x, 1.0 - v_uv.y));
        c = c * (1.0 - L.a) + L.rgb;
    }
    fragColor = vec4(clamp(c * u_layerPose.z, 0.0, 1.0) * coverage, coverage) * u_layerPose.w;
}
)";

const char* kPlainLayer = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_tex;
uniform vec4 u_layerPose;
void main() {
    vec2 uv = v_uv - u_layerPose.xy;
    vec4 c = any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) ? vec4(0.0) : texture(u_tex, vec2(uv.x, 1.0-uv.y));
    fragColor = vec4(min(c.rgb * u_layerPose.z, vec3(c.a)), c.a) * u_layerPose.w;
}
)";

const char* kLights = R"(
void main() {
    screenPx = v_uv * u_screen;
    vec2 uv = v_uv;
    vec4 L = vec4(0.0);
//@LIGHTS
    fragColor = L;
}
)";

}  // namespace

FxWidget::~FxWidget() {
  if (m_tex) glDeleteTextures(1, &m_tex);
  if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
}

void FxWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  m_inner->configure(cfg, noct);
  m_editable = m_inner->editable();
  m_fx.configure(cfg.options, noct);
  m_layerMotion.configure(m_inner->fullscreen() || m_inner->overlay() ? toml::table{} : cfg.options);
}

void FxWidget::tick(const TickContext& ctx) {
  m_inner->tick(ctx);
  if (!m_fx.empty()) m_fx.tick(ctx);
  m_layerMotion.tick(ctx.now, ctx.editing);
}

std::vector<std::string> FxWidget::watchedFiles() const {
  std::vector<std::string> files = m_inner->watchedFiles();
  if (!m_fx.empty()) files.push_back(userEffectsDir() + "/");
  return files;
}

void FxWidget::filesChanged() {
  m_inner->filesChanged();
  m_fx.refresh();
}

std::vector<Rect> FxWidget::inputRects() const {
  if (m_layerMotion.pose().opacity < 0.01F) return {};
  auto parts = m_inner->inputRects();
  for (auto& rect : parts) {
    rect.x += m_shiftX;
    rect.y += m_shiftY;
  }
  return parts;
}

bool FxWidget::onPointer(const PointerEvent& ev) {
  PointerEvent translated = ev;
  translated.x -= m_shiftX;
  translated.y -= m_shiftY;
  return m_inner->onPointer(translated);
}

void FxWidget::draw(const DrawContext& ctx) {
  const LayerPose pose = m_layerMotion.pose();
  m_shiftX = pose.x * ctx.w;
  m_shiftY = pose.y * ctx.h;
  if (m_fx.empty() && !m_layerMotion.enabled()) {
    m_inner->draw(ctx);
    return;
  }
  // the widget into a texture of the surface's size…
  GLint fbo = 0, vp[4] = {};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
  glGetIntegerv(GL_VIEWPORT, vp);
  const int w = std::max(1, vp[2]), h = std::max(1, vp[3]);
  if (!m_fbo || w != m_w || h != m_h) {
    if (!m_tex) glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!m_fbo) glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
    m_w = w, m_h = h;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
  glViewport(0, 0, w, h);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  m_inner->draw(ctx);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  auto setPose = [&](const Program& program) {
    glUniform4f(program.uniform("u_layerPose"), pose.x, pose.y, pose.brightness, pose.opacity);
  };
  if (m_fx.empty()) {
    if (!m_layerProgram.valid()) m_layerProgram.create(kQuadVertexShader, kPlainLayer, "scene layer");
    glUseProgram(m_layerProgram.id());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glUniform1i(m_layerProgram.uniform("u_tex"), 0);
    setPose(m_layerProgram);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawUnitQuad();
    glDisable(GL_BLEND);
    return;
  }
  // …then through the effects onto the surface
  m_fx.build(kHead, "", kBody, kLights);
  DrawContext own = ctx;  // no wallpaper depth or masks in a widget's own box
  own.depthField = 0;
  own.effectMasks = 0;
  own.fillMode = 1;
  auto picture = [&](const Program& prog) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glUniform1f(prog.uniform("u_aspect"), static_cast<float>(w) / static_cast<float>(h));
  };
  m_fx.drawLights(own, true, picture, true);  // its own render is premultiplied
  m_fx.use(0, own, true, true);
  glUniform2f(m_fx.program().uniform("u_screen"), static_cast<float>(w), static_cast<float>(h));
  picture(m_fx.program());
  setPose(m_fx.program());
  glDisable(GL_BLEND);
  drawUnitQuad();
}

}  // namespace undershell
