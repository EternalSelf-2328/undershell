// SPDX-License-Identifier: GPL-3.0-or-later
// The pixels look: a field of square pixels after the hero of omarchy.org.
// Slow value noise and a twinkle make each pixel's brightness; an ordered
// (Bayer) dither turns it into on or off, so the field thins into the empty
// middle like a printed gradient. A light wanders over it, the music rises
// from the bottom in columns (bass at the sides, treble in the middle), each
// strong beat opens a square that grows and fades, and a word can stand in
// the middle in big pixels, shaded top to bottom. Written from the idea, not
// from the site's code: the word is any text (@TIME@ for the time, @SONG@ for
// the song), drawn with a font through glyphSheet.
//
// The pixels are worked out on the CPU (a few thousand per frame) into a byte
// texture, one texel a pixel; the shader only looks them up.
#include "gl.hpp"
#include "looks.hpp"
#include "text.hpp"
#include "widget.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <vector>

namespace undershell {

namespace {

const char* kFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_cells;  // one texel a pixel: 0 off, 1..5 dim .. crest
uniform vec2 u_res;         // the surface, logical px
uniform vec2 u_origin;      // where the grid starts
uniform float u_cell;       // a pixel's side
uniform ivec2 u_grid;
uniform vec3 u_col[6];
uniform float u_opacity;
void main() {
    ivec2 c = ivec2(floor((v_uv * u_res - u_origin) / u_cell));
    if (c.x < 0 || c.y < 0 || c.x >= u_grid.x || c.y >= u_grid.y) { fragColor = vec4(0.0); return; }
    int lv = int(texelFetch(u_cells, c, 0).r * 255.0 + 0.5);
    if (lv == 0) { fragColor = vec4(0.0); return; }
    fragColor = vec4(u_col[lv] * u_opacity, u_opacity);
}
)";

enum Level : unsigned char { Off, Dim, Mid, Lit, Hover, Crest };

// a 4x4 ordered-dither matrix grown to 8x8 (the classic recursion)
std::array<float, 64> bayer8() {
  std::array<float, 64> m{};
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x) {
      int v = 0;
      for (int bit = 0; bit < 3; ++bit) {
        const int xb = (x >> bit) & 1, yb = (y >> bit) & 1;
        v |= ((xb ^ yb) << (2 * (2 - bit) + 1)) | (yb << (2 * (2 - bit)));
      }
      m[y * 8 + x] = (static_cast<float>(v) + 0.5F) / 64.0F;
    }
  return m;
}

float hash01(int x, int y) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393U + static_cast<uint32_t>(y) * 668265263U;
  h = (h ^ (h >> 13)) * 1274126177U;
  return static_cast<float>((h ^ (h >> 16)) & 0xffffffU) / 16777216.0F;
}

// 128x128 random values, blurred twice and stretched to 0..1: soft blobs
class Noise {
public:
  Noise() {
    constexpr int N = kN;
    std::vector<float> a(N * N), b(N * N);
    uint32_t s = 0x2545f491U;
    for (auto& v : a) {
      s ^= s << 13, s ^= s >> 17, s ^= s << 5;
      v = static_cast<float>(s >> 8) / 16777216.0F;
    }
    for (int pass = 0; pass < 2; ++pass) {
      for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
          float sum = 0;
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) sum += a[((y + dy + N) % N) * N + (x + dx + N) % N];
          b[y * N + x] = sum / 9;
        }
      a.swap(b);
    }
    const auto [lo, hi] = std::minmax_element(a.begin(), a.end());
    const float lo0 = *lo, span = std::max(1e-6F, *hi - lo0);
    for (auto& v : a) v = (v - lo0) / span;
    m_v = std::move(a);
  }
  // eased between lattice points, repeating
  [[nodiscard]] float at(float x, float y) const {
    const float fx = std::floor(x), fy = std::floor(y);
    const int x0 = (static_cast<int>(fx) % kN + kN) % kN, y0 = (static_cast<int>(fy) % kN + kN) % kN;
    const int x1 = (x0 + 1) % kN, y1 = (y0 + 1) % kN;
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * (3 - 2 * tx), ty = ty * ty * (3 - 2 * ty);
    const float a = m_v[y0 * kN + x0], b = m_v[y0 * kN + x1], c = m_v[y1 * kN + x0], d = m_v[y1 * kN + x1];
    return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
  }

