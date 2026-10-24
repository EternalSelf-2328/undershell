// SPDX-License-Identifier: GPL-3.0-or-later
// Wallpaper Engine wallpapers, read (see wescene.hpp). The field names were
// read from the scenes themselves (Fondos-Animados/02 in the author's notes).
#include "wescene.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace undershell {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::vector<std::string> steamLibraries() {
  std::vector<std::string> libs;
  const char* home = std::getenv("HOME");
  std::vector<fs::path> roots = {expandHome("~/.local/share/Steam"), expandHome("~/.steam/steam"),
                                 expandHome("~/.var/app/com.valvesoftware.Steam/.local/share/Steam")};
  (void)home;
  std::error_code ec;
  for (const fs::path& root : roots) {
    const std::string vdf = readFile((root / "steamapps/libraryfolders.vdf").string());
    static const std::regex kPath(R"re("path"\s+"([^"]+)")re");
    for (auto it = std::sregex_iterator(vdf.begin(), vdf.end(), kPath); it != std::sregex_iterator(); ++it) {
      std::string p = (*it)[1].str();
      for (size_t i = 0; (i = p.find("\\\\", i)) != std::string::npos;) p.erase(i, 1);
      if (std::ranges::find(libs, p) == libs.end()) libs.push_back(p);
    }
    if (fs::is_directory(root / "steamapps", ec) && std::ranges::find(libs, root.string()) == libs.end()) libs.push_back(root.string());
  }
  return libs;
}

json readJson(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return json();
  std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (s.size() >= 3 && s.compare(0, 3, "\xEF\xBB\xBF") == 0) s.erase(0, 3);  // a BOM
  return json::parse(s, nullptr, false, true);
}

// a field's value: itself, or the "value" of one bound to a user property
// (the property's own value when the project has it)
json valueOf(const json& v, const WeProject& project) {
  if (!v.is_object()) return v;
  if (v.contains("user") && v["user"].is_string()) {
    auto it = project.properties.find(v["user"].get<std::string>());
    if (it != project.properties.end()) {
      const std::string& s = it->second;
      if (s == "true") return true;
      if (s == "false") return false;
      char* end = nullptr;
      const double d = std::strtod(s.c_str(), &end);
      if (end && *end == '\0' && !s.empty()) return d;
      return s;
    }
  }
  return v.contains("value") ? v["value"] : json();
}

std::vector<float> floats(const json& v) {
  std::vector<float> out;
  if (v.is_number()) out.push_back(v.get<float>());
  else if (v.is_string()) {
    std::istringstream in(v.get<std::string>());
    float f = 0;
    while (in >> f) out.push_back(f);
  } else if (v.is_array()) {
    for (const auto& x : v)
      if (x.is_number()) out.push_back(x.get<float>());
  }
  return out;
}

float floatOf(const json& v, float def) {
  const auto f = floats(v);
  return f.empty() ? def : f[0];
}

// a point of an object (its parents' transforms applied), scene units, y up
std::array<float, 2> worldPoint(const WeScene& scene, int parent, float x, float y) {
  for (int depth = 0; parent >= 0 && depth < 32; ++depth) {
    const WeLayer* p = nullptr;
    for (const WeLayer& l : scene.layers)
      if (l.id == parent) p = &l;
    if (!p) break;
    const float c = std::cos(p->angle), s = std::sin(p->angle), sx = x * p->sx, sy = y * p->sy;
    x = p->ox + sx * c - sy * s, y = p->oy + sx * s + sy * c;
    parent = p->parent;
  }
  return {x, y};
}

}  // namespace

