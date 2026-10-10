// SPDX-License-Identifier: GPL-3.0-or-later
#include "text.hpp"

#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <unordered_map>
#include <filesystem>
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <sys/stat.h>
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
  if (const char* env = std::getenv("UNDERSHELL_DATA_DIR"); env && *env) candidates.push_back(std::string(env) + "/fonts");
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) candidates.push_back(std::string(xdg) + "/undershell/fonts");
  candidates.push_back(expandHome("~/.local/share/undershell/fonts"));
  if (*US_INSTALL_DATA_DIR) candidates.push_back(std::string(US_INSTALL_DATA_DIR) + "/fonts");
  if (*US_SOURCE_DATA_DIR) candidates.push_back(std::string(US_SOURCE_DATA_DIR) + "/fonts");
  for (auto& c : candidates)
    if (fs::is_directory(c)) return c;
  return {};
}

// the font folders' modification times: a font added, removed or renamed
// changes its folder's (fontconfig itself only looks every 30 s)
static std::string fontDirsStamp() {
  std::string stamp;
  auto add = [&](FcStrList* list) {
    if (!list) return;
    while (FcChar8* d = FcStrListNext(list)) {
      struct stat sb{};
      const bool ok = stat(reinterpret_cast<const char*>(d), &sb) == 0;
      stamp += std::format("{}:{};", reinterpret_cast<const char*>(d), ok ? static_cast<long long>(sb.st_mtime) : -1LL);
    }
    FcStrListDone(list);
  };
  add(FcConfigGetConfigDirs(nullptr));  // configured folders, even ones that do not exist yet
  add(FcConfigGetFontDirs(nullptr));    // and every folder fontconfig scanned
  return stamp;
}

bool TextRenderer::refreshFonts() {
  static std::string last = fontDirsStamp();
  const std::string now = fontDirsStamp();
  if (now == last) return false;
  last = now;
  // a new current config: register the bundled fonts on it again, and tell
  // Pango's font map so layouts see the new families
  if (!FcInitReinitialize()) return false;
  if (const std::string dir = fontsDir(); !dir.empty())
    FcConfigAppFontAddDir(FcConfigGetCurrent(), reinterpret_cast<const FcChar8*>(dir.c_str()));
  PangoFontMap* map = pango_cairo_font_map_get_default();
  if (PANGO_IS_FC_FONT_MAP(map)) {
    pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(map), FcConfigGetCurrent());
    pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(map));
  }
  last = fontDirsStamp();  // the new config may scan more folders
  US_INFO("fonts changed on disk: reloaded");
  return true;
}

void TextRenderer::registerBundledFonts() {
  static bool done = false;
  if (done) return;
  done = true;
  (void)refreshFonts();  // first call: note how the font folders look at startup
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

// undershell's own fonts, in a font map that knows only them. Matching a
// family against every font on the system costs several milliseconds for each
// (family, size, axes) never seen before -- about ten on a machine with a
// large collection -- and that is paid again for every size a widget animates
// through or every instance a variable font is asked for. Against the handful
// in data/fonts it is about three.
static PangoFontMap* ownFontMap() {
  static PangoFontMap* map = [] {
    PangoFontMap* m = nullptr;
    const std::string dir = TextRenderer::fontsDir();
    if (dir.empty()) return m;
    FcConfig* cfg = FcConfigCreate();  // kept for the life of the process
    if (!cfg) return m;
    // a config of its own has no cache directory, and fontconfig complains
    // on every scan without one
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    const std::string cache = std::string(xdg && *xdg ? xdg : expandHome("~/.cache")) + "/undershell/fontconfig";
    std::error_code ec;
    fs::create_directories(cache, ec);
    // The system's rules (hinting, antialiasing, subpixel order) come from
    // conf.d and the user's own file; the directories do not, which is the
    // whole point. Without them the bundled faces would be rendered unhinted
    // and would not look like the rest of the desktop.
    const std::string xml = "<?xml version=\"1.0\"?><fontconfig><cachedir>" + cache +
                            "</cachedir>"
                            "<include ignore_missing=\"yes\">/etc/fonts/conf.d</include>"
                            "<include ignore_missing=\"yes\">" +
                            expandHome("~/.config/fontconfig/conf.d") +
                            "</include>"
                            "<include ignore_missing=\"yes\">" +
                            expandHome("~/.config/fontconfig/fonts.conf") + "</include></fontconfig>";
    FcConfigParseAndLoadFromMemory(cfg, reinterpret_cast<const FcChar8*>(xml.c_str()), FcTrue);
    if (!FcConfigAppFontAddDir(cfg, reinterpret_cast<const FcChar8*>(dir.c_str()))) return m;
    m = pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
    pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(m), cfg);
    return m;
  }();
  return map;
}

