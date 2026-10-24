// SPDX-License-Identifier: GPL-3.0-or-later
// Wallpaper effects (see effects.hpp). The built-in ones are written from
// scratch for undershell (src/fx/*.inc) in the same format a person's own
// effects use.
#include "effects.hpp"

#include "common.hpp"
#include "config.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <regex>
#include <set>

namespace undershell {

namespace fs = std::filesystem;
using json = nlohmann::json;

const char* kEffectApi = R"(
uniform sampler2D u_tex;      // the wallpaper (mipmapped)
uniform sampler2D u_depth;    // its depth field: 0 far .. 1 near
uniform sampler2D u_noise;    // 256x256 random bytes, repeating
uniform float u_time;         // seconds
uniform vec4 u_audio;         // bass, mids, treble, beat: 0..1
uniform int u_quality;        // 0 eco, 1 balanced, 2 high, 3 ultra
uniform float u_aspect;       // the picture's width / height
uniform vec2 u_screen;        // the surface, px
uniform float u_depthOn, u_depthLevel, u_depthFeather;
uniform int u_wrap;           // the picture repeats (tiled)
uniform int u_flipY;          // the picture is an off-screen render: rows bottom up
uniform sampler2D u_fxmasks;  // the four painted effect masks (RGBA over the picture)
uniform float u_fxmasksOn;
vec2 screenPx;                // this pixel on the surface, px

vec2 edgeSafe(vec2 uv) { return u_wrap == 1 ? uv : clamp(uv, 0.0005, 0.9995); }
vec2 texUv(vec2 uv) { uv = edgeSafe(uv); return u_flipY == 1 ? vec2(uv.x, 1.0 - uv.y) : uv; }
vec3 wall(vec2 uv) { return texture(u_tex, texUv(uv)).rgb; }
vec3 wallLod(vec2 uv, float lod) { return textureLod(u_tex, texUv(uv), lod).rgb; }
float depth(vec2 uv) { return u_depthOn > 0.5 ? textureLod(u_depth, clamp(uv, 0.0, 1.0), 0.0).r : 0.0; }
float subject(vec2 uv) {
    if (u_depthOn < 0.5) return 0.0;
    float lo = u_depthLevel - u_depthFeather * 0.5, hi = u_depthLevel + u_depthFeather * 0.5;
    float d = depth(uv);
    return hi <= lo ? step(hi, d) : smoothstep(lo, hi, d);
}
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
// smooth value noise: one bilinear read between eased lattice points
vec4 noise4(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return textureLod(u_noise, (i + f + 0.5) / 256.0, 0.0);
}
float noise(vec2 p) { return noise4(p).r; }
vec2 flow(vec2 p) { return noise4(p).gb * 2.0 - 1.0; }
float fbm(vec2 p) {
    float s = 0.0, a = 0.5, total = 0.0;
    int n = 3 + u_quality;
    for (int i = 0; i < n; i++) {
        s += a * noise(p);
        total += a;
        p = p * 2.03 + 17.0;
        a *= 0.5;
    }
    return s / total;
}
// an offset in picture heights -> picture uv
vec2 pictureOffset(vec2 d) { return vec2(d.x / u_aspect, d.y); }
// where an effect applies: everywhere, the background, the subject, above or
// below a line, or one of the four painted masks
float fxMask(int mode, float split, vec2 uv) {
    if (mode >= 5) return u_fxmasksOn > 0.5 ? texture(u_fxmasks, clamp(uv, 0.0, 1.0))[mode - 5] : 0.0;
    if (mode == 1) return 1.0 - subject(uv);
    if (mode == 2) return subject(uv);
    if (mode == 3) return 1.0 - smoothstep(split - 0.03, split + 0.03, uv.y);
    if (mode == 4) return smoothstep(split - 0.03, split + 0.03, uv.y);
    return 1.0;
}
)";

