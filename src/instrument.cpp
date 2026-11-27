// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas.hpp"
#include "gl.hpp"
#include "stereo.hpp"
#include "widget.hpp"

#include <array>
#include <cmath>

namespace undershell {
namespace {

constexpr int kColumns = 160, kBands = 48;
constexpr double kHistoryStep = 0.04;
constexpr const char* kFragment = R"GLSL(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform highp sampler2D u_reading;
uniform vec2 u_size;
uniform vec4 u_plot;
uniform vec2 u_level, u_peak;
uniform vec3 u_left, u_right, u_case, u_mark, u_warning;
uniform int u_mode, u_latest;
uniform float u_plate;

vec4 over(vec4 base, vec3 rgb, float alpha) {
    return vec4(rgb * alpha, alpha) + base * (1.0 - alpha);
}

float roundedBox(vec2 p, vec2 halfSize, float radius) {
    vec2 d = abs(p) - halfSize + radius;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - radius;
}

float ruled(float coordinate, float divisions, float span) {
    float d = abs(fract(coordinate * divisions + 0.5) - 0.5) * span / divisions;
    return 1.0 - smoothstep(0.15, 1.15, d);
}

void main() {
    vec2 p = v_uv * u_size;
    float grain = fract(sin(dot(floor(p), vec2(12.9898,78.233))) * 43758.5453) - 0.5;
    float shape = roundedBox(p - u_size / 2.0, u_size / 2.0 - 0.7, clamp(u_size.y * 0.074, 3.0, 10.0));
    float caseAlpha = (1.0 - smoothstep(-0.6, 0.6, shape)) * u_plate;
    float bevel = smoothstep(-1.65, -0.35, shape);
    vec3 metal = u_case * mix(1.16, 0.82, v_uv.y) + grain * 0.013;
    vec4 base = over(vec4(0), mix(metal, u_mark, bevel * mix(0.24, 0.05, v_uv.y)), caseAlpha);
    vec2 span = u_plot.zw;
    vec2 plot = (p - u_plot.xy) / span;
    vec2 uv = clamp(plot, 0.0, 0.99999);
    float border = roundedBox((plot - 0.5) * span, span / 2.0, 3.0);
    if (border > 0.55) {
        fragColor = base;
        return;
    }
    float inside = 1.0 - smoothstep(-0.55, 0.55, border);
    vec3 screen = mix(metal * 0.43, metal * 0.67, sin(v_uv.y * 3.14159)) + grain * 0.007;
    vec4 face = over(base, mix(screen, u_mark, smoothstep(-2.0, -0.5, border) * 0.16), inside * u_plate);

    if (u_mode == 1) {
        int ch = uv.y < 0.5 ? 0 : 1;
        float track = fract(uv.y * 2.0);
        float height = max(1.0, span.y / 2.0);
        float bars = 1.0 - smoothstep(0.14, 0.14 + 2.0 / height, abs(track - 0.5));
        float index = floor(uv.x * 32.0);
        float cell = fract(uv.x * 32.0);
        float pixel = min(cell, 1.0 - cell) * span.x / 32.0;
        float gap = smoothstep(0.32, 1.35, pixel);
        float level = clamp(u_level[ch] * 32.0 - index, 0.0, 1.0);
        float hold = 1.0 - smoothstep(0.35, 1.5, abs(uv.x - u_peak[ch]) * span.x);
        vec3 channel = ch == 0 ? u_left : u_right;
        vec3 signal = uv.x < 0.875 ? channel : u_warning;
        face = over(face, u_mark, ruled(uv.x, 4.0, span.x) * inside * 0.17);
        face = over(face, signal, bars * gap * inside * (0.12 + 0.88 * level));
        fragColor = over(face, mix(signal, u_mark, 0.6), bars * hold * inside * step(0.01, u_peak[ch]));
        return;
    }

    if (u_mode == 2) {
        ivec2 dimensions = textureSize(u_reading, 0);
        int col = (int(uv.x * float(dimensions.x)) + u_latest + 1) % dimensions.x;
        int row = int((1.0 - uv.y) * float(dimensions.y));
        float energy = texelFetch(u_reading, ivec2(col, min(row, dimensions.y - 1)), 0).r;
        vec3 heat = mix(u_left * 0.65, u_right, smoothstep(0.28, 0.86, energy));
        heat = mix(heat, vec3(1.0, 0.94, 0.79), smoothstep(0.78, 1.0, energy) * 0.70);
        face = over(face, heat, pow(energy, 1.18) * inside);
        float grid = max(ruled(uv.x, 4.0, span.x), ruled(uv.y, 3.0, span.y));
        fragColor = over(face, u_mark, grid * inside * 0.15);
        return;
    }

    float split = 1.0 - smoothstep(0.28, 1.25, abs(uv.y - 0.5) * span.y);
    int ch = uv.y < 0.5 ? 0 : 1;
    float channelY = fract(uv.y * 2.0);
    float sweep = ruled(uv.x, 8.0, span.x);
    float center = ruled(channelY, 2.0, span.y * 0.5);
    float hash = ruled(uv.x, 40.0, span.x)
               * (1.0 - smoothstep(1.0, 2.8, abs(channelY - 0.5) * span.y * 0.5));
    float grid = max(max(sweep * 0.15, center * 0.17), max(hash * 0.30, split * 0.32));
    face = over(face, u_mark, grid * inside);
    float trace = texture(u_reading, vec2(uv.x, (float(ch) + 0.5) / 2.0)).r;
    float curve = (channelY - (0.5 - trace * 0.42)) * span.y / 2.0;
    float dist = abs(curve) / max(1.0, length(vec2(dFdx(curve), dFdy(curve))));
    vec3 signal = ch == 0 ? u_left : u_right;
    face = over(face, signal, (1.0 - smoothstep(1.1, 5.2, dist)) * inside * 0.26);
    fragColor = over(face, mix(signal, u_mark, 0.25), (1.0 - smoothstep(0.45, 1.4, dist)) * inside * 0.95);
}
)GLSL";

class InstrumentWidget final : public WidgetImpl {
public:
  ~InstrumentWidget() override {
    if (m_texture) glDeleteTextures(1, &m_texture);
  }

  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override {
    const auto style = cfg.options["style"].value_or(std::string("scope"));
    const int mode = style == "meters" ? 1 : style == "spectrogram" ? 2 : 0;
    if (m_mode != mode) {
      m_mode = mode;
      m_history.fill(0);
      m_latest = kColumns - 1;
      m_historyAt = -1;
      m_l = m_r = m_peakL = m_peakR = 0;
      m_dirty = true;
    }
    m_leftColor = noct.color(cfg.options["color"].value_or(std::string("primary")), Color::fromHex("#68ffd0"));
    m_rightColor = noct.color(cfg.options["color2"].value_or(std::string("tertiary")), Color::fromHex("#ffd090"));
    const Color surface = noct.color("surface", Color::fromHex("#25252a"));
    m_case = {surface.r * 0.38F + 0.044F, surface.g * 0.38F + 0.048F, surface.b * 0.38F + 0.054F, 1};
    m_mark = Color::fromHex("#c4c9d0");
    const auto soften = [&](Color c) {
      const float luminance = c.r * 0.2126F + c.g * 0.7152F + c.b * 0.0722F;
      const float lift = std::clamp((0.42F - luminance) * 1.4F, 0.0F, 0.50F);
      return Color{std::lerp(c.r, 1.0F, lift), std::lerp(c.g, 1.0F, lift), std::lerp(c.b, 1.0F, lift), 1};
    };
    m_leftColor = soften(m_leftColor);
    m_rightColor = soften(m_rightColor);
    const double gain = cfg.options["gain"].value_or(1.0);
    m_gain = std::isfinite(gain) ? static_cast<float>(std::clamp(gain, 0.2, 4.0)) : 1.0F;
    m_plate = cfg.options["plate"].value_or(true);
  }

