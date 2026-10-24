// SPDX-License-Identifier: GPL-3.0-or-later
// A Wallpaper Engine wallpaper drawn by undershell (see scene.hpp).
#include "scene.hpp"

#include "effects.hpp"
#include "offscreen.hpp"
#include "perf.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#ifdef US_HAVE_LZ4
#include <lz4.h>
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>
#include <unistd.h>

#ifndef US_LIBEXEC_DIR
#define US_LIBEXEC_DIR "/usr/libexec"
#endif

namespace undershell {

namespace fs = std::filesystem;

namespace {

const char* kVert = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;  // surface px
layout(location = 1) in vec2 a_uv;
uniform vec2 u_size;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos.x / u_size.x * 2.0 - 1.0, 1.0 - a_pos.y / u_size.y * 2.0, 0.0, 1.0);
}
)";

const char* kFrag = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_tex;
uniform vec4 u_tint;   // rgb (colour x brightness), alpha
uniform int u_solid;   // the clear colour: no texture
uniform int u_premul;  // the texture is premultiplied (a layer through effects)
void main() {
    vec4 t = u_solid == 1 ? vec4(1.0) : texture(u_tex, v_uv);
    if (u_premul == 1) {
        fragColor = vec4(t.rgb * u_tint.rgb, t.a) * u_tint.a;
        return;
    }
    float a = t.a * u_tint.a;
    fragColor = vec4(t.rgb * u_tint.rgb * a, a);
}
)";

// a layer through undershell's effects (effectchain.hpp): its texture
// (straight alpha, rows top down) in, premultiplied out
const char* kFxHead = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_lights;
uniform int u_hasLights;
)";

const char* kFxBody = R"(
void main() {
    screenPx = v_uv * u_screen;
    vec2 uv = v_uv;
//@WARPS
    vec4 t = any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) ? vec4(0.0) : texture(u_tex, uv);
    vec3 c = t.rgb;
//@COLOURS
    if (u_hasLights == 1) {
        vec4 L = texture(u_lights, vec2(v_uv.x, 1.0 - v_uv.y));
        c = c * (1.0 - L.a) + L.rgb;
    }
    fragColor = vec4(clamp(c, 0.0, 1.0) * t.a, t.a);
}
)";

const char* kFxLights = R"(
void main() {
    screenPx = v_uv * u_screen;
    vec2 uv = v_uv;
    vec4 L = vec4(0.0);
//@LIGHTS
    fragColor = L;
}
)";

// a mask texture's red at a point of another picture of size (w, h)
float sampleMask(const SceneWidget::Pixels& m, int x, int y, int w, int h) {
  if (m.w <= 0 || m.h <= 0) return 1;
  const int mx = std::clamp(x * m.w / std::max(1, w), 0, m.w - 1), my = std::clamp(y * m.h / std::max(1, h), 0, m.h - 1);
  return m.rgba[(static_cast<size_t>(my) * m.w + mx) * 4] / 255.0F;
}

bool executable(const std::string& p) { return !p.empty() && access(p.c_str(), X_OK) == 0; }

// runs the helper; its first line, or why it failed
bool runHelper(const std::vector<std::string>& args, std::string& said) {
  std::vector<gchar*> argv;
  for (const auto& a : args) argv.push_back(const_cast<gchar*>(a.c_str()));
  argv.push_back(nullptr);
  gchar *sout = nullptr, *serr = nullptr;
  gint status = 0;
  GError* err = nullptr;
  const bool spawned = g_spawn_sync(nullptr, argv.data(), nullptr, static_cast<GSpawnFlags>(0), nullptr, nullptr, &sout, &serr, &status, &err);
  const bool ok = spawned && g_spawn_check_wait_status(status, nullptr);
  said = ok ? (sout ? sout : "") : (err ? err->message : (serr ? serr : "the helper failed"));
  while (!said.empty() && (said.back() == '\n' || said.back() == ' ')) said.pop_back();
  g_free(sout);
  g_free(serr);
  if (err) g_error_free(err);
  return ok;
}