private:
  static constexpr int kN = 128;
  std::vector<float> m_v;
};

Color mixC(Color a, Color b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1}; }

class Pixels final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    m_cellPx = static_cast<float>(std::clamp(t["pixels_cell"].value_or(11.0), 4.0, 40.0));
    m_text = t["pixels_text"].value_or(std::string{});
    m_rows = std::clamp(static_cast<int>(t["pixels_text_size"].value_or(15.0)), 5, 30);
    const std::string font = t["pixels_font"].value_or(std::string("auto"));
    m_font = font.empty() || font == "auto" ? "JetBrains Mono" : font;
    m_density = static_cast<float>(std::clamp(t["pixels_density"].value_or(0.5), 0.0, 1.0));
    m_hole = static_cast<float>(std::clamp(t["pixels_hole"].value_or(0.42), 0.0, 0.9));
    m_spectrum = t["pixels_spectrum"].value_or(true);
    m_spotOn = t["pixels_spot"].value_or(true);
    m_stampsOn = t["pixels_stamps"].value_or(true);
    m_hits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
    m_preview = t["pixels_preview"].value_or(-1.0);
    if (t["pixels_colors"].value_or(std::string("frost")) == "theme") {
      const Color p = noct.color("primary", Color::fromHex("#7cbfe3")), bg = noct.color("surface", Color::fromHex("#0e0e14"));
      m_col = {Color{}, mixC(p, bg, 0.72F), mixC(p, bg, 0.45F), p, lighter(p, 0.45F), lighter(p, 0.8F)};
    } else {  // a cold blue, light at the top
      m_col = {Color{}, Color::fromHex("#2a4556"), Color::fromHex("#4d7d99"), Color::fromHex("#7cbfe3"), Color::fromHex("#a9d6ef"),
               Color::fromHex("#d6ecf8")};
    }
    m_wordKey.clear();  // the word again, with this font and size
    if (m_preview >= 0) {
      m_time = m_preview;
      for (size_t i = 0; i < m_bands.size(); ++i) m_bands[i] = 0.62F - 0.5F * static_cast<float>(i) / static_cast<float>(m_bands.size());
      m_stamps = {{0.3F, 0.62F, 0.45F, 0.6F, 0.35F}};
      m_spot = 0.7F;
    }
  }

  void tick(const Beat& b) override {
    if (m_preview >= 0) return;
    const float dt = static_cast<float>(std::clamp(b.dt, 0.0, 0.1));
    m_time += dt;
    if (!b.title.empty() || !b.playing) m_title = b.title;
    // the analyser's bands folded into 32, rising fast and falling slowly
    const float up = 1 - std::pow(0.3F, dt * 40), down = 1 - std::pow(0.86F, dt * 40);
    for (size_t i = 0; i < m_bands.size(); ++i) {
      float v = 0;
      if (b.live && b.bands && !b.bands->empty()) {
        const size_t n = b.bands->size(), from = i * n / m_bands.size(), to = std::max(from + 1, (i + 1) * n / m_bands.size());
        for (size_t k = from; k < to && k < n; ++k) v = std::max(v, (*b.bands)[k]);
      }
      m_bands[i] += (v - m_bands[i]) * (v > m_bands[i] ? up : down);
    }
    m_beat *= std::pow(0.84F, dt * 40);
    if (b.kick) m_beat = std::max(m_beat, static_cast<float>(b.strength * m_hits / 0.7));
    m_spot += ((m_spotOn ? 0.7F : 0.0F) - m_spot) * std::min(1.0F, dt * 4);
    // a square opens where the light is: on a strong beat, or now and then in silence
    for (auto& s : m_stamps) s.age += dt / s.life;
    std::erase_if(m_stamps, [](const Stamp& s) { return s.age >= 1; });
    m_quiet += dt;
    const bool beat = b.kick && b.strength * m_hits / 0.7 > 0.45 && m_quiet > 0.25;
    if (m_stampsOn && m_stamps.size() < 4 && (beat || m_quiet > m_nextQuiet)) {
      const float charge = beat ? static_cast<float>(std::clamp(b.strength, 0.0, 1.0)) : 0.15F;
      const auto [x, y] = spotAt(m_time);
      m_stamps.push_back({x, y, 0, 0.65F + 0.55F * charge, charge});
      m_quiet = 0;
      m_nextQuiet = 2.0F + hash01(static_cast<int>(m_time * 10), 7) * 1.0F;
    }
  }

  void draw(const DrawContext& ctx, float opacity) override {
    if (!m_prog.valid()) m_prog.create(kQuadVertexShader, kFrag, "pixels");
    const int cols = std::max(1, static_cast<int>(std::ceil(ctx.w / m_cellPx))), rows = std::max(1, static_cast<int>(std::ceil(ctx.h / m_cellPx)));
    const float ox = (ctx.w - static_cast<float>(cols) * m_cellPx) / 2, oy = (ctx.h - static_cast<float>(rows) * m_cellPx) / 2;
    makeWord(cols);
    fill(cols, rows, ctx.w, ctx.h, ox, oy);
    upload(cols, rows);
    glUseProgram(m_prog.id());
    auto U = [&](const char* n) { return m_prog.uniform(n); };
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glUniform1i(U("u_cells"), 0);
    glUniform2f(U("u_res"), ctx.w, ctx.h);
    glUniform2f(U("u_origin"), ox, oy);
    glUniform1f(U("u_cell"), m_cellPx);
    glUniform2i(U("u_grid"), cols, rows);
    std::array<float, 18> col{};
    for (int i = 0; i < 6; ++i) col[i * 3] = m_col[i].r, col[i * 3 + 1] = m_col[i].g, col[i * 3 + 2] = m_col[i].b;
    glUniform3fv(U("u_col"), 6, col.data());
    glUniform1f(U("u_opacity"), opacity);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawUnitQuad();
    glDisable(GL_BLEND);
  }

  [[nodiscard]] bool visible() const override { return true; }
  [[nodiscard]] bool moving() const override { return m_preview < 0; }
  // in silence the noise still drifts and the pixels twinkle: a few frames a second
  [[nodiscard]] double idleFrame() const override { return 1.0 / 12; }
  [[nodiscard]] bool wantsMedia() const override { return m_text.find("@SONG@") != std::string::npos; }

  ~Pixels() override {
    if (m_tex) glDeleteTextures(1, &m_tex);
  }