// One particle object of a scene, into undershell's particle terms: units of
// a 1080-high scene with y down (k = 1080 / height). The field names were
// read from the scenes' own files.
template <typename Locate, typename TexFile>
bool weParticle(const json& o, const WeProject& project, WeScene& out, Locate& locate, TexFile& texFile, ParticleDef& d) {
  const json p = readJson(locate(o["particle"].get<std::string>()));
  if (!p.is_object()) return false;
  const float H = out.height, W = out.width, k = 1080 / H, aspect = W / H;
  json io = o.value("instanceoverride", json::object());
  if (!io.is_object()) io = json::object();
  auto over = [&](const char* key) {
    const auto f = io.is_object() && io.contains(key) ? floats(valueOf(io[key], project)) : std::vector<float>();
    return f.empty() ? 1.0F : f[0];
  };
  float sc = 1;
  if (const auto s = floats(valueOf(o.value("scale", json()), project)); !s.empty()) sc = std::max(0.01F, std::abs(s[0]));
  const float ks = k * sc;  // distances and sizes follow the object's scale
  std::array<float, 2> origin{0, 0};
  if (const auto v = floats(valueOf(o.value("origin", json()), project)); v.size() >= 2) origin = {v[0], v[1]};
  origin = worldPoint(out, o.value("parent", -1), origin[0], origin[1]);
  d.id = "we" + std::to_string(o.value("id", 0));
  d.nameEn = d.nameEs = o.value("name", d.id);
  d.hash = std::hash<std::string>{}(p.dump() + io.dump());
  d.max = static_cast<int>(std::clamp(p.value("maxcount", 100.0F) * over("count"), 1.0F, 5000.0F));
  d.prewarm = std::clamp(p.value("starttime", 0.0F), 0.0F, 30.0F);
  // what it is drawn with: the material's texture, and its blending
  const json mat = readJson(locate(p.value("material", std::string())));
  const json pass = mat.is_object() && mat.contains("passes") && mat["passes"].is_array() && !mat["passes"].empty() ? mat["passes"][0] : json();
  if (!pass.is_object()) return false;
  // a refracting system bends what is behind it through a normal map: not a sprite to draw
  if (const json combos = pass.value("combos", json::object()); combos.is_object() && combos.value("REFRACT", 0) != 0) return false;
  d.additive = pass.value("blending", std::string()) == "additive";
  const json texs = pass.value("textures", json::array());
  if (!texs.is_array() || texs.empty() || !texs[0].is_string()) return false;
  d.sprite = ParticleDef::Texture;
  d.texture = "materials/" + texs[0].get<std::string>();
  if (!texFile(d.texture + ".tex") && std::ranges::find(out.needs, d.texture + ".tex") == out.needs.end()) out.needs.push_back(d.texture + ".tex");
  // a sprite sheet: its frames, from the texture's description
  if (const json tj = readJson(locate(d.texture + ".tex-json")); tj.is_object() && tj.contains("spritesheetsequences")) {
    const json& seq = tj["spritesheetsequences"];
    if (seq.is_array() && !seq.empty() && seq[0].is_object()) {
      d.frames = std::clamp(seq[0].value("frames", 1), 1, 1024);
      d.sheetTime = std::max(0.05F, seq[0].value("duration", 1.0F));
      // the grid comes from the cell size against the picture's (read when loaded); a square guess until then
      d.cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(d.frames)))));
      d.rows = (d.frames + d.cols - 1) / d.cols;
      d.cellW = seq[0].value("width", 0), d.cellH = seq[0].value("height", 0);
    }
  }
  // the emitter
  json em = p.contains("emitter") && p["emitter"].is_array() && !p["emitter"].empty() ? p["emitter"][0] : json::object();
  if (!em.is_object()) em = json::object();
  const std::string emitter = em.value("name", std::string("sphererandom"));
  std::array<float, 2> eo{0, 0};
  if (const auto v = floats(em.value("origin", json())); v.size() >= 2) eo = {v[0] * sc, v[1] * sc};
  const float ex = (origin[0] + eo[0]) * k, ey = (H - origin[1] - eo[1]) * k;
  d.ox = ex / (1080 * aspect), d.oy = ey / 1080;
  d.rate = std::clamp(em.value("rate", 5.0F) * over("rate"), 0.0F, 5000.0F);
  const auto dist = floats(em.value("distancemax", json(0.0)));
  if (emitter == "boxrandom") {
    d.sphere = false;
    const float bx = dist.empty() ? 0 : dist[0], by = dist.size() > 1 ? dist[1] : bx;
    d.sx = 2 * bx * ks / (1080 * aspect), d.sy = 2 * by * ks / 1080;
  } else {
    d.sphere = true;
    d.radius = (dist.empty() ? 0 : dist[0]) * ks / 1080;
    d.radiusMin = floatOf(em.value("distancemin", json(0.0)), 0) * ks / 1080;
    const float lo = em.value("speedmin", 0.0F), hi = em.value("speedmax", lo);
    d.radial = {lo * ks, hi * ks};
  }
  // where each starts from
  const float speed = over("speed"), size = over("size"), life = over("lifetime"), alpha = over("alpha");
  for (const json& in : p.value("initializer", json::array())) {
    if (!in.is_object()) continue;
    const std::string n = in.value("name", std::string());
    auto lohi = [&](float defLo, float defHi) {
      return std::array<float, 2>{floatOf(in.value("min", json(defLo)), defLo), floatOf(in.value("max", json(defHi)), defHi)};
    };
    if (n == "lifetimerandom") d.life = {lohi(1, 1)[0] * life, lohi(1, 1)[1] * life};
    else if (n == "sizerandom") d.size = {lohi(20, 20)[0] * ks * size, lohi(20, 20)[1] * ks * size};
    else if (n == "alpharandom") d.alpha = {std::clamp(lohi(1, 1)[0] * alpha, 0.0F, 1.0F), std::clamp(lohi(1, 1)[1] * alpha, 0.0F, 1.0F)};
    else if (n == "colorrandom") {
      auto col = [](const json& j) {
        auto f = floats(j);
        while (f.size() < 3) f.push_back(255);
        const float div = std::max({f[0], f[1], f[2]}) > 1.001F ? 255.0F : 1.0F;
        return Color{f[0] / div, f[1] / div, f[2] / div, 1};
      };
      d.c0 = col(in.value("min", json("255 255 255")));
      d.c1 = col(in.value("max", json("255 255 255")));
    } else if (n == "velocityrandom") {
      const auto lo = floats(in.value("min", json("0 0 0"))), hi = floats(in.value("max", json("0 0 0")));
      if (lo.size() >= 2 && hi.size() >= 2) {
        d.velBox = true;  // y flips: up in the scene is down here
        d.vel = {lo[0] * ks * speed, -hi[1] * ks * speed, hi[0] * ks * speed, -lo[1] * ks * speed};
      }
    } else if (n == "rotationrandom") {
      const auto lo = floats(in.value("min", json(0.0))), hi = floats(in.value("max", json(0.0)));
      const float a = lo.empty() ? 0 : lo.back(), b = hi.empty() ? 0 : hi.back();
      d.rot = {a * 57.29578F, b * 57.29578F};
    } else if (n == "angularvelocityrandom") {
      const auto lo = floats(in.value("min", json(0.0))), hi = floats(in.value("max", json(0.0)));
      d.spin = {(lo.empty() ? 0 : -lo.back()) * 57.29578F, (hi.empty() ? 0 : -hi.back()) * 57.29578F};
    } else if (n == "turbulentvelocityrandom") {
      d.jitter = {in.value("speedmin", 0.0F) * ks * speed, in.value("speedmax", 0.0F) * ks * speed};
    }
  }
  if (const auto c = floats(valueOf(io.value("colorn", json()), project)); c.size() >= 3)
    for (Color* col : {&d.c0, &d.c1}) col->r *= c[0], col->g *= c[1], col->b *= c[2];
  // what changes as they live
  for (const json& op : p.value("operator", json::array())) {
    if (!op.is_object()) continue;
    const std::string n = op.value("name", std::string());
    ParticleOp x;
    if (n == "movement") {
      if (const auto g = floats(op.value("gravity", json("0 0 0"))); g.size() >= 2 && (g[0] != 0 || g[1] != 0)) {
        x.type = ParticleOp::Gravity;
        x.v = {g[0] * ks, -g[1] * ks};
        d.ops.push_back(x);
      }
      if (const float drag = op.value("drag", 0.0F); drag > 0) {
        x = {};
        x.type = ParticleOp::Drag;
        x.v[0] = drag;
        d.ops.push_back(x);
      }
    } else if (n == "alphafade") {
      x.type = ParticleOp::Fade;
      x.v = {std::clamp(op.value("fadeintime", 0.0F), 0.0F, 1.0F), std::clamp(op.value("fadeouttime", 0.0F), 0.0F, 1.0F)};
      d.ops.push_back(x);
    } else if (n == "sizechange") {
      x.type = ParticleOp::SizeChange;
      const float a = floatOf(op.value("startvalue", json(1.0)), 1), b = floatOf(op.value("endvalue", json(1.0)), 1);
      x.v[0] = a > 0.001F ? b / a : b;
      d.ops.push_back(x);
    } else if (n == "colorchange") {
      x.type = ParticleOp::ColorChange;
      auto f = floats(op.value("endvalue", json("1 1 1")));
      while (f.size() < 3) f.push_back(1);
      const float div = std::max({f[0], f[1], f[2]}) > 1.001F ? 255.0F : 1.0F;
      x.color = Color{f[0] / div, f[1] / div, f[2] / div, 1};
      d.ops.push_back(x);
    } else if (n == "oscillatealpha" || n == "oscillateposition") {
      x.type = n == "oscillatealpha" ? ParticleOp::OscAlpha : ParticleOp::OscPosition;
      const float amount = op.value("scalemax", n == "oscillatealpha" ? 0.5F : 10.0F);
      x.v = {op.value("frequencymin", 0.5F), op.value("frequencymax", 1.0F),
             n == "oscillatealpha" ? std::clamp(amount, 0.0F, 1.0F) : amount * ks};
      d.ops.push_back(x);
    } else if (n == "turbulence") {
      x.type = ParticleOp::Turbulence;
      x.v = {std::max(0.0001F, op.value("scale", 0.005F) / ks), op.value("timescale", 1.0F), op.value("speedmax", 50.0F) * ks};
      d.ops.push_back(x);
    } else if (n == "vortex") {
      x.type = ParticleOp::Vortex;
      x.v = {d.ox, d.oy, op.value("speedouter", 50.0F) * ks};
      d.ops.push_back(x);
    }
  }
  // drawn as sprites, or stretched along their travel
  for (const json& r : p.value("renderer", json::array())) {
    if (!r.is_object()) continue;
    const std::string n = r.value("name", std::string());
    if (n == "spritetrail") d.streak = std::clamp(r.value("length", 0.05F), 0.0F, 0.5F);
    else if (n == "ropetrail") d.streak = 0.08F;
  }
  return true;
}