// the scenes unpacked here: the newest 12 stay
void prune(const fs::path& root, const fs::path& keep) {
  std::error_code ec;
  std::vector<std::pair<fs::file_time_type, fs::path>> dirs;
  for (const auto& e : fs::directory_iterator(root, ec))
    if (e.is_directory(ec) && e.path() != keep && e.path().filename() != "assets") dirs.emplace_back(e.last_write_time(ec), e.path());
  std::ranges::sort(dirs, [](auto& a, auto& b) { return a.first > b.first; });
  for (size_t i = 11; i < dirs.size(); ++i) fs::remove_all(dirs[i].second, ec);
}

}  // namespace

std::string SceneWidget::cacheDir() {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  return std::string(xdg && *xdg ? xdg : expandHome("~/.cache")) + "/undershell/scenes";
}

std::string SceneWidget::helperPath() {
  if (const char* env = std::getenv("UNDERSHELL_SCENE_HELPER"); env && *env) return executable(env) ? env : std::string();
  std::error_code ec;
  const fs::path self = fs::read_symlink("/proc/self/exe", ec);
  if (!ec && executable((self.parent_path() / "undershell-scene").string())) return (self.parent_path() / "undershell-scene").string();
  const std::string installed = std::string(US_LIBEXEC_DIR) + "/undershell-scene";
  return executable(installed) ? installed : std::string();
}

