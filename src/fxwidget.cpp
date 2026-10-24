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
)";

// the widget's picture is premultiplied: the effects see its colour, and
// what they make keeps the widget's own coverage
const char* kBody = R"(
void main() {
    screenPx = v_uv * u_screen;
    vec2 uv = v_uv;
//@WARPS
    vec4 t = any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) ? vec4(0.0) : texture(u_tex, vec2(uv.x, 1.0 - uv.y));
    vec3 c = t.a > 0.0 ? t.rgb / t.a : vec3(0.0);
//@COLOURS
    if (u_hasLights == 1) {
        vec4 L = texture(u_lights, vec2(v_uv.x, 1.0 - v_uv.y));
        c = c * (1.0 - L.a) + L.rgb;
    }
    fragColor = vec4(clamp(c, 0.0, 1.0) * t.a, t.a);
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
}

void FxWidget::tick(const TickContext& ctx) {
  m_inner->tick(ctx);
  if (!m_fx.empty()) m_fx.tick(ctx);
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

void FxWidget::draw(const DrawContext& ctx) {
  if (m_fx.empty()) {
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
  m_fx.drawLights(own, true, picture);
  m_fx.use(0, own, true);
  glUniform2f(m_fx.program().uniform("u_screen"), static_cast<float>(w), static_cast<float>(h));
  picture(m_fx.program());
  glDisable(GL_BLEND);
  drawUnitQuad();
}

}  // namespace undershell
