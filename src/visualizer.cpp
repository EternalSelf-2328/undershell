// SPDX-License-Identifier: GPL-3.0-or-later
// Port of Ryoku's ryoku/ui/SpectrumField.qml: the look's uniforms derived
// from the config, the widget box and the eased levels.
#include "visualizer.hpp"

#include <cmath>
#include <numbers>

#include "spectrum_shader.inc"

namespace undershell {

static const char* kStyles[] = {"bars", "split", "dots", "segments", "wave", "ribbon",
                                "curtain", "line", "frame", "radial", "orb", "spiral"};

void Visualizer::configure(const VisualizerConfig& cfg, const NoctaliaState& noct) {
  m_cfg = cfg;
  m_styleIndex = 0;
  for (int i = 0; i < 12; ++i)
    if (cfg.style == kStyles[i]) m_styleIndex = i;
  const bool polar = m_styleIndex >= 9;
  m_motion.gain = cfg.gain;
  m_motion.smoothing = cfg.smoothing;
  m_motion.mirror = cfg.mirror && !polar && m_styleIndex != 8;
  m_motion.idleWave = cfg.idleWave;
  m_motion.wantPeaks = cfg.peaks && (m_styleIndex == 0 || m_styleIndex == 3 || m_styleIndex == 8);
  m_motion.spin = polar ? cfg.spin : 0.0;
  m_motion.configure(cfg.bars);

  // Colour, like Ryoku's VisualizerView ramp: a two-stop gradient, or one base
  // colour walked gently from bass (darker) to treble (lighter).
  const Color white{1, 1, 1, 1}, black{0, 0, 0, 1};
  if (cfg.colorMode == "gradient") {
    Color a = noct.color(cfg.color), b = noct.color(cfg.color2);
    for (int k = 0; k < 8; ++k) m_ramp[k] = a.mix(b, k / 7.0F);
  } else {
    Color base = cfg.colorMode == "custom" ? noct.color(cfg.color) : noct.color("primary");
    for (int k = 0; k < 8; ++k) {
      float f = (k / 7.0F - 0.5F) * 0.36F;
      m_ramp[k] = f >= 0 ? base.mix(white, f * 0.55F) : base.mix(black, -f * 0.55F);
    }
  }
}

void Visualizer::tick(double dt, const std::vector<float>& raw, double energy) {
  m_motion.tick(dt, raw, energy);
}

void Visualizer::draw(float w, float h, float outputW, float outputH) {
  if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kSpectrumFrag, "spectrum");

  const bool polar = m_styleIndex >= 9;
  const bool frame = m_styleIndex == 8;
  const std::string& grow = m_cfg.grow;
  const bool vertical = grow == "left" || grow == "right";
  const bool centred = (grow == "center" && m_cfg.style != "curtain") || m_cfg.style == "split";
  const int bands = m_motion.bands;
  const float ui = std::clamp(std::min(outputW, outputH) / 900.0F, 0.75F, 2.5F);
  const float glow = static_cast<float>(m_cfg.glow);
  const float thickness = static_cast<float>(m_cfg.thickness);

  // The box is the surface minus room for the bloom to fall off in (Ryoku's
  // pass = box + margin). Margin depends on the band width, which depends on
  // the box: one refinement step settles it.
  float margin = 2 * ui;
  float bw = w, bh = h;
  float r0 = 0, rMax = 0, shapePx = 2, glowPx = 1.5F, maxLen = 2, reflectPx = 0;
  int drawBands = bands;
  const float framePerim = 2 * (outputW + outputH);
  for (int pass = 0; pass < 2; ++pass) {
    bw = frame ? w : std::max(24.0F, w - 2 * margin);
    bh = frame ? h : std::max(24.0F, h - 2 * margin);
    const float acrossFull = vertical ? bw : bh;
    const float alongFull = vertical ? bh : bw;
    reflectPx = (grow == "up" && !polar && !centred && !frame) ? std::round(acrossFull * static_cast<float>(m_cfg.reflection)) : 0;
    maxLen = frame ? std::max(6.0F, std::min(outputW, outputH) * 0.06F) : std::max(2.0F, std::round(acrossFull - reflectPx));
    const float radius = std::max(6.0F, std::min(bw, bh) / 2);
    r0 = radius * (m_cfg.style == "orb" ? 0.62F : 0.38F);
    rMax = radius - r0;
    drawBands = polar ? std::max(10, std::min(bands, static_cast<int>(std::floor(2 * std::numbers::pi_v<float> * r0 / (14 * ui)))))
                      : bands;
    const float slotPx = (frame ? framePerim : alongFull) / static_cast<float>(drawBands);
    shapePx = frame ? std::max(2.0F, framePerim / (drawBands * 3.0F) * thickness)
                    : (polar ? std::max(3.0F, 2 * std::numbers::pi_v<float> * r0 / drawBands * thickness)
                             : std::max(2.0F, slotPx * thickness));
    glowPx = std::max(1.5F, std::min(shapePx * 0.9F, 12 * ui)) * (0.35F + 0.65F * std::clamp(glow, 0.0F, 1.0F));
    margin = std::ceil(glow > 0 ? glowPx * 3.5F + 2 : 2 * ui);
  }
  const float pad = frame ? 0 : margin;