bool SceneWidget::loadTexture(const std::string& base, Pixels& out) {
  std::error_code ec;
  if (const std::string info = readFile(base + ".info"); !info.empty()) {  // "w h imageW imageH format"
    int v[5] = {};
    if (std::sscanf(info.c_str(), "%d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5) out.format = v[4];
  }
#ifdef US_HAVE_LZ4
  if (fs::exists(base + ".usimg", ec)) {
    const std::string d = readFile(base + ".usimg");
    if (d.size() < 24 || d.compare(0, 8, "USIMG001") != 0) return false;
    auto at = [&](size_t i) {
      uint32_t v = 0;
      for (int k = 0; k < 4; ++k) v |= static_cast<uint32_t>(static_cast<unsigned char>(d[i + k])) << (8 * k);
      return v;
    };
    const uint32_t w = at(8), h = at(12), raw = at(16), packed = at(20);
    if (w == 0 || h == 0 || w > 16384 || h > 16384 || raw != w * h * 4 || packed > d.size() - 24) return false;
    out.w = static_cast<int>(w), out.h = static_cast<int>(h);
    out.rgba.resize(raw);
    return LZ4_decompress_safe(d.data() + 24, reinterpret_cast<char*>(out.rgba.data()), static_cast<int>(packed), static_cast<int>(raw)) ==
           static_cast<int>(raw);
  }
#endif
  for (const char* ext : {".png", ".jpg"}) {
    if (!fs::exists(base + ext, ec)) continue;
    // from memory: the loaders refuse long file names, and Workshop names are long
    const std::string data = readFile(base + ext);
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    const bool fed = !data.empty() && gdk_pixbuf_loader_write(loader, reinterpret_cast<const guchar*>(data.data()), data.size(), nullptr);
    const bool closed = gdk_pixbuf_loader_close(loader, nullptr);
    GdkPixbuf* pb = fed && closed ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
    if (pb) g_object_ref(pb);
    g_object_unref(loader);
    if (!pb) return false;
    GdkPixbuf* a = gdk_pixbuf_add_alpha(pb, FALSE, 0, 0, 0);
    g_object_unref(pb);
    if (!a) return false;
    out.w = gdk_pixbuf_get_width(a), out.h = gdk_pixbuf_get_height(a);
    const int stride = gdk_pixbuf_get_rowstride(a);
    const guchar* px = gdk_pixbuf_get_pixels(a);
    out.rgba.resize(static_cast<size_t>(out.w) * out.h * 4);
    for (int y = 0; y < out.h; ++y) std::copy_n(px + static_cast<size_t>(y) * stride, out.w * 4, out.rgba.data() + static_cast<size_t>(y) * out.w * 4);
    g_object_unref(a);
    return true;
  }
  return false;
}

SceneWidget::~SceneWidget() {
  for (auto& [k, t] : m_tex) glDeleteTextures(1, &t.first);
}

SceneWidget::LayerFx::~LayerFx() {
  if (tex) glDeleteTextures(1, &tex);
  if (fbo) glDeleteFramebuffers(1, &fbo);
}

void SceneWidget::filesChanged() {
  effectLibrary().refresh();
}

void SceneWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  m_noct = noct;
  m_speed = std::clamp(cfg.options["speed"].value_or(1.0), 0.1, 4.0);
  const std::string scene = cfg.options["scene"].value_or(std::string());
  // the author's properties as set in the inspector (colours through the palette)
  std::map<std::string, std::string> overrides;
  if (const WeProject* p = scene.empty() ? nullptr : weProjectCached(scene))
    for (const WeProperty& wp : p->props) {
      std::string v = weOverride(wp, cfg.options);
      if (wp.type == "color" && !v.empty() && v.find(' ') == std::string::npos) {
        const Color c = noct.color(v, Color{1, 1, 1, 1});
        v = std::format("{:.4f} {:.4f} {:.4f}", c.r, c.g, c.b);
      }
      if (!v.empty()) overrides[wp.name] = v;
    }
  if (scene == m_scene) {
    if (overrides != m_overrides) {  // the same scene with other properties: read again, swap when ready
      m_overrides = std::move(overrides);
      start(true);
    }
    return;
  }
  m_scene = scene;
  m_overrides = std::move(overrides);
  for (auto& [k, t] : m_tex) glDeleteTextures(1, &t.first);
  m_tex.clear();
  m_fx.clear();
  m_pt.configureDefs({});
  m_ready = false;
  m_stillTried = false;
  m_stillPath.clear();
  m_still = std::make_shared<std::atomic<bool>>(false);
  m_video.close();
  m_model = {};
  start();
}

// off the frame: find it, unpack it, read it, decode its textures
void SceneWidget::start(bool keepShowing) {
  auto load = std::make_shared<Load>();
  if (keepShowing && m_ready) m_next = load;
  else m_load = load;
  const std::string id = m_scene;
  std::thread([load, id, overrides = m_overrides] {
    auto fail = [&](std::string why) {
      std::lock_guard g(load->m);
      load->error = std::move(why);
      load->done = true;
    };
    // a stranger's scene can hold anything: whatever it makes throw is its error, not undershell's
    try {
      loadScene(load, id, overrides, fail);
    } catch (const std::exception& ex) {
      fail(std::string("could not be read: ") + ex.what());
    }
  }).detach();
}

void SceneWidget::loadScene(const std::shared_ptr<Load>& load, const std::string& id, const std::map<std::string, std::string>& overrides,
                            const std::function<void(std::string)>& fail) {
  {
    if (id.empty()) return fail("no scene chosen");
    const std::string dir = weWallpaperDir(id);
    if (dir.empty()) return fail("not found: " + id);
    WeProject project;
    std::string why;
    if (!readWeProject(dir, project, why)) return fail(why);
    for (const auto& [name, value] : overrides) project.properties[name] = value;
    {
      std::lock_guard g(load->m);
      load->project = project;
    }
    if (project.type == "video") {
      std::lock_guard g(load->m);
      load->done = true;
      return;
    }
    if (project.type != "scene") return fail("a " + (project.type.empty() ? std::string("kind of") : project.type) + " wallpaper is not supported");
#ifndef US_HAVE_LZ4
    return fail("this undershell was built without scene support (liblz4)");
#else
    const std::string helper = helperPath();
    if (helper.empty()) return fail("undershell-scene is not installed");
    std::error_code ec;
    const fs::path src = fs::path(dir) / (fs::exists(fs::path(dir) / "scene.pkg", ec) ? "scene.pkg" : "project.json");
    const auto stamp = fs::last_write_time(src, ec).time_since_epoch().count();
    const fs::path out = fs::path(cacheDir()) / std::format("{}-{:x}", fs::path(dir).filename().string(), static_cast<uint64_t>(stamp));
    const fs::path assets = fs::path(cacheDir()) / "assets";
    std::string said;
    if (!fs::exists(out / "manifest.txt", ec)) {
      if (!runHelper({helper, "unpack", dir, out.string()}, said)) return fail("could not unpack it: " + said);
      prune(cacheDir(), out);
      US_INFO("scene {}: unpacked ({})", id, said);
    } else {
      fs::last_write_time(out, fs::file_time_type::clock::now(), ec);  // recently used
    }
    // the files it needs from Wallpaper Engine's assets, unpacked the same way
    const std::string weAssets = weAssetsDir();
    WeScene scene;
    for (int round = 0; round < 3; ++round) {
      if (!readWeScene(out.string(), assets.string(), project, scene, why)) return fail(why);
      if (scene.needs.empty() || weAssets.empty()) break;
      std::vector<std::string> args = {helper, "assets", weAssets, assets.string()};
      args.insert(args.end(), scene.needs.begin(), scene.needs.end());
      runHelper(args, said);
    }
    // decoded once each, wherever several layers share one
    std::map<std::string, Pixels> decoded;
    auto texture = [&](const std::string& rel) -> const Pixels* {
      if (rel.empty()) return nullptr;
      if (auto it = decoded.find(rel); it != decoded.end()) return it->second.w > 0 ? &it->second : nullptr;
      Pixels p;
      if (!loadTexture((out / (rel + ".tex")).string(), p) && !loadTexture((assets / (rel + ".tex")).string(), p)) {
        US_WARN("scene {}: texture {} could not be read", id, rel);
        p = {};
      }
      auto& slot = decoded[rel] = std::move(p);
      return slot.w > 0 ? &slot : nullptr;
    };
    std::map<std::string, Pixels> pixels;
    const size_t n = scene.layers.size();
    std::vector<std::string> layerTex(n), layerMasks(n);
    std::vector<toml::table> layerFx(n);
    for (size_t i = 0; i < n; ++i) {
      const WeLayer& l = scene.layers[i];
      if (l.texture.empty() || l.compose) continue;
      const Pixels* base = texture(l.texture);
      if (!base) continue;
      const int w = base->w, h = base->h;
      // what never moves (Wallpaper Engine's opacity and tint) baked in once
      if (l.opacity < 1 || !l.opacityMask.empty() || l.tintAmount > 0) {
        Pixels baked = *base;
        const Pixels* om = texture(l.opacityMask);
        const Pixels* tm = texture(l.tintMask);
        for (int y = 0; y < h; ++y)
          for (int x = 0; x < w; ++x) {
            uint8_t* px = &baked.rgba[(static_cast<size_t>(y) * w + x) * 4];
            const float a = l.opacity * (om ? sampleMask(*om, x, y, w, h) : 1);
            px[3] = static_cast<uint8_t>(px[3] * a);
            if (l.tintAmount > 0) {
              const float k = l.tintAmount * (tm ? sampleMask(*tm, x, y, w, h) : 1);
              const float tc[3] = {l.tint.r, l.tint.g, l.tint.b};
              for (int c = 0; c < 3; ++c) px[c] = static_cast<uint8_t>(std::lround(px[c] + (tc[c] * 255 - px[c]) * k));
            }
          }
        layerTex[i] = "layer:" + std::to_string(i);
        pixels[layerTex[i]] = std::move(baked);
      } else {
        layerTex[i] = l.texture;
        if (!pixels.contains(l.texture)) pixels[l.texture] = *base;
      }
      if (l.fx.empty()) continue;
      // what moves: undershell's effects, each limited by its Wallpaper
      // Engine mask, packed four to a texture (paint1..paint4)
      Pixels masks;
      masks.w = w, masks.h = h;
      masks.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
      toml::table opts;
      for (size_t k = 0; k < l.fx.size(); ++k) {
        toml::table fx = l.fx[k].options;
        const Pixels* m = k < 4 ? texture(l.fx[k].mask) : nullptr;
        if (m) {
          double dx = 0, dy = 0;
          for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
              float v = sampleMask(*m, x, y, w, h);
              if (l.fx[k].flowMap) {  // red and green: the direction, 0.5 still; how far from still: how much
                const int mx = std::clamp(x * m->w / w, 0, m->w - 1), my = std::clamp(y * m->h / h, 0, m->h - 1);
                const uint8_t* fp = &m->rgba[(static_cast<size_t>(my) * m->w + mx) * 4];
                const float fx2 = fp[0] / 255.0F - 0.5F, fy2 = fp[1] / 255.0F - 0.5F;
                v = std::min(1.0F, std::hypot(fx2, fy2) * 2.5F);
                dx += fx2, dy += fy2;
              }
              masks.rgba[(static_cast<size_t>(y) * w + x) * 4 + k] = static_cast<uint8_t>(v * 255);
            }
          if (l.fx[k].flowMap && (dx != 0 || dy != 0))
            for (auto&& [key, val] : fx)
              if (key.str().starts_with("fx_flow")) {
                fx.insert_or_assign("fx_flow_direction", std::atan2(dy, dx) * 57.29578);
                break;
              }
          for (auto&& [key, val] : fx)
            if (key.str().find('_', 3) == std::string::npos) {  // the switch itself: fx_<id>
              fx.insert_or_assign(std::string(key.str()) + "_mask", "paint" + std::to_string(k + 1));
              break;
            }
        }
        for (auto&& [key, val] : fx) opts.insert_or_assign(key, val);
      }
      layerFx[i] = std::move(opts);
      layerMasks[i] = "mask:" + std::to_string(i);
      pixels[layerMasks[i]] = std::move(masks);
    }
    // the particles' pictures, and their sprite sheets' grids from the real size
    for (ParticleDef& d : scene.particles) {
      const Pixels* p = texture(d.texture);
      if (!p) continue;
      if (!pixels.contains(d.texture)) pixels[d.texture] = *p;
      if (d.cellW > 0 && d.cellH > 0) {
        d.cols = std::max(1, p->w / d.cellW);
        d.rows = std::max(1, p->h / d.cellH);
        d.frames = std::min(d.frames, d.cols * d.rows);
      }
    }
    std::lock_guard g(load->m);
    load->out = out.string();
    load->scene = std::move(scene);
    load->pixels = std::move(pixels);
    load->layerTex = std::move(layerTex);
    load->layerMasks = std::move(layerMasks);
    load->layerFx = std::move(layerFx);
    load->done = true;
#endif
  }
}

