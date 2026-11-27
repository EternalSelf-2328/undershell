// SPDX-License-Identifier: GPL-3.0-or-later
#include "diorama.hpp"

#include "gl.hpp"
#include "i18n.hpp"
#include "perf.hpp"
#include "weatherfeed.hpp"
#include "widget.hpp"

#include <cmath>
#include <ctime>

namespace undershell {

bool dioramaRain(WeatherService* weather, const std::string& place) {
  if (place.empty()) return true;
  if (!weather) return false;
  const auto& report = weather->want(place, false, spanishUi());
  if (report.state != WeatherReport::State::Ready) return false;
  const Sky sky = skyOf(report.code);
  return sky == Sky::Rain || sky == Sky::Drizzle || sky == Sky::Thunder;
}

namespace {

constexpr const char* kFragment = R"GLSL(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 u_fit;
uniform vec3 u_tint;
uniform float u_time, u_day;
uniform int u_mode, u_rain, u_quality;
float rounded(vec2 p, vec2 size, float radius) {
    vec2 d = abs(p) - size + radius;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - radius;
}
float mask(float distance) { return 1.0 - smoothstep(-0.005, 0.005, distance); }
float hash(float i) { return fract(sin(i * 15.713 + 7.0) * 1849.13); }
float ellipse(vec2 p, vec2 size) { return (length(p / size) - 1.0) * min(size.x, size.y); }
vec3 paint(vec3 below, vec3 ink, float distance) { return mix(below, ink, mask(distance)); }

vec3 aquarium(vec2 q) {
    vec3 c = mix(vec3(0.024, 0.13, 0.18), vec3(0.12, 0.43, 0.48), 1.0 - q.y);
    float rays = pow(max(0.0, cos((q.x - 0.34 * q.y) * 16.0 + u_time * 0.18)), 6.0);
    c += vec3(0.04, 0.10, 0.10) * rays * (1.0 - q.y);
    float sand = 0.83 + 0.025 * sin(q.x * 11.0) + 0.012 * sin(q.x * 27.0);
    c = paint(c, vec3(0.62, 0.46, 0.29), sand - q.y);
    for (int i = 0; i < 6; ++i) {
        float fi = float(i);
        float off = q.x - (0.08 + hash(fi) * 0.87);
        off -= sin(q.y * 23.0 + fi + u_time * 0.7) * (0.86 - q.y) * 0.10;
        float tip = 0.43 + hash(fi+2.0) * 0.30;
        float leaf = max(abs(off) - (0.012 + 0.009 * cos(q.y * 15.0)), max(tip-q.y, q.y-0.89));
        c = paint(c, mix(vec3(0.09, 0.32, 0.19), vec3(0.35, 0.70, 0.40), hash(fi+1.0)), leaf);
    }
    for (int i = 0; i < 4; ++i) {
        float fi = float(i);
        if (i >= u_quality + 2) break;
        float phase = u_time * (0.18 + fi * 0.029) + fi * 1.72;
        vec2 origin = vec2(0.5 + cos(phase) * 0.36, 0.27 + fi * 0.11 + 0.053 * sin(phase * 1.7));
        vec2 p = q - origin;
        p.x *= sin(phase) > 0.0 ? -1.0 : 1.0;
        float body = ellipse(p, vec2(0.056, 0.026));
        float tail = max(abs(p.y) - clamp((-p.x-0.035) * 0.68, 0.0, 0.035), max(p.x+0.035, -p.x-0.092));
        vec3 fish = mix(vec3(0.97, 0.58, 0.32), u_tint * 0.85 + 0.12, hash(fi));
        c = paint(c, fish * 0.72, tail);
        c = paint(c, fish, body);
        c = paint(c, vec3(0.03, 0.09, 0.12), length(p-vec2(0.030, -0.006))-0.007);
    }
    for (int i = 0; i < 4; ++i) {
        float k = float(i);
        vec2 at = vec2(0.82 + sin(k + u_time)*0.015, 0.8 - fract(u_time * 0.13 + k * 0.23) * 0.70);
        c = paint(c, vec3(0.53, 0.80, 0.82), abs(length((q-at)*vec2(1.1, 0.85)) - 0.012) - 0.0025);
    }
    return c;
}

vec3 terrarium(vec2 q) {
    vec3 c = mix(mix(vec3(0.033, 0.076, 0.104), vec3(0.24, 0.46, 0.31), u_day),
                 mix(vec3(0.090, 0.15, 0.18), vec3(0.73, 0.77, 0.48), u_day), 1.0 - q.y);
    float soil = 0.74 + 0.045 * sin(q.x * 10.0);
    c = paint(c, vec3(0.24, 0.15, 0.11), soil - q.y);
    c = paint(c, vec3(0.28, 0.40, 0.16) * (0.55 + u_day * 0.45), abs(soil - q.y) - 0.024);
    for (int i = 0; i < 8; ++i) {
        float at = 0.08 + float(i) * 0.118;
        float h = 0.25 + hash(float(i) + 3.0) * 0.19;
        vec2 p = q - vec2(at, soil-0.015);
        float stem = abs(p.x - 0.024*sin(p.y*14.0)) - 0.006;
        vec3 green = mix(vec3(0.16, 0.34, 0.16), vec3(0.49, 0.67, 0.29), hash(float(i))) * (0.6 + u_day*0.4);
        c = paint(c, green * 0.68, max(stem, max(p.y, -p.y-h)));
        for (int j = 1; j < 5; ++j) {
            float leaf = float(j);
            vec2 d = p - vec2((j % 2 == 0 ? -1.0 : 1.0)*0.031, -h*leaf/5.0);
            d.y += (j % 2 == 0 ? -0.36 : 0.36) * d.x;
            c = paint(c, green, ellipse(d, vec2(0.042 - leaf * 0.004, 0.023 - leaf*0.002)));
        }
    }
    for (int i = 0; i < 4; ++i) {
        float fi = float(i);
        vec2 p = q - vec2(0.2+fi*0.2+0.06*sin(u_time*0.31+fi), 0.43+0.10*sin(u_time*0.67+fi));
        float glow = exp(-length(p)*36.0) * (0.22 + 0.24*sin(u_time*1.7+fi+1.8));
        c += vec3(0.83, 1.0, 0.38) * glow * (1.0 - 0.9*u_day);
    }
    return c;
}

vec3 room(vec2 q) {
    vec3 c = mix(vec3(0.24, 0.13, 0.12), vec3(0.43, 0.27, 0.20), 1.0 - q.y);
    vec2 window = q - vec2(0.52, 0.36);
    float pane = rounded(window, vec2(0.30, 0.28), 0.035);
    vec3 outside = mix(vec3(0.10, 0.18, 0.28), vec3(0.33, 0.49, 0.59), 1.0 - q.y);
    float skyline = 0.47 + 0.20 * hash(floor(q.x * 35.0));
    outside = paint(outside, vec3(0.095, 0.16, 0.22), skyline-q.y);
    if (u_rain > 0) {
        float col = floor(q.x * 75.0);
        float phase = fract(q.y * 2.5 - u_time * (0.30 + hash(col) * 0.34) + hash(col+3.0));
        float drop = (1.0-smoothstep(0.02, 0.18, abs(fract(q.x * 75.0) - 0.5))) * (1.0-smoothstep(0.08, 0.16, phase));
        outside += vec3(0.14, 0.28, 0.37) * drop;
    }
    if (pane < 0.005) c = mix(c, outside, mask(pane));
    c = paint(c, vec3(0.34, 0.19, 0.15), abs(pane) - 0.013);
    if (pane < 0.0) c = paint(c, vec3(0.29, 0.16, 0.14), min(abs(window.x)-0.009, abs(window.y)-0.014));
    c = paint(c, vec3(0.59, 0.34, 0.20), rounded(q - vec2(0.52, 0.66), vec2(0.33, 0.025), 0.014));
    c = paint(c, vec3(0.21, 0.13, 0.16), 0.76 - q.y);
    c = paint(c, mix(vec3(0.63, 0.31, 0.19), u_tint, 0.23), ellipse(q-vec2(0.54, 0.85),vec2(0.34, 0.08)));
    c = paint(c, vec3(1.0, 0.63, 0.30), rounded(q-vec2(0.12,0.64),vec2(0.036,0.047),0.012));
    return c;
}

void main() {
    vec2 q = (v_uv - 0.5) * u_fit + 0.5;
    float radius = u_mode == 1 ? 0.18 : 0.08;
    float rim = rounded(q - 0.5, vec2(0.495), radius);
    if (rim > 0.008) { fragColor = vec4(0.0); return; }
    vec3 c = u_mode == 0 ? aquarium(q) : u_mode == 1 ? terrarium(q) : room(q);
    float shine = (1.0-smoothstep(0.0, 0.07, abs(q.x + 0.32*q.y - 0.28))) * (1.0-q.y);
    if (u_mode != 2) c = mix(c, vec3(0.73, 0.87, 0.86), shine * 0.14);
    float edge = mask(abs(rim + 0.011) - 0.007);
    c = mix(c, vec3(0.59, 0.66, 0.63), edge * 0.47);
    float a = mask(rim);
    fragColor = vec4(c * a, a);
}
)GLSL";

class DioramaWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override {
    const auto name = cfg.options["style"].value_or(std::string("aquarium"));
    m_mode = name == "terrarium" ? 1 : name == "rainy_room" ? 2 : 0;
    m_weatherPlace = cfg.options["weather_location"].value_or(std::string());
    m_motion = cfg.options["motion"].value_or(true);
    m_accent = noct.color(cfg.options["color"].value_or(std::string("primary")), Color::fromHex("#91b6a5"));
  }

