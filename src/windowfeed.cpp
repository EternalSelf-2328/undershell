// SPDX-License-Identifier: GPL-3.0-or-later
#include "windowfeed.hpp"

#include "common.hpp"
#include "wallset.hpp"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <sys/wait.h>
#include <unistd.h>

namespace undershell {

bool parseWindowsEvent(const std::string& line, std::vector<WindowBox>& out) {
  try {
    const auto j = nlohmann::json::parse(line);
    if (!j.is_object() || j.value("event", std::string()) != "windows" || !j.contains("data") || !j["data"].is_array()) return false;
    out.clear();
    for (const auto& w : j["data"]) {
      if (!w.is_object()) continue;
      auto b = [&](const char* k) { return w.contains(k) && w[k].is_boolean() && w[k].get<bool>(); };
      auto n = [&](const char* k) { return w.contains(k) && w[k].is_number() ? w[k].get<double>() : 0.0; };
      if (!b("visible") || b("minimized") || b("fullscreen") || b("scratchpad") || b("tab_hidden")) continue;
      WindowBox box;
      box.output = w.contains("output") && w["output"].is_string() ? w["output"].get<std::string>() : std::string();
      box.x = n("x"), box.y = n("y"), box.w = n("w"), box.h = n("h");
      if (box.w > 1 && box.h > 1) out.push_back(std::move(box));
    }
    return true;
  } catch (...) {
    return false;
  }
}

void WindowFeed::want(bool on) {
  if (!on) {
    stop();
    m_tried = false;
    return;
  }
  if (m_fd >= 0 || m_tried) return;
  m_tried = true;  // one try per want(true): no umbriel, no retries every frame
  const std::string umbriel = findProgram("umbriel");
  if (umbriel.empty()) return;
  int fds[2];
  if (pipe2(fds, O_CLOEXEC) != 0) return;
  const pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return;
  }
  if (pid == 0) {
    dup2(fds[1], STDOUT_FILENO);
    const int null = open("/dev/null", O_RDWR);
    if (null >= 0) dup2(null, STDERR_FILENO), dup2(null, STDIN_FILENO);
    execl(umbriel.c_str(), "umbriel", "subscribe", "windows", static_cast<char*>(nullptr));
    _exit(127);
  }
  close(fds[1]);
  fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
  m_fd = fds[0];
  m_pid = pid;
  US_INFO("windows: following them through umbriel (for the pet)");
}

void WindowFeed::stop() {
  if (m_fd >= 0) close(m_fd);
  m_fd = -1;
  if (m_pid > 0) {
    kill(m_pid, SIGTERM);
    waitpid(m_pid, nullptr, 0);
  }
  m_pid = -1;
  m_buf.clear();
  m_windows.clear();
}

bool WindowFeed::dispatch() {
  if (m_fd < 0) return false;
  char buf[16384];
  for (;;) {
    const ssize_t n = read(m_fd, buf, sizeof(buf));
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {  // umbriel went away: no windows to stand on
      stop();
      return true;
    }
    if (n < 0) break;
    m_buf.append(buf, static_cast<size_t>(n));
    if (m_buf.size() > (4u << 20)) m_buf.clear();
  }
  bool changed = false;
  for (size_t nl; (nl = m_buf.find('\n')) != std::string::npos;) {
    std::vector<WindowBox> w;
    if (parseWindowsEvent(m_buf.substr(0, nl), w)) {
      m_windows = std::move(w);
      changed = true;
    }
    m_buf.erase(0, nl + 1);
  }
  return changed;
}

}  // namespace undershell