void SceneWidget::tick(const TickContext& ctx) {
  m_dt = m_lastTick < 0 ? 0 : std::clamp(ctx.now - m_lastTick, 0.0, 0.25);
  m_lastTick = ctx.now;
  m_moving = m_audio = false;
  if (m_ready && m_pt.systems() > 0) {
    m_pt.tick(ctx);
    m_moving = true;
  }
  for (auto& [i, f] : m_fx) {
    f->chain.tick(ctx);
    m_moving |= f->chain.moving();
    m_audio |= f->chain.audio();
  }
}

bool SceneWidget::animating(const TickContext&) const { return m_ready && ((m_video.isOpen() && !m_video.still()) || m_moving); }
// (a reload beside it is looked at by nextWakeup)

double SceneWidget::nextWakeup(double now) const {
  if (m_uploading) return now + 0.016;  // the next texture, next frame
  if (m_next) return now + 0.05;        // a reload: swap it in when done
  if (m_ready && m_video.isOpen()) return now + std::max(0.004, m_video.nextFrameIn(m_speed));
  return m_ready || !m_load ? 1e18 : now + 0.1;  // loading: look again soon
}

std::string SceneWidget::state() const {
  if (!m_load) return "nothing chosen";
  std::lock_guard g(m_load->m);
  if (!m_load->error.empty()) return "error: " + m_load->error;
  if (!m_load->done) return "loading";
  if (m_project.type == "video") return "video " + m_project.title;
  return std::format("scene {} ({} layers, {} particle systems, {} not drawn yet; effects {} of {} drawn)", m_project.title, m_model.layers.size(),
                     m_model.particles.size(), m_model.skipped, m_model.mapped, m_model.effects);
}

