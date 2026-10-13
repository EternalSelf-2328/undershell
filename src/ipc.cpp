// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc.hpp"

#include "common.hpp"

#include <algorithm>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace undershell {

// In the user's own runtime folder (0700). Never /tmp: there another user
// could take the name first and answer in undershell's place.
std::string IpcServer::socketPath() {
  const char* rt = std::getenv("XDG_RUNTIME_DIR");
  return (rt && *rt ? std::string(rt) : "/run/user/" + std::to_string(getuid())) + "/undershell.sock";
}

IpcServer::~IpcServer() {
  if (m_fd >= 0) {
    close(m_fd);
    struct stat st{};
    if (stat(socketPath().c_str(), &st) == 0 && st.st_ino == m_inode) unlink(socketPath().c_str());
  }
}

bool IpcServer::listen(Handler handler) {
  m_handler = std::move(handler);
  const std::string path = socketPath();
  // a live instance answers on the socket: refuse to start a second one
  if (ipcSend("ping") == 0) {
    US_ERROR("another undershell instance is already running");
    return false;
  }
  unlink(path.c_str());
  m_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (m_fd < 0) return false;
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
  if (bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(m_fd, 4) != 0) {
    US_ERROR("could not listen on {}: {}", path, std::strerror(errno));
    close(m_fd);
    m_fd = -1;
    return false;
  }
  struct stat st{};
  if (stat(path.c_str(), &st) == 0) m_inode = st.st_ino;
  return true;
}

void IpcServer::dispatch() {
  int c = accept4(m_fd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
  if (c < 0) return;
  // only this user's own programs (the folder is 0700 too)
  ucred cred{};
  socklen_t len = sizeof(cred);
  if (getsockopt(c, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0 || cred.uid != getuid()) {
    close(c);
    return;
  }
  // one line, at most 64 KiB, within a second: a slow or endless sender
  // cannot hold the main loop (and the widgets) up
  constexpr size_t kMax = 64 * 1024;
  const double until = nowSeconds() + 1.0;
  std::string cmd;
  char buf[4096];
  pollfd p{c, POLLIN, 0};
  bool ok = false;
  for (double left = 1.0; left > 0; left = until - nowSeconds()) {
    if (poll(&p, 1, static_cast<int>(left * 1000) + 1) <= 0) break;
    ssize_t n = read(c, buf, sizeof(buf));
    if (n <= 0) break;
    cmd.append(buf, static_cast<size_t>(n));
    if (cmd.find('\n') != std::string::npos) {
      cmd.resize(cmd.find('\n'));
      ok = true;
      break;
    }
    if (cmd.size() > kMax) break;
  }
  if (!ok && (cmd.empty() || cmd.size() > kMax)) {
    close(c);
    return;
  }
  while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == ' ')) cmd.pop_back();
  std::string reply = m_handler ? m_handler(cmd) : "error";
  reply += "\n";
  // written as the reader takes it, for a second at most
  size_t done = 0;
  pollfd w{c, POLLOUT, 0};
  while (done < reply.size() && poll(&w, 1, static_cast<int>(std::max(0.0, until + 1.0 - nowSeconds()) * 1000)) > 0) {
    const ssize_t n = write(c, reply.data() + done, reply.size() - done);
    if (n <= 0) break;
    done += static_cast<size_t>(n);
  }
  close(c);
}

int ipcSend(const std::string& command) {
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return 1;
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, IpcServer::socketPath().c_str(), sizeof(addr.sun_path) - 1);
  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(fd);
    return 2;
  }
  std::string line = command + "\n";
  (void)!write(fd, line.data(), line.size());
  std::string reply;
  char buf[512];
  pollfd p{fd, POLLIN, 0};
  while (poll(&p, 1, 2000) > 0) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n <= 0) break;
    reply.append(buf, static_cast<size_t>(n));
  }
  close(fd);
  if (command != "ping") std::fputs(reply.c_str(), stdout);
  return reply.rfind("error", 0) == 0 ? 1 : 0;
}

}  // namespace undershell
