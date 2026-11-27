// SPDX-License-Identifier: GPL-3.0-or-later
#include "gl.hpp"
#include "stereo.hpp"
#include "widget.hpp"

#include <array>
#include <cmath>
#include <numbers>

namespace undershell {
namespace {

constexpr int kColumns = 160, kBands = 48;
constexpr const char* kFragment = R"GLSL(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform highp sampler2D u_reading;
uniform vec2 u_size;
uniform vec2 u_level;
uniform vec3 u_left, u_right;
uniform int u_mode, u_latest;
uniform float u_ink, u_plate;

float line(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    return length(p - a - ab * clamp(dot(p-a, ab) / dot(ab, ab), 0.0, 1.0));
}

void main() {
    vec2 uv = v_uv;
    vec2 p = uv * u_size;
    vec4 base = vec4(vec3(0.018, 0.026, 0.038) * u_plate, u_plate);
    if (u_mode == 1) {
        float side = step(0.5, uv.x);
        vec2 dial = vec2(fract(uv.x * 2.0), uv.y);
        float unit = min(u_size.x * 0.50, u_size.y);
        vec2 d = (dial - vec2(0.5, 0.82)) * vec2(u_size.x * 0.50, u_size.y) / unit;
        float radius = 0.38;
        float a = atan(d.x, -d.y);
        float tick = abs(fract((a + 1.05) / 0.15 + 0.5) - 0.5);
        vec3 colour = mix(u_left, u_right, side);
        float ink = 0.0;
        if (abs(a) < 1.14) {
            ink = (1.0 - smoothstep(0.01, 0.022, abs(length(d) - radius))) * 0.38;
            if (length(d) >= radius && length(d) < radius + 0.045) ink = max(ink, 1.0 - smoothstep(0.07, 0.19, tick));
        }
        float reading = mix(u_level.x, u_level.y, side);
        float needle = mix(-1.05, 1.05, reading);
        float pointer = line(d, vec2(0.0), vec2(sin(needle), -cos(needle)) * radius);
        ink = max(ink, 1.0 - smoothstep(0.002, 0.012, pointer));
        ink = max(ink, 1.0 - smoothstep(0.021, 0.03, length(d)));
        float alpha = ink * u_ink;
        fragColor = vec4(colour * alpha, alpha) + base * (1.0 - alpha);
        return;
    }
    vec2 grid = abs(fract(uv * vec2(12.0, 8.0) + 0.5) - 0.5) * u_size / vec2(12.0, 8.0);
    float fine = (1.0 - smoothstep(0.0, 1.3, min(grid.x, grid.y))) * 0.16 * u_ink;
    base = vec4(u_left * fine, fine) + base * (1.0 - fine);
    if (u_mode == 2) {
        ivec2 size = textureSize(u_reading, 0);
        int col = (int(min(uv.x, 0.99999) * float(size.x)) + u_latest + 1) % size.x;
        int row = int(min(1.0 - uv.y, 0.99999) * float(size.y));
        float energy = texelFetch(u_reading, ivec2(col, row), 0).r;
        vec3 heat = mix(u_left, u_right, smoothstep(0.15, 0.85, energy));
        heat = mix(heat, vec3(1.0), smoothstep(0.75, 1.0, energy) * 0.5);
        float a = sqrt(energy) * u_ink;
        fragColor = vec4(heat * a, a) + base * (1.0 - a);
        return;
    }
    for (int ch = 0; ch < 2; ++ch) {
        float trace = texture(u_reading, vec2(uv.x, (float(ch) + 0.5) / 2.0)).r;
        float center = ch == 0 ? 0.30 : 0.70;
        float dist = abs(p.y - (center - trace * 0.18) * u_size.y);
        float a = (1.0 - smoothstep(0.7, 2.7, dist)) * u_ink;
        vec3 colour = ch == 0 ? u_left : u_right;
        float haze = (1.0 - smoothstep(1.4, 6.0, dist)) * 0.22 * u_ink;
        vec4 glow = vec4(colour * haze, haze) + base * (1.0 - haze);
        base = vec4(mix(colour, vec3(1.0), 0.25) * a, a) + glow * (1.0 - a);
    }
    fragColor = base;
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
      m_dirty = true;
    }
    m_leftColor = noct.color(cfg.options["color"].value_or(std::string("primary")), Color::fromHex("#68ffd0"));
    m_rightColor = noct.color(cfg.options["color2"].value_or(std::string("tertiary")), Color::fromHex("#ffd090"));
    const double gain = cfg.options["gain"].value_or(1.0);
    m_gain = std::isfinite(gain) ? static_cast<float>(std::clamp(gain, 0.2, 4.0)) : 1.0F;
    m_plate = cfg.options["plate"].value_or(true);
  }

  void tick(const TickContext& ctx) override {
    m_active = !ctx.audio.silent || ctx.audio.stereo;
    if (m_mode == 2) {
      if (!m_active || !ctx.audio.bands || ctx.audio.bands->empty()) return;
      m_latest = (m_latest + 1) % kColumns;
      const auto& bands = *ctx.audio.bands;
      for (int row = 0; row < kBands; ++row) {
        const size_t band = static_cast<size_t>(row) * bands.size() / kBands;
        m_history[static_cast<size_t>(row * kColumns + m_latest)] =
            static_cast<uint8_t>(255 * std::clamp(bands[band] * m_gain, 0.0F, 1.0F));
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
    auto level = [&](float rms) { return std::clamp((20 * std::log10(std::max(1e-6F, rms * m_gain)) + 50) / 50, 0.0F, 1.0F); };
    const float ease = static_cast<float>(1 - std::exp(-std::clamp(ctx.dt, 0.0, 0.2) * 12));
    m_l += (level(m_reading.left) - m_l) * ease;
    m_r += (level(m_reading.right) - m_r) * ease;
    if (!m_active && m_l < 0.002F && m_r < 0.002F) m_l = m_r = 0;
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
    glUniform1f(uniform("u_ink"), 1);
    glUniform1f(uniform("u_plate"), m_plate ? 0.96F : 0.0F);
    glUniform2f(uniform("u_size"), ctx.w, ctx.h);
    glUniform2f(uniform("u_level"), m_l, m_r);
    glUniform3f(uniform("u_left"), m_leftColor.r, m_leftColor.g, m_leftColor.b);
    glUniform3f(uniform("u_right"), m_rightColor.r, m_rightColor.g, m_rightColor.b);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawUnitQuad();
    glDisable(GL_BLEND);
  }

  bool animating(const TickContext&) const override { return m_active || (m_mode == 1 && (m_l > 0 || m_r > 0)); }
  int fps() const override { return 30; }
  bool usesAudio() const override { return true; }
  bool usesStereo() const override { return m_mode != 2; }
  Color accent() const override { return m_leftColor; }

private:
  Program m_program;
  GLuint m_texture = 0;
  int m_mode = 0, m_textureMode = -1, m_latest = kColumns - 1;
  bool m_plate = true, m_active = false, m_dirty = true;
  float m_gain = 1, m_l = 0, m_r = 0;
  Color m_leftColor, m_rightColor;
  StereoFrame m_reading;
  std::array<uint8_t, kColumns * kBands> m_history{};
};

}  // namespace

std::unique_ptr<WidgetImpl> makeInstrument() { return std::make_unique<InstrumentWidget>(); }

}  // namespace undershell
