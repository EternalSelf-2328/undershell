// SPDX-License-Identifier: GPL-3.0-or-later
// Particles over the wallpaper (see particles.hpp). The built-in systems
// (src/pt/*.inc) were written for undershell.
#include "particles.hpp"

#include "config.hpp"
#include "effects.hpp"
#include "perf.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <regex>

namespace undershell {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr float kTau = 6.2831853F;
constexpr size_t kMaxCap = 6000;  // per system, whatever the settings say
constexpr int kFloats = 12;       // per particle handed to the GPU

struct Builtin {
  const char* id;
  const char* text;
};
const Builtin kBuiltins[] = {
    {"snow",
#include "pt/snow.inc"
    },
    {"rain",
#include "pt/rain.inc"
    },
    {"petals",
#include "pt/petals.inc"
    },
    {"fireflies",
#include "pt/fireflies.inc"
    },
    {"dust",
#include "pt/dust.inc"
    },
    {"embers",
#include "pt/embers.inc"
    },
    {"bubbles",
#include "pt/bubbles.inc"
    },
    {"sparks",
#include "pt/sparks.inc"
    },
};

uint64_t fnv(const std::string& s) {
  uint64_t h = 1469598103934665603ULL;
  for (const unsigned char c : s) h = (h ^ c) * 1099511628211ULL;
  return h;
}

std::pair<std::string, std::string> labels(const json& j, const std::string& fallback) {
  if (j.is_string()) return {j.get<std::string>(), j.get<std::string>()};
  std::string en = fallback, es;
  if (j.is_object()) {
    if (j.contains("en") && j["en"].is_string()) en = j["en"].get<std::string>();
    if (j.contains("es") && j["es"].is_string()) es = j["es"].get<std::string>();
  }
  if (en.size() > 40) en.resize(40);
  if (es.size() > 40) es.resize(40);
  return {en, es.empty() ? en : es};
}

// [lo, hi] from a pair or a single number, clamped
std::array<float, 2> span(const json& j, std::array<float, 2> def, float lo, float hi) {
  std::array<float, 2> r = def;
  if (j.is_number()) r = {j.get<float>(), j.get<float>()};
  else if (j.is_array() && j.size() == 2 && j[0].is_number() && j[1].is_number()) r = {j[0].get<float>(), j[1].get<float>()};
  for (float& v : r) v = std::clamp(v, lo, hi);
  return r;
}

float number(const json& j, const char* key, float def, float lo, float hi) {
  return j.is_object() && j.contains(key) && j[key].is_number() ? std::clamp(j[key].get<float>(), lo, hi) : def;
}

float fadeAt(float t, float in, float out) {
  auto ss = [](float a, float b, float x) {
    const float k = std::clamp((x - a) / (b - a), 0.0F, 1.0F);
    return k * k * (3 - 2 * k);
  };
  float a = 1;
  if (in > 0) a *= ss(0, in, t);
  if (out > 0) a *= 1 - ss(1 - out, 1, t);
  return a;
}

}  // namespace