namespace {

struct Builtin {
  const char* id;
  const char* text;
};

// warps, then colours, then lights; within a stage this is the order they
// apply in (the camera moves the picture, then water and wind bend it; the
// lens and the tape read the picture again, so they go first)
const Builtin kBuiltins[] = {
    {"parallax",
#include "fx/parallax.inc"
    },
    {"waves",
#include "fx/waves.inc"
    },
    {"ripple",
#include "fx/ripple.inc"
    },
    {"flow",
#include "fx/flow.inc"
    },
    {"sway",
#include "fx/sway.inc"
    },
    {"chroma",
#include "fx/chroma.inc"
    },
    {"vhs",
#include "fx/vhs.inc"
    },
    {"godrays",
#include "fx/godrays.inc"
    },
    {"shine",
#include "fx/shine.inc"
    },
    {"twinkle",
#include "fx/twinkle.inc"
    },
    {"pulse",
#include "fx/pulse.inc"
    },
    {"mist",
#include "fx/mist.inc"
    },
    {"grain",
#include "fx/grain.inc"
    },
};

constexpr size_t kMaxFile = 64 * 1024;
constexpr size_t kMaxOwn = 64;

uint64_t fnv(const std::string& s) {
  uint64_t h = 1469598103934665603ULL;
  for (const unsigned char c : s) h = (h ^ c) * 1099511628211ULL;
  return h;
}

// names an option may not take: GLSL words and what kEffectApi defines
bool reservedName(const std::string& k) {
  static const std::set<std::string> kWords = {
      "strength", "mask", "split", "paint", "uv", "c", "L", "warp", "color", "light", "wall", "wallLod", "depth", "subject", "luma", "hash", "noise",
      "noise4", "flow", "fbm", "pictureOffset", "edgeSafe", "texUv", "fxMask", "screenPx", "float", "int", "uint", "bool", "vec2", "vec3",
      "vec4", "ivec2", "ivec3", "ivec4", "bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4", "void", "true", "false", "if", "else",
      "for", "while", "do", "return", "break", "continue", "discard", "const", "in", "out", "inout", "uniform", "struct",
      "precision", "highp", "mediump", "lowp", "sampler2D", "length", "distance", "dot", "cross", "normalize", "mix", "step",
      "smoothstep", "clamp", "min", "max", "abs", "sign", "floor", "ceil", "fract", "mod", "pow", "exp", "exp2", "log", "log2",
      "sqrt", "inversesqrt", "sin", "cos", "tan", "asin", "acos", "atan", "radians", "degrees", "texture", "textureLod",
      "reflect", "refract", "round", "trunc", "main"};
  return kWords.contains(k) || k.rfind("u_", 0) == 0 || k.rfind("gl_", 0) == 0;
}

std::string stripComments(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s.compare(i, 2, "//") == 0) {
      while (i < s.size() && s[i] != '\n') ++i;
      if (i < s.size()) out += '\n';
    } else if (s.compare(i, 2, "/*") == 0) {
      const size_t end = s.find("*/", i + 2);
      if (end == std::string::npos) break;
      out += ' ';
      i = end + 1;
    } else {
      out += s[i];
    }
  }
  return out;
}

struct Tok {
  enum Kind { Ident, Num, Space, Punct } kind;
  std::string text;
};

std::vector<Tok> lex(const std::string& s) {
  std::vector<Tok> t;
  size_t i = 0;
  auto isId = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
  while (i < s.size()) {
    const char c = s[i];
    size_t j = i + 1;
    Tok::Kind k = Tok::Punct;
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      k = Tok::Ident;
      while (j < s.size() && isId(s[j])) ++j;
    } else if (std::isdigit(static_cast<unsigned char>(c)) ||
               (c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
      k = Tok::Num;
      while (j < s.size() && (isId(s[j]) || s[j] == '.')) ++j;
    } else if (std::isspace(static_cast<unsigned char>(c))) {
      k = Tok::Space;
      while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) ++j;
    }
    t.push_back({k, s.substr(i, j - i)});
    i = j;
  }
  return t;
}

