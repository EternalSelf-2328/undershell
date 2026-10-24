// SPDX-License-Identifier: GPL-3.0-or-later
// The effects of one widget, ready to draw (see effectchain.hpp).
#include "effectchain.hpp"

#include "common.hpp"
#include "perf.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace undershell {

EffectChain::~EffectChain() {
  if (m_noise) glDeleteTextures(1, &m_noise);
  if (m_lightTex) glDeleteTextures(1, &m_lightTex);
  if (m_lightFbo) glDeleteFramebuffers(1, &m_lightFbo);
}

bool EffectChain::refresh() {
  if (!effectLibrary().refresh()) return false;
  configure(m_opts, m_noct);
  return true;
}

void EffectChain::configure(const toml::table& opts, const NoctaliaState& noct) {
  m_opts = opts;
  m_noct = noct;
  const std::vector<std::string>& kMasks = effectMaskModes();
  std::vector<Fx> fx;
  m_time = m_audio = m_depth = false;
  for (const EffectSpec* e : enabledEffects(m_opts)) {
    Fx f;
    f.id = e->id;
    f.hash = e->hash;
    auto num = [&](const std::string& key, double def, double lo, double hi) {
      double v = def;
      if (auto d = m_opts[key].value<double>()) v = *d;
      return static_cast<float>(std::clamp(v, lo, hi));
    };
    f.strength = num(effectKey(e->id, "strength"), 1, 0, 2);
    f.split = num(effectKey(e->id, "split"), 0.5, 0, 1);
    const std::string mask = m_opts[effectKey(e->id, "mask")].value_or(std::string("all"));
    f.mask = static_cast<int>(std::ranges::find(kMasks, mask) - kMasks.begin()) % static_cast<int>(kMasks.size());
    bool listening = e->audio;
    for (const EffectParam& p : e->params) {
      const std::string key = effectKey(e->id, p.key);
      std::array<float, 3> v{};
      if (p.kind == EffectParam::Number) {
        v[0] = num(key, p.def, p.min, p.max);
      } else if (p.kind == EffectParam::Bool) {
        v[0] = m_opts[key].value_or(p.def > 0.5) ? 1.0F : 0.0F;
        if (p.key == e->audioWhen) listening = e->audio && v[0] > 0.5F;
      } else if (p.kind == EffectParam::Choice) {
        const std::string c = m_opts[key].value_or(p.defText);
        auto it = std::ranges::find(p.options, c);
        if (it == p.options.end()) it = std::ranges::find(p.options, p.defText);
        v[0] = static_cast<float>(it - p.options.begin());
      } else {
        const Color c = m_noct.color(m_opts[key].value_or(p.defText), Color::fromHex(p.defText.starts_with('#') ? p.defText : "#ffffff"));
        v = {c.r, c.g, c.b};
      }
      f.values.push_back(v);
    }
    m_time |= e->time;
    m_audio |= listening;
    m_depth |= e->depth || f.mask == 1 || f.mask == 2 || f.mask >= 5;  // painted masks come with the depth field
    fx.push_back(std::move(f));
  }
  m_fps = static_cast<int>(std::clamp(m_opts["fx_fps"].value_or(30.0), 10.0, 60.0));
  m_fx = std::move(fx);
  m_moving = m_time;
}

void EffectChain::tick(const TickContext& ctx) {
  if (!m_audio) {
    m_level = {};
    m_moving = m_time;
    return;
  }
  // the music as four slow numbers: bass, mids, treble (fast up, slower
  // down) and the beat (the analyser's kick, fading)
  std::array<float, 3> want{};
  if (const auto* b = ctx.audio.bands; b && !b->empty() && !ctx.audio.silent) {
    const size_t n = b->size(), lo = std::max<size_t>(1, n * 15 / 100), mid = std::max(lo + 1, n / 2);
    auto avg = [&](size_t from, size_t to) {
      float s = 0;
      for (size_t i = from; i < to && i < n; ++i) s += (*b)[i];
      return to > from ? s / static_cast<float>(to - from) : 0.0F;
    };
    want = {avg(0, lo), avg(lo, mid), avg(mid, n)};
  }
  const float dt = static_cast<float>(std::clamp(ctx.dt, 0.0, 0.25));
  for (int i = 0; i < 3; ++i) {
    const float k = want[i] > m_level[i] ? 1 - std::exp(-dt * 30) : 1 - std::exp(-dt * 5);
    m_level[i] += (std::clamp(want[i] * 1.6F, 0.0F, 1.0F) - m_level[i]) * k;
  }
  m_level[3] = ctx.audio.onsets ? static_cast<float>(std::clamp(ctx.audio.kickStrength, 0.0, 1.0) * std::exp(-ctx.audio.kickAge * 5))
                                : std::max(0.0F, m_level[0] - 0.35F) * 1.5F;
  const float sum = m_level[0] + m_level[1] + m_level[2] + m_level[3];
  m_moving = m_time || !ctx.audio.silent || sum > 0.004F;
}