ParticleDef parseParticles(const std::string& id, const std::string& text) {
  ParticleDef d;
  d.id = id;
  d.nameEn = d.nameEs = id;
  d.hash = fnv(text);
  static const std::regex kId("^[a-z0-9_]{1,24}$");
  if (!std::regex_match(id, kId)) {
    d.error = "the file name must be a-z, 0-9 or _ (up to 24)";
    return d;
  }
  const json j = json::parse(text, nullptr, false, true);  // comments allowed
  if (j.is_discarded() || !j.is_object()) {
    d.error = "not valid JSON";
    return d;
  }
  std::tie(d.nameEn, d.nameEs) = labels(j.value("name", json()), id);
  d.category = j.value("category", std::string());
  d.max = static_cast<int>(number(j, "max", 200, 1, 5000));
  d.rate = number(j, "rate", 20, 0, 5000);
  d.prewarm = number(j, "prewarm", 0, 0, 30);
  d.streak = number(j, "streak", 0, 0, 0.5F);
  d.react = number(j, "react", 0, 0, 1);
  d.additive = j.value("blend", std::string("alpha")) == "add";
  static const std::map<std::string, ParticleDef::Sprite> kSprites = {
      {"dot", ParticleDef::Dot},       {"glow", ParticleDef::Glow},     {"flake", ParticleDef::Flake}, {"streak", ParticleDef::Streak},
      {"petal", ParticleDef::Petal},   {"bubble", ParticleDef::Bubble}, {"spark", ParticleDef::Spark}, {"leaf", ParticleDef::Leaf}};
  const std::string sprite = j.value("sprite", std::string("dot"));
  if (auto it = kSprites.find(sprite); it != kSprites.end()) d.sprite = it->second;
  else {
    d.error = "\"sprite\" must be dot, glow, flake, streak, petal, bubble, spark or leaf";
    return d;
  }
  const json em = j.value("emitter", json::object());
  d.sphere = em.value("shape", std::string("box")) == "sphere";
  const auto origin = span(em.value("origin", json()), {0.5F, 0.5F}, -1, 2);
  d.ox = origin[0], d.oy = origin[1];
  const auto size = span(em.value("size", json()), {1, 1}, 0, 3);
  d.sx = size[0], d.sy = size[1];
  d.radius = number(em, "radius", 0.1F, 0, 2);
  const json in = j.value("init", json::object());
  d.life = span(in.value("lifetime", json()), d.life, 0.05F, 120);
  d.size = span(in.value("size", json()), d.size, 0.2F, 400);
  d.speed = span(in.value("speed", json()), d.speed, 0, 5000);
  d.dir = span(in.value("direction", json()), d.dir, -720, 720);
  d.alpha = span(in.value("alpha", json()), d.alpha, 0, 1);
  d.rot = span(in.value("rotation", json()), d.rot, -720, 720);
  d.spin = span(in.value("spin", json()), d.spin, -3600, 3600);
  d.depth = span(in.value("depth", json()), d.depth, 0, 1);
  const json cols = in.value("color", json());
  auto colour = [](const json& c, Color def) { return c.is_string() ? Color::fromHex(c.get<std::string>(), def) : def; };
  d.c0 = d.c1 = Color{1, 1, 1, 1};
  if (cols.is_string()) d.c0 = d.c1 = colour(cols, d.c0);
  else if (cols.is_array() && !cols.empty()) {
    d.c0 = colour(cols[0], d.c0);
    d.c1 = cols.size() > 1 ? colour(cols[1], d.c0) : d.c0;
  }
  static const std::map<std::string, ParticleOp::Type> kOps = {
      {"gravity", ParticleOp::Gravity},          {"drag", ParticleOp::Drag},
      {"turbulence", ParticleOp::Turbulence},    {"fade", ParticleOp::Fade},
      {"oscillate_alpha", ParticleOp::OscAlpha}, {"oscillate_position", ParticleOp::OscPosition},
      {"size_change", ParticleOp::SizeChange},   {"color_change", ParticleOp::ColorChange},
      {"vortex", ParticleOp::Vortex},            {"audio", ParticleOp::Audio}};
  for (const json& oj : j.value("operators", json::array())) {
    if (!oj.is_object()) continue;
    const auto it = kOps.find(oj.value("type", std::string()));
    if (it == kOps.end()) {
      d.error = "unknown operator \"" + oj.value("type", std::string()) + "\"";
      return d;
    }
    ParticleOp op;
    op.type = it->second;
    switch (op.type) {
    case ParticleOp::Gravity: {
      const auto g = span(oj.value("value", json()), {0, 0}, -10000, 10000);
      op.v = {g[0], g[1]};
      break;
    }
    case ParticleOp::Drag: op.v[0] = number(oj, "value", 0.5F, 0, 50); break;
    case ParticleOp::Turbulence:
      op.v = {number(oj, "scale", 0.005F, 0.0001F, 1), number(oj, "speed", 0.3F, 0, 20), number(oj, "strength", 20, 0, 5000)};
      break;
    case ParticleOp::Fade: op.v = {number(oj, "in", 0.1F, 0, 1), number(oj, "out", 0.2F, 0, 1)}; break;
    case ParticleOp::OscAlpha:
    case ParticleOp::OscPosition: {
      const auto f = span(oj.value("freq", json()), {0.5F, 1}, 0, 50);
      op.v = {f[0], f[1], number(oj, "amount", op.type == ParticleOp::OscAlpha ? 0.5F : 10, 0, op.type == ParticleOp::OscAlpha ? 1 : 2000)};
      break;
    }
    case ParticleOp::SizeChange: op.v[0] = number(oj, "end", 0.5F, 0, 20); break;
    case ParticleOp::ColorChange: op.color = Color::fromHex(oj.value("end", std::string("#ffffff")), Color{1, 1, 1, 1}); break;
    case ParticleOp::Vortex: {
      const auto c = span(oj.value("center", json()), {0.5F, 0.5F}, -1, 2);
      op.v = {c[0], c[1], number(oj, "strength", 40, -5000, 5000)};
      break;
    }
    case ParticleOp::Audio: {
      static const std::map<std::string, float> kBands = {{"bass", 0}, {"mid", 1}, {"treble", 2}, {"beat", 3}};
      const auto b = kBands.find(oj.value("band", std::string("bass")));
      op.v = {b != kBands.end() ? b->second : 0, number(oj, "rate", 0, 0, 100), number(oj, "size", 0, 0, 10), number(oj, "alpha", 0, 0, 10)};
      break;
    }
    }
    d.ops.push_back(op);
    if (d.ops.size() > 24) {
      d.error = "up to 24 operators";
      return d;
    }
  }
  return d;
}

std::vector<ParticleDef> builtinParticles() {
  std::vector<ParticleDef> all;
  for (const Builtin& b : kBuiltins) {
    ParticleDef d = parseParticles(b.id, b.text);
    if (!d.error.empty()) US_ERROR("built-in particles {}: {}", b.id, d.error);
    all.push_back(std::move(d));
  }
  return all;
}

// ── the library ─────────────────────────────────────────────────────────────