private:
  struct Stamp {
    float x, y;  // 0..1 over the box
    float age, life, charge;
  };

  // the wandering light: a slow figure over the whole box (0..1)
  static std::pair<float, float> spotAt(double t) {
    const double ax = 0.44 * (1 + 0.1 * std::sin(t * 0.11)), ay = 0.38 * (1 + 0.1 * std::sin(t * 0.09 + 2));
    return {static_cast<float>(0.5 + ax * std::sin(t * 0.65)), static_cast<float>(0.48 + ay * std::sin(t * 0.39 + 1.1))};
  }

  // the word, rasterised once per change into whole pixels (true = on)
  void makeWord(int gridCols) {
    std::string word = m_text;
    auto replace = [&word](const std::string& key, const std::string& by) {
      for (size_t at = 0; (at = word.find(key, at)) != std::string::npos; at += by.size()) word.replace(at, key.size(), by);
    };
    if (word.find("@TIME@") != std::string::npos) {
      const std::time_t now = m_preview >= 0 ? 0 : std::time(nullptr);
      std::tm tm{};
      localtime_r(&now, &tm);
      char hm[8];
      std::strftime(hm, sizeof(hm), "%H:%M", &tm);
      replace("@TIME@", m_preview >= 0 ? "12:34" : hm);
    }
    replace("@SONG@", m_title);
    const std::string key = word + "\x1f" + std::to_string(gridCols);
    if (key == m_wordKey) return;
    m_wordKey = key;
    m_word.clear();
    m_wordW = m_wordH = 0;
    if (word.empty()) return;
    // as tall as asked, unless that would not fit across: then smaller
    for (int rows = m_rows; rows >= 5; --rows) {
      TextStyle st;
      st.family = m_font;
      st.weight = 800;
      st.size = static_cast<float>(rows) * 1.22F;  // capitals fill about 0.73 of the size
      float w = 0, h = 0, base = 0;
      TextRenderer::measure(word, st, w, h, base);
      const int cols = static_cast<int>(std::ceil(w)) + 2;
      if (cols > gridCols * 9 / 10 && rows > 5) continue;
      const int cellH = static_cast<int>(std::ceil(h));
      const std::vector<std::uint8_t> a = TextRenderer::glyphSheet({word}, st, cols, cellH, 1);
      if (a.size() < static_cast<size_t>(cols * cellH)) return;
      // only the rows something is drawn in
      int top = cellH, bottom = -1;
      for (int y = 0; y < cellH; ++y)
        for (int x = 0; x < cols; ++x)
          if (a[static_cast<size_t>(y * cols + x)] > 110) top = std::min(top, y), bottom = std::max(bottom, y);
      if (bottom < top) return;
      m_wordW = cols, m_wordH = bottom - top + 1;
      m_word.assign(static_cast<size_t>(m_wordW * m_wordH), false);
      for (int y = 0; y < m_wordH; ++y)
        for (int x = 0; x < cols; ++x) m_word[static_cast<size_t>(y * cols + x)] = a[static_cast<size_t>((y + top) * cols + x)] > 110;
      return;
    }
  }

  void fill(int cols, int rows, float w, float h, float ox, float oy) {
    m_cells.assign(static_cast<size_t>(cols * rows), Off);
    const float t = static_cast<float>(m_time);
    const auto [spx, spy] = spotAt(m_time);
    const float sx = spx * w, sy = spy * h;
    const float reach = 12 * m_cellPx * (0.45F + 0.55F * m_spot) * (1 + 0.8F * m_beat);
    // where the word sits: in the middle, a little above
    const int wc0 = (cols - m_wordW) / 2, wr0 = static_cast<int>(std::lround(rows * 0.45F - m_wordH / 2.0F));
    auto inWord = [&](int c, int r) {
      const int x = c - wc0, y = r - wr0;
      return m_wordW > 0 && x >= 0 && y >= 0 && x < m_wordW && y < m_wordH && m_word[static_cast<size_t>(y * m_wordW + x)];
    };
    // a square's outline where (px, py) is on one, 0..1
    auto stampAt = [&](float px, float py) {
      float best = 0;
      for (const Stamp& s : m_stamps) {
        const float ease = 1 - std::pow(1 - s.age, 3.0F);
        const float from = 2.0F, to = from + 6 + 16 * s.charge;
        const float R = m_cellPx * (from + (to - from) * ease);
        const float d = std::max(std::abs(px - s.x * w), std::abs(py - s.y * h));
        if (std::abs(d - R) < m_cellPx * 0.9F) best = std::max(best, std::pow(1 - s.age, 1.7F));
      }
      return best;
    };
    static const std::array<float, 64> kBayer = bayer8();
    for (int r = 0; r < rows; ++r) {
      const float cy = oy + (static_cast<float>(r) + 0.5F) * m_cellPx;
      const float ny = cy / h * 2 - 1;
      const int fromBottom = rows - 1 - r;
      for (int c = 0; c < cols; ++c) {
        const float cx = ox + (static_cast<float>(c) + 0.5F) * m_cellPx;
        unsigned char& out = m_cells[static_cast<size_t>(r * cols + c)];
        // the light and the squares, near the pixel
        float light = 0;
        if (m_spot > 0.01F) {
          const float d = std::hypot(cx - sx, cy - sy);
          if (d < reach) light = (1 - d / reach) * (1 - d / reach) * m_spot;
        }
        const float stamp = m_stamps.empty() ? 0 : stampAt(cx, cy);
        if (inWord(c, r)) {  // the word: shaded top to bottom, lit by what passes over it
          const float q = static_cast<float>(r - wr0) / static_cast<float>(std::max(1, m_wordH));
          const float over = std::max(light, stamp);
          out = over > 0.45F ? Crest : over > 0.12F ? Hover : q < 0.27F ? Crest : q < 0.37F ? Hover : q < 0.58F ? Lit : q < 0.74F ? Mid : Dim;
          continue;
        }
        // the field: empty in the middle, fuller toward the corners
        const float nx = cx / w * 2 - 1, s = std::sqrt(nx * nx + ny * ny * 0.82F);
        const float edge = std::clamp((s - m_hole) / 0.85F, 0.0F, 1.0F), f = edge * edge;
        float v = 0;
        if (f > 0.002F) {
          const float gx = static_cast<float>(c) / 9, gy = static_cast<float>(r) / 9;
          const float n = 0.6F * m_noise.at(gx + t * 0.14F, gy - t * 0.055F) + 0.4F * m_noise.at(gx * 0.55F - t * 0.08F, gy * 0.55F + t * 0.06F);
          const float twinkle = 0.5F + 0.5F * std::sin(t * 1.1F + hash01(c, r) * 6.2832F);
          v = f * (0.3F + 0.52F * n * n + 0.18F * twinkle) * 0.62F * m_density * 2;
        }
        v += light * 0.6F + stamp * 1.15F;
        // the music, in columns from the bottom: bass at the sides
        float music = 0;
        if (m_spectrum && f > 0.002F) {
          const float u = (static_cast<float>(c) + 0.5F) / static_cast<float>(cols);
          const float bi = (1 - std::abs(u - 0.5F) * 2) * 32 - 0.5F;
          const int i0 = std::clamp(static_cast<int>(std::floor(bi)), 0, 31), i1 = std::min(31, i0 + 1);
          const float k = std::clamp(bi - static_cast<float>(i0), 0.0F, 1.0F);
          const float band = m_bands[i0] * (1 - k) + m_bands[i1] * k;
          const float level = std::max(0.0F, (band - 0.08F) / 0.92F), height = level * static_cast<float>(rows) * 0.92F;
          if (level > 0 && static_cast<float>(fromBottom) < height) {
            music = level * std::pow(1 - static_cast<float>(fromBottom) / height, 0.85F);
            v += music * 0.7F * std::min(1.0F, f * 3);
          }
        }
        // on or off: an ordered dither, a little shaken so it never looks ruled
        const float threshold = 0.78F * kBayer[(r & 7) * 8 + (c & 7)] + 0.22F * hash01(c & 63, (r & 63) + 977);
        if (v <= threshold) continue;
        const float strong = std::max({light, stamp, music * 0.5F});
        out = strong > 0.34F ? Lit : strong > 0.1F ? Mid : Dim;
      }
    }
  }

  void upload(int cols, int rows) {
    if (!m_tex) glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (cols != m_texW || rows != m_texH) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, cols, rows, 0, GL_RED, GL_UNSIGNED_BYTE, m_cells.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      m_texW = cols, m_texH = rows;
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RED, GL_UNSIGNED_BYTE, m_cells.data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  }

  float m_cellPx = 11, m_density = 0.5F, m_hole = 0.42F;
  std::string m_text, m_font, m_title, m_wordKey;
  int m_rows = 15;
  bool m_spectrum = true, m_spotOn = true, m_stampsOn = true;
  double m_hits = 0.7, m_preview = -1, m_time = 0;
  std::array<Color, 6> m_col{};
  std::array<float, 32> m_bands{};
  float m_beat = 0, m_spot = 0, m_quiet = 0, m_nextQuiet = 1.5F;
  std::vector<Stamp> m_stamps;
  std::vector<bool> m_word;
  int m_wordW = 0, m_wordH = 0;
  std::vector<unsigned char> m_cells;
  Noise m_noise;
  Program m_prog;
  GLuint m_tex = 0;
  int m_texW = 0, m_texH = 0;
};

}  // namespace

std::unique_ptr<StrokeLook> makePixels() { return std::make_unique<Pixels>(); }

}  // namespace undershell
