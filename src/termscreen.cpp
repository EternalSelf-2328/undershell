// SPDX-License-Identifier: GPL-3.0-or-later
#include "termscreen.hpp"

#include "text.hpp"

#include <algorithm>
#include <cmath>

namespace undershell {

namespace {

const char* kFace = "JetBrains Mono";

std::string utf8Of(char32_t c) {
  std::string s;
  if (c < 0x80) {
    s += static_cast<char>(c);
  } else if (c < 0x800) {
    s += static_cast<char>(0xC0 | (c >> 6));
    s += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    s += static_cast<char>(0xE0 | (c >> 12));
    s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (c & 0x3F));
  } else {
    s += static_cast<char>(0xF0 | (c >> 18));
    s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (c & 0x3F));
  }
  return s;
}

const char* kGridVertex = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in float a_bright;
uniform vec2 u_res;
out vec2 v_uv;
out float v_bright;
void main() {
    v_uv = a_uv;
    v_bright = a_bright;
    gl_Position = vec4(a_pos.x / u_res.x * 2.0 - 1.0, 1.0 - a_pos.y / u_res.y * 2.0, 0.0, 1.0);
}
)";

const char* kGridFragment = R"(#version 300 es
precision highp float;
in vec2 v_uv;
in float v_bright;
out vec4 fragColor;
uniform sampler2D u_sheet;
void main() {
    float a = texture(u_sheet, v_uv).r * v_bright;
    fragColor = vec4(a, a, a, a);
}
)";

// The screen through its tube: the grid's picture curved like the glass,
// its glow read off the mipmaps, scanlines across, a frame's flicker, a dark
// phosphor-tinted glass behind, corners rounded and shaded.
const char* kCrtFragment = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_tex;
uniform vec2 u_res;      // device px
uniform vec3 u_ph;
uniform float u_glow;
uniform float u_scan;
uniform float u_pitch;   // device px between scanlines
uniform float u_flick;
uniform float u_curve;
uniform float u_bg;
uniform float u_opacity;
void main() {
    vec2 c = v_uv * 2.0 - 1.0;
    c *= 1.0 + u_curve * 0.1 * (c.yx * c.yx);
    vec2 uv = c * 0.5 + 0.5;
    // the glass: a rounded rectangle, softly edged
    vec2 q = abs(c) - (1.0 - 0.06);
    float edge = length(max(q, 0.0)) - 0.06;
    float inside = 1.0 - smoothstep(-0.008, 0.004, edge);
    vec2 t = vec2(uv.x, 1.0 - uv.y);
    float text = texture(u_tex, t).r;
    float glow = textureLod(u_tex, t, 2.0).r * 0.55 + textureLod(u_tex, t, 4.0).r * 0.45;
    float v = text + glow * u_glow * 1.5;
    v *= 1.0 - u_scan * 0.42 * (0.5 + 0.5 * cos(uv.y * u_res.y * 6.2831853 / u_pitch));
    v *= 1.0 - u_flick;
    v *= 1.0 - 0.3 * u_curve * dot(c, c) * 0.5;  // darker toward the corners
    float a = clamp(v, 0.0, 1.0);
    // a stroke hotter than full goes toward white, as phosphor does
    vec3 hot = mix(u_ph, vec3(1.0), clamp(v - 1.0, 0.0, 1.0) * 0.6);
    vec3 glass = u_ph * 0.04 + vec3(0.012);
    float alpha = (u_bg + a * (1.0 - u_bg)) * inside;
    vec3 col = (glass * u_bg * (1.0 - a) + hot * a) * inside;
    fragColor = vec4(min(col, vec3(alpha)), alpha) * u_opacity;
}
)";

}  // namespace

std::u32string TermScreen::decode(std::string_view s) {
  std::u32string out;
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    char32_t cp;
    size_t n;
    if (c < 0x80) cp = c, n = 1;
    else if (c < 0xE0) cp = c & 0x1F, n = 2;
    else if (c < 0xF0) cp = c & 0x0F, n = 3;
    else cp = c & 0x07, n = 4;
    for (size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    out += cp;
    i += n;
  }
  return out;
}