  void tick(const TickContext& ctx) override {
    m_active = !ctx.audio.silent || ctx.audio.stereo;
    if (m_mode == 2) {
      if (!m_active || !ctx.audio.bands || ctx.audio.bands->empty()) {
        m_historyAt = -1;
        return;
      }
      const int steps = m_historyAt < 0 ? 1 :
          static_cast<int>(std::clamp((ctx.now - m_historyAt) / kHistoryStep, 0.0, double(kColumns)));
      if (!steps) return;
      if (m_historyAt < 0 || steps == kColumns) m_historyAt = ctx.now;
      else m_historyAt += steps * kHistoryStep;
      m_latest = (m_latest + steps) % kColumns;
      const auto& bands = *ctx.audio.bands;
      for (int row = 0; row < kBands; ++row) {
        const size_t band = static_cast<size_t>(row) * bands.size() / kBands;
        const auto value = static_cast<uint8_t>(255 * std::clamp(bands[band] * m_gain, 0.0F, 1.0F));
        for (int i = 0; i < steps; ++i) m_history[row * kColumns + (kColumns + m_latest - i) % kColumns] = value;
      }
      m_dirty = true;
      return;
    }

    if (ctx.audio.stereo) {
      m_reading = *ctx.audio.stereo;
    } else if (m_active && !ctx.audio.onsets) {  // the editor's demo spectrum
      for (int i = 0; i < StereoFrame::samples; ++i) {
        const double t = i * 0.105 + ctx.now * 4;
        m_reading.wave[i] = 0.45F * static_cast<float>(std::sin(t));
        m_reading.wave[i + StereoFrame::samples] = 0.35F * static_cast<float>(std::sin(t * 1.5 + 0.7));
      }
      m_reading.left = static_cast<float>(0.2 + 0.15 * std::sin(ctx.now * 5));
      m_reading.right = static_cast<float>(0.2 + 0.15 * std::sin(ctx.now * 4));
    } else {
      m_reading = {};
    }
    if (m_mode == 1) {
      auto level = [&](float rms) { return std::clamp((20 * std::log10(std::max(1e-6F, rms * m_gain)) + 48) / 48, 0.0F, 1.0F); };
      const float dt = static_cast<float>(std::clamp(ctx.dt, 0.0, 0.2));
      auto update = [dt, now = ctx.now](float measured, float& smoothed, float& peak, double& heldUntil) {
        smoothed = std::lerp(smoothed, measured, 1 - std::exp(-dt * (measured > smoothed ? 30 : 8)));
        if (measured > peak) {
          peak = measured;
          heldUntil = now + 0.85;
        } else if (now >= heldUntil) {
          peak = std::max(measured, peak - dt * 0.65F);
        }
        if (measured == 0 && smoothed < 0.002F) smoothed = 0;
      };
      update(level(m_reading.left), m_l, m_peakL, m_heldL);
      update(level(m_reading.right), m_r, m_peakR, m_heldR);
    }
    if (m_mode == 0) {
      for (float& value : m_reading.wave) value = std::clamp(value * m_gain, -1.0F, 1.0F);
      m_dirty = true;
    }
  }

