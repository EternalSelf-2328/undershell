// SPDX-License-Identifier: GPL-3.0-or-later
#include "text.hpp"

#include <cairo.h>
#include <cmath>
#include <filesystem>
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <vector>

#ifndef US_SOURCE_DATA_DIR
#define US_SOURCE_DATA_DIR ""
#endif
#ifndef US_INSTALL_DATA_DIR
#define US_INSTALL_DATA_DIR ""
#endif

namespace undershell {

namespace fs = std::filesystem;

std::string TextRenderer::fontsDir() {
  std::vector<std::string> candidates;
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) candidates.push_back(std::string(xdg) + "/undershell/fonts");
  candidates.push_back(expandHome("~/.local/share/undershell/fonts"));
  if (*US_INSTALL_DATA_DIR) candidates.push_back(std::string(US_INSTALL_DATA_DIR) + "/fonts");
  if (*US_SOURCE_DATA_DIR) candidates.push_back(std::string(US_SOURCE_DATA_DIR) + "/fonts");
  for (auto& c : candidates)
    if (fs::is_directory(c)) return c;
  return {};
}

void TextRenderer::registerBundledFonts() {
  static bool done = false;
  if (done) return;
  done = true;
  const std::string dir = fontsDir();
  if (dir.empty()) {
    US_WARN("bundled fonts not found; falling back to system fonts");
    return;
  }
  FcConfig* cfg = FcConfigGetCurrent();
  if (!FcConfigAppFontAddDir(cfg, reinterpret_cast<const FcChar8*>(dir.c_str()))) {
    US_WARN("could not register fonts in {}", dir);
    return;
  }
  US_DEBUG("registered fonts from {}", dir);
}

static PangoLayout* makeLayout(cairo_t* cr, const std::string& text, const TextStyle& st) {
  PangoLayout* layout = pango_cairo_create_layout(cr);
  PangoFontDescription* fd = pango_font_description_new();
  pango_font_description_set_family(fd, st.family.c_str());
  pango_font_description_set_absolute_size(fd, st.size * PANGO_SCALE);
  pango_font_description_set_weight(fd, static_cast<PangoWeight>(std::clamp(st.weight, 100, 1000)));
  if (st.italic) pango_font_description_set_style(fd, PANGO_STYLE_ITALIC);
  pango_layout_set_font_description(layout, fd);
  pango_font_description_free(fd);
  if (st.letterSpacing != 0) {
    PangoAttrList* attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_letter_spacing_new(static_cast<int>(st.letterSpacing * PANGO_SCALE)));
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
  }
  pango_layout_set_text(layout, text.c_str(), static_cast<int>(text.size()));
  return layout;
}

void TextRenderer::measure(const std::string& text, const TextStyle& st, float& w, float& h, float& baseline) {
  registerBundledFonts();
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
  cairo_t* cr = cairo_create(s);
  PangoLayout* layout = makeLayout(cr, text, st);
  PangoRectangle ink, logical;
  pango_layout_get_pixel_extents(layout, &ink, &logical);
  w = static_cast<float>(logical.width);
  h = static_cast<float>(logical.height);
  baseline = static_cast<float>(pango_layout_get_baseline(layout)) / PANGO_SCALE;
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(s);
}

TextRenderer::~TextRenderer() = default;  // GL objects go with the context

void TextRenderer::releaseGl() {
  for (auto& [k, e] : m_cache)
    if (e.img.texture) glDeleteTextures(1, &e.img.texture);
  m_cache.clear();
  m_prog.destroy();
}

