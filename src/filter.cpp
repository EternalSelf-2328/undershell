// SPDX-License-Identifier: GPL-3.0-or-later
// The wallpaper filter (see filter.hpp).
#include "filter.hpp"

#include "common.hpp"
#include "perf.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace undershell {

namespace {
template <typename T>
T get(const toml::table& t, std::string_view key, T def) {
  if (auto v = t[key].value<T>()) return *v;
  return def;
}

// the shader in three parts: this head, effects.cpp's API and the effects
// that are on, then the body (whose main() applies them)
const char* kHead = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 u_size;      // the surface, logical px (the whole output)
uniform vec2 u_img;       // the picture's own size (its aspect decides the fit)
uniform int u_fill;       // center crop fit stretch repeat span
uniform vec2 u_texSize;   // the texture, px
uniform float u_bright, u_contrast, u_sat, u_hue, u_temp;
uniform vec4 u_tint;      // rgb, amount
uniform int u_duo;
uniform vec3 u_dark, u_light;
uniform float u_vignette, u_blur, u_grain, u_levels, u_pixel, u_opacity;
uniform sampler2D u_lights;  // the light effects, drawn smaller (lights pass)
uniform int u_hasLights;
)";

const char* kFit = R"(
// Noctalia's calculateWallpaperUV (MIT): an output point -> the picture's uv
bool wallUv(vec2 p, out vec2 uv) {
    vec2 x = p / u_size;
    vec2 sw = u_size, iw = u_img;
    if (u_fill == 0) {
        uv = (x * sw - (sw - iw) * 0.5) / iw;
    } else if (u_fill == 1 || u_fill == 5) {
        float s = max(sw.x / iw.x, sw.y / iw.y);
        vec2 off = (iw * s - sw) / (iw * s);
        uv = x * (1.0 - off) + off * 0.5;
    } else if (u_fill == 2) {
        float s = min(sw.x / iw.x, sw.y / iw.y);
        uv = ((x * sw - (sw - iw * s) * 0.5) / s) / iw;
    } else if (u_fill == 3) {
        uv = x;
    } else {
        uv = fract(x * sw / iw);
        return true;
    }
    return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
}

)";

// the lights pass: the light effects alone, on a smaller surface
const char* kLights = R"(
void main() {
    screenPx = v_uv * u_screen;
    vec2 uv;
    if (!wallUv(v_uv * u_size, uv)) { fragColor = vec4(0.0); return; }
    vec4 L = vec4(0.0);
//@LIGHTS
    fragColor = L;
}
)";

const char* kBody = R"(
vec3 hueTurn(vec3 c, float a) {  // about the grey axis
    const vec3 k = vec3(0.57735);
    float ca = cos(a), sa = sin(a);
    return c * ca + cross(k, c) * sa + k * dot(k, c) * (1.0 - ca);
}

void main() {
    screenPx = v_uv * u_screen;
    vec2 p = v_uv * u_size;
    if (u_pixel > 0.0) p = (floor(p / u_pixel) + 0.5) * u_pixel;
    vec2 uv;
    if (!wallUv(p, uv)) { fragColor = vec4(0.0); return; }  // a letterbox: the shell's own colour shows
    // the effects: warps bend where the picture is read…
//@WARPS
    uv = edgeSafe(uv);
    vec3 c;
    if (u_blur > 0.0) {
        // a soft copy: taps round the point, read from a smaller mipmap
        float lod = log2(max(1.0, u_blur));
        vec2 d = vec2(u_blur) / u_texSize;
        c = textureLod(u_tex, uv, lod).rgb * 0.2;
        for (int i = 0; i < 8; i++) {
            float a = float(i) * 0.785398;
            c += textureLod(u_tex, uv + vec2(cos(a), sin(a)) * d, lod).rgb * 0.1;
        }
    } else {
        c = texture(u_tex, uv).rgb;
    }
    // …then the colour stages change what was read
//@COLOURS
    // …and the lights are laid over it
    if (u_hasLights == 1) {
        vec4 L = texture(u_lights, v_uv);
        c = c * (1.0 - L.a) + L.rgb;
    }
    c += u_bright;
    c = (c - 0.5) * u_contrast + 0.5;
    c = mix(vec3(luma(c)), c, u_sat);
    if (u_hue != 0.0) c = hueTurn(c, u_hue);
    c += vec3(0.16, 0.05, -0.16) * u_temp;  // warmer: redder and less blue
    c = clamp(c, 0.0, 1.0);
    if (u_duo == 1) c = mix(u_dark, u_light, smoothstep(0.0, 1.0, luma(c)));
    if (u_tint.a > 0.0) c = mix(c, u_tint.rgb * (0.35 + 0.9 * luma(c)), u_tint.a);
    if (u_levels > 1.0) c = floor(c * u_levels + 0.5) / u_levels;
    if (u_vignette > 0.0) {
        vec2 q = v_uv - 0.5;
        q.x *= u_size.x / u_size.y;
        c *= 1.0 - u_vignette * smoothstep(0.3, 1.0, length(q) * 1.25);
    }
    if (u_grain > 0.0) {
        float n = fract(sin(dot(floor(p * 1.0), vec2(12.9898, 78.233))) * 43758.5453);
        c += (n - 0.5) * 0.16 * u_grain;
    }
    c = clamp(c, 0.0, 1.0);
    fragColor = vec4(c * u_opacity, u_opacity);
}
)";
}  // namespace

