// SPDX-License-Identifier: GPL-3.0-or-later
// An old TV, for the screens in a wallpaper. It shows static (snow), the
// SMPTE colour bars, a blue NO SIGNAL screen, your GIFs and videos, or flips
// between all of them like a channel being changed. With the music: a kick
// throws interference across the picture (bands slipping sideways, colours
// split, a burst of snow), videos play faster as the song rises, hard kicks
// change the channel ("CH 03" in the corner), and when the music stops the
// set switches off the old way, into a line and then a dot. Optionally a VHS
// tape's look (tracking noise, bleeding colour, "PLAY ▶"). All through a
// colour CRT: glow, scanlines, flicker, the tube's curve.
#include "looks.hpp"

#include "canvas.hpp"
#include "i18n.hpp"
#include "video.hpp"
#include "widget.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <vector>

namespace undershell {

namespace {

namespace fs = std::filesystem;

enum Channel { Snow = 0, Bars = 1, NoSignal = 2, Media = 3 };

// the picture, before the tube: content, interference, VHS
const char* kContentFragment = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform int u_mode;          // 0 snow, 1 bars, 2 no signal, 3 media
uniform sampler2D u_tex;     // luma
uniform sampler2D u_texU;    // the chromas, half size
uniform sampler2D u_texV;
uniform vec2 u_texSize;
uniform vec2 u_res;          // device px
uniform float u_cover;       // 1 cover, 0 contain
uniform float u_time;
uniform float u_static;      // snow over the picture
uniform float u_glitch;      // a kick's interference, 0..1
uniform float u_vhs;         // 0 / 1

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 snow(vec2 uv) {
    vec2 cell = floor(uv * u_res / 2.0);  // TV "pixels" of 2 device px
    float f = floor(u_time * 50.0);
    float n = hash(cell + f * vec2(17.13, 31.71));
    float line = hash(vec2(cell.y, f)) * 0.25;  // each line a little lighter or darker
    float roll = 0.08 * smoothstep(0.0, 0.08, abs(fract(uv.y - u_time * 0.15) - 0.5) - 0.38);
    float v = clamp(n * 0.85 + line - 0.1 + roll, 0.0, 1.0);
    return vec3(v);
}

vec3 bars(vec2 uv) {
    float x = uv.x, y = uv.y;
    if (y < 0.67) {
        int i = int(floor(x * 7.0));
        vec3 c[7] = vec3[7](vec3(0.75), vec3(0.75, 0.75, 0.0), vec3(0.0, 0.75, 0.75), vec3(0.0, 0.75, 0.0),
                            vec3(0.75, 0.0, 0.75), vec3(0.75, 0.0, 0.0), vec3(0.0, 0.0, 0.75));
        return c[clamp(i, 0, 6)];
    }
    if (y < 0.75) {
        int i = int(floor(x * 7.0));
        vec3 c[7] = vec3[7](vec3(0.0, 0.0, 0.75), vec3(0.0), vec3(0.75, 0.0, 0.75), vec3(0.0),
                            vec3(0.0, 0.75, 0.75), vec3(0.0), vec3(0.75));
        return c[clamp(i, 0, 6)];
    }
    if (x < 0.178) return vec3(0.0, 0.13, 0.29);   // -I
    if (x < 0.357) return vec3(1.0);               // white
    if (x < 0.535) return vec3(0.2, 0.0, 0.42);    // +Q
    if (x < 0.714) return vec3(0.0);
    if (x < 0.762) return vec3(0.03);              // the PLUGE steps
    if (x < 0.810) return vec3(0.075);
    if (x < 0.857) return vec3(0.11);
    return vec3(0.0);
}

vec3 media(vec2 uv) {
    if (u_texSize.x < 1.0) return vec3(0.0);
    float boxA = u_res.x / u_res.y, texA = u_texSize.x / u_texSize.y;
    vec2 t;
    if (u_cover > 0.5) {  // fills the box, cropping what overhangs
        vec2 s = texA > boxA ? vec2(boxA / texA, 1.0) : vec2(1.0, texA / boxA);
        t = (uv - 0.5) * s + 0.5;
    } else {              // fits inside, black bars where it does not reach
        vec2 s = texA > boxA ? vec2(1.0, boxA / texA) : vec2(texA / boxA, 1.0);
        t = (uv - 0.5) / s + 0.5;
        if (t.x < 0.0 || t.x > 1.0 || t.y < 0.0 || t.y > 1.0) return vec3(0.0);
    }
    // full range BT.601, the form the decoder hands over
    float y = texture(u_tex, t).r, u = texture(u_texU, t).r - 0.5, v = texture(u_texV, t).r - 0.5;
    return clamp(vec3(y + 1.402 * v, y - 0.344136 * u - 0.714136 * v, y + 1.772 * u), 0.0, 1.0);
}

vec3 content(vec2 uv) {
    if (u_mode == 0) return snow(uv);
    if (u_mode == 1) return bars(uv);
    if (u_mode == 2) return vec3(0.0, 0.1, 0.72);
    return media(uv);
}

void main() {
    vec2 uv = v_uv;
    float f = floor(u_time * 30.0);
    // a kick's interference: bands slip sideways, now and then a big tear
    float band = floor(uv.y * 14.0 + hash(vec2(f, 3.0)) * 6.0);
    float slip = (hash(vec2(band, f)) - 0.5) * 0.14 * u_glitch * step(1.0 - u_glitch * 0.7, hash(vec2(band, f + 9.0)));
    // VHS: a slow wobble across the lines, a tracking band near the foot
    float track = 0.0;
    if (u_vhs > 0.5) {
        slip += 0.0025 * sin(uv.y * 38.0 + u_time * 2.7);
        float ty = fract(0.86 + 0.05 * sin(u_time * 0.31));
        track = smoothstep(0.035, 0.0, abs(uv.y - ty));
        slip += track * (hash(vec2(floor(uv.y * u_res.y / 2.0), f)) - 0.5) * 0.05;
    }
    vec2 p = vec2(uv.x + slip, uv.y);
    // colours split apart on a kick (and a little, always, on tape)
    float split = 0.012 * u_glitch + 0.0035 * u_vhs;
    // (three looks at the picture only when they differ)
    vec3 col = split > 0.0 ? vec3(content(p + vec2(split, 0.0)).r, content(p).g, content(p - vec2(split, 0.0)).b) : content(p);
    if (u_vhs > 0.5) {
        float l = dot(col, vec3(0.299, 0.587, 0.114));
        col = mix(vec3(l), col, 0.85);                 // a worn tape's colour
        col = mix(col, snow(uv), track * 0.7);
    }
    col = mix(col, snow(uv), clamp(u_static + u_glitch * 0.35, 0.0, 1.0));
    fragColor = vec4(col, 1.0);
}
)";

// the colour tube: curve, glow off the mipmaps, scanlines, flicker, glass,
// and switching off into a line and then a dot
const char* kTubeFragment = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_tex;
uniform vec2 u_res;
uniform float u_glow;
uniform float u_scan;
uniform float u_flick;
uniform float u_curve;
uniform float u_bg;
uniform float u_opacity;
uniform float u_power;     // 1 on .. 0 off
void main() {
    vec2 c = v_uv * 2.0 - 1.0;
    c *= 1.0 + u_curve * 0.1 * (c.yx * c.yx);
    vec2 q = abs(c) - (1.0 - 0.06);
    float inside = 1.0 - smoothstep(-0.008, 0.004, length(max(q, 0.0)) - 0.06);
    // switching off: the picture folds into a line, the line into a dot
    float sy = u_power > 0.5 ? mix(0.006, 1.0, (u_power - 0.5) * 2.0) : 0.006;
    float sx = u_power > 0.5 ? 1.0 : max(u_power * 2.0, 0.002);
    vec2 pc = c / vec2(sx, sy);
    float lit = step(abs(pc.x), 1.0) * step(abs(pc.y), 1.0);
    vec2 uv = pc * 0.5 + 0.5;
    vec2 t = vec2(uv.x, 1.0 - uv.y);
    vec3 col = texture(u_tex, t).rgb;
    vec3 glow = textureLod(u_tex, t, 2.0).rgb * 0.55 + textureLod(u_tex, t, 4.0).rgb * 0.45;
    col += glow * u_glow * 0.6;
    col *= 1.0 - u_scan * 0.42 * (0.5 + 0.5 * cos(uv.y * u_res.y * 6.2831853 / 3.0));
    col *= 1.0 - u_flick;
    col *= 1.0 - 0.3 * u_curve * dot(c, c) * 0.5;
    col *= 1.0 + (1.0 - u_power) * 2.5;   // the folding picture burns brighter
    col = mix(col, vec3(1.0), clamp((0.65 - u_power) * 2.2, 0.0, 0.9));  // into a white line, a white dot
    col *= lit;
    float a = clamp(max(max(col.r, col.g), col.b), 0.0, 1.0) * lit;
    vec3 glass = vec3(0.02, 0.022, 0.025);
    float alpha = (u_bg + a * (1.0 - u_bg)) * inside;
    vec3 outc = (glass * u_bg * (1.0 - a) + min(col, vec3(1.0)) * max(a, lit)) * inside;
    fragColor = vec4(min(outc, vec3(alpha)), alpha) * u_opacity;
}
)";