// an effect whose GLSL does not compile is found, left out and named in the
// log and the inspector
void EffectChain::build(const char* head, const char* fit, const char* body, const char* lights) {
  auto signature = [&] {
    std::string sig;
    for (const Fx& f : m_fx) sig += std::format("{}:{};", f.id, f.hash);
    return sig;
  };
  if (m_prog.valid() && signature() == m_built) return;
  auto specs = [&](const std::vector<Fx>& list) {
    std::vector<const EffectSpec*> out;
    for (const Fx& f : list)
      if (const EffectSpec* e = effectLibrary().find(f.id); e && e->hash == f.hash) out.push_back(e);
    return out;
  };
  auto sources = [&](const std::vector<const EffectSpec*>& fx) {
    const EffectCode code = effectCode(fx);
    std::string b = body, l = lights;
    b.replace(b.find("//@WARPS"), 8, code.warps);
    b.replace(b.find("//@COLOURS"), 10, code.colours);
    l.replace(l.find("//@LIGHTS"), 9, code.lights);
    const std::string common = std::string(head) + kEffectApi + code.decls + fit;
    return std::pair{common + b, code.lights.empty() ? std::string() : common + l};
  };
  auto create = [&](const std::vector<Fx>& list) {
    const auto [main, light] = sources(specs(list));
    m_prog.create(kQuadVertexShader, main, "effects");
    if (light.empty()) m_lightProg.destroy();
    else m_lightProg.create(kQuadVertexShader, light, "effect lights");
  };
  try {
    create(m_fx);
  } catch (const std::exception&) {
    // which one: each alone (only on this rare path)
    for (const Fx& f : m_fx) {
      const auto one = specs({f});
      if (one.empty()) continue;
      try {
        const auto [main, light] = sources(one);
        Program test;
        test.create(kQuadVertexShader, main, "effect");
        if (!light.empty()) test.create(kQuadVertexShader, light, "effect");
      } catch (const std::exception& why) {
        US_ERROR("effect {}: {}", f.id, why.what());
        effectLibrary().markBroken(f.id, f.hash, why.what());
      }
    }
    std::erase_if(m_fx, [](const Fx& f) {
      const EffectSpec* e = effectLibrary().find(f.id);
      return !e || !effectLibrary().brokenWhy(*e).empty();
    });
    try {
      create(m_fx);
    } catch (const std::exception& again) {
      US_ERROR("effects: {}", again.what());
      m_fx.clear();
      create(m_fx);
    }
  }
  m_built = signature();
  // where each effect's options went, in both programs
  for (size_t i = 0; i < m_fx.size(); ++i) {
    Fx& f = m_fx[i];
    for (int which = 0; which < 2; ++which) {
      const Program& prog = which == 0 ? m_prog : m_lightProg;
      std::vector<GLint>& loc = f.loc[which];
      loc.clear();
      if (!prog.valid()) continue;
      if (const EffectSpec* e = effectLibrary().find(f.id))
        for (const EffectParam& p : e->params) loc.push_back(prog.uniform(std::format("fx{}_{}", i, p.key).c_str()));
      for (const char* own : {"strength", "mask", "split"}) loc.push_back(prog.uniform(std::format("fxo{}_{}", i, own).c_str()));
    }
  }
}

