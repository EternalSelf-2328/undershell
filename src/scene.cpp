// SPDX-License-Identifier: GPL-3.0-or-later
// A Wallpaper Engine wallpaper drawn by undershell (see scene.hpp).
#include "scene.hpp"

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
void main() {
    vec4 t = u_solid == 1 ? vec4(1.0) : texture(u_tex, v_uv);
    float a = t.a * u_tint.a;
    fragColor = vec4(t.rgb * u_tint.rgb * a, a);
}
)";

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

void SceneWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  (void)noct;
  m_speed = std::clamp(cfg.options["speed"].value_or(1.0), 0.1, 4.0);
  const std::string scene = cfg.options["scene"].value_or(std::string());
  if (scene == m_scene) return;
  m_scene = scene;
  for (auto& [k, t] : m_tex) glDeleteTextures(1, &t.first);
  m_tex.clear();
  m_ready = false;
  m_video.close();
  m_model = {};
  start();
}

// off the frame: find it, unpack it, read it, decode its textures
void SceneWidget::start() {
  auto load = std::make_shared<Load>();
  m_load = load;
  const std::string id = m_scene;
  std::thread([load, id] {
    auto fail = [&](std::string why) {
      std::lock_guard g(load->m);
      load->error = std::move(why);
      load->done = true;
    };
    if (id.empty()) return fail("no scene chosen");
    const std::string dir = weWallpaperDir(id);
    if (dir.empty()) return fail("not found: " + id);
    WeProject project;
    std::string why;
    if (!readWeProject(dir, project, why)) return fail(why);
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
    std::map<std::string, Pixels> pixels;
    for (const WeLayer& l : scene.layers) {
      if (l.texture.empty() || pixels.contains(l.texture)) continue;
      Pixels p;
      if (loadTexture((out / (l.texture + ".tex")).string(), p) || loadTexture((assets / (l.texture + ".tex")).string(), p))
        pixels[l.texture] = std::move(p);
      else
        US_WARN("scene {}: texture {} could not be read", id, l.texture);
    }
    std::lock_guard g(load->m);
    load->scene = std::move(scene);
    load->pixels = std::move(pixels);
    load->done = true;
#endif
  }).detach();
}

void SceneWidget::tick(const TickContext& ctx) {
  m_dt = m_lastTick < 0 ? 0 : std::clamp(ctx.now - m_lastTick, 0.0, 0.25);
  m_lastTick = ctx.now;
}

bool SceneWidget::animating(const TickContext&) const { return m_ready && m_video.isOpen() && !m_video.still(); }

double SceneWidget::nextWakeup(double now) const {
  if (m_uploading) return now + 0.016;  // the next texture, next frame
  if (m_ready && m_video.isOpen()) return now + std::max(0.004, m_video.nextFrameIn(m_speed));
  return m_ready || !m_load ? 1e18 : now + 0.1;  // loading: look again soon
}

std::string SceneWidget::state() const {
  if (!m_load) return "nothing chosen";
  std::lock_guard g(m_load->m);
  if (!m_load->error.empty()) return "error: " + m_load->error;
  if (!m_load->done) return "loading";
  if (m_project.type == "video") return "video " + m_project.title;
  return std::format("scene {} ({} layers, {} not drawn yet)", m_project.title, m_model.layers.size(), m_model.skipped);
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
  // what the loader has finished
  if (!m_ready && m_load) {
    std::lock_guard g(m_load->m);
    if (m_load->done) {
      m_project = m_load->project;
      if (m_load->error.empty()) {
        m_model = std::move(m_load->scene);
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
    if (!m_load->pixels.empty()) {
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
      m_load->pixels.erase(it);
    }
    m_uploading = !m_load->pixels.empty();
  }
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
  for (const WeLayer& l : m_model.layers) {
    if (!l.visible || l.compose || l.texture.empty() || l.alpha <= 0) continue;
    const auto t = m_tex.find(l.texture);
    if (t == m_tex.end()) continue;
    const auto q = weLayerQuad(m_model, l, static_cast<float>(t->second.second[0]), static_cast<float>(t->second.second[1]));
    const auto a = toSurface(q[0], q[1]), b = toSurface(q[2], q[3]), c = toSurface(q[4], q[5]), d = toSurface(q[6], q[7]);
    const std::array<float, 12> pos = {a[0], a[1], b[0], b[1], c[0], c[1], c[0], c[1], b[0], b[1], d[0], d[1]};
    glBindTexture(GL_TEXTURE_2D, t->second.first);
    glUniform4f(m_prog.uniform("u_tint"), l.color.r * l.brightness, l.color.g * l.brightness, l.color.b * l.brightness, l.alpha);
    if (l.additive) glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
    else glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos.data());
    glDrawArrays(GL_TRIANGLES, 0, 6);
  }
  glDisableVertexAttribArray(1);
  glDisable(GL_BLEND);
}

}  // namespace undershell
