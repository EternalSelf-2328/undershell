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
        vec4 L = texture(u_lights, vec2(v_uv.x, 1.0 - v_uv.y));  // an off-screen render: rows bottom up
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

void FilterWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  m_cfg = FilterConfig::fromTable(cfg.options);
  m_tintC = noct.color(m_cfg.tint, Color::fromHex("#e2342a"));
  m_darkC = noct.color(m_cfg.dark, Color::fromHex("#1b1530"));
  m_lightC = noct.color(m_cfg.light, Color::fromHex("#ffd9a8"));
  effectLibrary().refresh();  // a file added while undershell was not watching
  particleLibrary().refresh();
  m_fx.configure(cfg.options, noct);
  m_pt.configure(cfg.options, noct);
}

void FilterWidget::draw(const DrawContext& ctx) {
  if (ctx.movingWallpaper || ctx.wallpaper.empty() || ctx.wallpaper.rfind("color:", 0) == 0) return;  // nothing still to filter
  const int maxSide = static_cast<int>(std::ceil(std::max(ctx.outputW, ctx.outputH) * std::max(1.0F, ctx.scale)));
  if (!m_fade.update(ctx.wallpaper, maxSide, ctx.now)) return;  // the old picture stays until the new one is in
  m_fx.build(kHead, kFit, kBody, kLights);
  const WallPic& cur = *m_fade.current();
  // the picture's own uniforms: where it lies on the output
  auto picture = [&](const Program& prog, const WallPic& pic) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, pic.tex);
    glUniform1i(prog.uniform("u_fill"), ctx.fillMode);
    glUniform2f(prog.uniform("u_size"), ctx.w, ctx.h);
    glUniform2f(prog.uniform("u_img"), static_cast<float>(pic.imgW), static_cast<float>(pic.imgH));
    glUniform1f(prog.uniform("u_aspect"), pic.imgH > 0 ? static_cast<float>(pic.imgW) / static_cast<float>(pic.imgH) : 1.0F);
  };
  m_fx.drawLights(ctx, false, [&](const Program& prog) { picture(prog, cur); });
  m_fx.use(0, ctx, false);
  const Program& prog = m_fx.program();
  glUniform2f(prog.uniform("u_screen"), ctx.w * ctx.scale, ctx.h * ctx.scale);
  const float opacity = static_cast<float>(m_cfg.opacity);
  // a new wallpaper: the old one under it, the new one fading in on top
  // (only then does it blend: alone, it lands on a cleared surface)
  if (const WallPic* prev = m_fade.previous()) {
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    picture(prog, *prev);
    drawPicture(*prev, ctx, opacity);
    picture(prog, cur);
    drawPicture(cur, ctx, opacity * m_fade.mix());
    glDisable(GL_BLEND);
  } else {
    glDisable(GL_BLEND);
    picture(prog, cur);
    drawPicture(cur, ctx, opacity);
  }
  // the particles over it, in front of or behind the subject
  m_pt.draw(ctx, static_cast<float>(cur.imgW), static_cast<float>(cur.imgH));
}

void FilterWidget::drawPicture(const WallPic& pic, const DrawContext& ctx, float opacity) {
  const Program& prog = m_fx.program();
  const int tw = pic.texW, th = pic.texH;
  glUniform2f(prog.uniform("u_texSize"), static_cast<float>(tw), static_cast<float>(th));
  glUniform1f(prog.uniform("u_bright"), static_cast<float>(m_cfg.brightness));
  glUniform1f(prog.uniform("u_contrast"), static_cast<float>(m_cfg.contrast));
  glUniform1f(prog.uniform("u_sat"), static_cast<float>(m_cfg.saturation));
  glUniform1f(prog.uniform("u_hue"), static_cast<float>(m_cfg.hue * M_PI / 180));
  glUniform1f(prog.uniform("u_temp"), static_cast<float>(m_cfg.temperature));
  glUniform4f(prog.uniform("u_tint"), m_tintC.r, m_tintC.g, m_tintC.b, static_cast<float>(m_cfg.tintAmount));
  glUniform1i(prog.uniform("u_duo"), m_cfg.duotone ? 1 : 0);
  glUniform3f(prog.uniform("u_dark"), m_darkC.r, m_darkC.g, m_darkC.b);
  glUniform3f(prog.uniform("u_light"), m_lightC.r, m_lightC.g, m_lightC.b);
  glUniform1f(prog.uniform("u_vignette"), static_cast<float>(m_cfg.vignette));
  // blur in device px of the texture: a share of its size, so it looks the same on any screen
  glUniform1f(prog.uniform("u_blur"), static_cast<float>(m_cfg.blur * std::max(tw, th) / 1000.0));
  glUniform1f(prog.uniform("u_grain"), static_cast<float>(m_cfg.grain));
  glUniform1f(prog.uniform("u_levels"), static_cast<float>(m_cfg.posterize));
  glUniform1f(prog.uniform("u_pixel"), static_cast<float>(m_cfg.pixelate));
  glUniform1f(prog.uniform("u_opacity"), opacity);
  drawUnitQuad();
}

}  // namespace undershell