void ParticleLibrary::load(const std::string& userDir) {
  m_dir = userDir;
  m_all = builtinParticles();
  std::vector<fs::path> files;
  std::error_code ec;
  if (!m_dir.empty())
    for (const auto& de : fs::directory_iterator(m_dir, ec))
      if (de.path().extension() == ".json" && de.is_regular_file(ec)) files.push_back(de.path());
  std::ranges::sort(files);
  if (files.size() > 64) files.resize(64);
  for (const fs::path& f : files) {
    if (fs::file_size(f, ec) > 64 * 1024) {
      US_ERROR("particles {}: larger than 64 KiB, skipped", f.string());
      continue;
    }
    ParticleDef d = parseParticles(f.stem().string(), readFile(f.string()));
    d.file = f.string();
    if (!d.error.empty()) US_ERROR("particles {}: {}", f.string(), d.error);
    auto same = std::ranges::find_if(m_all, [&](const ParticleDef& x) { return x.id == d.id; });
    if (same != m_all.end()) *same = std::move(d);
    else m_all.push_back(std::move(d));
  }
  m_stamp = stamp();
}

std::string ParticleLibrary::stamp() const {
  std::string s;
  std::error_code ec;
  if (m_dir.empty()) return s;
  for (const auto& de : fs::directory_iterator(m_dir, ec)) {
    if (de.path().extension() != ".json") continue;
    const auto t = fs::last_write_time(de.path(), ec);
    s += std::format("{}:{}:{};", de.path().filename().string(), t.time_since_epoch().count(), de.file_size(ec));
  }
  return s;
}

bool ParticleLibrary::refresh() {
  if (stamp() == m_stamp) return false;
  load(m_dir);
  return true;
}

const ParticleDef* ParticleLibrary::find(const std::string& id) const {
  auto it = std::ranges::find_if(m_all, [&](const ParticleDef& d) { return d.id == id; });
  return it == m_all.end() ? nullptr : &*it;
}

std::string userParticlesDir() { return (fs::path(Config::defaultPath()).parent_path() / "particles").string(); }

ParticleLibrary& particleLibrary() {
  static ParticleLibrary lib = [] {
    ParticleLibrary l;
    l.load(userParticlesDir());
    return l;
  }();
  return lib;
}

// ── inspector rows ──────────────────────────────────────────────────────────

std::vector<PropSpec> particleRows(const toml::table& opts, bool all) {
  using K = PropSpec::Kind;
  std::vector<PropSpec> rows;
  PropSpec head;
  head.kind = K::Header;
  head.key = "_particles";
  head.labelEn = "Particles";
  head.labelEs = "Partículas";
  rows.push_back(head);
  auto row = [](PropSpec p) {
    p.indent = true;
    return p;
  };
  for (const ParticleDef& d : particleLibrary().all()) {
    const std::string key = particleKey(d.id);
    // only those that are on, with a button that takes them away (as the effects)
    PropSpec sw{all ? K::Bool : K::Action, all ? key : removeKey(key), d.nameEn, d.nameEs, {}, 0, 1, 1, 0};
    if (!d.error.empty()) sw.labelEn += " (error)", sw.labelEs += " (error)";
    if (!all && !opts[key].value_or(false)) continue;
    rows.push_back(sw);
    rows.push_back(row({K::Number, particleKey(d.id, "amount"), "Amount", "Cantidad", {}, 0.1, 3, 0.05, 1}));
    rows.push_back(row({K::Number, particleKey(d.id, "speed"), "Speed", "Velocidad", {}, 0.1, 3, 0.05, 1}));
    rows.push_back(row({K::Number, particleKey(d.id, "size"), "Size", "Tamaño", {}, 0.25, 3, 0.05, 1}));
    rows.push_back(row({K::Number, particleKey(d.id, "wind"), "Wind", "Viento", {}, -1, 1, 0.05, 0}));
    rows.push_back(row({K::Color, particleKey(d.id, "tint"), "Tint", "Teñido", {}, 0, 0, 0, 0, "#ffffff"}));
    rows.push_back(row({K::Number, particleKey(d.id, "react"), "With the music", "Con la música", {}, 0, 1, 0.05, d.react}));
    rows.push_back(row({K::Bool, particleKey(d.id, "behind"), "Behind the subject", "Tras la figura", {}, 0, 1, 1, 0}));
    PropSpec where{K::Enum, particleKey(d.id, "mask"), "Where", "Dónde", {"all", "above", "below", "paint1", "paint2", "paint3", "paint4"},
                   0, 0, 0, 0, "all"};
    where.optionNames = {{"Everywhere", "En todo"},   {"Above a line", "Arriba de línea"}, {"Below a line", "Abajo de línea"},
                         {"Painted 1", "Pintura 1"}, {"Painted 2", "Pintura 2"},          {"Painted 3", "Pintura 3"},
                         {"Painted 4", "Pintura 4"}};
    rows.push_back(row(where));
    const std::string m = opts[particleKey(d.id, "mask")].value_or(std::string("all"));
    if (all || m == "above" || m == "below")
      rows.push_back(row({K::Number, particleKey(d.id, "split"), "Line height", "Altura de línea", {}, 0, 1, 0.01, 0.5}));
    if (all || m.starts_with("paint")) {
      PropSpec paint{K::Action, particleKey(d.id, "paint"), "Paint the mask…", "Pintar la máscara…"};
      paint.defText = m;
      rows.push_back(row(paint));
    }
  }
  if (!all) rows.push_back({K::Action, "_pt_add", "+ Add particles", "+ Añadir partículas"});
  return rows;
}

// ── one system ──────────────────────────────────────────────────────────────

