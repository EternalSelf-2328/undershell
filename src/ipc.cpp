// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc.hpp"

#include "common.hpp"

#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace undershell {

std::string IpcServer::socketPath() {
  const char* rt = std::getenv("XDG_RUNTIME_DIR");
  return std::string(rt && *rt ? rt : "/tmp") + "/undershell.sock";
}

IpcServer::~IpcServer() {
  if (m_fd >= 0) {
    close(m_fd);
    unlink(socketPath().c_str());
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
  return true;
}

void IpcServer::dispatch() {
  int c = accept4(m_fd, nullptr, nullptr, SOCK_CLOEXEC);
  if (c < 0) return;
  std::string cmd;
  char buf[512];
  pollfd p{c, POLLIN, 0};
  while (poll(&p, 1, 200) > 0) {
    ssize_t n = read(c, buf, sizeof(buf));
    if (n <= 0) break;
    cmd.append(buf, static_cast<size_t>(n));
    if (cmd.find('\n') != std::string::npos) break;
  }
  while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == ' ')) cmd.pop_back();
  std::string reply = m_handler ? m_handler(cmd) : "error";
  reply += "\n";
  (void)!write(c, reply.data(), reply.size());
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
