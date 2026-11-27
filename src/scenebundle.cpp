// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenebundle.hpp"

#include "common.hpp"
#include "notes.hpp"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <toml++/toml.hpp>

namespace undershell {
namespace {
namespace fs = std::filesystem;
constexpr std::string_view kManifest = "scene.toml";

class NewSceneDirectory {
public:
  explicit NewSceneDirectory(const fs::path& parent) {
    auto pattern = (parent / "undershell-scene-XXXXXX").string();
    if (!mkdtemp(pattern.data())) throw std::system_error(errno, std::generic_category(), "create scene folder");
    path = pattern;
  }
  ~NewSceneDirectory() {
    if (!finished) {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  }
  NewSceneDirectory(const NewSceneDirectory&) = delete;
  NewSceneDirectory& operator=(const NewSceneDirectory&) = delete;

  fs::path path;
  bool finished = false;
};

const toml::array& widgetsOf(const toml::table& document) {
  const auto* widgets = document["widget"].as_array();
  if (!widgets || widgets->empty()) throw std::runtime_error("the scene has no widgets");
  const auto supported = widgetTypes();
  for (const auto& widget : *widgets) {
    const auto* row = widget.as_table();
    if (!row) throw std::runtime_error("expected a widget table");
    if (std::ranges::find(supported, (*row)["type"].value_or(std::string("visualizer"))) == supported.end())
      throw std::runtime_error("unsupported widget type in scene");
  }
  return *widgets;
}

std::string widgetText(const toml::array& widgets) {
  toml::table document;
  document.insert("widget", widgets);
  std::ostringstream out;
  // Keep strings in the same one-line, double-quoted form used by the editor.
  out << toml::toml_formatter(document, toml::format_flags::allow_unicode_strings) << '\n';
  return out.str();
}

bool inAssets(const fs::path& p) {
  if (p.is_absolute() || p.empty() || *p.begin() != "assets" || p == "assets") return false;
  return std::ranges::none_of(p, [](const fs::path& part) { return part.empty() || part == ".." || part == "."; });
}

class AssetCopies {
public:
  AssetCopies(fs::path destination, fs::path source) : m_destination(std::move(destination)), m_source(std::move(source)) {}

  std::string emptyNote() {
    const fs::path path = m_destination / "assets" / ("note-" + std::to_string(++m_notes) + ".md");
    fs::create_directories(path.parent_path());
    if (!writeFileAtomic(path.string(), "")) throw std::runtime_error("could not create the scene note");
    return m_source.empty() ? path.lexically_relative(m_destination).string() : path.string();
  }

  std::string operator()(const std::string& value) {
    if (value.empty()) return {};
    fs::path from = expandHome(value);
    if (!m_source.empty()) {
      if (!inAssets(from)) throw std::runtime_error("scene asset must be inside assets/");
      from = m_source / from;
      // Do not follow a symlink to another folder or a user's other files.
      if (!inAssets(fs::canonical(from).lexically_relative(m_source)))
        throw std::runtime_error("asset escapes the scene folder");
    } else {
      from = fs::canonical(from);
    }
    if (const auto found = m_copies.find(from); found != m_copies.end()) return found->second;
    fs::path item = fs::path("assets") / std::to_string(m_copies.size() + 1) / from.filename();
    const fs::path to = m_destination / item;
    fs::create_directories(to.parent_path());
    if (fs::is_regular_file(from)) {
      fs::copy_file(from, to);
    } else if (fs::is_directory(from)) {
      fs::create_directory(to);
      for (const auto& entry : fs::directory_iterator(from)) {
        // Photo and media pickers read the files in the selected directory.
        // Preserve names and reject special files and links inside bundles.
        if (fs::is_regular_file(entry.symlink_status())) fs::copy_file(entry.path(), to / entry.path().filename());
      }
    } else {
      throw std::runtime_error("missing scene asset: " + from.filename().string());
    }

    const auto translated = m_source.empty() ? item.string() : to.string();
    m_copies.emplace(from, translated);
    return translated;
  }

private:
  fs::path m_destination, m_source;
  std::map<fs::path, std::string> m_copies;
  unsigned m_notes = 0;
};

void remapWidget(toml::table& options, AssetCopies& copy, bool importing) {
  const std::string type = options["type"].value_or(std::string("visualizer"));
  auto remap = [&](const char* key) {
    if (const auto value = options[key].value<std::string>(); value && !value->empty())
      options.insert_or_assign(key, copy(*value));
  };
  if (type == "image") {
    remap("file");
    remap("photo_folder");
  } else if (type == "now_playing") {
    remap("cover_file");
  } else if (type == "visualizer") {
    if (auto* files = options["tv_files"].as_array()) {
      for (size_t index = 0; index < files->size(); ++index) {
        if (auto* path = (*files)[index].as_string(); path && !path->get().empty()) path->get() = copy(path->get());
      }
    } else {
      remap("tv_files");
    }
  } else if (type == "text") {
    remap("data_file");
  } else if (type == "notes") {
    if (!importing && options["file"].value_or(std::string()).empty()) {
      const std::string path = NotesWidget::pathFor(options["id"].value_or(std::string()), "");
      if (fs::is_regular_file(path)) options.insert_or_assign("file", path);
    }
    if (options["file"].value_or(std::string()).empty()) options.insert_or_assign("file", copy.emptyNote());
    else remap("file");
  } else if (type == "scene") {
    // Workshop items are looked up in the importing user's Steam installation.
    const auto id = options["scene"].value_or(std::string());
    if (!id.empty() && !std::ranges::all_of(id, [](char ch) { return ch >= '0' && ch <= '9'; }))
      throw std::runtime_error("export a native composition or use a Workshop item ID");
  }
  // Machine-specific actions and services come from the receiving computer.
  options.erase("music_app");
  options.erase("output");
  if (type == "calendar") options.erase("ics");
}

}  // namespace

SceneBundle exportScene(const std::string& config, const std::string& destination) {
  toml::array widgets = widgetsOf(toml::parse(config));
  const fs::path parent = fs::canonical(expandHome(destination));
  NewSceneDirectory folder(parent);
  AssetCopies copy(folder.path, {});
  for (auto& widget : widgets) remapWidget(*widget.as_table(), copy, false);
  const auto text = widgetText(widgets);
  const std::string header = "[undershell_scene]\nversion = 1\n\n";
  if (!writeFileAtomic((folder.path / kManifest).string(), header + text))
    throw std::runtime_error("could not write the scene manifest");
  folder.finished = true;
  return {folder.path.string(), {}};
}

SceneBundle importScene(const std::string& scene, const std::string& destination) {
  const fs::path root = fs::canonical(expandHome(scene));
  const auto manifest = root / kManifest;
  if (!fs::is_regular_file(fs::symlink_status(manifest)) || fs::file_size(manifest) > 1024 * 1024)
    throw std::runtime_error("missing or invalid scene manifest");
  const auto config = toml::parse_file(manifest.string());
  if (config["undershell_scene"]["version"].value_or(0) != 1)
    throw std::runtime_error("unsupported scene version");
  toml::array widgets = widgetsOf(config);

  const fs::path parent = fs::absolute(destination);
  fs::create_directories(parent);
  NewSceneDirectory folder(parent);
  AssetCopies copy(folder.path, root);
  for (auto& widget : widgets) remapWidget(*widget.as_table(), copy, true);
  const auto text = widgetText(widgets);
  folder.finished = true;
  return {folder.path.string(), text};
}

}  // namespace undershell
