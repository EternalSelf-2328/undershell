// SPDX-License-Identifier: GPL-3.0-or-later
// Conway's Game of Life to the music: cells live and die by the usual rule
// (born with three neighbours, kept with two or three) on a grid that wraps
// round, a little faster with the groove; each kick drops gliders and small
// seeds where the music is loudest, and a dead cell leaves a fading trail.
// The grid is worked out on the CPU (a byte a cell) and drawn by one pass
// that looks each cell up, like the pixels look.
#include "looks.hpp"

#include "gl.hpp"
#include "widget.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace undershell {

namespace {

const char* kFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_cells;  // 0 empty, 1..127 a trail fading, 128..255 alive (older: higher)
uniform vec2 u_res;
uniform vec2 u_origin;
uniform float u_cell;
uniform ivec2 u_grid;
uniform vec3 u_young, u_old, u_trail;
uniform float u_opacity;
void main() {
    vec2 p = (v_uv * u_res - u_origin) / u_cell;
    ivec2 c = ivec2(floor(p));
    if (c.x < 0 || c.y < 0 || c.x >= u_grid.x || c.y >= u_grid.y) { fragColor = vec4(0.0); return; }
    int v = int(texelFetch(u_cells, c, 0).r * 255.0 + 0.5);
    if (v == 0) { fragColor = vec4(0.0); return; }
    vec2 f = fract(p) - 0.5;
    float sq = 1.0 - smoothstep(0.36, 0.44, max(abs(f.x), abs(f.y)));  // a square with a hair of gap
    if (v >= 128) {
        float age = float(v - 128) / 127.0;
        fragColor = vec4(mix(u_young, u_old, sqrt(age)) * sq, sq) * u_opacity;
    } else {
        float a = float(v) / 127.0 * 0.55 * sq;
        fragColor = vec4(u_trail * a, a) * u_opacity;
    }
}
)";

class Life final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    m_cellPx = static_cast<float>(std::clamp(t["life_cell"].value_or(9.0), 4.0, 40.0));
    m_speed = std::clamp(t["life_speed"].value_or(8.0), 1.0, 30.0);
    m_trail = std::clamp(t["life_trail"].value_or(0.6), 0.0, 1.0);
    m_hits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
    m_preview = t["life_preview"].value_or(-1.0);
    m_young = lighter(noct.color("primary"), 0.35F);
    m_old = noct.color("tertiary", noct.color("primary"));
    m_trailC = noct.color("secondary", noct.color("primary"));
  }

  void tick(const Beat& b) override {
    if (m_preview >= 0 || m_cols == 0) return;
    if (b.bands && !b.bands->empty()) {  // the spectrum in 16 columns across the grid
      const size_t n = b.bands->size();
      for (size_t i = 0; i < m_bands.size(); ++i) m_bands[i] = (*b.bands)[std::min(n - 1, i * n / m_bands.size())];
    }
    if (b.kick && b.live) seed(std::clamp(b.strength, 0.3, 1.0) * m_hits / 0.7);
    m_acc += b.dt * m_speed * (0.6 + 0.8 * (b.live ? b.energy : 0.0));
    for (int k = 0; k < 3 && m_acc >= 1; ++k, m_acc -= 1) step();
    m_acc = std::min(m_acc, 1.0);
  }

  void draw(const DrawContext& ctx, float opacity) override {
    if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kFrag, "life");
    const int cols = std::clamp(static_cast<int>(ctx.w / m_cellPx), 4, 1024), rows = std::clamp(static_cast<int>(ctx.h / m_cellPx), 4, 1024);
    if (cols != m_cols || rows != m_rows) {
      m_cols = cols, m_rows = rows;
      m_cells.assign(static_cast<size_t>(cols) * rows, 0);
      m_next = m_cells;
      scatter(0.18);
      if (m_preview >= 0)
        for (int i = 0; i < static_cast<int>(m_preview); ++i) step();
    }
    upload();
    const float ox = (ctx.w - static_cast<float>(cols) * m_cellPx) / 2, oy = (ctx.h - static_cast<float>(rows) * m_cellPx) / 2;
    glUseProgram(m_prog.id());
    auto U = [&](const char* n) { return m_prog.uniform(n); };
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glUniform1i(U("u_cells"), 0);
    glUniform2f(U("u_res"), ctx.w, ctx.h);
    glUniform2f(U("u_origin"), ox, oy);
    glUniform1f(U("u_cell"), m_cellPx);
    glUniform2i(U("u_grid"), cols, rows);
    glUniform3f(U("u_young"), m_young.r, m_young.g, m_young.b);
    glUniform3f(U("u_old"), m_old.r, m_old.g, m_old.b);
    glUniform3f(U("u_trail"), m_trailC.r, m_trailC.g, m_trailC.b);
    glUniform1f(U("u_opacity"), opacity);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawUnitQuad();
    glDisable(GL_BLEND);
  }

  [[nodiscard]] bool visible() const override { return true; }
  [[nodiscard]] bool moving() const override { return m_preview < 0; }
  // in silence it keeps living, at its own pace
  [[nodiscard]] double idleFrame() const override { return 1.0 / std::max(2.0, m_speed * 0.6); }

  ~Life() override {
    if (m_tex) glDeleteTextures(1, &m_tex);
  }