ParticleSystem::ParticleSystem(const ParticleDef& def, const ParticleSettings& s, uint64_t seed)
    : m_def(def), m_set(s), m_rng(seed | 1) {
  resize(wantedCap());
  // already under way: snow falling, not about to
  const MusicLevels quiet;
  for (float t = 0; t < m_def.prewarm; t += 1.0F / 15) step(1.0F / 15, 16.0F / 9, quiet);
}

size_t ParticleSystem::wantedCap() const {
  const double n = m_def.max * m_set.amount * perfKnobs().particles;
  return std::clamp<size_t>(static_cast<size_t>(std::ceil(n)), 1, kMaxCap);
}

void ParticleSystem::resize(size_t cap) {
  for (auto* v : {&m_x, &m_y, &m_px, &m_py, &m_vx, &m_vy, &m_age, &m_life, &m_size, &m_rot, &m_spin, &m_alpha, &m_depth, &m_seed, &m_fa, &m_fp})
    v->resize(cap);
  m_col.resize(cap);
  m_cap = cap;
  m_n = std::min(m_n, cap);
}

void ParticleSystem::settle(const ParticleSettings& s) {
  m_set = s;
  if (const size_t c = wantedCap(); c != m_cap) resize(c);
}

void ParticleSystem::spawn(float aspect) {
  if (m_n >= m_cap) return;
  const size_t i = m_n++;
  const float W = 1080 * aspect, H = 1080;
  float away = 0, awayX = 0, awayY = 0;  // the direction from the emitter's centre
  if (m_def.sphere) {
    const float a = rnd() * kTau, lo = std::min(m_def.radiusMin, m_def.radius);
    const float r = (lo + (m_def.radius - lo) * std::sqrt(rnd())) * H;
    m_x[i] = m_def.ox * W + std::cos(a) * r;
    m_y[i] = m_def.oy * H + std::sin(a) * r;
    awayX = std::cos(a), awayY = std::sin(a), away = 1;
  } else {
    m_x[i] = (m_def.ox + (rnd() - 0.5F) * m_def.sx) * W;
    m_y[i] = (m_def.oy + (rnd() - 0.5F) * m_def.sy) * H;
  }
  m_px[i] = m_x[i], m_py[i] = m_y[i];
  m_depth[i] = range(m_def.depth);
  const float near = 0.55F + 0.9F * m_depth[i];  // nearer: bigger and faster
  if (m_def.velBox) {
    m_vx[i] = (m_def.vel[0] + (m_def.vel[2] - m_def.vel[0]) * rnd()) * near * m_set.speed;
    m_vy[i] = (m_def.vel[1] + (m_def.vel[3] - m_def.vel[1]) * rnd()) * near * m_set.speed;
  } else {
    const float dir = range(m_def.dir) * kTau / 360, sp = range(m_def.speed) * near * m_set.speed;
    m_vx[i] = std::cos(dir) * sp;
    m_vy[i] = std::sin(dir) * sp;
  }
  if (m_def.radial[1] > 0 && away > 0) {
    const float sp = range(m_def.radial) * near * m_set.speed;
    m_vx[i] += awayX * sp, m_vy[i] += awayY * sp;
  }
  if (m_def.jitter[1] > 0) {
    const float a = rnd() * kTau, sp = range(m_def.jitter) * near * m_set.speed;
    m_vx[i] += std::cos(a) * sp, m_vy[i] += std::sin(a) * sp;
  }
  m_size[i] = range(m_def.size) * near * m_set.size;
  m_life[i] = range(m_def.life);
  m_age[i] = 0;
  m_rot[i] = range(m_def.rot) * kTau / 360;
  m_spin[i] = range(m_def.spin) * kTau / 360;
  m_alpha[i] = range(m_def.alpha);
  m_seed[i] = rnd();
  const float t = rnd();
  m_col[i] = {m_def.c0.r + (m_def.c1.r - m_def.c0.r) * t, m_def.c0.g + (m_def.c1.g - m_def.c0.g) * t, m_def.c0.b + (m_def.c1.b - m_def.c0.b) * t};
  m_fa[i] = m_fp[i] = 0;
  for (const ParticleOp& op : m_def.ops) {
    if (op.type == ParticleOp::OscAlpha) m_fa[i] = op.v[0] + (op.v[1] - op.v[0]) * rnd();
    if (op.type == ParticleOp::OscPosition) m_fp[i] = op.v[0] + (op.v[1] - op.v[0]) * rnd();
  }
}