const std::vector<std::string>& FilterWidget::styles() {
  static const std::vector<std::string> all = {"warm", "cool", "noir", "vintage", "dream", "night", "vivid", "faded", "duotone", "custom"};
  return all;
}

const std::map<std::string, std::string>& FilterWidget::preset(const std::string& style) {
  static const std::map<std::string, std::string> base = {
      {"brightness", "0.0"}, {"contrast", "1.0"}, {"saturation", "1.0"}, {"hue", "0"},       {"temperature", "0.0"},
      {"tint_amount", "0.0"}, {"duotone", "false"}, {"vignette", "0.0"}, {"blur", "0.0"},   {"grain", "0.0"},
      {"posterize", "0"},    {"pixelate", "0.0"},
  };
  auto with = [&](std::initializer_list<std::pair<const std::string, std::string>> over) {
    auto m = base;
    for (const auto& [k, v] : over) m[k] = v;
    return m;
  };
  static const std::map<std::string, std::map<std::string, std::string>> all = {
      {"custom", base},
      {"warm", with({{"temperature", "0.6"}, {"saturation", "1.08"}, {"vignette", "0.15"}})},
      {"cool", with({{"temperature", "-0.6"}, {"saturation", "0.95"}, {"contrast", "1.05"}})},
      {"noir", with({{"saturation", "0.0"}, {"contrast", "1.3"}, {"brightness", "-0.04"}, {"vignette", "0.45"}, {"grain", "0.3"}})},
      {"vintage", with({{"saturation", "0.65"}, {"temperature", "0.7"}, {"contrast", "0.9"}, {"brightness", "0.03"}, {"vignette", "0.35"},
                        {"grain", "0.45"}})},
      {"dream", with({{"blur", "4.0"}, {"brightness", "0.05"}, {"saturation", "1.2"}, {"contrast", "0.9"}})},
      {"night", with({{"brightness", "-0.22"}, {"saturation", "0.7"}, {"temperature", "-0.8"}, {"vignette", "0.5"}})},
      {"vivid", with({{"saturation", "1.45"}, {"contrast", "1.12"}})},
      {"faded", with({{"contrast", "0.75"}, {"brightness", "0.08"}, {"saturation", "0.75"}})},
      {"duotone", with({{"duotone", "true"}, {"contrast", "1.1"}})},
  };
  auto it = all.find(style);
  return it != all.end() ? it->second : base;
}

FilterConfig FilterConfig::fromTable(const toml::table& given) {
  toml::table t = given;
  const std::string style = given["style"].value_or(std::string("warm"));
  for (const auto& [k, v] : FilterWidget::preset(style)) {
    if (t.contains(k)) continue;
    try {
      auto parsed = toml::parse("v = " + v);
      if (auto* n = parsed.get("v")) t.insert(k, *n);
    } catch (const std::exception&) {
    }
  }
  FilterConfig c;
  c.style = style;
  auto num = [&](const char* key, double def, double lo, double hi) {
    double v = def;
    if (auto d = t[key].value<double>()) v = *d;
    return std::clamp(v, lo, hi);
  };
  c.brightness = num("brightness", c.brightness, -1, 1);
  c.contrast = num("contrast", c.contrast, 0, 2);
  c.saturation = num("saturation", c.saturation, 0, 2);
  c.hue = num("hue", c.hue, -180, 180);
  c.temperature = num("temperature", c.temperature, -1, 1);
  c.tint = get<std::string>(t, "tint", c.tint);
  c.tintAmount = num("tint_amount", c.tintAmount, 0, 1);
  c.duotone = get<bool>(t, "duotone", c.duotone);
  c.dark = get<std::string>(t, "dark", c.dark);
  c.light = get<std::string>(t, "light", c.light);
  c.vignette = num("vignette", c.vignette, 0, 1);
  c.blur = num("blur", c.blur, 0, 20);
  c.grain = num("grain", c.grain, 0, 1);
  c.posterize = static_cast<int>(num("posterize", c.posterize, 0, 16));
  c.pixelate = num("pixelate", c.pixelate, 0, 40);
  c.opacity = num("opacity", c.opacity, 0, 1);
  return c;
}

