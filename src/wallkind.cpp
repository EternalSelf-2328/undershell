// SPDX-License-Identifier: GPL-3.0-or-later
#include "wallkind.hpp"

#include "common.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

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

std::map<std::string, std::string> skwdPaths(const std::string& json) {
  std::map<std::string, std::string> out;
  static const std::regex kOutput(R"(\{[^{}]*\})");
  static const std::regex kName(R"re("name"\s*:\s*"([^"]*)")re");
  static const std::regex kPath(R"re("path"\s*:\s*"((?:[^"\\]|\\.)*)")re");
  static const std::regex kConnected(R"re("connected"\s*:\s*(true|false))re");
  for (auto it = std::sregex_iterator(json.begin(), json.end(), kOutput); it != std::sregex_iterator(); ++it) {
    const std::string obj = it->str();
    std::smatch name, path, conn;
    if (!std::regex_search(obj, name, kName) || !std::regex_search(obj, path, kPath)) continue;
    if (std::regex_search(obj, conn, kConnected) && conn[1] == "false") continue;
    std::string p;  // JSON escapes: \" \\ \/ (paths need no others)
    const std::string raw = path[1].str();
    for (size_t i = 0; i < raw.size(); ++i) p += raw[i] == '\\' && i + 1 < raw.size() ? raw[++i] : raw[i];
    if (!p.empty() && p[0] == '/' && !name[1].str().empty()) out[name[1].str()] = p;
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