private:
  [[nodiscard]] float rnd() {
    m_rng ^= m_rng << 13, m_rng ^= m_rng >> 7, m_rng ^= m_rng << 17;
    return static_cast<float>(m_rng >> 40) / static_cast<float>(1ULL << 24);
  }
  unsigned char& at(int x, int y) {
    x = (x % m_cols + m_cols) % m_cols, y = (y % m_rows + m_rows) % m_rows;
    return m_cells[static_cast<size_t>(y) * m_cols + x];
  }
  void scatter(double share) {
    for (auto& c : m_cells)
      if (rnd() < share) c = 128;
  }
  // a kick: gliders and R-pentominoes where the spectrum is loudest
  void seed(double strength) {
    static constexpr int kGlider[5][2] = {{1, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}};
    static constexpr int kR[5][2] = {{1, 0}, {2, 0}, {0, 1}, {1, 1}, {1, 2}};
    const int n = 1 + static_cast<int>(strength * 3);
    for (int k = 0; k < n; ++k) {
      // a column by the spectrum: louder bands are likelier
      float sum = 0;
      for (float v : m_bands) sum += v;
      int band = 0;
      for (float r = rnd() * std::max(sum, 1e-3F); band < static_cast<int>(m_bands.size()) - 1 && r > m_bands[band]; ++band) r -= m_bands[band];
      const int x = sum > 0.01F ? band * m_cols / static_cast<int>(m_bands.size()) + static_cast<int>(rnd() * 3) : static_cast<int>(rnd() * m_cols);
      const int y = static_cast<int>(rnd() * m_rows);
      const auto& shape = rnd() < 0.6F ? kGlider : kR;
      const bool flip = rnd() < 0.5F;
      for (const auto& p : shape) at(x + (flip ? -p[0] : p[0]), y + p[1]) = 128;
    }
  }
  void step() {
    const int W = m_cols, H = m_rows;
    int alive = 0;
    const int fade = std::max(1, static_cast<int>(127 * (1 - m_trail * 0.92) / 6));
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        int n = 0;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx)
            if ((dx || dy) && at(x + dx, y + dy) >= 128) ++n;
        const unsigned char c = m_cells[static_cast<size_t>(y) * W + x];
        unsigned char& o = m_next[static_cast<size_t>(y) * W + x];
        if (c >= 128) {
          o = (n == 2 || n == 3) ? static_cast<unsigned char>(std::min(255, c + 4)) : (m_trail > 0 ? 127 : 0);
        } else {
          o = n == 3 ? 128 : static_cast<unsigned char>(std::max(0, c - fade));
        }
        alive += o >= 128;
      }
    m_cells.swap(m_next);
    // a world that died out or froze into a few still lifes starts again
    if (alive < W * H / 60 && m_preview < 0) scatter(0.12);
  }
  void upload() {
    if (!m_tex) glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (m_cols != m_texW || m_rows != m_texH) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_cols, m_rows, 0, GL_RED, GL_UNSIGNED_BYTE, m_cells.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      m_texW = m_cols, m_texH = m_rows;
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_cols, m_rows, GL_RED, GL_UNSIGNED_BYTE, m_cells.data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  }

  float m_cellPx = 9;
  double m_speed = 8, m_trail = 0.6, m_hits = 0.7, m_preview = -1, m_acc = 0;
  Color m_young, m_old, m_trailC;
  std::array<float, 16> m_bands{};
  int m_cols = 0, m_rows = 0;
  std::vector<unsigned char> m_cells, m_next;
  uint64_t m_rng = 0x2545F4914F6CDD1DULL;
  Program m_prog;
  GLuint m_tex = 0;
  int m_texW = 0, m_texH = 0;
};

}  // namespace

std::unique_ptr<StrokeLook> makeLife() { return std::make_unique<Life>(); }

}  // namespace undershell