void TermScreen::layout(float w, float h, float scale, float textScale) {
  // at least ~40 columns and ~18 rows at the usual size
  const float cellH = std::clamp(std::min(h / 18, w / 40 / 0.6F * 1.0F), 8.0F, 48.0F) * textScale;
  m_em = cellH / 1.32F;
  float mw, mh, mb;
  TextStyle st;
  st.family = kFace;
  st.size = m_em;
  st.weight = 600;
  TextRenderer::measure("M", st, mw, mh, mb);
  m_scale = scale;
  m_cellH = cellH;
  m_cellW = std::max(3.0F, mw);
  m_cw = std::max(2, static_cast<int>(std::lround(m_cellW * scale)));
  m_ch = std::max(4, static_cast<int>(std::lround(m_cellH * scale)));
  const int cols = std::max(1, static_cast<int>(w * scale / static_cast<float>(m_cw)));
  const int rows = std::max(1, static_cast<int>(h * scale / static_cast<float>(m_ch)));
  if (cols != m_cols || rows != m_rows) {
    m_cols = cols;
    m_rows = rows;
    m_cells.assign(static_cast<size_t>(cols) * rows, Cell{});
  }
}

void TermScreen::clear() { std::fill(m_cells.begin(), m_cells.end(), Cell{}); }

void TermScreen::put(int col, int row, char32_t ch, float bright) {
  if (col < 0 || row < 0 || col >= m_cols || row >= m_rows) return;
  m_cells[static_cast<size_t>(row) * m_cols + col] = {ch, bright, 0, 0};
}

int TermScreen::text(int col, int row, std::string_view utf8, float bright) {
  int n = 0;
  for (char32_t c : decode(utf8)) put(col + n++, row, c, bright);
  return n;
}

void TermScreen::bar(int col, int row, float fill, float bright) {
  if (col < 0 || row < 0 || col >= m_cols || row >= m_rows || fill <= 0) return;
  m_cells[static_cast<size_t>(row) * m_cols + col] = {0, bright, 1, std::min(1.0F, fill)};
}

void TermScreen::hbar(int col, int row, float fill, float bright) {
  if (col < 0 || row < 0 || col >= m_cols || row >= m_rows || fill <= 0) return;
  m_cells[static_cast<size_t>(row) * m_cols + col] = {0, bright, 2, std::min(1.0F, fill)};
}

int TermScreen::glyphIndex(char32_t c) const {
  auto it = m_index.find(c);
  if (it != m_index.end()) return it->second;
  auto q = m_index.find(U'?');
  return q == m_index.end() ? 0 : q->second;
}

void TermScreen::buildSheet() {
  if (m_charset.empty()) {
    m_charset.push_back(0);  // cell 0: solid, for the bars
    for (char32_t c = 33; c < 127; ++c) m_charset.push_back(c);
    for (char32_t c : std::u32string(U"♪♫·…—–→←↑↓°±×÷■□▪◆●○▲▼►◄▔─━│┃┌┐└┘├┤┬┴┼═║╔╗╚╝░▒▓✓✗λ$"))
      m_charset.push_back(c);
    for (char32_t c = 0xFF66; c <= 0xFF9D; ++c) m_charset.push_back(c);  // halfwidth katakana
    for (size_t i = 0; i < m_charset.size(); ++i) m_index[m_charset[i]] = static_cast<int>(i);
  }
  std::vector<std::string> glyphs;
  for (char32_t c : m_charset) glyphs.push_back(c ? utf8Of(c) : std::string());
  TextStyle st;
  st.family = kFace;
  st.size = m_em * m_scale;
  st.weight = 600;
  std::vector<std::uint8_t> px = TextRenderer::glyphSheet(glyphs, st, m_cw, m_ch, m_perRow);
  for (int y = 0; y < m_ch; ++y) std::fill_n(px.begin() + static_cast<ptrdiff_t>(y) * m_perRow * m_cw, m_cw, 255);  // the solid cell
  m_sheetW = m_perRow * m_cw;
  m_sheetH = static_cast<int>(px.size()) / m_sheetW;
  if (!m_sheet) glGenTextures(1, &m_sheet);
  glBindTexture(GL_TEXTURE_2D, m_sheet);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_sheetW, m_sheetH, 0, GL_RED, GL_UNSIGNED_BYTE, px.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);  // cells sit on device pixels
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  m_sheetEm = static_cast<int>(std::lround(m_em * m_scale * 8)) * 10000 + m_cw * 100 + m_ch;
}

