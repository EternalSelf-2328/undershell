// SPDX-License-Identifier: GPL-3.0-or-later
#include "wallkind.hpp"

#include <algorithm>
#include <regex>

namespace undershell {

bool skwdShowsMotion(const std::string& json, const std::vector<std::string>& outputs) {
  // one {...} per output; only the fields we need, no JSON library required
  static const std::regex kOutput(R"(\{[^{}]*\})");
  static const std::regex kName(R"re("name"\s*:\s*"([^"]*)")re");
  static const std::regex kType(R"re("type"\s*:\s*"([^"]*)")re");
  static const std::regex kConnected(R"re("connected"\s*:\s*(true|false))re");
  for (auto it = std::sregex_iterator(json.begin(), json.end(), kOutput); it != std::sregex_iterator(); ++it) {
    const std::string obj = it->str();
    std::smatch name, type, conn;
    if (!std::regex_search(obj, type, kType)) continue;
    if (std::regex_search(obj, conn, kConnected) && conn[1] == "false") continue;
    if (!outputs.empty() && std::regex_search(obj, name, kName) &&
        std::find(outputs.begin(), outputs.end(), name[1].str()) == outputs.end())
      continue;
    if (type[1] == "video" || type[1] == "we") return true;
  }
  return false;
}

bool isVideoPath(const std::string& path) {
  const size_t dot = path.rfind('.');
  if (dot == std::string::npos) return false;
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (const char* v : {"mp4", "webm", "mkv", "mov", "avi", "m4v", "gif", "ogv"})
    if (ext == v) return true;
  return false;
}

}  // namespace undershell