  void draw(const DrawContext& ctx) override {
    if (!m_program.valid()) m_program.create(kQuadVertexShader, kFragment, "instrument");
    if (!m_texture) glGenTextures(1, &m_texture);
    glUseProgram(m_program.id());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    if (m_mode != m_textureMode) {
      m_textureMode = m_mode;
      const auto filter = m_mode == 2 ? GL_NEAREST : GL_LINEAR;
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      if (m_mode == 2) glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kColumns, kBands, 0, GL_RED, GL_UNSIGNED_BYTE, m_history.data());
      else glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, StereoFrame::samples, 2, 0, GL_RED, GL_FLOAT, m_reading.wave.data());
      m_dirty = false;
    }
    if (m_dirty && m_mode == 2)
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kColumns, kBands, GL_RED, GL_UNSIGNED_BYTE, m_history.data());
    if (m_dirty && m_mode == 0)
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, StereoFrame::samples, 2, GL_RED, GL_FLOAT, m_reading.wave.data());
    m_dirty = false;
    auto uniform = [&](const char* key) { return m_program.uniform(key); };
    glUniform1i(uniform("u_reading"), 0);
    glUniform1i(uniform("u_mode"), m_mode);
    glUniform1i(uniform("u_latest"), m_latest);
    glUniform1f(uniform("u_plate"), m_plate ? 0.96F : 0.0F);
    glUniform2f(uniform("u_size"), ctx.w, ctx.h);
    const float left = std::min(m_mode == 2 ? 47.0F : 27.0F, ctx.w * (m_mode == 2 ? 0.23F : 0.13F));
    const float right = std::min(16.0F, ctx.w * 0.06F);
    const float top = std::min(13.0F, ctx.h * 0.12F);
    const float bottom = std::min(m_mode == 0 ? 13.0F : 24.0F, ctx.h * (m_mode == 0 ? 0.12F : 0.24F));
    const float w = std::max(1.0F, ctx.w - left - right), h = std::max(1.0F, ctx.h - top - bottom);
    glUniform4f(uniform("u_plot"), left, top, w, h);
    glUniform2f(uniform("u_level"), m_l, m_r);
    glUniform2f(uniform("u_peak"), m_peakL, m_peakR);
    glUniform3f(uniform("u_case"), m_case.r, m_case.g, m_case.b);
    glUniform3f(uniform("u_mark"), m_mark.r, m_mark.g, m_mark.b);
    glUniform3f(uniform("u_warning"), 1.0F, 0.53F, 0.36F);
    glUniform3f(uniform("u_left"), m_leftColor.r, m_leftColor.g, m_leftColor.b);
    glUniform3f(uniform("u_right"), m_rightColor.r, m_rightColor.g, m_rightColor.b);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawUnitQuad();
    glDisable(GL_BLEND);

    if (!ctx.text || ctx.w < 122 || ctx.h < 55) return;
    m_canvas.begin(ctx.w, ctx.h, ctx.scale, ctx.text);
    const TextStyle label{.family = "JetBrains Mono", .size = std::clamp(ctx.h * 0.10F, 8.0F, 11.0F), .weight = 600};
    auto write = [&](const std::string& value, float x, float y, float align, Color color) {
      const auto measured = Canvas::measure(value, label);
      m_canvas.text(value, label, std::clamp(x - measured.w * align, 3.0F, std::max(3.0F, ctx.w - measured.w - 3.0F)),
                    y - measured.h * 0.5F, color);
    };
    if (m_mode == 2) {
      const bool frequencyLabels = left >= 42.0F && h >= 46.0F;
      if (frequencyLabels) {
        write("20 kHz", left - 5, top + 4, 1, m_mark);
        write("2 kHz", left - 5, top + h / 3, 1, m_mark);
        write("200 Hz", left - 5, top + h * 2 / 3, 1, m_mark);
        write("20 Hz", left - 5, top + h - 4, 1, m_mark);
      }
      write("−6.4 s", left, top + h + bottom / 2, 0, m_mark);
      write("0 s", left + w, top + h + bottom / 2, 1, m_mark);
    } else {
      write("L", left / 2, top + h * 0.25F, 0.5F, m_leftColor);
      write("R", left / 2, top + h * 0.75F, 0.5F, m_rightColor);
      if (m_mode == 1 && ctx.w >= 174) {
        const std::array<const char*, 5> marks = {"−48", "−36", "−24", "−12", "0"};
        for (int i = 0; i < 5; ++i) write(marks[i], left + w * i / 4, top + h + bottom / 2, 0.5F, m_mark);
        if (ctx.w >= 228) write("dBFS", left + w * 0.90F, top + h + bottom / 2, 0.5F, m_mark);
      }
    }
  }

  bool animating(const TickContext&) const override { return m_active || (m_mode == 1 && (m_l > 0 || m_r > 0 || m_peakL > 0 || m_peakR > 0)); }
  int fps() const override { return 30; }
  bool usesAudio() const override { return true; }
  bool usesStereo() const override { return m_mode != 2; }
  Color accent() const override { return m_leftColor; }

private:
  Program m_program;
  Canvas m_canvas;
  GLuint m_texture = 0;
  int m_mode = 0, m_textureMode = -1, m_latest = kColumns - 1;
  bool m_plate = true, m_active = false, m_dirty = true;
  float m_gain = 1, m_l = 0, m_r = 0;
  float m_peakL = 0, m_peakR = 0;
  double m_heldL = 0, m_heldR = 0, m_historyAt = -1;
  Color m_leftColor, m_rightColor, m_case, m_mark;
  StereoFrame m_reading;
  std::array<uint8_t, kColumns * kBands> m_history{};
};

}  // namespace

std::unique_ptr<WidgetImpl> makeInstrument() { return std::make_unique<InstrumentWidget>(); }

}  // namespace undershell
