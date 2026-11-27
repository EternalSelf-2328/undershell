// SPDX-License-Identifier: GPL-3.0-or-later
#include "diorama.hpp"

#include "gl.hpp"
#include "i18n.hpp"
#include "perf.hpp"
#include "weatherfeed.hpp"
#include "widget.hpp"

#include <array>
#include <cairo.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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

constexpr std::array kArt = {"aquarium.png", "terrarium.png", "rainy_room.png"};

GLuint loadPlate(int mode) {
  const char* env = std::getenv("UNDERSHELL_DATA_DIR");
  const std::array roots = {env, US_INSTALL_DATA_DIR,
#ifdef US_SOURCE_DATA_DIR
                            US_SOURCE_DATA_DIR,
#endif
  };
  for (const char* root : roots) {
    if (!root || !*root) continue;
    const auto path = (std::filesystem::path(root) / "dioramas" / kArt[mode]).string();
    using Surface = std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)>;
    Surface pixels(cairo_image_surface_create_from_png(path.c_str()), cairo_surface_destroy);
    if (cairo_surface_status(pixels.get()) != CAIRO_STATUS_SUCCESS) continue;
    const int w = cairo_image_surface_get_width(pixels.get());
    const int h = cairo_image_surface_get_height(pixels.get());
    const int stride = cairo_image_surface_get_stride(pixels.get());
    const auto* src = cairo_image_surface_get_data(pixels.get());
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
      const auto* row = reinterpret_cast<const uint32_t*>(src + y * stride);
      for (int x = 0; x < w; ++x) {
        auto* rgbPixel = rgb.data() + (static_cast<size_t>(y) * w + x) * 3;
        rgbPixel[0] = static_cast<uint8_t>(row[x] >> 16);
        rgbPixel[1] = static_cast<uint8_t>(row[x] >> 8);
        rgbPixel[2] = static_cast<uint8_t>(row[x]);
      }
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGenerateMipmap(GL_TEXTURE_2D);
    return tex;
  }
  US_WARN("could not load diorama illustration {}", kArt[mode]);
  return 0;
}

constexpr const char* kFragment = R"GLSL(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_plate;
uniform vec2 u_fit, u_size;
uniform vec3 u_tint;
uniform float u_time, u_day;
uniform int u_mode, u_rain, u_quality;

float hash(float i) { return fract(sin(i * 15.713 + 7.0) * 1849.13); }
float ellipse(vec2 p, vec2 size) { return (length(p / size) - 1.0) * min(size.x, size.y); }
float cover(float d) { return 1.0 - smoothstep(-0.004, 0.004, d); }

vec3 sweepFish(vec2 q, vec3 c, float i) {
    float phase = u_time*(0.31+i*.012) + i*.54;
    vec2 origin = vec2(.36+sin(phase)*.205, .48+(i-1.5)*.045+sin(phase*1.27+i)*.043);
    vec2 p = (q-origin)*vec2(1.0, .667);
    p.x *= cos(phase)>0.0?1.0:-1.0;
    p.x /= max(.23,abs(cos(phase)));
    p *= 1.18 + .08 * i;
    p.y -= .10*p.x;
    if (abs(p.x)>.047 || abs(p.y)>.014) return c;
    float fin = ellipse(p+vec2(.028,.003*sin(u_time*5.0+i)), vec2(.012,.006));
    float body = ellipse(p, vec2(.026,.0068));
    vec3 tint = mix(vec3(.55,.79,.76), mix(u_tint, vec3(.53,.76,.80), .58), .55);
    c = mix(c, c*.68+tint*.27, cover(fin)*.90);
    c = mix(c, mix(tint*.48,tint*1.25, (1.0-smoothstep(-.004,.006,p.y))), cover(body));
    float band = cover(abs(p.y+.0008)-.00085)*cover(abs(p.x+.003)-.019);
    c = mix(c, tint*vec3(.45,1.03,1.24), band*.8);
    c = mix(c, vec3(.035,.075,.08), cover(length(p-vec2(.016,-.001))-.0017));
    return c;
}

vec3 aquarium(vec2 q) {
    float wave = sin(47.0*q.x + 24.0*q.y + u_time*.6) * sin(25.0*q.x-38.0*q.y-u_time*.77);
    vec2 sway = vec2(.0017 * wave, .0012 * wave) * (1.0 - q.y);
    vec3 c = texture(u_plate, q+sway).rgb;
    float sunlight = pow(max(0.0,cos((q.x-.6*q.y)*22.0+u_time*.6)), 8.0) * (1.0-q.y);
    c += c * vec3(.075,.068,.05) * sunlight;
    for (int i=0; i<4; ++i) {
        if (i>=u_quality+2) break;
        c = sweepFish(q,c,float(i));
    }
    return c;
}