const TextImage& TextRenderer::get(const std::string& text, const TextStyle& st, int scale) {
  registerBundledFonts();
  const std::string key = std::format("{}\x1f{}\x1f{:.2f}\x1f{}\x1f{:.2f}\x1f{}\x1f{}", text, st.family, st.size, st.weight,
                                      st.letterSpacing, st.italic ? 1 : 0, scale);
  auto it = m_cache.find(key);
  if (it != m_cache.end()) {
    it->second.lastUse = nowSeconds();
    return it->second.img;
  }

  // measure at the target scale so hinting matches what is drawn
  cairo_surface_t* probe = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
  cairo_t* pcr = cairo_create(probe);
  cairo_scale(pcr, scale, scale);
  PangoLayout* layout = makeLayout(pcr, text, st);
  PangoRectangle ink, logical;
  pango_layout_get_extents(layout, &ink, &logical);
  const double baseline = static_cast<double>(pango_layout_get_baseline(layout)) / PANGO_SCALE;
  g_object_unref(layout);
  cairo_destroy(pcr);
  cairo_surface_destroy(probe);

  auto toPx = [](int v) { return static_cast<double>(v) / PANGO_SCALE; };
  // the texture covers the union of ink and logical boxes (ink can overhang)
  const double left = std::min(toPx(ink.x), toPx(logical.x));
  const double top = std::min(toPx(ink.y), toPx(logical.y));
  const double right = std::max(toPx(ink.x + ink.width), toPx(logical.x + logical.width));
  const double bottom = std::max(toPx(ink.y + ink.height), toPx(logical.y + logical.height));
  const int pad = 1;
  const int pxW = std::max(1, static_cast<int>(std::ceil((right - left) * scale)) + 2 * pad);
  const int pxH = std::max(1, static_cast<int>(std::ceil((bottom - top) * scale)) + 2 * pad);

  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, pxW, pxH);
  cairo_t* cr = cairo_create(surf);
  cairo_translate(cr, pad, pad);
  cairo_scale(cr, scale, scale);
  cairo_translate(cr, -left, -top);
  cairo_set_source_rgba(cr, 1, 1, 1, 1);
  layout = makeLayout(cr, text, st);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_flush(surf);

  TextImage img;
  img.pxW = pxW;
  img.pxH = pxH;
  img.w = static_cast<float>(toPx(logical.width));
  img.h = static_cast<float>(toPx(logical.height));
  img.baseline = static_cast<float>(baseline);
  img.inkLeft = static_cast<float>(toPx(ink.x));
  img.inkTop = static_cast<float>(toPx(ink.y));
  img.inkW = static_cast<float>(toPx(ink.width));
  img.inkH = static_cast<float>(toPx(ink.height));
  // the texture quad relative to the logical box origin, logical px
  img.quadX = static_cast<float>(left - toPx(logical.x)) - pad / static_cast<float>(scale);
  img.quadY = static_cast<float>(top - toPx(logical.y)) - pad / static_cast<float>(scale);
  img.quadW = static_cast<float>(pxW) / static_cast<float>(scale);
  img.quadH = static_cast<float>(pxH) / static_cast<float>(scale);

  glGenTextures(1, &img.texture);
  glBindTexture(GL_TEXTURE_2D, img.texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  const int stride = cairo_image_surface_get_stride(surf);
  const unsigned char* data = cairo_image_surface_get_data(surf);
  std::vector<unsigned char> tight(static_cast<size_t>(pxW) * pxH);
  for (int y = 0; y < pxH; ++y) std::copy_n(data + static_cast<size_t>(y) * stride, pxW, tight.data() + static_cast<size_t>(y) * pxW);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, pxW, pxH, 0, GL_RED, GL_UNSIGNED_BYTE, tight.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  cairo_surface_destroy(surf);

  auto& e = m_cache[key];
  e.img = img;
  e.lastUse = nowSeconds();
  return e.img;
}

static const char* kTextFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_tex;
uniform vec4 u_color;   // straight alpha
void main() {
    float a = texture(u_tex, v_uv).r * u_color.a;
    fragColor = vec4(u_color.rgb * a, a);
}
)";

static const char* kTextVert = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
uniform vec4 u_rect;     // x, y, w, h in surface logical px
uniform vec2 u_surface;  // surface logical size
out vec2 v_uv;
void main() {
    v_uv = a_pos;
    vec2 p = (u_rect.xy + a_pos * u_rect.zw) / u_surface;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
)";

void TextRenderer::draw(const TextImage& img, float x, float y, Color color, float surfaceW, float surfaceH,
                        float opacity) {
  if (!img.texture || surfaceW <= 0 || surfaceH <= 0) return;
  if (!m_prog.valid()) m_prog.create(kTextVert, kTextFrag, "text");
  glUseProgram(m_prog.id());
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, img.texture);
  glUniform1i(m_prog.uniform("u_tex"), 0);
  glUniform4f(m_prog.uniform("u_color"), color.r, color.g, color.b, color.a * opacity);
  glUniform2f(m_prog.uniform("u_surface"), surfaceW, surfaceH);
  glUniform4f(m_prog.uniform("u_rect"), x + img.quadX, y + img.quadY, img.quadW, img.quadH);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  drawUnitQuad();
  glDisable(GL_BLEND);
}

void TextRenderer::collect(double maxAgeSec) {
  const double now = nowSeconds();
  for (auto it = m_cache.begin(); it != m_cache.end();) {
    if (now - it->second.lastUse > maxAgeSec) {
      if (it->second.img.texture) glDeleteTextures(1, &it->second.img.texture);
      it = m_cache.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace undershell