// the families that map holds, by name
static bool bundledFamily(const std::string& family) {
  static const std::vector<std::string> names = [] {
    std::vector<std::string> out;
    if (PangoFontMap* m = ownFontMap()) {
      PangoFontFamily** fam = nullptr;
      int n = 0;
      pango_font_map_list_families(m, &fam, &n);
      for (int i = 0; i < n; ++i) out.emplace_back(pango_font_family_get_name(fam[i]));
      g_free(fam);
    }
    return out;
  }();
  return std::find(names.begin(), names.end(), family) != names.end();
}

static PangoLayout* newLayout(cairo_t* cr, bool own) {
  if (!own) return pango_cairo_create_layout(cr);
  static PangoContext* ctx = nullptr;  // one context: making one is not free either
  if (!ctx) ctx = pango_font_map_create_context(ownFontMap());
  pango_cairo_update_context(cr, ctx);
  return pango_layout_new(ctx);
}

static void applyTo(PangoLayout* layout, const std::string& text, const TextStyle& st) {
  PangoFontDescription* fd = pango_font_description_new();
  pango_font_description_set_family(fd, st.family.c_str());
  pango_font_description_set_absolute_size(fd, st.size * PANGO_SCALE);
  pango_font_description_set_weight(fd, static_cast<PangoWeight>(std::clamp(st.weight, 100, 1000)));
  if (!st.variations.empty()) pango_font_description_set_variations(fd, st.variations.c_str());
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
  if (st.maxWidth > 0) {
    pango_layout_set_width(layout, static_cast<int>(st.maxWidth * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_height(layout, -std::max(1, st.maxLines));  // negative = line count
    pango_layout_set_alignment(layout, st.align == 1 ? PANGO_ALIGN_CENTER : (st.align == 2 ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT));
  }
}

static PangoLayout* makeLayout(cairo_t* cr, const std::string& text, const TextStyle& st) {
  const bool own = ownFontMap() && bundledFamily(st.family);
  PangoLayout* layout = newLayout(cr, own);
  applyTo(layout, text, st);
  // A map that holds only undershell's own fonts cannot stand in for the
  // system's when the text asks for a glyph none of them has -- a title in
  // Japanese, an emoji in a lyric. Then it is laid out again, the usual way,
  // and the fallback the system offers comes back with it.
  if (own && pango_layout_get_unknown_glyphs_count(layout) > 0) {
    g_object_unref(layout);
    layout = newLayout(cr, false);
    applyTo(layout, text, st);
  }
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

TextRenderer::Ink TextRenderer::measureInk(const std::string& text, const TextStyle& st) {
  static std::unordered_map<std::string, Ink> cache;
  const std::string key = std::format("{}\x1f{}\x1f{:.2f}\x1f{}\x1f{:.2f}\x1f{}\x1f{:.2f}\x1f{}\x1f{:.2f}\x1f{}\x1f{}", text, st.family,
                                      st.size, st.weight, st.letterSpacing, st.italic ? 1 : 0, st.stroke, st.variations,
                                      st.maxWidth, st.maxLines, st.align);
  if (auto it = cache.find(key); it != cache.end()) return it->second;
  registerBundledFonts();
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
  cairo_t* cr = cairo_create(s);
  PangoLayout* layout = makeLayout(cr, text, st);
  PangoRectangle ink, logical;
  pango_layout_get_extents(layout, &ink, &logical);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(s);
  const auto px = [](int v) { return static_cast<float>(v) / PANGO_SCALE; };
  Ink r;
  r.x = px(ink.x - logical.x) - st.stroke;  // a hollow outline sits outside the glyph
  r.y = px(ink.y - logical.y) - st.stroke;
  r.w = px(ink.width) + 2 * st.stroke;
  r.h = px(ink.height) + 2 * st.stroke;
  r.boxW = px(logical.width);
  r.boxH = px(logical.height);
  if (cache.size() > 512) cache.clear();
  cache.emplace(key, r);
  return r;
}

TextRenderer::~TextRenderer() = default;  // GL objects go with the context

void TextRenderer::releaseGl() {
  for (auto& [k, e] : m_cache)
    if (e.img.texture) glDeleteTextures(1, &e.img.texture);
  m_cache.clear();
  m_prog.destroy();
}

const TextImage& TextRenderer::get(const std::string& text, const TextStyle& st, float scale) {
  registerBundledFonts();
  const std::string key = std::format("{}\x1f{}\x1f{:.2f}\x1f{}\x1f{:.2f}\x1f{}\x1f{}\x1f{:.2f}\x1f{:.1f}\x1f{}\x1f{}\x1f{}", text,
                                      st.family, st.size, st.weight, st.letterSpacing, st.italic ? 1 : 0, std::round(scale * 1000) / 1000, st.stroke,
                                      st.maxWidth, st.maxLines, st.align, st.variations);
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
  // room for an outline and for a blurred halo sampled around the glyphs
  const int pad = 2 + static_cast<int>(std::ceil(st.stroke * scale)) + static_cast<int>(std::ceil(st.size * 0.12 * scale));
  const int pxW = std::max(1, static_cast<int>(std::ceil((right - left) * scale)) + 2 * pad);
  const int pxH = std::max(1, static_cast<int>(std::ceil((bottom - top) * scale)) + 2 * pad);

  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, pxW, pxH);
  cairo_t* cr = cairo_create(surf);
  cairo_translate(cr, pad, pad);
  cairo_scale(cr, scale, scale);
  cairo_translate(cr, -left, -top);
  cairo_set_source_rgba(cr, 1, 1, 1, 1);
  layout = makeLayout(cr, text, st);
  if (st.stroke > 0) {
    // Hollow text: the outer half of a double-width stroke. Many fonts build
    // a glyph from overlapping contours (an R's bowl over its stem, a 4's bar
    // across its stem, static instances of variable fonts); a plain stroke
    // outlines every piece and shows those seams. Clearing the filled glyph
    // (non-zero winding: the union of its contours) keeps only the outline.
    pango_cairo_layout_path(cr, layout);
    cairo_set_line_width(cr, 2 * st.stroke);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke_preserve(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    cairo_fill(cr);
  } else {
    pango_cairo_show_layout(cr, layout);
  }
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
  img.scale = scale;

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
uniform vec2 u_blur;    // blur radius in uv units (0 = sharp)
void main() {
    float a;
    if (u_blur.x > 0.0) {
        // 7x7 gaussian-ish tap grid: a soft halo behind the glyphs
        float sum = 0.0, wsum = 0.0;
        for (int j = -3; j <= 3; j++)
            for (int i = -3; i <= 3; i++) {
                float w = exp(-float(i * i + j * j) / 6.0);
                sum += texture(u_tex, v_uv + vec2(float(i), float(j)) * u_blur / 3.0).r * w;
                wsum += w;
            }
        a = sum / wsum;
    } else {
        a = texture(u_tex, v_uv).r;
    }
    a *= u_color.a;
    fragColor = vec4(u_color.rgb * a, a);
}
)";

static const char* kTextVert = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
uniform vec4 u_rect;     // x, y, w, h in surface logical px
uniform vec2 u_surface;  // surface logical size
uniform vec3 u_turn;     // the centre it turns about (logical px) and the angle
out vec2 v_uv;
void main() {
    v_uv = a_pos;
    vec2 q = u_rect.xy + a_pos * u_rect.zw - u_turn.xy;
    float c = cos(u_turn.z), s = sin(u_turn.z);
    vec2 p = (u_turn.xy + vec2(c * q.x - s * q.y, s * q.x + c * q.y)) / u_surface;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
)";

std::vector<TextRenderer::Contour> TextRenderer::outline(const std::string& text, const TextStyle& style, float tolerance) {
  std::vector<Contour> out;
  if (text.empty()) return out;
  registerBundledFonts();
  cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
  cairo_t* cr = cairo_create(surface);
  PangoLayout* layout = makeLayout(cr, text, style);
  PangoRectangle ink, logical;
  pango_layout_get_extents(layout, &ink, &logical);
  cairo_set_tolerance(cr, tolerance);
  cairo_move_to(cr, -static_cast<double>(logical.x) / PANGO_SCALE, -static_cast<double>(logical.y) / PANGO_SCALE);
  pango_cairo_layout_path(cr, layout);
  cairo_path_t* path = cairo_copy_path_flat(cr);
  for (int i = 0; i < path->num_data; i += path->data[i].header.length) {
    const cairo_path_data_t& d = path->data[i];
    if (d.header.type == CAIRO_PATH_MOVE_TO) out.emplace_back();
    if ((d.header.type == CAIRO_PATH_MOVE_TO || d.header.type == CAIRO_PATH_LINE_TO) && !out.empty())
      out.back().emplace_back(static_cast<float>(path->data[i + 1].point.x), static_cast<float>(path->data[i + 1].point.y));
  }
  cairo_path_destroy(path);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  std::erase_if(out, [](const Contour& c) { return c.size() < 2; });
  return out;
}

std::vector<std::uint8_t> TextRenderer::glyphSheet(const std::vector<std::string>& glyphs, const TextStyle& style, int cellW, int cellH,
                                                    int perRow) {
  registerBundledFonts();
  const int rowsN = (static_cast<int>(glyphs.size()) + perRow - 1) / perRow;
  const int W = perRow * cellW, H = std::max(1, rowsN) * cellH;
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, W, H);
  cairo_t* cr = cairo_create(surf);
  cairo_set_source_rgba(cr, 1, 1, 1, 1);
  // the baseline every cell shares: an "M" centred in its cell
  PangoLayout* ref = makeLayout(cr, "M", style);
  PangoRectangle ink, logical;
  pango_layout_get_extents(ref, &ink, &logical);
  const double baseY = (cellH - static_cast<double>(logical.height) / PANGO_SCALE) / 2 +
                       static_cast<double>(pango_layout_get_baseline(ref)) / PANGO_SCALE;
  g_object_unref(ref);
  for (size_t i = 0; i < glyphs.size(); ++i) {
    if (glyphs[i].empty() || glyphs[i] == " ") continue;
    const int cx = static_cast<int>(i % perRow) * cellW, cy = static_cast<int>(i / perRow) * cellH;
    PangoLayout* l = makeLayout(cr, glyphs[i], style);
    pango_layout_get_extents(l, &ink, &logical);
    const double w = static_cast<double>(logical.width) / PANGO_SCALE;
    const double base = static_cast<double>(pango_layout_get_baseline(l)) / PANGO_SCALE;
    cairo_save(cr);
    cairo_rectangle(cr, cx, cy, cellW, cellH);  // a wide fallback glyph keeps to its cell
    cairo_clip(cr);
    cairo_move_to(cr, cx + (cellW - w) / 2 - static_cast<double>(logical.x) / PANGO_SCALE, cy + baseY - base);
    pango_cairo_show_layout(cr, l);
    cairo_restore(cr);
    g_object_unref(l);
  }
  cairo_destroy(cr);
  cairo_surface_flush(surf);
  std::vector<std::uint8_t> out(static_cast<size_t>(W) * H);
  const int stride = cairo_image_surface_get_stride(surf);
  const unsigned char* data = cairo_image_surface_get_data(surf);
  for (int y = 0; y < H; ++y) std::copy_n(data + static_cast<size_t>(y) * stride, W, out.data() + static_cast<size_t>(y) * W);
  cairo_surface_destroy(surf);
  return out;
}

void TextRenderer::draw(const TextImage& img, float x, float y, Color color, float surfaceW, float surfaceH,
                        float opacity) {
  drawEx(img, x, y, color, surfaceW, surfaceH, 1, 1, 0, opacity);
}

void TextRenderer::drawEx(const TextImage& img, float x, float y, Color color, float surfaceW, float surfaceH, float sx,
                          float sy, float blur, float opacity, float angle) {
  if (!img.texture || surfaceW <= 0 || surfaceH <= 0) return;
  if (!m_prog.valid()) m_prog.create(kTextVert, kTextFrag, "text");
  glUseProgram(m_prog.id());
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, img.texture);
  glUniform1i(m_prog.uniform("u_tex"), 0);
  glUniform4f(m_prog.uniform("u_color"), color.r, color.g, color.b, color.a * opacity);
  glUniform2f(m_prog.uniform("u_surface"), surfaceW, surfaceH);
  glUniform2f(m_prog.uniform("u_blur"), blur > 0 ? blur / img.quadW : 0.0F, blur > 0 ? blur / img.quadH : 0.0F);
  // scale about the logical box centre
  const float cx = x + img.w / 2, cy = y + img.h / 2;
  float qx = cx + (x + img.quadX - cx) * sx, qy = cy + (y + img.quadY - cy) * sy;
  glUniform3f(m_prog.uniform("u_turn"), cx, cy, angle);
  if (sx == 1 && sy == 1 && blur <= 0 && angle == 0) {
    // texels on device pixels: a quad between pixels is resampled soft
    qx = std::round(qx * img.scale) / img.scale;
    qy = std::round(qy * img.scale) / img.scale;
  }
  glUniform4f(m_prog.uniform("u_rect"), qx, qy, img.quadW * sx, img.quadH * sy);
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
