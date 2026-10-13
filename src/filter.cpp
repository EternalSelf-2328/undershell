// SPDX-License-Identifier: GPL-3.0-or-later
// The wallpaper filter (see filter.hpp).
#include "filter.hpp"

#include <EGL/egl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

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
uniform vec2 u_size;      // the surface, logical px (the whole output)
uniform vec2 u_img;       // the picture's own size (its aspect decides the fit)
uniform int u_fill;       // center crop fit stretch repeat span
uniform vec2 u_texSize;   // the texture, px
uniform float u_bright, u_contrast, u_sat, u_hue, u_temp;
uniform vec4 u_tint;      // rgb, amount
uniform int u_duo;
uniform vec3 u_dark, u_light;
uniform float u_vignette, u_blur, u_grain, u_levels, u_pixel, u_opacity;

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

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 hueTurn(vec3 c, float a) {  // about the grey axis
    const vec3 k = vec3(0.57735);
    float ca = cos(a), sa = sin(a);
    return c * ca + cross(k, c) * sa + k * dot(k, c) * (1.0 - ca);
}

void main() {
    vec2 p = v_uv * u_size;
    if (u_pixel > 0.0) p = (floor(p / u_pixel) + 0.5) * u_pixel;
    vec2 uv;
    if (!wallUv(p, uv)) { fragColor = vec4(0.0); return; }  // a letterbox: the shell's own colour shows
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

struct FilterWidget::Load {
  std::mutex lock;
  std::atomic<bool> done{false};
  std::vector<uint8_t> rgba;
  int w = 0, h = 0, imgW = 0, imgH = 0;
};

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
  if (m_texture && eglGetCurrentContext() != EGL_NO_CONTEXT) glDeleteTextures(1, &m_texture);
}

void FilterWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  m_cfg = FilterConfig::fromTable(cfg.options);
  m_tintC = noct.color(m_cfg.tint, Color::fromHex("#e2342a"));
  m_darkC = noct.color(m_cfg.dark, Color::fromHex("#1b1530"));
  m_lightC = noct.color(m_cfg.light, Color::fromHex("#ffd9a8"));
}

// the picture decoded on its own thread, no larger than the output needs
void FilterWidget::startLoad(const std::string& path, int maxSide) {
  auto load = std::make_shared<Load>();
  m_load = load;
  m_loading = true;
  std::thread([load, path, maxSide] {
    int iw = 0, ih = 0;
    if (gdk_pixbuf_get_file_info(path.c_str(), &iw, &ih) && iw > 0 && ih > 0 && iw <= 32768 && ih <= 32768) {
      GError* err = nullptr;
      GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), std::min(iw, maxSide), std::min(ih, maxSide), TRUE, &err);
      if (err) g_error_free(err);
      if (pb) {
        GdkPixbuf* rgba = gdk_pixbuf_add_alpha(pb, FALSE, 0, 0, 0);
        std::lock_guard g(load->lock);
        load->w = gdk_pixbuf_get_width(rgba);
        load->h = gdk_pixbuf_get_height(rgba);
        load->imgW = iw;
        load->imgH = ih;
        const int stride = gdk_pixbuf_get_rowstride(rgba);
        const guchar* src = gdk_pixbuf_read_pixels(rgba);
        load->rgba.resize(static_cast<size_t>(load->w) * load->h * 4);
        for (int y = 0; y < load->h; ++y)
          std::copy_n(src + static_cast<size_t>(y) * stride, load->w * 4, load->rgba.data() + static_cast<size_t>(y) * load->w * 4);
        g_object_unref(rgba);
        g_object_unref(pb);
      }
    }
    load->done = true;
  }).detach();
}

void FilterWidget::draw(const DrawContext& ctx) {
  if (ctx.movingWallpaper || ctx.wallpaper.empty() || ctx.wallpaper.rfind("color:", 0) == 0) return;  // nothing still to filter
  const int maxSide = static_cast<int>(std::ceil(std::max(ctx.outputW, ctx.outputH) * std::max(1.0F, ctx.scale)));
  if (ctx.wallpaper != m_path) {
    m_path = ctx.wallpaper;
    startLoad(m_path, maxSide);  // the old picture stays until the new one is in
  }
  if (m_load && m_load->done) {
    std::lock_guard g(m_load->lock);
    m_loading = false;
    if (!m_load->rgba.empty()) {
      if (!m_texture) glGenTextures(1, &m_texture);
      glBindTexture(GL_TEXTURE_2D, m_texture);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_load->w, m_load->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_load->rgba.data());
      glGenerateMipmap(GL_TEXTURE_2D);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
      m_texW = m_load->imgW;
      m_texH = m_load->imgH;
      m_pxW = m_load->w;
      m_pxH = m_load->h;
    }
    m_load.reset();
  }
  if (!m_texture || m_texW <= 0) return;
  if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kFrag, "filter");
  glUseProgram(m_prog.id());
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_texture);
  const int tw = m_pxW, th = m_pxH;
  glUniform1i(m_prog.uniform("u_tex"), 0);
  glUniform2f(m_prog.uniform("u_size"), ctx.w, ctx.h);
  glUniform2f(m_prog.uniform("u_img"), static_cast<float>(m_texW), static_cast<float>(m_texH));
  glUniform1i(m_prog.uniform("u_fill"), ctx.fillMode);
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
  glUniform1f(m_prog.uniform("u_opacity"), static_cast<float>(m_cfg.opacity));
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  drawUnitQuad();
  glDisable(GL_BLEND);
}

}  // namespace undershell