void ParticleSystem::step(float dt, float aspect, const MusicLevels& music) {
  const float W = 1080 * aspect, H = 1080;
  m_time += dt;
  // birth: the rate, the amount, the level, and the music when it drives it
  float rate = m_def.rate * m_set.amount * perfKnobs().particles;
  for (const ParticleOp& op : m_def.ops)
    if (op.type == ParticleOp::Audio) rate *= 1 + op.v[1] * music.v[static_cast<size_t>(op.v[0])] * m_set.react;
  m_emit += rate * dt;
  while (m_emit >= 1 && m_n < m_cap) {
    spawn(aspect);
    m_emit -= 1;
  }
  m_emit = std::min(m_emit, 1.0F);
  const auto t = static_cast<float>(m_time);
  for (size_t i = 0; i < m_n;) {
    m_px[i] = m_x[i], m_py[i] = m_y[i];
    m_age[i] += dt;
    for (const ParticleOp& op : m_def.ops) {
      switch (op.type) {
      case ParticleOp::Gravity:
        m_vx[i] += op.v[0] * dt * m_set.speed;
        m_vy[i] += op.v[1] * dt * m_set.speed;
        break;
      case ParticleOp::Drag: {
        const float k = std::exp(-op.v[0] * dt);
        m_vx[i] *= k;
        m_vy[i] *= k;
        break;
      }
      case ParticleOp::Turbulence: {
        // a smooth field of sines drifting in time: cheap, and no two particles agree
        const float s = op.v[0], sp = op.v[1] * t, ph = m_seed[i] * kTau, X = m_x[i], Y = m_y[i];
        const float ax = std::sin(Y * s + sp + ph) + 0.5F * std::sin((X + Y) * s * 1.7F - sp * 1.3F);
        const float ay = std::cos(X * s * 1.1F - sp * 0.9F + ph * 0.5F) + 0.5F * std::cos((X - Y) * s * 1.3F + sp);
        m_vx[i] += ax * op.v[2] * dt;
        m_vy[i] += ay * op.v[2] * dt;
        break;
      }
      case ParticleOp::Vortex: {
        const float dx = m_x[i] - op.v[0] * W, dy = m_y[i] - op.v[1] * H, r = std::max(20.0F, std::hypot(dx, dy));
        m_vx[i] += -dy / r * op.v[2] * dt;
        m_vy[i] += dx / r * op.v[2] * dt;
        break;
      }
      default: break;  // the rest is how it looks, worked out when drawn
      }
    }
    const float near = 0.55F + 0.9F * m_depth[i];
    m_x[i] += (m_vx[i] + m_set.wind * 90 * near) * dt;
    m_y[i] += m_vy[i] * dt;
    m_rot[i] += m_spin[i] * dt;
    const bool out = m_x[i] < -0.2F * W || m_x[i] > 1.2F * W || m_y[i] < -0.25F * H || m_y[i] > 1.25F * H;
    if (m_age[i] >= m_life[i] || out) {
      const size_t last = --m_n;  // the last one takes its place
      if (i != last) {
        for (auto* v : {&m_x, &m_y, &m_px, &m_py, &m_vx, &m_vy, &m_age, &m_life, &m_size, &m_rot, &m_spin, &m_alpha, &m_depth, &m_seed, &m_fa, &m_fp})
          (*v)[i] = (*v)[last];
        m_col[i] = m_col[last];
      }
      continue;
    }
    ++i;
  }
}

void ParticleSystem::advance(double dt, float aspect, const MusicLevels& music) {
  if (const size_t c = wantedCap(); c != m_cap) resize(c);  // the level changed
  const double h = 1.0 / std::max(15, perfKnobs().simHz);
  m_acc += std::clamp(dt, 0.0, 0.25);
  int steps = 0;
  while (m_acc >= h && steps < 8) {
    step(static_cast<float>(h), aspect, music);
    m_acc -= h;
    ++steps;
  }
  if (steps == 8) m_acc = 0;  // fell behind: drop the rest rather than spiral
  m_blend = static_cast<float>(m_acc / h);
}

size_t ParticleSystem::fill(std::vector<float>& out, float aspect, const MusicLevels& music) const {
  (void)aspect;
  out.resize(m_n * kFloats);
  float fadeIn = 0, fadeOut = 0, oscA = 0, oscP = 0, sizeEnd = 1, audioSize = 0, audioAlpha = 0;
  bool colourChange = false, fades = false;
  Color end;
  float level = 0;
  for (const ParticleOp& op : m_def.ops) {
    switch (op.type) {
    case ParticleOp::Fade: fadeIn = op.v[0], fadeOut = op.v[1], fades = true; break;
    case ParticleOp::OscAlpha: oscA = op.v[2]; break;
    case ParticleOp::OscPosition: oscP = op.v[2]; break;
    case ParticleOp::SizeChange: sizeEnd = op.v[0]; break;
    case ParticleOp::ColorChange: colourChange = true, end = op.color; break;
    case ParticleOp::Audio:
      level = music.v[static_cast<size_t>(op.v[0])] * m_set.react;
      audioSize = op.v[2], audioAlpha = op.v[3];
      break;
    default: break;
    }
  }
  size_t n = 0;
  for (size_t i = 0; i < m_n; ++i) {
    const float t = std::clamp(m_age[i] / m_life[i], 0.0F, 1.0F);
    const float near = 0.55F + 0.9F * m_depth[i];
    float x = m_px[i] + (m_x[i] - m_px[i]) * m_blend, y = m_py[i] + (m_y[i] - m_py[i]) * m_blend;
    if (oscP > 0) x += std::sin(m_age[i] * m_fp[i] * kTau + m_seed[i] * kTau) * oscP * near;
    float a = m_alpha[i] * (fades ? fadeAt(t, fadeIn, fadeOut) : 1);
    if (oscA > 0) a *= 1 - oscA * (0.5F + 0.5F * std::sin(m_age[i] * m_fa[i] * kTau + m_seed[i] * kTau));
    a = std::clamp(a * (1 + audioAlpha * level), 0.0F, 1.0F);
    if (a < 0.004F) continue;
    const float size = m_size[i] * (1 + (sizeEnd - 1) * t) * (1 + audioSize * level);
    auto c = m_col[i];
    if (colourChange) c = {c[0] + (end.r - c[0]) * t, c[1] + (end.g - c[1]) * t, c[2] + (end.b - c[2]) * t};
    float* o = &out[n * kFloats];
    o[0] = x, o[1] = y, o[2] = size, o[3] = m_rot[i];
    o[4] = c[0] * m_set.tint.r * a, o[5] = c[1] * m_set.tint.g * a, o[6] = c[2] * m_set.tint.b * a, o[7] = a;
    o[8] = m_vx[i] + m_set.wind * 90 * near, o[9] = m_vy[i], o[10] = m_depth[i], o[11] = m_seed[i];
    if (m_def.frames > 1)  // a sprite sheet: the frame due at its age
      o[11] = static_cast<float>(static_cast<int>(m_age[i] / std::max(0.01F, m_def.sheetTime) * m_def.frames) % m_def.frames);
    ++n;
  }
  out.resize(n * kFloats);
  return n;
}