// the names an effect declares at its top level (functions, constants,
// globals, structs): they get the effect's prefix like its options
std::set<std::string> topLevelNames(const std::vector<Tok>& t, std::string& error) {
  std::set<std::string> names;
  int braces = 0, parens = 0;
  auto prev = [&](size_t i) -> const Tok* {
    while (i-- > 0)
      if (t[i].kind != Tok::Space) return &t[i];
    return nullptr;
  };
  auto next = [&](size_t i) -> const Tok* {
    for (++i; i < t.size(); ++i)
      if (t[i].kind != Tok::Space) return &t[i];
    return nullptr;
  };
  for (size_t i = 0; i < t.size(); ++i) {
    const Tok& k = t[i];
    if (k.kind == Tok::Punct) {
      if (k.text == "{") ++braces;
      else if (k.text == "}") --braces;
      else if (k.text == "(") ++parens;
      else if (k.text == ")") --parens;
      else if (k.text == "#") error = "no preprocessor lines (#…) in an effect: use const";
      continue;
    }
    if (k.kind != Tok::Ident) continue;
    if (k.text == "uniform" || k.text == "precision" || k.text == "layout") {
      error = "an effect cannot declare '" + k.text + "': its options are declared for it";
      continue;
    }
    if (braces != 0 || parens != 0) continue;
    if (k.text == "in" || k.text == "out") {
      error = "an effect cannot declare inputs or outputs";
      continue;
    }
    const Tok* p = prev(i);
    const Tok* n = next(i);
    if (!p || !n) continue;
    const bool typed = p->kind == Tok::Ident && p->text != "return";
    const bool listed = p->kind == Tok::Punct && p->text == ",";
    const bool ends = n->kind == Tok::Punct && (n->text == "=" || n->text == ";" || n->text == "[" || n->text == ",");
    if ((typed && (ends || n->text == "(")) || (listed && ends) || (p->text == "struct")) names.insert(k.text);
  }
  if (braces != 0 || parens != 0) error = "unbalanced braces or parentheses";
  return names;
}

std::string rename(const std::vector<Tok>& t, const std::set<std::string>& names, const std::string& prefix) {
  std::string out;
  const Tok* last = nullptr;
  for (const Tok& k : t) {
    const bool member = last && last->kind == Tok::Punct && last->text == ".";  // a swizzle or a field
    if (k.kind == Tok::Ident && !member && names.contains(k.text)) out += prefix + k.text;
    else out += k.text;
    if (k.kind != Tok::Space) last = &k;
  }
  return out;
}

std::pair<std::string, std::string> labels(const json& j, const std::string& fallback) {
  if (j.is_string()) return {j.get<std::string>(), j.get<std::string>()};
  std::string en = fallback, es;
  if (j.is_object()) {
    if (j.contains("en") && j["en"].is_string()) en = j["en"].get<std::string>();
    if (j.contains("es") && j["es"].is_string()) es = j["es"].get<std::string>();
  }
  if (es.empty()) es = en;
  // a row label, not a paragraph
  if (en.size() > 40) en.resize(40);
  if (es.size() > 40) es.resize(40);
  return {en, es};
}

const char* glslType(EffectParam::Kind k) {
  switch (k) {
  case EffectParam::Bool: return "bool";
  case EffectParam::Choice: return "int";
  case EffectParam::Color: return "vec3";
  default: return "float";
  }
}

}  // namespace

