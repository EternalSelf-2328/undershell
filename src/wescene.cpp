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

}  // namespace

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
    }
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
  const json general = scene.value("general", json::object());
  if (general.contains("orthogonalprojection") && general["orthogonalprojection"].is_object()) {
    const json& op = general["orthogonalprojection"];
    out.width = std::clamp(op.value("width", 1920.0F), 16.0F, 16384.0F);
    out.height = std::clamp(op.value("height", 1080.0F), 16.0F, 16384.0F);
  }
  if (const auto c = floats(valueOf(general.value("clearcolor", json()), project)); c.size() >= 3) out.clear = Color{c[0], c[1], c[2], 1};
  for (const json& o : scene.value("objects", json::array())) {
    if (!o.is_object()) continue;
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