void EffectChain::use(int which, const DrawContext& ctx, bool flipY) {
  const Program& prog = which == 0 ? m_prog : m_lightProg;
  if (!m_noise) {
    const std::vector<uint8_t> px = effectNoise();
    glGenTextures(1, &m_noise);
    glBindTexture(GL_TEXTURE_2D, m_noise);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }
  if (m_start < 0) m_start = ctx.now;
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, ctx.depthField);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, m_noise);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, which == 0 && hasLights() ? m_lightTex : 0);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, ctx.effectMasks);
  glActiveTexture(GL_TEXTURE0);
  glUseProgram(prog.id());
  glUniform1f(prog.uniform("u_time"), static_cast<float>(std::fmod(ctx.now - m_start, 20000.0)));
  glUniform4f(prog.uniform("u_audio"), m_level[0], m_level[1], m_level[2], m_level[3]);
  glUniform1i(prog.uniform("u_quality"), perfKnobs().quality);
  glUniform1i(prog.uniform("u_wrap"), ctx.fillMode == 4 && !flipY ? 1 : 0);
  glUniform1i(prog.uniform("u_flipY"), flipY ? 1 : 0);
  glUniform1f(prog.uniform("u_depthOn"), ctx.depthField ? 1.0F : 0.0F);
  glUniform1f(prog.uniform("u_depthLevel"), ctx.depthLevel);
  glUniform1f(prog.uniform("u_depthFeather"), ctx.depthFeather);
  glUniform1i(prog.uniform("u_tex"), 0);
  glUniform1i(prog.uniform("u_depth"), 1);
  glUniform1i(prog.uniform("u_noise"), 2);
  glUniform1i(prog.uniform("u_lights"), 3);
  glUniform1i(prog.uniform("u_hasLights"), which == 0 && hasLights() ? 1 : 0);
  glUniform1i(prog.uniform("u_fxmasks"), 4);
  glUniform1f(prog.uniform("u_fxmasksOn"), ctx.effectMasks ? 1.0F : 0.0F);
  const EffectLibrary& lib = effectLibrary();
  for (const Fx& f : m_fx) {
    const EffectSpec* e = lib.find(f.id);
    const std::vector<GLint>& loc = f.loc[which];
    if (!e || loc.size() != e->params.size() + 3) continue;
    for (size_t k = 0; k < e->params.size(); ++k) {
      const auto& v = f.values[k];
      switch (e->params[k].kind) {
      case EffectParam::Number: glUniform1f(loc[k], v[0]); break;
      case EffectParam::Bool:
      case EffectParam::Choice: glUniform1i(loc[k], static_cast<int>(v[0])); break;
      case EffectParam::Color: glUniform3f(loc[k], v[0], v[1], v[2]); break;
      }
    }
    const size_t o = e->params.size();
    glUniform1f(loc[o], f.strength);
    glUniform1i(loc[o + 1], f.mask);
    glUniform1f(loc[o + 2], f.split);
  }
}

// the light effects on a surface a fraction of the size (they are soft:
// rays, fog), stretched over the sharp picture by the main pass; the cost of
// their many samples falls with the area, 4 to 16 times
void EffectChain::drawLights(const DrawContext& ctx, bool flipY, const std::function<void(const Program&)>& picture) {
  if (!hasLights()) return;
  GLint fbo = 0, vp[4] = {};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
  glGetIntegerv(GL_VIEWPORT, vp);
  const int q = perfKnobs().quality, div = q >= 3 ? 2 : q == 2 ? 3 : 4;
  const int lw = std::max(1, vp[2] / div), lh = std::max(1, vp[3] / div);
  if (!m_lightFbo || lw != m_lightW || lh != m_lightH) {
    if (!m_lightTex) glGenTextures(1, &m_lightTex);
    glBindTexture(GL_TEXTURE_2D, m_lightTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!m_lightFbo) glGenFramebuffers(1, &m_lightFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_lightFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_lightTex, 0);
    m_lightW = lw, m_lightH = lh;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, m_lightFbo);
  glViewport(0, 0, lw, lh);
  glDisable(GL_BLEND);
  use(1, ctx, flipY);
  glUniform2f(m_lightProg.uniform("u_screen"), static_cast<float>(lw), static_cast<float>(lh));
  picture(m_lightProg);
  drawUnitQuad();
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
  glViewport(vp[0], vp[1], vp[2], vp[3]);
}

}  // namespace undershell