EffectSpec parseEffect(const std::string& id, const std::string& text) {
  EffectSpec e;
  e.id = id;
  e.nameEn = e.nameEs = id;
  e.hash = fnv(text);
  static const std::regex kId("^[a-z0-9_]{1,24}$");
  if (!std::regex_match(id, kId)) {
    e.error = "the file name must be a-z, 0-9 or _ (up to 24)";
    return e;
  }
  const size_t open = text.find("/* undershell-effect");
  const size_t close = open == std::string::npos ? open : text.find("*/", open);
  if (close == std::string::npos) {
    e.error = "no /* undershell-effect { … } */ header";
    return e;
  }
  const size_t from = open + std::string_view("/* undershell-effect").size();
  const json h = json::parse(text.substr(from, close - from), nullptr, false);
  if (h.is_discarded() || !h.is_object()) {
    e.error = "the header is not valid JSON";
    return e;
  }
  std::tie(e.nameEn, e.nameEs) = labels(h.value("name", json()), id);
  const std::string stage = h.value("stage", std::string());
  if (stage == "warp") e.stage = EffectSpec::Warp;
  else if (stage == "colour" || stage == "color") e.stage = EffectSpec::Colour;
  else if (stage == "light") e.stage = EffectSpec::Light;
  else {
    e.error = "\"stage\" must be \"warp\", \"colour\" or \"light\"";
    return e;
  }
  if (h.contains("uses") && h["uses"].is_array())
    for (const auto& u : h["uses"])
      if (u.is_string()) {
        const std::string s = u.get<std::string>();
        e.time |= s == "time";
        e.depth |= s == "depth";
        e.audio |= s == "audio";
      }
  e.audioWhen = h.value("audio_when", std::string());
  static const std::regex kKey("^[a-z][a-z0-9_]{0,23}$");
  static const std::regex kWord("^[a-z0-9_#]{1,24}$");
  std::set<std::string> keys;
  if (h.contains("params") && h["params"].is_array()) {
    for (const auto& pj : h["params"]) {
      if (!pj.is_object()) continue;
      EffectParam p;
      p.key = pj.value("key", std::string());
      if (!std::regex_match(p.key, kKey) || reservedName(p.key) || !keys.insert(p.key).second) {
        e.error = "option \"" + p.key + "\": a new name of a-z, 0-9 and _ that GLSL does not use";
        return e;
      }
      std::tie(p.labelEn, p.labelEs) = labels(pj.value("label", json()), p.key);
      const std::string kind = pj.value("kind", std::string("number"));
      if (kind == "number") {
        p.kind = EffectParam::Number;
        p.min = pj.value("min", 0.0);
        p.max = pj.value("max", 1.0);
        p.step = pj.value("step", 0.05);
        p.integer = pj.value("integer", false);
        if (!(p.max > p.min) || !(p.step > 0)) {
          e.error = "option \"" + p.key + "\": max must be above min and step above 0";
          return e;
        }
        p.def = std::clamp(pj.value("default", p.min), p.min, p.max);
      } else if (kind == "bool") {
        p.kind = EffectParam::Bool;
        p.def = pj.value("default", false) ? 1 : 0;
      } else if (kind == "choice") {
        p.kind = EffectParam::Choice;
        if (pj.contains("options") && pj["options"].is_array())
          for (const auto& o : pj["options"])
            if (o.is_string() && std::regex_match(o.get<std::string>(), kWord)) p.options.push_back(o.get<std::string>());
        if (p.options.empty() || p.options.size() > 12) {
          e.error = "option \"" + p.key + "\": a choice needs 1 to 12 options of a-z, 0-9 and _";
          return e;
        }
        p.defText = pj.value("default", p.options[0]);
        if (std::ranges::find(p.options, p.defText) == p.options.end()) p.defText = p.options[0];
        const json names = pj.value("option_names", json());
        for (size_t i = 0; i < p.options.size(); ++i) {
          std::string en = p.options[i], es;
          if (names.is_object()) {
            if (names.contains("en") && names["en"].is_array() && i < names["en"].size() && names["en"][i].is_string())
              en = names["en"][i].get<std::string>();
            if (names.contains("es") && names["es"].is_array() && i < names["es"].size() && names["es"][i].is_string())
              es = names["es"][i].get<std::string>();
          }
          p.optionNames.emplace_back(en, es.empty() ? en : es);
        }
      } else if (kind == "color" || kind == "colour") {
        p.kind = EffectParam::Color;
        p.defText = pj.value("default", std::string("#ffffff"));
        if (!std::regex_match(p.defText, kWord)) p.defText = "#ffffff";
      } else {
        e.error = "option \"" + p.key + "\": kind must be number, bool, choice or color";
        return e;
      }
      e.params.push_back(std::move(p));
      if (e.params.size() > 16) {
        e.error = "up to 16 options per effect";
        return e;
      }
    }
  }
  if (!e.audioWhen.empty()) {
    const auto it = std::ranges::find_if(e.params, [&](const EffectParam& p) { return p.key == e.audioWhen; });
    if (it == e.params.end() || it->kind != EffectParam::Bool) {
      e.error = "\"audio_when\" must name one of its bool options";
      return e;
    }
  }
  e.body = stripComments(text.substr(close + 2));
  std::string why;
  const std::set<std::string> names = topLevelNames(lex(e.body), why);
  if (!why.empty()) {
    e.error = why;
    return e;
  }
  const char* entry = e.stage == EffectSpec::Warp ? "warp" : e.stage == EffectSpec::Colour ? "color" : "light";
  if (!names.contains(entry)) {
    e.error = e.stage == EffectSpec::Warp     ? "a warp needs vec2 warp(vec2 uv)"
              : e.stage == EffectSpec::Colour ? "a colour effect needs vec3 color(vec3 c, vec2 uv)"
                                              : "a light needs vec4 light(vec2 uv)";
    return e;
  }
  return e;
}

std::vector<EffectSpec> builtinEffects() {
  std::vector<EffectSpec> all;
  for (const Builtin& b : kBuiltins) {
    EffectSpec e = parseEffect(b.id, b.text);
    if (!e.error.empty()) US_ERROR("built-in effect {}: {}", b.id, e.error);
    all.push_back(std::move(e));
  }
  return all;
}