  void tick(const TickContext& ctx) override {
    if (m_origin < 0) m_origin = ctx.now;
    m_time = m_motion ? std::fmod(std::max(0.0, ctx.now - m_origin), 1800.0) : 0;
    if (m_mode == 2) {
      m_rain = dioramaRain(ctx.weather, m_weatherPlace.empty() ? ctx.weatherPlace : m_weatherPlace);
    } else if (m_mode == 1 && ctx.now >= m_checkDay) {
      const std::time_t now = std::time(nullptr);
      std::tm tm{};
      localtime_r(&now, &tm);
      constexpr double pi = 3.141592653589793;
      const double hour = tm.tm_hour + tm.tm_min / 60.0;
      m_day = static_cast<float>(std::clamp(0.18 + std::cos((hour - 12) * pi / 12) * 1.2, 0.0, 1.0));
      m_checkDay = ctx.now + 60 - tm.tm_sec;
    }
  }

  void draw(const DrawContext& ctx) override {
    if (!m_program.valid()) m_program.create(kQuadVertexShader, kFragment, "diorama");
    glUseProgram(m_program.id());
    const float ratio = ctx.w / std::max(1.0F, ctx.h);
    const float fitting = ratio / 1.6F;
    glUniform2f(m_program.uniform("u_fit"), std::max(1.0F, fitting), std::max(1.0F, 1.0F / std::max(0.01F, fitting)));
    glUniform3f(m_program.uniform("u_tint"), m_accent.r, m_accent.g, m_accent.b);
    glUniform1f(m_program.uniform("u_time"), static_cast<float>(m_time));
    glUniform1f(m_program.uniform("u_day"), m_day);
    glUniform1i(m_program.uniform("u_mode"), m_mode);
    glUniform1i(m_program.uniform("u_rain"), m_rain);
    glUniform1i(m_program.uniform("u_quality"), perfKnobs().quality);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawUnitQuad();
    glDisable(GL_BLEND);
  }

  double nextWakeup(double now) const override {
    if (m_motion && (m_mode != 2 || m_rain)) return now + 1.0 / fps();
    if (m_mode == 1) return m_checkDay > now ? m_checkDay : now + 1;
    return m_mode == 2 ? now + 30 : 1e18;
  }

  bool wantsWeather() const override { return m_mode == 2; }
  int fps() const override { return perfKnobs().quality == 0 ? 4 : 8; }
  Color accent() const override { return m_accent; }

private:
  Color m_accent;
  Program m_program;
  std::string m_weatherPlace;
  double m_time = 0, m_origin = -1, m_checkDay = 0;
  float m_day = 0.5F;
  int m_mode = 0;
  bool m_motion = true, m_rain = true;
};

}  // namespace

std::unique_ptr<WidgetImpl> makeDiorama() { return std::make_unique<DioramaWidget>(); }

}  // namespace undershell
