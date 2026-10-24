// SPDX-License-Identifier: GPL-3.0-or-later
// The scene widget's side of undershell-script (see scripthost.hpp).
#include "scripthost.hpp"

#include "common.hpp"

#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>

#ifndef US_LIBEXEC_DIR
#define US_LIBEXEC_DIR "/usr/libexec"
#endif

namespace undershell {

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string ScriptHost::helperPath() {
  auto ok = [](const std::string& p) { return !p.empty() && access(p.c_str(), X_OK) == 0; };
  if (const char* env = std::getenv("UNDERSHELL_SCRIPT_HELPER"); env && *env) return ok(env) ? env : std::string();
  std::error_code ec;
  const fs::path self = fs::read_symlink("/proc/self/exe", ec);
  if (!ec && ok((self.parent_path() / "undershell-script").string())) return (self.parent_path() / "undershell-script").string();
  const std::string installed = std::string(US_LIBEXEC_DIR) + "/undershell-script";
  return ok(installed) ? installed : std::string();
}

ScriptHost::~ScriptHost() { stop(); }

void ScriptHost::stop() {
  if (m_in >= 0) close(m_in);
  if (m_out >= 0) close(m_out);
  m_in = m_out = -1;
  if (m_pid > 0) {
    kill(m_pid, SIGTERM);
    waitpid(m_pid, nullptr, 0);  // it ends at once: its input is closed
    g_spawn_close_pid(m_pid);
  }
  m_pid = 0;
}

bool ScriptHost::start() {
  const std::string helper = helperPath();
  if (helper.empty()) return false;
  const gchar* argv[] = {helper.c_str(), nullptr};
  GError* err = nullptr;
  const bool ok = g_spawn_async_with_pipes(nullptr, const_cast<gchar**>(argv), nullptr, static_cast<GSpawnFlags>(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL),
                                           nullptr, nullptr, &m_pid, &m_in, &m_out, nullptr, &err);
  if (!ok) {
    US_WARN("scene scripts: {}", err ? err->message : "the helper did not start");
    if (err) g_error_free(err);
    return false;
  }
  fcntl(m_out, F_SETFL, fcntl(m_out, F_GETFL) | O_NONBLOCK);
  fcntl(m_in, F_SETFD, FD_CLOEXEC);
  fcntl(m_out, F_SETFD, FD_CLOEXEC);
  signal(SIGPIPE, SIG_IGN);  // a helper that died must not take the widgets with it
  return true;
}

void ScriptHost::send(const std::string& line) {
  if (m_in < 0) return;
  std::string data = line + "\n";
  size_t off = 0;
  while (off < data.size()) {
    const ssize_t n = write(m_in, data.data() + off, data.size() - off);
    if (n <= 0) {
      stop();  // it went away
      return;
    }
    off += static_cast<size_t>(n);
  }
}

void ScriptHost::load(int id, const std::string& source, const std::string& value, const std::string& propsJson) {
  json msg = {{"op", "load"}, {"id", id}, {"source", source}, {"value", value}};
  if (const json p = json::parse(propsJson.empty() ? "{}" : propsJson, nullptr, false); p.is_object()) msg["props"] = p;
  send(msg.dump());
}

void ScriptHost::update(double runtime, double dt, double timeOfDay, const std::vector<int>& ids) {
  if (m_waiting || ids.empty()) return;
  send(json{{"op", "update"}, {"runtime", runtime}, {"dt", dt}, {"timeofday", timeOfDay}, {"ids", ids}}.dump());
  m_waiting = m_in >= 0;
}

bool ScriptHost::poll(std::map<int, std::string>& values) {
  if (m_out < 0) return false;
  char buf[8192];
  for (;;) {
    const ssize_t n = read(m_out, buf, sizeof buf);
    if (n > 0) m_buf.append(buf, static_cast<size_t>(n));
    if (n <= 0 || m_buf.size() > (4u << 20)) break;
  }
  bool changed = false;
  for (size_t nl; (nl = m_buf.find('\n')) != std::string::npos;) {
    const json msg = json::parse(m_buf.substr(0, nl), nullptr, false);
    m_buf.erase(0, nl + 1);
    if (!msg.is_object()) continue;
    if (msg.contains("values") && msg["values"].is_object()) {
      m_waiting = false;
      for (const auto& [k, v] : msg["values"].items())
        if (v.is_string()) {
          std::string& slot = values[std::atoi(k.c_str())];
          if (slot != v.get<std::string>()) slot = v.get<std::string>(), changed = true;
        }
    } else if (msg.contains("id") && msg.contains("value") && msg["value"].is_string()) {  // a load's first value
      values[msg.value("id", -1)] = msg["value"].get<std::string>();
      changed = true;
    }
  }
  return changed;
}

}  // namespace undershell