vec3 terrarium(vec2 q) {
    vec3 c = texture(u_plate,q).rgb;
    vec3 midnight = c*vec3(.51,.65,.83) + vec3(.010,.018,.031);
    c = mix(midnight,c,.19+u_day*.81);
    float sunbeam = clamp(1.0-abs((q.x+.42*q.y)-(.54+.035*sin(u_time*.24)))*3.9,0.0,1.0);
    c += c*vec3(.043,.032,.017)*sunbeam*u_day;
    for (int i=0; i<5; ++i) {
        if (i>=u_quality+3) break;
        float seed = float(i);
        vec2 p = q - vec2(.54 + seed*.079 + .046*sin(u_time*.36+seed*1.9),
                          .43 + .095*sin(u_time*.49+seed*1.64));
        p.x *= 1.5;
        if (abs(p.x)>.048 || abs(p.y)>.048) continue;
        float glow = exp(-dot(p,p)*3990.0);
        float strength = pow(max(0.0,sin(u_time*1.1+seed*.96)), 3.0)*(1.0-.97*u_day);
        c += vec3(.65,.76,.26)*glow*strength*.28;
        c += vec3(1.0,.94,.61)*exp(-dot(p,p)*62000.0)*strength*.75;
    }
    return c;
}

vec3 room(vec2 q) {
    vec3 c = texture(u_plate,q).rgb;
    // Glass bounds in the illustration's normalized coordinates.
    if (u_rain==0 || q.x<.302 || q.x>.923 || q.y<.11 || q.y>.576) return c;
    float seed = floor(q.x*109.0);
    float variable = hash(seed+4.0);
    float phase = fract(q.y*(2.2+variable)-u_time*(.2+hash(seed)*.33)+hash(seed+6.0));
    float center = fract(q.x*109.0)-.5 + (hash(seed+8.0)-.5)*.45;
    float lead = (1.0-smoothstep(.02,.24,abs(center))) * (1.0-smoothstep(.022,.092,phase));
    float tail = (1.0-smoothstep(.015,.16,abs(center))) * (1.0-smoothstep(.03,.31,phase));
    float edge = smoothstep(.0,.012,min(q.y-.11,.576-q.y));
    float wet = (lead*.78+tail*.22)*edge*(.50+.50*variable);
    vec3 refracted = texture(u_plate, q+vec2(.003, -.003)*sign(center)).rgb;
    return mix(c, refracted, wet*.7) + (1.0-c)*vec3(.105,.121,.14)*wet;
}

void main() {
    // A thin, rounded image edge. The illustration remains the whole scene.
    vec2 p = v_uv*u_size, halfBox = u_size*.5;
    float radius = min(8.0, min(halfBox.x,halfBox.y)*.075);
    vec2 d = abs(p-halfBox)-halfBox+radius+.6;
    float edge = length(max(d,0.0)) + min(max(d.x,d.y),0.0)-radius;
    float alpha = 1.0-smoothstep(-.55,.55,edge);
    if (alpha<=0.0) { fragColor = vec4(0.0); return; }
    vec2 q = (v_uv-.5)*u_fit+.5;
    vec3 c = u_mode==0 ? aquarium(q) : u_mode==1 ? terrarium(q) : room(q);
    c = mix(c, c*(.92+.16*u_tint),.25);
    c = mix(c, vec3(.77,.84,.88), (1.0-smoothstep(.15,1.3,abs(edge+.55)))*.14);
    fragColor = vec4(clamp(c,0.0,1.0)*alpha,alpha);
}
)GLSL";

class DioramaWidget final : public WidgetImpl {
public:
  ~DioramaWidget() override { if (m_plate) glDeleteTextures(1, &m_plate); }

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
    if (m_plateMode != m_mode) {
      if (m_plate) glDeleteTextures(1, &m_plate);
      m_plate = loadPlate(m_mode);
      m_plateMode = m_mode;
    }
    if (!m_plate) return;
    glUseProgram(m_program.id());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_plate);
    glUniform1i(m_program.uniform("u_plate"), 0);
    const float ratio = ctx.w / std::max(1.0F, ctx.h);
    const float fitting = ratio / 1.5F;
    glUniform2f(m_program.uniform("u_fit"), std::min(1.0F, fitting), std::min(1.0F, 1.0F / std::max(0.01F, fitting)));
    glUniform2f(m_program.uniform("u_size"), ctx.w, ctx.h);
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
  GLuint m_plate = 0;
  int m_plateMode = -1;
  std::string m_weatherPlace;
  double m_time = 0, m_origin = -1, m_checkDay = 0;
  float m_day = 0.5F;
  int m_mode = 0;
  bool m_motion = true, m_rain = true;
};

}  // namespace

std::unique_ptr<WidgetImpl> makeDiorama() { return std::make_unique<DioramaWidget>(); }

}  // namespace undershell