void EffectLibrary::load(const std::string& userDir) {
  m_dir = userDir;
  m_all = builtinEffects();
  std::vector<fs::path> files;
  std::error_code ec;
  if (!m_dir.empty())
    for (const auto& de : fs::directory_iterator(m_dir, ec))
      if (de.path().extension() == ".glsl" && de.is_regular_file(ec)) files.push_back(de.path());
  std::ranges::sort(files);
  if (files.size() > kMaxOwn) files.resize(kMaxOwn);
  for (const fs::path& f : files) {
    if (fs::file_size(f, ec) > kMaxFile) {
      US_ERROR("effect {}: larger than 64 KiB, skipped", f.string());
      continue;
    }
    EffectSpec e = parseEffect(f.stem().string(), readFile(f.string()));
    e.file = f.string();
    if (!e.error.empty()) US_ERROR("effect {}: {}", f.string(), e.error);
    // one of the person's own replaces the built-in of that name
    auto same = std::ranges::find_if(m_all, [&](const EffectSpec& x) { return x.id == e.id; });
    if (same != m_all.end()) *same = std::move(e);
    else m_all.push_back(std::move(e));
  }
  // warps, colours, lights; the built-in order within each
  std::ranges::stable_sort(m_all, [](const EffectSpec& a, const EffectSpec& b) { return a.stage < b.stage; });
  m_stamp = stamp();
}

std::string EffectLibrary::stamp() const {
  std::string s;
  std::error_code ec;
  if (m_dir.empty()) return s;
  for (const auto& de : fs::directory_iterator(m_dir, ec)) {
    if (de.path().extension() != ".glsl") continue;
    const auto t = fs::last_write_time(de.path(), ec);
    s += std::format("{}:{}:{};", de.path().filename().string(), t.time_since_epoch().count(), de.file_size(ec));
  }
  return s;
}

bool EffectLibrary::refresh() {
  if (stamp() == m_stamp) return false;
  load(m_dir);
  return true;
}

const EffectSpec* EffectLibrary::find(const std::string& id) const {
  auto it = std::ranges::find_if(m_all, [&](const EffectSpec& e) { return e.id == id; });
  return it == m_all.end() ? nullptr : &*it;
}

void EffectLibrary::markBroken(const std::string& id, uint64_t hash, const std::string& why) { m_broken[id] = {hash, why}; }

std::string EffectLibrary::brokenWhy(const EffectSpec& e) const {
  if (!e.error.empty()) return e.error;
  auto it = m_broken.find(e.id);
  return it != m_broken.end() && it->second.first == e.hash ? it->second.second : std::string();
}

std::string userEffectsDir() { return (fs::path(Config::defaultPath()).parent_path() / "effects").string(); }

EffectLibrary& effectLibrary() {
  static EffectLibrary lib = [] {
    EffectLibrary l;
    l.load(userEffectsDir());
    return l;
  }();
  return lib;
}

std::vector<const EffectSpec*> enabledEffects(const toml::table& opts) {
  std::vector<const EffectSpec*> on;
  const EffectLibrary& lib = effectLibrary();
  for (const EffectSpec& e : lib.all())
    if (opts[effectKey(e.id)].value_or(false) && lib.brokenWhy(e).empty()) on.push_back(&e);
  return on;
}

EffectCode effectCode(const std::vector<const EffectSpec*>& fx) {
  EffectCode code;
  for (size_t i = 0; i < fx.size(); ++i) {
    const EffectSpec& e = *fx[i];
    const std::string pre = std::format("fx{}_", i);
    std::string err;
    const std::vector<Tok> toks = lex(e.body);
    std::set<std::string> names = topLevelNames(toks, err);
    for (const auto& p : e.params) names.insert(p.key);
    // its own three (strength, where, the line) can never meet an option's name
    const std::string own = std::format("fxo{}_", i);
    code.decls += std::format("// {}\nuniform float {}strength;\nuniform int {}mask;\nuniform float {}split;\n", e.id, own, own, own);
    for (const auto& p : e.params) code.decls += std::format("uniform {} {}{};\n", glslType(p.kind), pre, p.key);
    code.decls += rename(toks, names, pre) + "\n";
    const std::string mask = std::format("fxMask({0}mask, {0}split, uv) * {0}strength", own);
    if (e.stage == EffectSpec::Warp)
      code.warps += std::format("    {{ float m = {}; if (m > 0.0) uv = mix(uv, {}warp(uv), m); }}\n", mask, pre);
    else if (e.stage == EffectSpec::Colour)
      code.colours += std::format("    {{ float m = {}; if (m > 0.0) c = mix(c, {}color(c, uv), m); }}\n", mask, pre);
    else  // laid over what the lights before it laid
      code.lights += std::format("    {{ float m = {}; if (m > 0.0) {{ vec4 l = clamp({}light(uv), 0.0, 1.0) * m; "
                                 "L = vec4(L.rgb * (1.0 - l.a) + l.rgb, 1.0 - (1.0 - L.a) * (1.0 - l.a)); }} }}\n",
                                 mask, pre);
  }
  return code;
}

