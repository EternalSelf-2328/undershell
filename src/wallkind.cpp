// SPDX-License-Identifier: GPL-3.0-or-later
#include "wallkind.hpp"

#include "common.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace undershell {

namespace {
void collectOutputObjects(const nlohmann::json& node, std::vector<const nlohmann::json*>& out) {
  if (node.is_array()) {
    for (const auto& item : node) collectOutputObjects(item, out);
  } else if (node.is_object()) {
    if (node.contains("name") && (node.contains("type") || node.contains("path"))) {
      out.push_back(&node);
      return;
    }
    for (const auto& [k, v] : node.items()) collectOutputObjects(v, out);
  }
}
}  // namespace

bool skwdShowsMotion(const std::string& jsonText, const std::vector<std::string>& outputs) {
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(jsonText);
  } catch (const std::exception&) {
    return false;
  }
  std::vector<const nlohmann::json*> objs;
  collectOutputObjects(doc, objs);
  for (const nlohmann::json* obj : objs) {
    auto itType = obj->find("type");
    if (itType == obj->end() || !itType->is_string()) continue;
    if (auto itConn = obj->find("connected"); itConn != obj->end() && itConn->is_boolean() && !itConn->get<bool>())
      continue;
    if (!outputs.empty()) {
      if (auto itName = obj->find("name"); itName != obj->end() && itName->is_string() &&
          std::find(outputs.begin(), outputs.end(), itName->get<std::string>()) == outputs.end())
        continue;
    }
    const std::string& type = itType->get_ref<const std::string&>();
    if (type == "video" || type == "we") return true;
  }
  return false;
}

std::map<std::string, std::string> skwdPaths(const std::string& jsonText) {
  std::map<std::string, std::string> out;
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(jsonText);
  } catch (const std::exception&) {
    return out;
  }
  std::vector<const nlohmann::json*> objs;
  collectOutputObjects(doc, objs);
  for (const nlohmann::json* obj : objs) {
    auto itName = obj->find("name");
    auto itPath = obj->find("path");
    if (itName == obj->end() || !itName->is_string() || itPath == obj->end() || !itPath->is_string()) continue;
    if (auto itConn = obj->find("connected"); itConn != obj->end() && itConn->is_boolean() && !itConn->get<bool>())
      continue;
    const std::string& name = itName->get_ref<const std::string&>();
    const std::string& p = itPath->get_ref<const std::string&>();
    if (!p.empty() && p[0] == '/' && !name.empty()) out[name] = p;
  }
  return out;
}

SkwdEvents::~SkwdEvents() { close(); }

void SkwdEvents::close() {
  if (m_fd >= 0) ::close(m_fd);
  m_fd = -1;
  m_buf.clear();
}

void SkwdEvents::tick(double now) {
  if (m_fd >= 0 || now < m_retryAt) return;
  m_retryAt = now + 10;
  std::vector<std::string> paths;
  if (const char* env = std::getenv("SKWD_WALL_V2_SOCK"); env && *env) paths.emplace_back(env);
  if (const char* rt = std::getenv("XDG_RUNTIME_DIR"); rt && *rt) {
    paths.push_back(std::string(rt) + "/skwd-wall-v2/wall.sock");
    paths.push_back(std::string(rt) + "/undershell-wall/wall.sock");
  }
  for (const std::string& p : paths) {
    sockaddr_un a{};
    if (p.size() >= sizeof(a.sun_path)) continue;
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) continue;
    a.sun_family = AF_UNIX;
    std::memcpy(a.sun_path, p.c_str(), p.size());
    if (connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0) {
      ::close(fd);
      continue;
    }
    static const char kSubscribe[] = "{\"method\":\"subscribe\",\"params\":{},\"id\":1}\n";
    if (send(fd, kSubscribe, sizeof(kSubscribe) - 1, MSG_NOSIGNAL) != static_cast<ssize_t>(sizeof(kSubscribe) - 1)) {
      ::close(fd);
      continue;
    }
    m_fd = fd;
    US_INFO("wallpaper program: following its changes on {}", p);
    return;
  }
}

bool SkwdEvents::changes(const std::string& line) {
  for (const char* ev : {"\"skwd.wall.applied\"", "\"skwd.wall.cleared\"", "\"skwd.wall.outputs_changed\""})
    if (line.find(ev) != std::string::npos && line.find("\"event\"") != std::string::npos) return true;
  return false;
}

bool SkwdEvents::dispatch() {
  if (m_fd < 0) return false;
  bool changed = false;
  char buf[4096];
  for (;;) {
    const ssize_t n = recv(m_fd, buf, sizeof(buf), 0);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {  // the daemon went away
      close();
      return true;  // (what it drew may be gone too: ask again)
    }
    if (n < 0) break;
    m_buf.append(buf, static_cast<size_t>(n));
    if (m_buf.size() > (1u << 20)) m_buf.erase(0, m_buf.size() - 4096);  // a runaway line: keep the tail
  }
  for (size_t nl; (nl = m_buf.find('\n')) != std::string::npos;) {
    changed = changed || changes(m_buf.substr(0, nl));
    m_buf.erase(0, nl + 1);
  }
  return changed;
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