// ── the field: every system of a widget, drawn ──────────────────────────────

namespace {

const char* kVert = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 i_a;    // x, y (1080-high units), size, rotation
layout(location = 2) in vec4 i_col;  // premultiplied
layout(location = 3) in vec4 i_b;    // vx, vy, depth, seed
uniform vec2 u_size;     // the surface, logical px
uniform float u_unit;    // logical px per unit
uniform vec2 u_offset;   // where unit 0 lands
uniform float u_streak;  // stretched by this many seconds of travel
out vec2 v_q;
out vec4 v_col;
out vec2 v_p;
out float v_depth;
out float v_frame;
void main() {
    vec2 q = a_pos * 2.0 - 1.0;
    vec2 c = i_a.xy * u_unit + u_offset;
    float hw = i_a.z * 0.5 * u_unit;
    vec2 ax, ay;
    if (u_streak > 0.0) {
        vec2 v = i_b.xy * u_unit;
        float sp = length(v);
        vec2 d = sp > 0.001 ? v / sp : vec2(0.0, 1.0);
        ax = d * max(hw, sp * u_streak * 0.5);
        ay = vec2(-d.y, d.x) * hw;
    } else {
        float cs = cos(i_a.w), sn = sin(i_a.w);
        ax = vec2(cs, sn) * hw;
        ay = vec2(-sn, cs) * hw;
    }
    vec2 p = c + ax * q.x + ay * q.y;
    v_q = q;
    v_col = i_col;
    v_p = p;
    v_depth = i_b.z;
    v_frame = i_b.w;
    gl_Position = vec4(p.x / u_size.x * 2.0 - 1.0, 1.0 - p.y / u_size.y * 2.0, 0.0, 1.0);
}
)";

const char* kFrag = R"(#version 300 es
precision highp float;
in vec2 v_q;
in vec4 v_col;
in vec2 v_p;
in float v_depth;
in float v_frame;
out vec4 fragColor;
uniform sampler2D u_spriteTex;  // sprite 8: a picture
uniform vec2 u_cells;           // its sprite sheet's columns and rows
uniform int u_alphaRed;         // its red is the coverage
uniform vec2 u_size;
uniform vec2 u_img;
uniform int u_fill;
uniform int u_sprite;
uniform sampler2D u_depth;
uniform float u_depthOn, u_behind, u_feather;
uniform sampler2D u_fxmasks;
uniform float u_fxmasksOn;
uniform int u_mask;
uniform float u_split;

// Noctalia's calculateWallpaperUV (MIT): an output point -> the picture's uv
bool wallUv(vec2 p, out vec2 uv) {
    vec2 x = p / u_size;
    vec2 sw = u_size, iw = u_img;
    if (u_fill == 0) {
        uv = (x * sw - (sw - iw) * 0.5) / iw;
    } else if (u_fill == 1 || u_fill == 5) {
        float s = max(sw.x / iw.x, sw.y / iw.y);
        vec2 off = (iw * s - sw) / (iw * s);
        uv = x * (1.0 - off) + off * 0.5;
    } else if (u_fill == 2) {
        float s = min(sw.x / iw.x, sw.y / iw.y);
        uv = ((x * sw - (sw - iw * s) * 0.5) / s) / iw;
    } else if (u_fill == 3) {
        uv = x;
    } else {
        uv = fract(x * sw / iw);
        return true;
    }
    return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
}