void SceneWidget::drawCover(GLuint tex, float texW, float texH, const DrawContext& ctx, float alpha) {
  if (!tex || texW <= 0 || texH <= 0) return;
  const float s = std::max(ctx.w / texW, ctx.h / texH), w = texW * s, h = texH * s;
  const float x0 = (ctx.w - w) / 2, y0 = (ctx.h - h) / 2;
  const std::array<float, 12> pos = {x0, y0, x0 + w, y0, x0, y0 + h, x0, y0 + h, x0 + w, y0, x0 + w, y0 + h};
  static constexpr std::array<float, 12> kUv = {0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1};
  glUseProgram(m_prog.id());
  glUniform2f(m_prog.uniform("u_size"), ctx.w, ctx.h);
  glUniform4f(m_prog.uniform("u_tint"), 1, 1, 1, alpha);
  glUniform1i(m_prog.uniform("u_solid"), 0);
  glUniform1i(m_prog.uniform("u_tex"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos.data());
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, kUv.data());
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(1);
}

void SceneWidget::draw(const DrawContext& ctx) {
  if (!m_prog.valid()) m_prog.create(kVert, kFrag, "scene");
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  // a reload beside it (other properties) finished: swap, all at once
  if (m_next) {
    bool done = false;
    {
      std::lock_guard g(m_next->m);
      done = m_next->done;
    }
    if (done) {
      for (auto& [k, t] : m_tex) glDeleteTextures(1, &t.first);
      m_tex.clear();
      m_formats.clear();
      m_fx.clear();
      m_load = std::move(m_next);
      m_next.reset();
      m_ready = false;
      m_uploadAll = true;
      m_stillTried = false;
    }
  }
  // what the loader has finished
  if (!m_ready && m_load) {
    std::lock_guard g(m_load->m);
    if (m_load->done) {
      m_project = m_load->project;
      if (m_load->error.empty()) {
        m_model = std::move(m_load->scene);
        m_layerTex = m_load->layerTex;
        m_layerMasks = m_load->layerMasks;
        for (size_t i = 0; i < m_load->layerFx.size(); ++i) {
          if (m_load->layerFx[i].empty()) continue;
          auto f = std::make_unique<LayerFx>();
          f->chain.configure(m_load->layerFx[i], m_noct);
          if (!f->chain.empty()) m_fx[i] = std::move(f);
        }
        m_pt.configureDefs(m_model.particles);
        m_stillPath = m_load->out.empty() ? std::string() : m_load->out + "/still.png";
        m_pt.setTextures([this](const std::string& key) -> std::pair<GLuint, bool> {
          const auto t = m_tex.find(key);
          if (t == m_tex.end()) return {0, false};
          const auto f = m_formats.find(key);
          return {t->second.first, f != m_formats.end() && f->second == 9};
        });
        m_ready = true;
      } else {
        US_WARN("scene {}: {}", m_scene, m_load->error);
        m_ready = true;
      }
    } else if (m_project.preview.empty()) {
      m_project.preview = m_load->project.preview;
    }
  }
  // a video: its frames, covering the screen
  if (m_ready && m_project.type == "video") {
    const int bw = static_cast<int>(std::lround(ctx.w * ctx.scale)), bh = static_cast<int>(std::lround(ctx.h * ctx.scale));
    if (!m_video.isOpen() || m_video.path() != m_project.file) m_video.open(m_project.file, bw, bh, true);
    m_video.advance(m_dt, m_speed);
    m_dt = 0;
    if (m_video.texture()) {
      drawCover(m_video.texture(), static_cast<float>(m_video.width()), static_cast<float>(m_video.height()), ctx, 1);
      glDisable(GL_BLEND);
      return;
    }
  }
  // a scene still loading (or one that failed): its preview stands in
  const bool sceneReady = m_ready && m_project.type == "scene" && m_load && m_load->error.empty();
  if (!sceneReady) {
    if (!m_project.preview.empty()) {
      const int maxSide = static_cast<int>(std::ceil(std::max(ctx.w, ctx.h) * std::max(1.0F, ctx.scale)));
      if (m_preview.update(m_project.preview, maxSide, ctx.now))
        if (const WallPic* p = m_preview.current())
          drawCover(p->tex, static_cast<float>(p->imgW), static_cast<float>(p->imgH), ctx, 1);
    }
    glDisable(GL_BLEND);
    return;
  }
  // one texture uploaded per frame, so loading never stalls one
  {
    std::lock_guard g(m_load->m);
    while (!m_load->pixels.empty()) {
      auto it = m_load->pixels.begin();
      GLuint tex = 0;
      glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_2D, tex);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, it->second.w, it->second.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, it->second.rgba.data());
      glGenerateMipmap(GL_TEXTURE_2D);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      m_tex[it->first] = {tex, {it->second.w, it->second.h}};
      m_formats[it->first] = it->second.format;
      m_load->pixels.erase(it);
      if (!m_uploadAll) break;  // one per frame, unless swapping in a reload
    }
    m_uploadAll = false;
    m_uploading = !m_load->pixels.empty();
  }
  drawLayers(ctx, true);
  captureStill(ctx);
}