void TermScreen::draw(const Crt& crt) {
  if (m_cols <= 0 || m_rows <= 0) return;
  GLint prevFbo = 0, vp[4] = {0, 0, 1, 1};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
  glGetIntegerv(GL_VIEWPORT, vp);
  const int W = std::max(1, vp[2]), H = std::max(1, vp[3]);
  const int key = static_cast<int>(std::lround(m_em * m_scale * 8)) * 10000 + m_cw * 100 + m_ch;
  if (!m_sheet || key != m_sheetEm) buildSheet();
  // the grid's picture, as big as the target
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
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  // every cell, a quad on device pixels, centred in the target
  const float ox = std::floor((static_cast<float>(W) - static_cast<float>(m_cols * m_cw)) / 2);
  const float oy = std::floor((static_cast<float>(H) - static_cast<float>(m_rows * m_ch)) / 2);
  const float su = 1.0F / static_cast<float>(m_sheetW), sv = 1.0F / static_cast<float>(m_sheetH);
  m_verts.clear();
  for (int r = 0; r < m_rows; ++r)
    for (int c = 0; c < m_cols; ++c) {
      const Cell& cell = m_cells[static_cast<size_t>(r) * m_cols + c];
      if (cell.bright <= 0 || (cell.kind == 0 && (cell.ch == 0 || cell.ch == U' '))) continue;
      float x0 = ox + static_cast<float>(c * m_cw), y0 = oy + static_cast<float>(r * m_ch);
      float x1 = x0 + static_cast<float>(m_cw), y1 = y0 + static_cast<float>(m_ch);
      int g = 0;  // the solid cell, for bars
      if (cell.kind == 1) y0 = y1 - std::round(static_cast<float>(m_ch) * cell.fill);
      else if (cell.kind == 2) x1 = x0 + std::round(static_cast<float>(m_cw) * cell.fill);
      else g = cell.ch == U'█' ? 0 : glyphIndex(cell.ch);
      if (x1 <= x0 || y1 <= y0) continue;
      const float u0 = static_cast<float>((g % m_perRow) * m_cw) * su, v0 = static_cast<float>((g / m_perRow) * m_ch) * sv;
      const float u1 = u0 + static_cast<float>(m_cw) * su * (cell.kind == 2 ? cell.fill : 1.0F);
      const float v1 = v0 + static_cast<float>(m_ch) * sv;
      const float b = cell.bright;
      const float q[4][4] = {{x0, y0, u0, v0}, {x1, y0, u1, v0}, {x0, y1, u0, v1}, {x1, y1, u1, v1}};
      for (int k : {0, 1, 2, 2, 1, 3}) m_verts.insert(m_verts.end(), {q[k][0], q[k][1], q[k][2], q[k][3], b});
    }
  if (!m_gridProg.valid()) m_gridProg.create(kGridVertex, kGridFragment, "termgrid");
  if (!m_verts.empty()) {
    glUseProgram(m_gridProg.id());
    glUniform2f(m_gridProg.uniform("u_res"), static_cast<float>(W), static_cast<float>(H));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sheet);
    glUniform1i(m_gridProg.uniform("u_sheet"), 0);
    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (GLuint a = 0; a < 3; ++a) glEnableVertexAttribArray(a);
    const GLsizei stride = 5 * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, m_verts.data());
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, m_verts.data() + 2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, m_verts.data() + 4);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_verts.size() / 5));
    for (GLuint a = 0; a < 3; ++a) glDisableVertexAttribArray(a);
  }
  glBindTexture(GL_TEXTURE_2D, m_tex);
  glGenerateMipmap(GL_TEXTURE_2D);
  // and through the tube onto the target
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  if (!m_crtProg.valid()) m_crtProg.create(kQuadVertexShader, kCrtFragment, "crt");
  glUseProgram(m_crtProg.id());
  auto U = [&](const char* n) { return m_crtProg.uniform(n); };
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_tex);
  glUniform1i(U("u_tex"), 0);
  glUniform2f(U("u_res"), static_cast<float>(W), static_cast<float>(H));
  glUniform3fv(U("u_ph"), 1, crt.phosphor);
  glUniform1f(U("u_glow"), crt.glow);
  glUniform1f(U("u_scan"), crt.scan);
  glUniform1f(U("u_pitch"), std::max(2.0F, static_cast<float>(m_ch) / 4.0F));
  glUniform1f(U("u_flick"), crt.flicker * 0.14F * crt.noise);
  glUniform1f(U("u_curve"), crt.curve);
  glUniform1f(U("u_bg"), crt.background);
  glUniform1f(U("u_opacity"), crt.opacity);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  drawUnitQuad();
  glDisable(GL_BLEND);
}

void TermScreen::release() {
  if (m_sheet) glDeleteTextures(1, &m_sheet);
  if (m_tex) glDeleteTextures(1, &m_tex);
  if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
  m_sheet = m_tex = m_fbo = 0;
  m_gridProg.destroy();
  m_crtProg.destroy();
}

}  // namespace undershell