  // levels: a folded ring averages its groups first
  const std::vector<float>* lv = &m_motion.levels;
  const std::vector<float>* pk = &m_motion.peaks;
  if (drawBands < bands) {
    Motion::resample(m_motion.levels, drawBands, m_drawLevels);
    lv = &m_drawLevels;
    if (m_motion.wantPeaks) {
      Motion::resample(m_motion.peaks, drawBands, m_drawPeaks);
      pk = &m_drawPeaks;
    }
  }

  const float fade = static_cast<float>(m_motion.fade());
  glUseProgram(m_prog.id());
  auto U = [&](const char* n) { return m_prog.uniform(n); };
  glUniform1fv(U("u_lv"), static_cast<GLsizei>(std::min<size_t>(128, lv->size())), lv->data());
  if (m_motion.wantPeaks) glUniform1fv(U("u_pk"), static_cast<GLsizei>(std::min<size_t>(128, pk->size())), pk->data());
  const char* cn[] = {"c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7"};
  for (int i = 0; i < 8; ++i) glUniform4f(U(cn[i]), m_ramp[i].r, m_ramp[i].g, m_ramp[i].b, 1.0F);
  glUniform2f(U("res"), w, h);
  glUniform2f(U("origin"), pad + bw / 2, pad + bh / 2);
  glUniform1f(U("style"), static_cast<float>(m_styleIndex));
  const float posMode = frame ? 0.0F
                              : (grow == "down" ? 1.0F : grow == "center" ? 2.0F : grow == "left" ? 3.0F : grow == "right" ? 4.0F : 0.0F);
  glUniform1f(U("posMode"), posMode);
  glUniform1f(U("pad"), pad);
  glUniform1f(U("bands"), static_cast<float>(drawBands));
  glUniform1f(U("maxLen"), maxLen);
  glUniform1f(U("minLen"), std::max(1.5F, 2 * ui) * (fade > 0.9F ? 1.0F : fade));
  glUniform1f(U("thickness"), std::clamp(thickness, 0.05F, 1.0F));
  glUniform1f(U("shapeW"), shapePx);
  glUniform1f(U("capR"), m_cfg.shape == "rounded" ? shapePx * 0.5F : std::min(2 * ui, shapePx * 0.2F));
  const float segN = static_cast<float>(std::clamp(m_cfg.segments, 3, 24));
  glUniform1f(U("segN"), segN);
  glUniform1f(U("segGap"), std::max(1.5F * ui, maxLen / segN * 0.26F));
  glUniform1f(U("gapPx"), std::max(2 * ui, maxLen * 0.06F));
  glUniform1f(U("glowAmt"), std::clamp(glow, 0.0F, 1.0F));
  glUniform1f(U("glowPx"), glowPx);
  glUniform1f(U("reflectPx"), reflectPx);
  glUniform1f(U("peakOn"), m_motion.wantPeaks ? 1.0F : 0.0F);
  glUniform1f(U("r0"), r0);
  glUniform1f(U("rMax"), rMax);
  glUniform1f(U("spinRad"), static_cast<float>(m_motion.spinDeg * std::numbers::pi / 180.0));
  glUniform1f(U("energy"), static_cast<float>(std::clamp(m_motion.maxLevel, 0.0, 1.0)));
  glUniform1f(U("fade"), fade);
  glUniform1f(U("aa"), 0.85F);
  glUniform1f(U("u_opacity"), static_cast<float>(m_cfg.opacity));
  glDisable(GL_BLEND);
  drawUnitQuad();
}

}  // namespace undershell