// the scene itself: clear colour, layers (some through effects) and,
// unless asked not to, its particles among them
void SceneWidget::drawLayers(const DrawContext& ctx, bool particles) {
  glEnable(GL_BLEND);
  // the scene's projection over the screen, filling it (cropped like a wallpaper)
  const float s = std::max(ctx.w / m_model.width, ctx.h / m_model.height);
  auto toSurface = [&](float x, float y) {
    return std::array<float, 2>{(x - m_model.width / 2) * s + ctx.w / 2, (m_model.height / 2 - y) * s + ctx.h / 2};
  };
  glUseProgram(m_prog.id());
  glUniform2f(m_prog.uniform("u_size"), ctx.w, ctx.h);
  glUniform1i(m_prog.uniform("u_tex"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  static constexpr std::array<float, 12> kUv = {0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1};
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, kUv.data());
  // the clear colour first
  {
    const std::array<float, 12> full = {0, 0, ctx.w, 0, 0, ctx.h, 0, ctx.h, ctx.w, 0, ctx.w, ctx.h};
    glUniform1i(m_prog.uniform("u_solid"), 1);
    glUniform4f(m_prog.uniform("u_tint"), m_model.clear.r, m_model.clear.g, m_model.clear.b, 1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, full.data());
    glDrawArrays(GL_TRIANGLES, 0, 6);
  }
  glUniform1i(m_prog.uniform("u_solid"), 0);
  glUniform1i(m_prog.uniform("u_premul"), 0);
  static constexpr std::array<float, 12> kUvFlipped = {0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0};
  // the particles' units (a 1080-high scene, y down) onto the surface
  m_pt.setPlacement(s * m_model.height / 1080, ctx.w / 2 - m_model.width / 2 * s, ctx.h / 2 - m_model.height / 2 * s,
                    m_model.width / m_model.height);
  auto sceneState = [&] {  // the scene's own drawing again, after a pass of something else
    glEnable(GL_BLEND);
    glUseProgram(m_prog.id());
    glUniform2f(m_prog.uniform("u_size"), ctx.w, ctx.h);
    glUniform1i(m_prog.uniform("u_tex"), 0);
    glUniform1i(m_prog.uniform("u_solid"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
  };
  for (size_t li = 0; li < m_model.layers.size(); ++li) {
    const WeLayer& l = m_model.layers[li];
    if (l.particle >= 0) {  // a particle system, where its object stands among the layers
      if (particles) {
        m_pt.draw(ctx, 0, 0, l.particle);
        sceneState();
      }
      continue;
    }
    if (!l.visible || l.compose || li >= m_layerTex.size() || m_layerTex[li].empty() || l.alpha <= 0) continue;
    const auto t = m_tex.find(m_layerTex[li]);
    if (t == m_tex.end()) continue;
    GLuint tex = t->second.first;
    bool premul = false;
    // through undershell's effects first, into the layer's own texture
    if (auto f = m_fx.find(li); f != m_fx.end()) {
      LayerFx& L = *f->second;
      const int tw = t->second.second[0], th = t->second.second[1];
      // at the size it shows on screen (device px), never above its own: a
      // 4K layer shown on 1366 px needs no more, and the cost goes with the area
      const auto q0 = weLayerQuad(m_model, l, static_cast<float>(tw), static_cast<float>(th));
      const auto qa = toSurface(q0[0], q0[1]), qb = toSurface(q0[2], q0[3]), qc = toSurface(q0[4], q0[5]);
      // (on the lower performance levels, at their render scale: waves and glows are soft)
      const float level = std::clamp(perfKnobs().renderScale, 0.25F, 1.0F);
      const float shownW = std::hypot(qb[0] - qa[0], qb[1] - qa[1]) * ctx.scale * level;
      const float shownH = std::hypot(qc[0] - qa[0], qc[1] - qa[1]) * ctx.scale * level;
      const int fw = std::clamp(static_cast<int>(std::ceil(std::min(shownW, static_cast<float>(tw)))), 1, 4096);
      const int fh = std::clamp(static_cast<int>(std::ceil(std::min(shownH, static_cast<float>(th)))), 1, 4096);
      // where the scene draws, kept before anything else is bound
      GLint fbo = 0, vp[4] = {};
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
      glGetIntegerv(GL_VIEWPORT, vp);
      if (!L.fbo || L.w != fw || L.h != fh) {
        if (!L.tex) glGenTextures(1, &L.tex);
        glBindTexture(GL_TEXTURE_2D, L.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (!L.fbo) glGenFramebuffers(1, &L.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, L.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, L.tex, 0);
        L.w = fw, L.h = fh;
      }
      glBindFramebuffer(GL_FRAMEBUFFER, L.fbo);
      glViewport(0, 0, fw, fh);
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      DrawContext lc;
      lc.w = static_cast<float>(fw), lc.h = static_cast<float>(fh), lc.now = ctx.now, lc.fillMode = 1;
      if (li < m_layerMasks.size())
        if (auto mt = m_tex.find(m_layerMasks[li]); mt != m_tex.end()) lc.effectMasks = mt->second.first;
      L.chain.build(kFxHead, "", kFxBody, kFxLights);
      auto picture = [&](const Program& p) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, t->second.first);
        glUniform1f(p.uniform("u_aspect"), static_cast<float>(tw) / static_cast<float>(th));
      };
      L.chain.drawLights(lc, false, picture);
      L.chain.use(0, lc, false);
      glUniform2f(L.chain.program().uniform("u_screen"), static_cast<float>(fw), static_cast<float>(fh));
      picture(L.chain.program());
      glDisable(GL_BLEND);
      drawUnitQuad();
      glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
      glViewport(vp[0], vp[1], vp[2], vp[3]);
      sceneState();
      tex = L.tex;
      premul = true;
    }
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, premul ? kUvFlipped.data() : kUv.data());
    glUniform1i(m_prog.uniform("u_premul"), premul ? 1 : 0);
    const auto q = weLayerQuad(m_model, l, static_cast<float>(t->second.second[0]), static_cast<float>(t->second.second[1]));
    const auto a = toSurface(q[0], q[1]), b = toSurface(q[2], q[3]), c = toSurface(q[4], q[5]), d = toSurface(q[6], q[7]);
    const std::array<float, 12> pos = {a[0], a[1], b[0], b[1], c[0], c[1], c[0], c[1], b[0], b[1], d[0], d[1]};
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform4f(m_prog.uniform("u_tint"), l.color.r * l.brightness, l.color.g * l.brightness, l.color.b * l.brightness, l.alpha);
    if (l.additive) glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
    else glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos.data());
    glDrawArrays(GL_TRIANGLES, 0, 6);
  }
  glDisableVertexAttribArray(1);
  glDisable(GL_BLEND);
}

