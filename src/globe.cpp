// SPDX-License-Identifier: GPL-3.0-or-later
// A globe of dots: the continents as a grid of dots on a turning sphere
// (Natural Earth's land, public domain, 256 x 128 cells), the night side
// dimmer where the real sun has set (its place worked out from the date and
// the time), a thin atmosphere round the rim. The bass swells the dots, and
// each kick sends a ring across the surface from a place of your choice.
#include "looks.hpp"

#include "gl.hpp"
#include "widget.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>

namespace undershell {

namespace {

#include "worldland.inc"

const char* kFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform vec2 u_res;
uniform float u_size;        // the sphere's radius, a share of the box's smaller side
uniform float u_turn;        // radians: how far it has turned
uniform float u_tilt;        // radians: the axis leaning towards the viewer
uniform vec3 u_sun;          // the direction of the sun, in the globe's own frame (x at 0°, z at the pole)
uniform float u_daynight;    // 1: the night side dims
uniform float u_ocean;       // how much the sea's dots show
uniform float u_dots;        // dots round the equator
uniform float u_swell;       // the bass
uniform vec3 u_mark;         // the place the rings start from (unit vector; 0: none)
uniform vec4 u_rings;        // how far each ring has gone (radians on the sphere; < 0: none)
uniform vec3 u_land, u_sea, u_glow, u_ring;
uniform float u_opacity;
uniform sampler2D u_map;     // land: one byte a cell
const float PI = 3.14159265;
void main() {
    vec2 p = (v_uv * u_res - 0.5 * u_res) / (min(u_res.x, u_res.y) * u_size);
    p.y = -p.y;
    float r2 = dot(p, p);
    vec4 col = vec4(0.0);
    // the atmosphere: a glow just outside the rim
    float rim = sqrt(r2);
    if (rim > 0.98) {
        float a = exp(-(rim - 1.0) * 18.0) * smoothstep(0.98, 1.01, rim) * 0.5;
        fragColor = vec4(u_glow * a, a) * u_opacity;
        return;
    }
    vec3 n = vec3(p, sqrt(1.0 - r2));                 // the point on the sphere, seen from the front
    // into the globe's frame: undo the tilt, then the turn
    float ct = cos(u_tilt), st = sin(u_tilt);
    vec3 q = vec3(n.x, n.y * ct + n.z * st, -n.y * st + n.z * ct);   // y up, z towards us
    float cr = cos(u_turn), sr = sin(u_turn);
    vec3 g = vec3(q.x * cr + q.z * sr, -q.x * sr + q.z * cr, q.y);  // x, y on the equator; z the north pole
    float lat = asin(clamp(g.z, -1.0, 1.0));
    float lon = atan(g.y, g.x);
    // the dot grid: rows of latitude, each with as many dots as fit round it
    float rows = u_dots * 0.5;
    float row = floor((lat / PI + 0.5) * rows);
    float rowLat = ((row + 0.5) / rows - 0.5) * PI;
    float perRow = max(1.0, floor(u_dots * cos(rowLat)));
    float cell = floor((lon / (2.0 * PI) + 0.5) * perRow);
    float cellLon = ((cell + 0.5) / perRow - 0.5) * 2.0 * PI;
    // distance to the dot's centre, in dot spacings
    vec2 d = vec2((lon - cellLon) * cos(lat) * perRow / (2.0 * PI * max(cos(rowLat), 0.05)), (lat - rowLat) / PI * rows);
    vec2 uv = vec2(cellLon / (2.0 * PI) + 0.5, 0.5 - rowLat / PI);
    float land = texture(u_map, uv).r;
    vec3 c = vec3(cos(rowLat) * cos(cellLon), cos(rowLat) * sin(cellLon), sin(rowLat));
    float light = 1.0;
    if (u_daynight > 0.5) light = mix(0.28, 1.0, smoothstep(-0.12, 0.12, dot(c, u_sun)));
    float ring = 0.0;
    if (dot(u_mark, u_mark) > 0.5) {
        float ang = acos(clamp(dot(c, u_mark), -1.0, 1.0));
        for (int i = 0; i < 4; i++)
            if (u_rings[i] >= 0.0) ring = max(ring, exp(-pow((ang - u_rings[i]) * 9.0, 2.0)) * (1.0 - u_rings[i] / PI));
        ring = max(ring, smoothstep(0.06, 0.0, ang));  // the place itself
    }
    float size = (land > 0.5 ? 0.3 : 0.2) * (1.0 + 0.45 * u_swell + 0.6 * ring);
    float dot_ = smoothstep(size, size * 0.6, length(d));
    float shade = 0.55 + 0.45 * n.z;                    // a little darker towards the rim
    vec3 base = land > 0.5 ? u_land : u_sea;
    float a = dot_ * (land > 0.5 ? 1.0 : u_ocean) * light * shade;
    vec3 rgb = mix(base, u_ring, ring) * (land > 0.5 || ring > 0.0 ? 1.0 : 1.0);
    a = max(a, dot_ * ring * shade);
    col = vec4(rgb * a, a);
    fragColor = col * u_opacity;
}
)";

class Globe final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    m_spin = std::clamp(t["globe_spin"].value_or(6.0), 0.0, 60.0);
    m_size = std::clamp(t["globe_size"].value_or(0.9), 0.3, 1.0);
    m_tilt = std::clamp(t["globe_tilt"].value_or(18.0), -60.0, 60.0);
    m_dots = std::clamp(t["globe_dots"].value_or(140.0), 40.0, 300.0);
    m_ocean = std::clamp(t["globe_ocean"].value_or(0.25), 0.0, 1.0);
    m_daynight = t["globe_daynight"].value_or(true);
    m_hits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
    m_preview = t["globe_preview"].value_or(-1.0);
    const double lat = t["globe_lat"].value_or(-999.0), lon = t["globe_lon"].value_or(-999.0);
    m_mark = lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
    if (m_mark) {
      const double la = lat * M_PI / 180, lo = lon * M_PI / 180;
      m_markV = {static_cast<float>(std::cos(la) * std::cos(lo)), static_cast<float>(std::cos(la) * std::sin(lo)), static_cast<float>(std::sin(la))};
      if (m_preview < 0 && !m_turnSet) {  // start with the place facing us
        m_turn = M_PI / 2 - lo;  // the longitude facing us is pi/2 - turn
        m_turnSet = true;
      }
    }
    m_land = noct.color("primary");
    m_sea = lighter(noct.color("primary"), 0.5F);
    m_glow = noct.color("tertiary", noct.color("primary"));
    m_ringC = lighter(noct.color("tertiary", noct.color("primary")), 0.4F);
    if (m_preview >= 0) {
      m_turn = m_preview;
      m_rings = {0.6F, -1, -1, -1};
    }
  }

  void tick(const Beat& b) override {
    if (m_preview >= 0) return;
    m_turn += b.dt * m_spin * M_PI / 180 * (b.live ? 1.0 + 0.6 * b.energy : 1.0);
    m_turn = std::fmod(m_turn, 2 * M_PI);
    double bass = 0;
    if (b.live && b.bands && !b.bands->empty()) {
      const size_t n = std::max<size_t>(1, b.bands->size() / 8);
      for (size_t i = 0; i < n; ++i) bass += (*b.bands)[i];
      bass /= static_cast<double>(n);
    }
    m_swell += (bass - m_swell) * std::min(1.0, b.dt * 10);
    for (float& r : m_rings)
      if (r >= 0) r = r + static_cast<float>(b.dt * 1.1) > M_PI ? -1.0F : r + static_cast<float>(b.dt * 1.1);
    if (b.kick && b.live && m_mark && b.strength * m_hits > 0.15)
      for (float& r : m_rings)
        if (r < 0) {
          r = 0;
          break;
        }
  }

  void draw(const DrawContext& ctx, float opacity) override {
    if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kFrag, "globe");
    if (!m_map) upload();
    glUseProgram(m_prog.id());
    auto U = [&](const char* n) { return m_prog.uniform(n); };
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_map);
    glUniform1i(U("u_map"), 0);
    glUniform2f(U("u_res"), ctx.w, ctx.h);
    glUniform1f(U("u_size"), static_cast<float>(m_size * 0.5));
    glUniform1f(U("u_turn"), static_cast<float>(m_turn));
    glUniform1f(U("u_tilt"), static_cast<float>(m_tilt * M_PI / 180));
    const std::array<float, 3> sun = sunNow();
    glUniform3f(U("u_sun"), sun[0], sun[1], sun[2]);
    glUniform1f(U("u_daynight"), m_daynight ? 1.0F : 0.0F);
    glUniform1f(U("u_ocean"), static_cast<float>(m_ocean));
    glUniform1f(U("u_dots"), static_cast<float>(m_dots));
    glUniform1f(U("u_swell"), static_cast<float>(m_swell));
    glUniform3f(U("u_mark"), m_mark ? m_markV[0] : 0, m_mark ? m_markV[1] : 0, m_mark ? m_markV[2] : 0);
    glUniform4f(U("u_rings"), m_rings[0], m_rings[1], m_rings[2], m_rings[3]);
    glUniform3f(U("u_land"), m_land.r, m_land.g, m_land.b);
    glUniform3f(U("u_sea"), m_sea.r, m_sea.g, m_sea.b);
    glUniform3f(U("u_glow"), m_glow.r, m_glow.g, m_glow.b);
    glUniform3f(U("u_ring"), m_ringC.r, m_ringC.g, m_ringC.b);
    glUniform1f(U("u_opacity"), opacity);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawUnitQuad();
    glDisable(GL_BLEND);
  }

  [[nodiscard]] bool visible() const override { return true; }
  [[nodiscard]] bool moving() const override { return m_preview < 0 && (m_spin > 0 || m_swell > 0.01); }
  // in silence it keeps turning, slowly: a frame every few degrees
  [[nodiscard]] double idleFrame() const override { return m_spin > 0 ? std::clamp(1.5 / m_spin, 1.0 / 30, 1.0) : 60.0; }

  ~Globe() override {
    if (m_map) glDeleteTextures(1, &m_map);
  }