const WeProject* weProjectCached(const std::string& idOrPath) {
  static std::map<std::string, std::pair<std::string, WeProject>> cache;  // id -> (stamp, project)
  const std::string dir = weWallpaperDir(idOrPath);
  if (dir.empty()) return nullptr;
  std::error_code ec;
  const auto t = fs::last_write_time(fs::path(dir) / "project.json", ec);
  const std::string stamp = dir + std::to_string(t.time_since_epoch().count());
  auto& slot = cache[idOrPath];
  if (slot.first != stamp) {
    std::string why;
    slot.second = {};
    if (!readWeProject(dir, slot.second, why)) return nullptr;
    slot.first = stamp;
  }
  return &slot.second;
}

std::string weOverride(const WeProperty& p, const toml::table& opts) {
  const auto node = opts[p.key];
  if (!node) return {};
  if (p.type == "bool") return node.value_or(false) ? "true" : "false";
  if (p.type == "slider") {
    const double v = node.value_or(0.0);
    return std::format("{}", std::clamp(v, p.min, p.max));
  }
  const std::string s = node.value_or(std::string());
  if (p.type == "color" && s.size() == 7 && s[0] == '#') {
    const Color c = Color::fromHex(s);
    return std::format("{:.4f} {:.4f} {:.4f}", c.r, c.g, c.b);
  }
  return s;
}