// the sprites, drawn in a -1..1 square (x along the travel for streaks)
float sprite(vec2 q) {
    float r = length(q);
    if (u_sprite == 0) return smoothstep(1.0, 0.5, r);                           // dot
    if (u_sprite == 1) return exp(-r * r * 5.0) * smoothstep(1.0, 0.8, r);       // glow
    if (u_sprite == 2) {                                                         // flake: six arms and a core
        float arms = 1.0 - smoothstep(0.03, 0.1, r * abs(sin(3.0 * atan(q.y, q.x))));
        return max(arms * smoothstep(1.0, 0.55, r) * 0.85, smoothstep(0.38, 0.0, r));
    }
    if (u_sprite == 3) return (1.0 - q.x * q.x) * smoothstep(1.0, 0.15, abs(q.y));  // streak
    if (u_sprite == 4) {                                                         // petal: an oval narrowing to one end
        float d = length(vec2(q.x, q.y * 1.8 / (0.75 + 0.25 * q.x)));
        return smoothstep(1.0, 0.82, d) * (0.82 + 0.18 * (1.0 - d));
    }
    if (u_sprite == 5) {                                                         // bubble: a thin rim, a glint
        float rim = smoothstep(0.13, 0.0, abs(r - 0.82));
        float glint = smoothstep(0.28, 0.0, length(q - vec2(-0.35, -0.38)));
        return max(rim * 0.85, glint) + 0.07 * smoothstep(0.85, 0.0, r);
    }
    if (u_sprite == 6) return (1.0 - q.x * q.x) * exp(-q.y * q.y * 7.0);         // spark
    float w = 0.48 * (1.0 - q.x * q.x);                                          // leaf, with its midrib
    return smoothstep(w, w - 0.08, abs(q.y)) * (1.0 - 0.35 * smoothstep(0.04, 0.0, abs(q.y)));
}

void main() {
    vec3 rgb = vec3(1.0);
    float a;
    if (u_sprite == 8) {
        vec2 cell = vec2(mod(v_frame, u_cells.x), floor(v_frame / u_cells.x));
        vec4 t = texture(u_spriteTex, (cell + v_q * 0.5 + 0.5) / u_cells);
        if (u_alphaRed == 1) t = vec4(1.0, 1.0, 1.0, t.r);
        rgb = t.rgb;
        a = t.a;
    } else {
        a = sprite(v_q);
    }
    if (a <= 0.003) discard;
    vec2 uv;
    bool on = wallUv(v_p, uv);
    // behind the subject: the wallpaper nearer than this particle's plane hides it
    if (u_behind > 0.5 && u_depthOn > 0.5 && on)
        a *= 1.0 - smoothstep(v_depth - u_feather * 0.5, v_depth + u_feather * 0.5, texture(u_depth, uv).r);
    if (u_mask == 3 || u_mask == 4) {
        float s = smoothstep(u_split - 0.03, u_split + 0.03, on ? uv.y : v_p.y / u_size.y);
        a *= u_mask == 3 ? 1.0 - s : s;
    } else if (u_mask >= 5) {
        a *= u_fxmasksOn > 0.5 && on ? texture(u_fxmasks, uv)[u_mask - 5] : 0.0;
    }
    if (a <= 0.003) discard;
    fragColor = vec4(v_col.rgb * rgb, v_col.a) * a;
}
)";

}  // namespace

ParticleField::~ParticleField() {
  if (m_vbo) glDeleteBuffers(1, &m_vbo);
}

void ParticleField::configure(const toml::table& opts, const NoctaliaState& noct) {
  m_opts = opts;
  m_noct = noct;
  const ParticleLibrary& lib = particleLibrary();
  std::vector<std::unique_ptr<ParticleSystem>> next;
  for (const ParticleDef& d : lib.all()) {
    if (!d.error.empty() || !opts[particleKey(d.id)].value_or(false)) continue;
    auto num = [&](const char* opt, double def, double lo, double hi) {
      double v = def;
      if (auto x = opts[particleKey(d.id, opt)].value<double>()) v = *x;
      return static_cast<float>(std::clamp(v, lo, hi));
    };
    ParticleSettings s;
    s.amount = num("amount", 1, 0.1, 3);
    s.speed = num("speed", 1, 0.1, 3);
    s.size = num("size", 1, 0.25, 3);
    s.wind = num("wind", 0, -1, 1);
    s.react = num("react", d.react, 0, 1);
    s.split = num("split", 0.5, 0, 1);
    s.tint = noct.color(opts[particleKey(d.id, "tint")].value_or(std::string("#ffffff")), Color{1, 1, 1, 1});
    s.behind = opts[particleKey(d.id, "behind")].value_or(false);
    const std::string mask = opts[particleKey(d.id, "mask")].value_or(std::string("all"));
    const auto& modes = effectMaskModes();
    s.mask = static_cast<int>(std::ranges::find(modes, mask) - modes.begin());
    if (s.mask == 1 || s.mask == 2 || s.mask >= static_cast<int>(modes.size())) s.mask = 0;  // not offered here
    // the same system, still running, takes the new settings; a new one starts
    auto same = std::ranges::find_if(m_sys, [&](const auto& p) { return p && p->def().id == d.id && p->def().hash == d.hash; });
    if (same != m_sys.end()) {
      (*same)->settle(s);
      next.push_back(std::move(*same));
    } else {
      next.push_back(std::make_unique<ParticleSystem>(d, s, fnv(d.id) ^ 0x9e3779b97f4a7c15ULL));
    }
  }
  m_sys = std::move(next);
}

void ParticleField::configureDefs(const std::vector<ParticleDef>& defs) {
  std::vector<std::unique_ptr<ParticleSystem>> next;
  for (size_t i = 0; i < defs.size(); ++i) {
    const ParticleDef& d = defs[i];
    // the same system, still running, stays as it is
    auto same = std::ranges::find_if(m_sys, [&](const auto& p) { return p && p->def().id == d.id && p->def().hash == d.hash; });
    if (same != m_sys.end()) next.push_back(std::move(*same));
    else next.push_back(std::make_unique<ParticleSystem>(d, ParticleSettings{}, fnv(d.id) ^ (0x9e3779b97f4a7c15ULL * (i + 1))));
  }
  m_sys = std::move(next);
}