FilterWidget::~FilterWidget() {
  if (m_noise) glDeleteTextures(1, &m_noise);
  if (m_lightTex) glDeleteTextures(1, &m_lightTex);
  if (m_lightFbo) glDeleteFramebuffers(1, &m_lightFbo);
}

void FilterWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  m_cfg = FilterConfig::fromTable(cfg.options);
  m_tintC = noct.color(m_cfg.tint, Color::fromHex("#e2342a"));
  m_darkC = noct.color(m_cfg.dark, Color::fromHex("#1b1530"));
  m_lightC = noct.color(m_cfg.light, Color::fromHex("#ffd9a8"));
  m_opts = cfg.options;
  m_noct = noct;
  effectLibrary().refresh();  // a file added while undershell was not watching
  applyEffects();
}

void FilterWidget::filesChanged() {
  if (effectLibrary().refresh()) applyEffects();
}

std::vector<std::string> FilterWidget::watchedFiles() const { return {userEffectsDir() + "/"}; }

// the effects that are on and the values of their options
void FilterWidget::applyEffects() {
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

void FilterWidget::tick(const TickContext& ctx) {
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

// the programs for the effects that are on (and their lights pass, if any
// light is on); an effect whose GLSL does not compile is found, left out
// and named in the log and the inspector
void FilterWidget::build() {
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
  auto sources = [](const std::vector<const EffectSpec*>& fx) {
    const EffectCode code = effectCode(fx);
    std::string body = kBody, lights = kLights;
    body.replace(body.find("//@WARPS"), 8, code.warps);
    body.replace(body.find("//@COLOURS"), 10, code.colours);
    lights.replace(lights.find("//@LIGHTS"), 9, code.lights);
    const std::string common = std::string(kHead) + kEffectApi + code.decls + kFit;
    return std::pair{common + body, code.lights.empty() ? std::string() : common + lights};
  };
  auto create = [&](const std::vector<Fx>& list) {
    const auto [main, lights] = sources(specs(list));
    m_prog.create(kQuadVertexShader, main, "filter");
    if (lights.empty()) m_lightProg.destroy();
    else m_lightProg.create(kQuadVertexShader, lights, "filter lights");
  };
  try {
    create(m_fx);
  } catch (const std::exception& ex) {
    // which one: each alone (only on this rare path)
    for (const Fx& f : m_fx) {
      const auto one = specs({f});
      if (one.empty()) continue;
      try {
        const auto [main, lights] = sources(one);
        Program test;
        test.create(kQuadVertexShader, main, "effect");
        if (!lights.empty()) test.create(kQuadVertexShader, lights, "effect");
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
      US_ERROR("wallpaper effects: {}", again.what());
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

// what both programs share: time, music, depth, noise and the effects' options
void FilterWidget::shared(const Program& prog, int which, const DrawContext& ctx) {
  glUseProgram(prog.id());
  glUniform1f(prog.uniform("u_time"), static_cast<float>(std::fmod(ctx.now - m_start, 20000.0)));
  glUniform4f(prog.uniform("u_audio"), m_level[0], m_level[1], m_level[2], m_level[3]);
  glUniform1i(prog.uniform("u_quality"), perfKnobs().quality);
  glUniform1i(prog.uniform("u_wrap"), ctx.fillMode == 4 ? 1 : 0);
  glUniform1f(prog.uniform("u_depthOn"), ctx.depthField ? 1.0F : 0.0F);
  glUniform1f(prog.uniform("u_depthLevel"), ctx.depthLevel);
  glUniform1f(prog.uniform("u_depthFeather"), ctx.depthFeather);
  glUniform1i(prog.uniform("u_tex"), 0);
  glUniform1i(prog.uniform("u_depth"), 1);
  glUniform1i(prog.uniform("u_noise"), 2);
  glUniform1i(prog.uniform("u_lights"), 3);
  glUniform1i(prog.uniform("u_fxmasks"), 4);
  glUniform1f(prog.uniform("u_fxmasksOn"), ctx.effectMasks ? 1.0F : 0.0F);
  glUniform1i(prog.uniform("u_fill"), ctx.fillMode);
  glUniform2f(prog.uniform("u_size"), ctx.w, ctx.h);
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
// their many samples falls with the area, 9 to 16 times
void FilterWidget::drawLights(const WallPic& pic, const DrawContext& ctx) {
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
  shared(m_lightProg, 1, ctx);
  glUniform2f(m_lightProg.uniform("u_screen"), static_cast<float>(lw), static_cast<float>(lh));
  glUniform2f(m_lightProg.uniform("u_img"), static_cast<float>(pic.imgW), static_cast<float>(pic.imgH));
  glUniform1f(m_lightProg.uniform("u_aspect"), pic.imgH > 0 ? static_cast<float>(pic.imgW) / static_cast<float>(pic.imgH) : 1.0F);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, pic.tex);
  drawUnitQuad();
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
  glViewport(vp[0], vp[1], vp[2], vp[3]);
}

void FilterWidget::draw(const DrawContext& ctx) {
  if (ctx.movingWallpaper || ctx.wallpaper.empty() || ctx.wallpaper.rfind("color:", 0) == 0) return;  // nothing still to filter
  const int maxSide = static_cast<int>(std::ceil(std::max(ctx.outputW, ctx.outputH) * std::max(1.0F, ctx.scale)));
  if (!m_fade.update(ctx.wallpaper, maxSide, ctx.now)) return;  // the old picture stays until the new one is in
  build();
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
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, ctx.effectMasks);
  const bool lights = m_lightProg.valid();
  if (lights) drawLights(*m_fade.current(), ctx);
  shared(m_prog, 0, ctx);
  glUniform2f(m_prog.uniform("u_screen"), ctx.w * ctx.scale, ctx.h * ctx.scale);
  glUniform1i(m_prog.uniform("u_hasLights"), lights ? 1 : 0);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, lights ? m_lightTex : 0);
  const float opacity = static_cast<float>(m_cfg.opacity);
  // a new wallpaper: the old one under it, the new one fading in on top
  // (only then does it blend: alone, it lands on a cleared surface)
  if (const WallPic* prev = m_fade.previous()) {
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawPicture(*prev, ctx, opacity);
    drawPicture(*m_fade.current(), ctx, opacity * m_fade.mix());
    glDisable(GL_BLEND);
  } else {
    glDisable(GL_BLEND);
    drawPicture(*m_fade.current(), ctx, opacity);
  }
  glActiveTexture(GL_TEXTURE0);
}

void FilterWidget::drawPicture(const WallPic& pic, const DrawContext& ctx, float opacity) {
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, pic.tex);
  const int tw = pic.texW, th = pic.texH;
  glUniform2f(m_prog.uniform("u_img"), static_cast<float>(pic.imgW), static_cast<float>(pic.imgH));
  glUniform1f(m_prog.uniform("u_aspect"), pic.imgH > 0 ? static_cast<float>(pic.imgW) / static_cast<float>(pic.imgH) : 1.0F);
  glUniform2f(m_prog.uniform("u_texSize"), static_cast<float>(tw), static_cast<float>(th));
  glUniform1f(m_prog.uniform("u_bright"), static_cast<float>(m_cfg.brightness));
  glUniform1f(m_prog.uniform("u_contrast"), static_cast<float>(m_cfg.contrast));
  glUniform1f(m_prog.uniform("u_sat"), static_cast<float>(m_cfg.saturation));
  glUniform1f(m_prog.uniform("u_hue"), static_cast<float>(m_cfg.hue * M_PI / 180));
  glUniform1f(m_prog.uniform("u_temp"), static_cast<float>(m_cfg.temperature));
  glUniform4f(m_prog.uniform("u_tint"), m_tintC.r, m_tintC.g, m_tintC.b, static_cast<float>(m_cfg.tintAmount));
  glUniform1i(m_prog.uniform("u_duo"), m_cfg.duotone ? 1 : 0);
  glUniform3f(m_prog.uniform("u_dark"), m_darkC.r, m_darkC.g, m_darkC.b);
  glUniform3f(m_prog.uniform("u_light"), m_lightC.r, m_lightC.g, m_lightC.b);
  glUniform1f(m_prog.uniform("u_vignette"), static_cast<float>(m_cfg.vignette));
  // blur in device px of the texture: a share of its size, so it looks the same on any screen
  glUniform1f(m_prog.uniform("u_blur"), static_cast<float>(m_cfg.blur * std::max(tw, th) / 1000.0));
  glUniform1f(m_prog.uniform("u_grain"), static_cast<float>(m_cfg.grain));
  glUniform1f(m_prog.uniform("u_levels"), static_cast<float>(m_cfg.posterize));
  glUniform1f(m_prog.uniform("u_pixel"), static_cast<float>(m_cfg.pixelate));
  glUniform1f(m_prog.uniform("u_opacity"), opacity);
  drawUnitQuad();
}

}  // namespace undershell