private:
  // where the sun is overhead now: the declination from the day of the year,
  // the longitude from the time of day (UTC); a preview fixes a date
  [[nodiscard]] std::array<float, 3> sunNow() const {
    const std::time_t now = m_preview >= 0 ? 1782820800 : std::time(nullptr);  // (the preview: 2026-06-30, noon UTC)
    std::tm tm{};
    gmtime_r(&now, &tm);
    const double day = tm.tm_yday + 1, hours = tm.tm_hour + tm.tm_min / 60.0;
    const double decl = -23.44 * M_PI / 180 * std::cos(2 * M_PI / 365 * (day + 10));
    const double lon = (12.0 - hours) * 15.0 * M_PI / 180;
    return {static_cast<float>(std::cos(decl) * std::cos(lon)), static_cast<float>(std::cos(decl) * std::sin(lon)), static_cast<float>(std::sin(decl))};
  }
  void upload() {
    constexpr int W = 256, H = 128;
    std::array<unsigned char, W * H> px{};
    for (int i = 0; i < W * H; ++i) px[static_cast<size_t>(i)] = (kWorldLand[i / 8] >> (i % 8)) & 1 ? 255 : 0;
    glGenTextures(1, &m_map);
    glBindTexture(GL_TEXTURE_2D, m_map);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, W, H, 0, GL_RED, GL_UNSIGNED_BYTE, px.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  double m_spin = 6, m_size = 0.9, m_tilt = 18, m_dots = 140, m_ocean = 0.25, m_hits = 0.7, m_preview = -1;
  bool m_daynight = true, m_mark = false, m_turnSet = false;
  std::array<float, 3> m_markV{};
  double m_turn = 0, m_swell = 0;
  std::array<float, 4> m_rings{-1, -1, -1, -1};
  Color m_land, m_sea, m_glow, m_ringC;
  Program m_prog;
  GLuint m_map = 0;
};

}  // namespace

std::unique_ptr<StrokeLook> makeGlobe() { return std::make_unique<Globe>(); }

}  // namespace undershell