std::vector<std::string> weWorkshopDirs() {
  std::vector<std::string> dirs;
  std::error_code ec;
  for (const auto& lib : steamLibraries()) {
    const fs::path d = fs::path(lib) / "steamapps/workshop/content/431960";
    if (fs::is_directory(d, ec)) dirs.push_back(d.string());
  }
  return dirs;
}

std::string weAssetsDir() {
  std::error_code ec;
  for (const auto& lib : steamLibraries()) {
    const fs::path d = fs::path(lib) / "steamapps/common/wallpaper_engine/assets";
    if (fs::is_directory(d, ec)) return d.string();
  }
  return {};
}

std::string weWallpaperDir(const std::string& idOrPath) {
  std::error_code ec;
  static const std::regex kId("^[0-9]{1,20}$");
  if (std::regex_match(idOrPath, kId)) {
    for (const auto& d : weWorkshopDirs())
      if (fs::is_directory(fs::path(d) / idOrPath, ec)) return (fs::path(d) / idOrPath).string();
    return {};
  }
  const fs::path p = fs::path(expandHome(idOrPath));
  return fs::is_directory(p, ec) && fs::exists(p / "project.json", ec) ? fs::canonical(p, ec).string() : std::string();
}

bool readWeProject(const std::string& folder, WeProject& out, std::string& why) {
  const json j = readJson(fs::path(folder) / "project.json");
  if (!j.is_object()) {
    why = "no readable project.json";
    return false;
  }
  out = {};
  std::string type = j.value("type", std::string());
  std::ranges::transform(type, type.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  out.type = type;
  out.title = j.value("title", std::string());
  out.file = j.value("file", std::string());
  // a name from the project: one file inside its folder, nothing else
  auto inside = [&](const std::string& name) {
    if (name.empty() || name.find("..") != std::string::npos || name[0] == '/') return std::string();
    std::error_code ec;
    const fs::path p = fs::path(folder) / name;
    return fs::is_regular_file(p, ec) ? p.string() : std::string();
  };
  out.preview = inside(j.value("preview", std::string()));
  if (out.type == "video") {
    out.file = inside(out.file);
    if (out.file.empty()) {
      why = "the video file is missing";
      return false;
    }
  }
  const json props = j.contains("general") && j["general"].is_object() ? j["general"].value("properties", json::object()) : json::object();
  if (props.is_object())
    for (const auto& [name, p] : props.items()) {
      if (!p.is_object() || !p.contains("value")) continue;
      const json& v = p["value"];
      if (v.is_boolean()) out.properties[name] = v.get<bool>() ? "true" : "false";
      else if (v.is_number()) out.properties[name] = std::format("{}", v.get<double>());
      else if (v.is_string()) out.properties[name] = v.get<std::string>();
      // the ones the inspector can show
      WeProperty wp;
      wp.name = name;
      wp.type = p.value("type", std::string());
      if (wp.type != "color" && wp.type != "bool" && wp.type != "slider" && wp.type != "combo") continue;
      wp.key = "we_";
      for (const unsigned char c : name) wp.key += std::isalnum(c) ? static_cast<char>(std::tolower(c)) : '_';
      if (wp.key.size() > 60) continue;
      // its label: the author's text without markup, or a stock key made readable
      std::string label = p.value("text", name);
      std::string plain;
      for (bool tag = false; const char c : label) {
        if (c == '<') tag = true;
        else if (c == '>') tag = false;
        else if (!tag) plain += c;
      }
      if (plain.rfind("ui_browse_properties_", 0) == 0) plain = plain.substr(21);
      if (plain.rfind("ui_", 0) == 0) plain = plain.substr(3);
      if (plain.find(' ') == std::string::npos) std::ranges::replace(plain, '_', ' ');
      while (!plain.empty() && std::isspace(static_cast<unsigned char>(plain.back()))) plain.pop_back();
      wp.label = plain.empty() ? name : plain.substr(0, 40);
      wp.value = out.properties[name];
      wp.order = p.value("order", 0);
      if (wp.type == "slider") {
        wp.min = p.value("min", 0.0), wp.max = p.value("max", 1.0), wp.step = std::max(1e-4, p.value("step", 0.01));
        if (!(wp.max > wp.min)) continue;
      }
      if (wp.type == "combo") {
        for (const json& o : p.value("options", json::array()))
          if (o.is_object() && o.contains("value")) {
            const std::string val = o["value"].is_string() ? o["value"].get<std::string>() : o["value"].dump();
            wp.options.emplace_back(val, o.value("label", val));
          }
        if (wp.options.empty()) continue;
      }
      out.props.push_back(std::move(wp));
    }
  std::ranges::sort(out.props, [](const WeProperty& a, const WeProperty& b) { return a.order < b.order; });
  return true;
}

bool readWeScene(const std::string& unpacked, const std::string& assets, const WeProject& project, WeScene& out, std::string& why) {
  out = {};
  const fs::path dir = unpacked;
  json scene = readJson(dir / "scene.json");
  if (!scene.is_object()) scene = readJson(dir / "gifscene.json");
  if (!scene.is_object()) {
    why = "no readable scene.json";
    return false;
  }
  std::error_code ec;
  // a file of the scene, else of the assets; "" (and listed) when neither has it
  auto locate = [&](const std::string& rel) -> std::string {
    if (rel.empty() || rel.find("..") != std::string::npos || rel[0] == '/') return {};
    if (fs::exists(dir / rel, ec)) return (dir / rel).string();
    if (!assets.empty() && fs::exists(fs::path(assets) / rel, ec)) return (fs::path(assets) / rel).string();
    if (std::ranges::find(out.needs, rel) == out.needs.end()) out.needs.push_back(rel);
    return {};
  };
  auto texFile = [&](const std::string& rel) {  // any of the forms undershell-scene writes
    for (const char* ext : {".usimg", ".png", ".jpg", ".mp4"}) {
      if (fs::exists(dir / (rel + ext), ec)) return true;
      if (!assets.empty() && fs::exists(fs::path(assets) / (rel + ext), ec)) return true;
    }
    return false;
  };
  json general = scene.value("general", json::object());
  if (!general.is_object()) general = json::object();
  if (general.contains("orthogonalprojection") && general["orthogonalprojection"].is_object()) {
    const json& op = general["orthogonalprojection"];
    out.width = std::clamp(op.value("width", 1920.0F), 16.0F, 16384.0F);
    out.height = std::clamp(op.value("height", 1080.0F), 16.0F, 16384.0F);
  }
  if (const auto c = floats(valueOf(general.value("clearcolor", json()), project)); c.size() >= 3) out.clear = Color{c[0], c[1], c[2], 1};
  auto flag = [&](const char* key) {
    const json v = valueOf(general.value(key, json(false)), project);
    return v.is_boolean() && v.get<bool>();
  };
  auto number = [&](const char* key, float def) { return floatOf(valueOf(general.value(key, json(def)), project), def); };
  out.parallax = flag("cameraparallax");
  out.parallaxAmount = std::clamp(number("cameraparallaxamount", 0.5F), 0.0F, 4.0F);
  out.shake = flag("camerashake");
  out.shakeAmplitude = std::clamp(number("camerashakeamplitude", 0.5F), 0.0F, 4.0F);
  out.shakeRoughness = std::clamp(number("camerashakeroughness", 1), 0.0F, 4.0F);
  out.shakeSpeed = std::clamp(number("camerashakespeed", 3), 0.0F, 20.0F);
  out.bloom = flag("bloom");
  out.bloomStrength = std::clamp(number("bloomstrength", 2), 0.0F, 8.0F);
  out.bloomThreshold = std::clamp(number("bloomthreshold", 0.65F), 0.0F, 0.99F);
  for (const json& o : scene.value("objects", json::array())) {
    if (!o.is_object()) continue;
    if (o.contains("particle") && o["particle"].is_string()) {
      WeLayer node;
      node.id = o.value("id", -1);
      node.parent = o.value("parent", -1);
      node.visible = false;
      node.compose = true;
      const json v = valueOf(o.value("visible", json(true)), project);
      ParticleDef def;
      std::string why;
      if ((!v.is_boolean() || v.get<bool>()) && weParticle(o, project, out, locate, texFile, def)) {
        node.particle = static_cast<int>(out.particles.size());
        out.particles.push_back(std::move(def));
      } else {
        ++out.skipped;
      }
      if (const auto p = floats(valueOf(o.value("origin", json()), project)); p.size() >= 2) node.ox = p[0], node.oy = p[1];
      out.layers.push_back(node);
      continue;
    }
    if (o.contains("text")) {  // a text object: drawn with undershell's text, its script run apart
      WeLayer t;
      t.isText = true;
      t.compose = false;
      t.id = o.value("id", -1);
      t.parent = o.value("parent", -1);
      t.name = o.value("name", std::string());
      if (const auto v = floats(valueOf(o.value("origin", json()), project)); v.size() >= 2) t.ox = v[0], t.oy = v[1];
      if (const auto v = floats(valueOf(o.value("scale", json()), project)); v.size() >= 2) t.sx = v[0], t.sy = v[1];
      if (const auto v = floats(valueOf(o.value("angles", json()), project)); v.size() >= 3) t.angle = v[2];
      if (const json v = valueOf(o.value("visible", json(true)), project); v.is_boolean()) t.visible = v.get<bool>();
      t.alpha = std::clamp(floatOf(valueOf(o.value("alpha", json(1.0)), project), 1), 0.0F, 1.0F);
      if (const auto c = floats(valueOf(o.value("color", json()), project)); c.size() >= 3) t.color = Color{c[0], c[1], c[2], 1};
      t.pointsize = std::clamp(floatOf(valueOf(o.value("pointsize", json(32.0)), project), 32), 1.0F, 1000.0F);
      if (const json a = o.value("horizontalalign", json()); a.is_string()) t.halign = a.get<std::string>();
      if (const json a = o.value("verticalalign", json()); a.is_string()) t.valign = a.get<std::string>();
      // "systemfont_arial" names a system font; a font of the scene's own falls back to the system's
      const std::string font = o.value("font", std::string());
      if (font.rfind("systemfont_", 0) == 0) {
        t.font = font.substr(11);
        if (!t.font.empty()) t.font[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t.font[0])));
      }
      const json tx = o["text"];
      if (tx.is_string()) t.text = tx.get<std::string>();
      else if (tx.is_object()) {
        const json v = valueOf(tx, project);
        if (v.is_string()) t.text = v.get<std::string>();
        if (tx.contains("script") && tx["script"].is_string()) t.script = tx["script"].get<std::string>();
        json props = json::object();
        if (tx.contains("scriptproperties") && tx["scriptproperties"].is_object())
          for (const auto& [k, pv] : tx["scriptproperties"].items()) props[k] = valueOf(pv, project);
        t.scriptProps = props.dump();
      }
      out.layers.push_back(std::move(t));
      continue;
    }
    if (!o.contains("image") || !o["image"].is_string()) {
      ++out.skipped;
      // a parent that is not drawn still places its children
      WeLayer node;
      node.id = o.value("id", -1);
      node.parent = o.value("parent", -1);
      if (const auto v = floats(valueOf(o.value("origin", json()), project)); v.size() >= 2) node.ox = v[0], node.oy = v[1];
      if (const auto v = floats(valueOf(o.value("scale", json()), project)); v.size() >= 2) node.sx = v[0], node.sy = v[1];
      if (const auto v = floats(valueOf(o.value("angles", json()), project)); v.size() >= 3) node.angle = v[2];
      node.visible = false;
      node.compose = true;
      out.layers.push_back(node);
      continue;
    }
    WeLayer l;
    l.id = o.value("id", -1);
    l.parent = o.value("parent", -1);
    l.name = o.value("name", std::string());
    if (const auto v = floats(valueOf(o.value("origin", json()), project)); v.size() >= 2) l.ox = v[0], l.oy = v[1];
    if (const auto v = floats(valueOf(o.value("scale", json()), project)); v.size() >= 2) l.sx = v[0], l.sy = v[1];
    if (const auto v = floats(valueOf(o.value("angles", json()), project)); v.size() >= 3) l.angle = v[2];
    if (const auto v = floats(valueOf(o.value("size", json()), project)); v.size() >= 2) l.w = v[0], l.h = v[1];
    if (const json a = valueOf(o.value("alignment", json()), project); a.is_string()) l.align = a.get<std::string>();
    if (const json v = valueOf(o.value("visible", json(true)), project); v.is_boolean()) l.visible = v.get<bool>();
    l.alpha = std::clamp(floatOf(valueOf(o.value("alpha", json(1.0)), project), 1), 0.0F, 1.0F);
    if (const auto v = floats(valueOf(o.value("parallaxDepth", json()), project)); v.size() >= 2) l.depthX = v[0], l.depthY = v[1];
    l.brightness = std::clamp(floatOf(valueOf(o.value("brightness", json(1.0)), project), 1), 0.0F, 10.0F);
    if (const auto c = floats(valueOf(o.value("color", json()), project)); c.size() >= 3) l.color = Color{c[0], c[1], c[2], 1};
    // its model, then the model's material, then the material's first texture
    const json model = readJson(locate(o["image"].get<std::string>()));
    const std::string matPath = model.is_object() ? model.value("material", std::string()) : std::string();
    const json mat = matPath.empty() ? json() : readJson(locate(matPath));
    const json pass = mat.is_object() && mat.contains("passes") && mat["passes"].is_array() && !mat["passes"].empty() ? mat["passes"][0] : json();
    if (pass.is_object()) {
      l.additive = pass.value("blending", std::string()) == "additive";
      const json texs = pass.value("textures", json::array());
      // "_rt_…" names a render target (what is drawn behind it): an effect layer
      if (texs.is_array() && !texs.empty() && texs[0].is_string() && !texs[0].get<std::string>().empty() &&
          texs[0].get<std::string>().rfind("_rt_", 0) != 0) {
        l.texture = "materials/" + texs[0].get<std::string>();
        // a plain white or black canvas under effects is what the effects
        // draw on (audio bars, generated shapes): nothing to show by itself
        const bool effects = o.contains("effects") && o["effects"].is_array() && !o["effects"].empty();
        if (effects && l.texture.rfind("materials/util/", 0) == 0) l.compose = true;
        if (!texFile(l.texture + ".tex")) {
          if (std::ranges::find(out.needs, l.texture + ".tex") == out.needs.end()) out.needs.push_back(l.texture + ".tex");
        }
      } else {
        l.compose = true;
      }
    } else if (model.is_object() || !matPath.empty()) {
      l.compose = true;  // no material at hand (yet)
    } else {
      l.compose = true;
    }
    // its effects
    for (const json& e : o.value("effects", json::array())) {
      if (!e.is_object()) continue;
      if (const json v = valueOf(e.value("visible", json(true)), project); v.is_boolean() && !v.get<bool>()) continue;
      ++out.effects;
      const std::string file = e.value("file", std::string());
      const std::string name = file.rfind("effects/", 0) == 0 ? file.substr(8, file.find('/', 8) - 8) : std::string();
      const json pass = e.contains("passes") && e["passes"].is_array() && !e["passes"].empty() ? e["passes"][0] : json::object();
      const json csv = pass.is_object() ? pass.value("constantshadervalues", json::object()) : json::object();
      const json texs = pass.is_object() ? pass.value("textures", json::array()) : json::array();
      auto num = [&](const char* key, float def) {
        const auto f = csv.is_object() && csv.contains(key) ? floats(valueOf(csv[key], project)) : std::vector<float>();
        return f.empty() ? def : f[0];
      };
      auto vec = [&](const char* key) {
        return csv.is_object() && csv.contains(key) ? floats(valueOf(csv[key], project)) : std::vector<float>();
      };
      auto slot = [&](size_t i) -> std::string {  // a mask the effect brings, found or asked for
        if (!texs.is_array() || i >= texs.size() || !texs[i].is_string() || texs[i].get<std::string>().empty()) return {};
        const std::string t = "materials/" + texs[i].get<std::string>();
        if (t.find("/util/") != std::string::npos) return {};  // a stock placeholder, not a painted mask
        if (!texFile(t + ".tex") && std::ranges::find(out.needs, t + ".tex") == out.needs.end()) out.needs.push_back(t + ".tex");
        return t;
      };
      auto deg = [](float rad) { return static_cast<double>(rad * 57.29578F); };
      WeLayer::Fx f;
      auto put = [&](const char* id, std::initializer_list<std::pair<const char*, double>> opts, std::string mask, bool flow = false) {
        f.options.insert_or_assign(std::string("fx_") + id, true);
        for (const auto& [k, v] : opts) f.options.insert_or_assign(std::string("fx_") + id + "_" + k, v);
        f.mask = std::move(mask);
        f.flowMap = flow;
        l.fx.push_back(std::move(f));
        ++out.mapped;
      };
      // the numbers are scaled so Wallpaper Engine's defaults land on undershell's
      if (name == "opacity") {
        l.opacity *= std::clamp(num("alpha", 1), 0.0F, 1.0F);
        if (l.opacityMask.empty()) l.opacityMask = slot(1);
        ++out.mapped;
      } else if (name == "tint") {
        if (const auto c = vec("color"); c.size() >= 3) l.tint = Color{c[0], c[1], c[2], 1};
        l.tintAmount = std::clamp(num("alpha", 1), 0.0F, 1.0F);
        l.tintMask = slot(1);
        ++out.mapped;
      } else if (name == "waterwaves") {
        put("waves", {{"speed", num("speed", 5) / 5}, {"size", 200 / std::max(1.0F, num("scale", 200))}, {"strength", num("strength", 0.1F) / 0.1},
                      {"direction", deg(num("direction", 0))}}, slot(1));
      } else if (name == "waterripple") {
        put("ripple", {{"speed", num("animationspeed", 0.15F) / 0.15}, {"size", 1 / std::max(0.1F, num("scale", 1))},
                       {"strength", num("ripplestrength", 0.1F) / 0.1}}, slot(1));
      } else if (name == "foliagesway") {
        put("sway", {{"speed", num("speeduv", 5) / 5}, {"size", 0.05 / std::max(0.005F, num("scale", 0.05F))}, {"strength", num("strength", 0.4F) / 0.4},
                     {"gusts", 0.3}}, slot(1));
      } else if (name == "shake") {
        put("sway", {{"speed", num("speed", 1)}, {"size", 1.5}, {"strength", num("strength", 0.1F) / 0.1}, {"gusts", 0}}, slot(1));
      } else if (name == "waterflow") {
        put("flow", {{"speed", num("speed", 1)}, {"strength", num("strength", 1)}, {"size", 1}}, slot(1), true);
      } else if (name == "pulse") {
        put("pulse", {{"strength", num("amount", 1)}}, slot(1));
      } else if (name == "shine") {
        put("shine", {{"angle", deg(num("direction", 0))}, {"width", std::clamp(num("raylength", 0.1F), 0.02F, 0.6F)},
                      {"strength", num("rayintensity", 1)}}, slot(1));
      } else if (name == "godrays" || name == "lightshafts") {
        const auto c = vec("center");
        put("godrays", {{"x", c.size() >= 2 ? c[0] : 0.5}, {"y", c.size() >= 2 ? c[1] : 0.1}, {"reach", std::clamp(num("raylength", 0.5F), 0.1F, 1.0F)},
                        {"threshold", std::clamp(num("raythreshold", 0.5F), 0.0F, 0.95F)}, {"strength", num("rayintensity", 1)}}, slot(1));
      } else if (name == "filmgrain") {
        put("grain", {{"amount", std::clamp(num("strength", 0.5F), 0.0F, 1.0F)}}, slot(1));
      } else if (name == "vhs") {
        put("vhs", {{"snow", std::clamp(num("strength", 0.5F), 0.0F, 1.0F)}}, slot(1));
      } else if (name == "scroll") {
        put("scroll", {{"speed_x", num("speedx", 0.2F)}, {"speed_y", -num("speedy", 0.2F)}}, std::string());
      } else {
        ++l.unmapped;
      }
    }
    out.layers.push_back(std::move(l));
  }
  return true;
}