void ParticleField::setPlacement(float unit, float offX, float offY, float aspect) {
  m_placed = true;
  m_unit = unit, m_offX = offX, m_offY = offY, m_aspect = aspect;
}

bool ParticleField::refresh() {
  if (!particleLibrary().refresh()) return false;
  configure(m_opts, m_noct);
  return true;
}

bool ParticleField::audio() const {
  return std::ranges::any_of(m_sys, [](const auto& s) {
    return s->settings().react > 0 && std::ranges::any_of(s->def().ops, [](const ParticleOp& o) { return o.type == ParticleOp::Audio; });
  });
}

bool ParticleField::depth() const {
  return std::ranges::any_of(m_sys, [](const auto& s) { return s->settings().behind || s->settings().mask >= 5; });
}

size_t ParticleField::count() const {
  size_t n = 0;
  for (const auto& s : m_sys) n += s->count();
  return n;
}

void ParticleField::tick(const TickContext& ctx) {
  if (m_sys.empty()) return;
  if (audio()) m_music.tick(ctx);
  else m_music.clear();
  const double dt = m_last < 0 ? 0 : ctx.now - m_last;
  m_last = ctx.now;
  for (auto& s : m_sys) s->advance(dt, m_aspect, m_music);
}

void ParticleField::draw(const DrawContext& ctx, float imgW, float imgH, int only) {
  if (m_sys.empty() || ctx.w <= 0 || ctx.h <= 0) return;
  if (!m_placed) m_aspect = ctx.w / ctx.h;
  if (!m_prog.valid()) m_prog.create(kVert, kFrag, "particles");
  glUseProgram(m_prog.id());
  glUniform2f(m_prog.uniform("u_size"), ctx.w, ctx.h);
  glUniform1f(m_prog.uniform("u_unit"), m_placed ? m_unit : ctx.h / 1080.0F);
  glUniform2f(m_prog.uniform("u_offset"), m_placed ? m_offX : 0.0F, m_placed ? m_offY : 0.0F);
  glUniform1i(m_prog.uniform("u_spriteTex"), 5);
  glUniform2f(m_prog.uniform("u_img"), imgW, imgH);
  glUniform1i(m_prog.uniform("u_fill"), ctx.fillMode);
  glUniform1i(m_prog.uniform("u_depth"), 1);
  glUniform1f(m_prog.uniform("u_depthOn"), ctx.depthField ? 1.0F : 0.0F);
  glUniform1f(m_prog.uniform("u_feather"), std::max(0.04F, ctx.depthFeather));
  glUniform1i(m_prog.uniform("u_fxmasks"), 4);
  glUniform1f(m_prog.uniform("u_fxmasksOn"), ctx.effectMasks ? 1.0F : 0.0F);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, ctx.depthField);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, ctx.effectMasks);
  glActiveTexture(GL_TEXTURE0);
  if (!m_vbo) glGenBuffers(1, &m_vbo);
  static constexpr std::array<GLfloat, 12> kQuad = {0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1};
  glEnable(GL_BLEND);
  for (size_t si = 0; si < m_sys.size(); ++si) {
    if (only >= 0 && static_cast<size_t>(only) != si) continue;
    const auto& s = m_sys[si];
    const ParticleDef& d = s->def();
    GLuint picture = 0;
    bool red = false;
    if (d.sprite == ParticleDef::Texture) {
      if (m_textures) std::tie(picture, red) = m_textures(d.texture);
      if (!picture) continue;  // not loaded (yet)
      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_2D, picture);
      glActiveTexture(GL_TEXTURE0);
      glUniform2f(m_prog.uniform("u_cells"), static_cast<float>(std::max(1, d.cols)), static_cast<float>(std::max(1, d.rows)));
      glUniform1i(m_prog.uniform("u_alphaRed"), red ? 1 : 0);
    }
    const size_t n = s->fill(m_buf, m_aspect, m_music);
    if (n == 0) continue;
    glUniform1i(m_prog.uniform("u_sprite"), static_cast<int>(d.sprite));
    glUniform1f(m_prog.uniform("u_streak"), d.streak);
    glUniform1f(m_prog.uniform("u_behind"), s->settings().behind ? 1.0F : 0.0F);
    glUniform1i(m_prog.uniform("u_mask"), s->settings().mask);
    glUniform1f(m_prog.uniform("u_split"), s->settings().split);
    if (d.additive) glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);  // light adds; the coverage stays the picture's
    else glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_buf.size() * sizeof(float)), m_buf.data(), GL_STREAM_DRAW);
    const GLsizei stride = kFloats * sizeof(float);
    for (int a = 0; a < 3; ++a) {
      glEnableVertexAttribArray(1 + a);
      glVertexAttribPointer(1 + a, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(static_cast<uintptr_t>(a * 4 * sizeof(float))));
      glVertexAttribDivisor(1 + a, 1);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kQuad.data());
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(n));
  }
  for (int a = 1; a <= 3; ++a) {  // leave the attributes as every other pass expects them
    glVertexAttribDivisor(a, 0);
    glDisableVertexAttribArray(a);
  }
  glDisable(GL_BLEND);
}

}  // namespace undershell