// Once loaded, a still of the scene (no particles: the same every time)
// kept beside its unpacked files: it stands for the output's wallpaper
// (depth, layouts per wallpaper, colours).
void SceneWidget::captureStill(const DrawContext& ctx) {
  if (m_stillTried || m_uploading || m_stillPath.empty()) return;
  {
    std::lock_guard g(m_load->m);
    if (!m_load->pixels.empty()) return;
  }
  m_stillTried = true;
  std::error_code ec;
  if (fs::exists(m_stillPath, ec)) {
    m_still->store(true);
    return;
  }
  const int w = std::clamp(static_cast<int>(std::lround(ctx.w * ctx.scale)), 16, 4096);
  const int h = std::clamp(static_cast<int>(std::lround(ctx.h * ctx.scale)), 16, 4096);
  GLint fbo = 0, vp[4] = {};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
  glGetIntegerv(GL_VIEWPORT, vp);
  GLuint tex = 0, target = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glGenFramebuffers(1, &target);
  glBindFramebuffer(GL_FRAMEBUFFER, target);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  glViewport(0, 0, w, h);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  drawLayers(ctx, false);
  auto img = std::make_shared<Image>();
  img->w = w, img->h = h;
  std::vector<uint8_t> rows(static_cast<size_t>(w) * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
  img->rgba.resize(rows.size());
  for (int y = 0; y < h; ++y)  // rows bottom up -> top down, made opaque
    for (int x = 0; x < w * 4; ++x)
      img->rgba[static_cast<size_t>(y) * w * 4 + x] = (x % 4 == 3) ? 255 : rows[static_cast<size_t>(h - 1 - y) * w * 4 + x];
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glDeleteFramebuffers(1, &target);
  glDeleteTextures(1, &tex);
  // written off the frame, then announced
  std::thread([img, path = m_stillPath, done = m_still] {
    if (writePng(*img, path + ".part", false)) {
      std::error_code e;
      fs::rename(path + ".part", path, e);
      if (!e) done->store(true);
    }
  }).detach();
}

std::string SceneWidget::stillWallpaper() const { return m_still->load() ? m_stillPath : std::string(); }

}  // namespace undershell