std::array<float, 8> weLayerQuad(const WeScene& scene, const WeLayer& layer, float texW, float texH) {
  const float w = layer.w > 0 ? layer.w : texW, h = layer.h > 0 ? layer.h : texH;
  // where the origin sits on the layer: its centre, an edge or a corner
  const std::string& a = layer.align;
  float x0 = -w / 2, y0 = -h / 2;  // bottom-left corner relative to the origin (y up)
  if (a.find("left") != std::string::npos) x0 = 0;
  else if (a.find("right") != std::string::npos) x0 = -w;
  if (a.find("bottom") != std::string::npos) y0 = 0;
  else if (a.find("top") != std::string::npos) y0 = -h;
  std::array<std::array<float, 2>, 4> pts = {{{x0, y0 + h}, {x0 + w, y0 + h}, {x0, y0}, {x0 + w, y0}}};
  // its own transform, then each parent's (by id), at most 32 deep
  const WeLayer* node = &layer;
  for (int depth = 0; node && depth < 32; ++depth) {
    const float c = std::cos(node->angle), s = std::sin(node->angle);
    for (auto& p : pts) {
      const float x = p[0] * node->sx, y = p[1] * node->sy;
      p = {node->ox + x * c - y * s, node->oy + x * s + y * c};
    }
    const WeLayer* parent = nullptr;
    if (node->parent >= 0)
      for (const WeLayer& l : scene.layers)
        if (l.id == node->parent && &l != node) parent = &l;
    node = parent;
  }
  return {pts[0][0], pts[0][1], pts[1][0], pts[1][1], pts[2][0], pts[2][1], pts[3][0], pts[3][1]};
}

}  // namespace undershell