bool isMediaFile(const fs::path& p) {
  static const char* exts[] = {".gif", ".mp4", ".webm", ".mkv", ".mov", ".avi", ".m4v", ".apng", ".webp"};
  std::string e = p.extension().string();
  std::transform(e.begin(), e.end(), e.begin(), ::tolower);
  for (const char* x : exts)
    if (e == x) return true;
  return false;
}

class Tv final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    (void)noct;
    const std::string mode = t["tv_mode"].value_or(std::string("static"));
    m_glitchAmount = std::clamp(t["tv_glitch"].value_or(0.6), 0.0, 1.0);
    m_speedAmount = std::clamp(t["tv_speed"].value_or(0.5), 0.0, 1.0);
    m_zap = std::clamp(t["tv_zap"].value_or(0.5), 0.0, 1.0);
    m_staticAmount = std::clamp(t["tv_static"].value_or(0.06), 0.0, 1.0);
    m_vhs = t["tv_vhs"].value_or(false);
    m_powerOff = t["tv_poweroff"].value_or(true);
    m_cover = t["tv_fit"].value_or(std::string("fill")) != "fit";
    m_glow = static_cast<float>(std::clamp(t["tv_glow"].value_or(0.5), 0.0, 1.0));
    m_scan = static_cast<float>(std::clamp(t["tv_scan"].value_or(0.5), 0.0, 1.0));
    m_flicker = static_cast<float>(std::clamp(t["tv_flicker"].value_or(0.2), 0.0, 1.0));
    m_curve = static_cast<float>(std::clamp(t["tv_curve"].value_or(0.3), 0.0, 1.0));
    m_glass = static_cast<float>(std::clamp(t["tv_background"].value_or(0.9), 0.0, 1.0));
    m_preview = t["tv_preview"].value_or(-1.0);
    // your files: paths, or folders of them, in order
    std::vector<std::string> files;
    auto add = [&files](const std::string& raw) {
      if (raw.empty()) return;
      const fs::path p = expandHome(raw);
      std::error_code ec;
      if (fs::is_directory(p, ec)) {
        std::vector<std::string> in;
        for (auto& e : fs::directory_iterator(p, ec))
          if (e.is_regular_file() && isMediaFile(e.path())) in.push_back(e.path().string());
        std::sort(in.begin(), in.end());
        files.insert(files.end(), in.begin(), in.end());
      } else {
        files.push_back(p.string());
      }
    };
    if (const toml::array* a = t["tv_files"].as_array()) {
      for (auto&& n : *a)
        if (auto s = n.value<std::string>()) add(*s);
    } else {
      add(t["tv_files"].value_or(std::string()));
    }
    if (files.size() > 8) files.resize(8);
    // the channels: the mode's own, or all of them when it zaps
    std::vector<int> channels;
    std::vector<std::string> paths;
    if (mode == "channels") {
      for (const std::string& f : files) channels.push_back(Media), paths.push_back(f);
      for (int c : {Bars, Snow, NoSignal}) channels.push_back(c), paths.push_back("");
    } else if (mode == "media" && !files.empty()) {
      for (const std::string& f : files) channels.push_back(Media), paths.push_back(f);
    } else {
      channels.push_back(mode == "colorbars" ? Bars : mode == "nosignal" || mode == "media" ? NoSignal : Snow);
      paths.push_back("");
    }
    if (channels != m_channels || paths != m_paths) {
      m_channels = channels;
      m_paths = paths;
      m_sources.clear();
      m_current = 0;
    }
    if (m_preview >= 0) {
      m_glitch = std::exp(-m_preview / 0.12) * m_glitchAmount;
      m_power = 1;
      m_live = true;
    }
  }

  void tick(const Beat& b) override {
    if (m_preview >= 0) return;
    m_dt += b.dt;  // the video's clock moves on in draw (it needs GL)
    m_time += b.dt;
    m_live = b.live;
    m_energy = b.energy;
    if (b.kick) m_glitch = std::max(m_glitch, b.strength * m_glitchAmount);
    m_glitch *= std::exp(-b.dt / 0.12);
    if (m_glitch < 0.004) m_glitch = 0;
    // a hard kick changes the channel, not more often than Zap allows
    if (m_switch >= 0 && (m_switch += b.dt) > 0.2) m_switch = -1;
    if (b.kick && m_zap > 0 && m_channels.size() > 1 && b.strength > 0.75 && m_time - m_lastZap > 8 - 6.8 * m_zap) {
      m_lastZap = m_time;
      m_current = (m_current + 1) % m_channels.size();
      m_switch = 0;
      m_osd = 1.6;
    }
    if (m_osd > 0) m_osd -= b.dt;
    // with the music gone the set switches off; it comes back with it
    m_silence = b.live ? 0 : m_silence + b.dt;
    const double target = !m_powerOff || b.live || m_silence < 1.5 ? 1.0 : 0.0;
    m_power += std::clamp(target - m_power, -b.dt / 0.5, b.dt / 0.35);
  }

  void draw(const DrawContext& ctx, float opacity) override {
    GLint prevFbo = 0, vp[4] = {0, 0, 1, 1};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, vp);
    const int W = std::max(1, vp[2]), H = std::max(1, vp[3]);
    const int chan = m_channels.empty() ? Snow : m_channels[m_current % m_channels.size()];
    // the video due now (its own clock, faster with the music)
    VideoSource* src = nullptr;
    if (chan == Media && m_preview < 0) {
      auto& s = m_sources[m_current];
      if (!s) {
        s = std::make_unique<VideoSource>();
        s->open(m_paths[m_current], W, H);
      }
      src = s.get();
      const double speed = m_live ? std::clamp(1 + (m_energy - 0.5) * 1.2 * m_speedAmount, 0.5, 1.8) : 1.0;
      src->advance(m_dt, speed);
    }
    m_dt = 0;
    // the picture, into its own texture
    if (!m_tex || m_texW != W || m_texH != H) {
      if (!m_tex) glGenTextures(1, &m_tex);
      glBindTexture(GL_TEXTURE_2D, m_tex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      if (!m_fbo) glGenFramebuffers(1, &m_fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
      m_texW = W;
      m_texH = H;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, W, H);
    if (!m_content.valid()) m_content.create(kQuadVertexShader, kContentFragment, "tv");
    glUseProgram(m_content.id());
    auto U = [this](const char* n) { return m_content.uniform(n); };
    // between channels, a moment of snow; a video not decoded yet shows snow too
    const bool ready = chan != Media || (src && src->texture());
    const int mode = m_switch >= 0 || !ready ? Snow : chan;
    glUniform1i(U("u_mode"), mode);
    const bool planes = src && src->texture();
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, planes ? src->textureU() : 0);
    glUniform1i(U("u_texU"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, planes ? src->textureV() : 0);
    glUniform1i(U("u_texV"), 2);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, planes ? src->texture() : 0);
    glUniform1i(U("u_tex"), 0);
    glUniform2f(U("u_texSize"), src ? static_cast<float>(src->width()) : 0.0F, src ? static_cast<float>(src->height()) : 0.0F);
    glUniform2f(U("u_res"), static_cast<float>(W), static_cast<float>(H));
    glUniform1f(U("u_cover"), m_cover ? 1.0F : 0.0F);
    glUniform1f(U("u_time"), static_cast<float>(m_preview >= 0 ? 12.34 : m_time));
    glUniform1f(U("u_static"), mode == Snow ? 0.0F : static_cast<float>(m_staticAmount));
    glUniform1f(U("u_glitch"), static_cast<float>(m_glitch));
    glUniform1f(U("u_vhs"), m_vhs ? 1.0F : 0.0F);
    glDisable(GL_BLEND);
    drawUnitQuad();
    // the words on screen
    if (ctx.text) {
      Canvas& c = m_canvas;
      c.begin(ctx.w, ctx.h, ctx.scale, ctx.text);
      c.setTransform(1, 0, 0);
      const float px = std::max(9.0F, ctx.h * 0.075F);
      TextStyle st;
      st.family = "JetBrains Mono";
      st.weight = 700;
      st.size = px;
      if (mode == NoSignal) {
        const std::string msg = spanishUi() ? "SIN SEÑAL" : "NO SIGNAL";
        const Canvas::Size z = Canvas::measure(msg, st);
        c.text(msg, st, (ctx.w - z.w) / 2, (ctx.h - z.h) / 2, Color{1, 1, 1, 1});
      }
      if (m_vhs) {
        c.text("PLAY ▶", st, ctx.w * 0.07F, ctx.h * 0.08F, Color{1, 1, 1, 1}, 0.9F);
        const long s = static_cast<long>(m_preview >= 0 ? 192 : m_time);
        const std::string tc = std::format("SP {}:{:02}:{:02}", s / 3600, (s / 60) % 60, s % 60);
        const Canvas::Size z = Canvas::measure(tc, st);
        c.text(tc, st, ctx.w * 0.93F - z.w, ctx.h * 0.86F - z.h, Color{1, 1, 1, 1}, 0.9F);
      }
      if (m_osd > 0 || m_preview >= 0) {
        const std::string ch = std::format("CH {:02}", m_current + 1);
        const Canvas::Size z = Canvas::measure(ch, st);
        const float a = static_cast<float>(std::min(1.0, m_preview >= 0 ? 1.0 : m_osd / 0.3));
        c.text(ch, st, ctx.w * 0.93F - z.w + px * 0.06F, ctx.h * 0.08F + px * 0.06F, Color{0, 0, 0, 1}, a * 0.8F);
        c.text(ch, st, ctx.w * 0.93F - z.w, ctx.h * 0.08F, Color{0.45F, 1.0F, 0.45F, 1}, a);
      }
    }
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glGenerateMipmap(GL_TEXTURE_2D);
    // through the tube
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    if (!m_tube.valid()) m_tube.create(kQuadVertexShader, kTubeFragment, "tvtube");
    glUseProgram(m_tube.id());
    auto T = [this](const char* n) { return m_tube.uniform(n); };
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glUniform1i(T("u_tex"), 0);
    glUniform2f(T("u_res"), static_cast<float>(W), static_cast<float>(H));
    glUniform1f(T("u_glow"), m_glow * (1 + 0.5F * static_cast<float>(m_glitch)));
    glUniform1f(T("u_scan"), m_scan);
    glUniform1f(T("u_flick"), m_flicker * 0.12F * (m_preview >= 0 ? 0.5F : m_rng.next()));
    glUniform1f(T("u_curve"), m_curve);
    glUniform1f(T("u_bg"), m_glass);
    glUniform1f(T("u_opacity"), opacity);
    glUniform1f(T("u_power"), static_cast<float>(m_power));
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawUnitQuad();
    glDisable(GL_BLEND);
  }

  [[nodiscard]] bool visible() const override { return true; }
  [[nodiscard]] bool moving() const override {
    const int chan = m_channels.empty() ? Snow : m_channels[m_current % m_channels.size()];
    const bool transition = (m_power > 0.001 && m_power < 0.999) || m_glitch > 0 || m_switch >= 0 || m_osd > 0;
    const bool animated = m_power > 0.001 && (chan == Snow || chan == Media || m_vhs);
    return transition || animated || m_live;
  }

private:
  double m_glitchAmount = 0.6, m_speedAmount = 0.5, m_zap = 0.5, m_staticAmount = 0.06, m_preview = -1;
  bool m_vhs = false, m_powerOff = true, m_cover = true, m_live = false;
  float m_glow = 0.5F, m_scan = 0.5F, m_flicker = 0.2F, m_curve = 0.3F, m_glass = 0.9F;
  double m_time = 0, m_dt = 0, m_energy = 0, m_glitch = 0, m_switch = -1, m_osd = 0, m_lastZap = -100, m_silence = 0;
  double m_power = 1;
  std::vector<int> m_channels{Snow};
  std::vector<std::string> m_paths{""};
  size_t m_current = 0;
  std::map<size_t, std::unique_ptr<VideoSource>> m_sources;  // opened when first shown, kept (a cached clip stays)
  GLuint m_fbo = 0, m_tex = 0;
  int m_texW = 0, m_texH = 0;
  Program m_content, m_tube;
  Canvas m_canvas;
  BoltRandom m_rng{404};
};

}  // namespace

std::unique_ptr<StrokeLook> makeTv() { return std::make_unique<Tv>(); }

}  // namespace undershell