std::vector<PropSpec> effectRows(const toml::table& opts, bool all) {
  using K = PropSpec::Kind;
  std::vector<PropSpec> rows;
  PropSpec head;
  head.kind = K::Header;
  head.key = "_effects";
  head.labelEn = "Animated effects";
  head.labelEs = "Efectos animados";
  rows.push_back(head);
  const EffectLibrary& lib = effectLibrary();
  bool any = false;
  for (const EffectSpec& e : lib.all()) {
    const std::string key = effectKey(e.id);
    const bool on = opts[key].value_or(false);
    PropSpec sw{K::Bool, key, e.nameEn, e.nameEs, {}, 0, 1, 1, 0};
    if (!lib.brokenWhy(e).empty()) sw.labelEn += " (error)", sw.labelEs += " (error)";
    rows.push_back(sw);
    if (!on && !all) continue;
    any = true;
    auto row = [](PropSpec p) {
      p.indent = true;
      return p;
    };
    rows.push_back(row({K::Number, effectKey(e.id, "strength"), "Strength", "Intensidad", {}, 0, 2, 0.05, 1}));
    for (const EffectParam& p : e.params) {
      PropSpec r;
      r.key = effectKey(e.id, p.key);
      r.labelEn = p.labelEn;
      r.labelEs = p.labelEs;
      r.indent = true;
      switch (p.kind) {
      case EffectParam::Number:
        r.kind = K::Number;
        r.min = p.min, r.max = p.max, r.step = p.step, r.def = p.def, r.integer = p.integer;
        break;
      case EffectParam::Bool:
        r.kind = K::Bool;
        r.min = 0, r.max = 1, r.step = 1, r.def = p.def;
        break;
      case EffectParam::Choice:
        r.kind = K::Enum;
        r.options = p.options;
        r.optionNames = p.optionNames;
        r.defText = p.defText;
        break;
      case EffectParam::Color:
        r.kind = K::Color;
        r.defText = p.defText;
        break;
      }
      rows.push_back(std::move(r));
    }
    PropSpec where{K::Enum, effectKey(e.id, "mask"), "Where", "Dónde", effectMaskModes(), 0, 0, 0, 0, "all"};
    where.optionNames = {{"Everywhere", "En todo"}, {"Background", "Solo el fondo"}, {"Subject", "Solo la figura"},
                         {"Above a line", "Arriba de línea"}, {"Below a line", "Abajo de línea"},
                         {"Painted 1", "Pintura 1"}, {"Painted 2", "Pintura 2"}, {"Painted 3", "Pintura 3"}, {"Painted 4", "Pintura 4"}};
    rows.push_back(row(where));
    const std::string m = opts[effectKey(e.id, "mask")].value_or(std::string("all"));
    if (all || m == "above" || m == "below")
      rows.push_back(row({K::Number, effectKey(e.id, "split"), "Line height", "Altura de línea", {}, 0, 1, 0.01, 0.5}));
    if (all || m.starts_with("paint")) {
      // the brush, open on that mask
      PropSpec paint{K::Action, effectKey(e.id, "paint"), "Paint the mask…", "Pintar la máscara…"};
      paint.defText = m;
      rows.push_back(row(paint));
    }
  }
  if (any) rows.push_back({K::Number, "fx_fps", "Effects FPS", "FPS de efectos", {}, 10, 60, 5, 30, "", true});
  return rows;
}

const std::vector<std::string>& effectMaskModes() {
  static const std::vector<std::string> modes = {"all", "background", "subject", "above", "below", "paint1", "paint2", "paint3", "paint4"};
  return modes;
}

std::vector<uint8_t> effectNoise() {
  std::vector<uint8_t> px(256 * 256 * 4);
  uint32_t s = 0x9e3779b9U;
  for (auto& b : px) {
    s = s * 1664525U + 1013904223U;
    b = static_cast<uint8_t>(s >> 24);
  }
  return px;
}

}  // namespace undershell
