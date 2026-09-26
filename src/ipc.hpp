// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>

namespace undershell {

// A line-based unix socket at $XDG_RUNTIME_DIR/undershell.sock.
class IpcServer {
public:
  using Handler = std::function<std::string(const std::string&)>;
  ~IpcServer();
  bool listen(Handler handler);
  [[nodiscard]] int fd() const { return m_fd; }
  void dispatch();  // accept + serve one pending client
  static std::string socketPath();

private:
  int m_fd = -1;
  Handler m_handler;
};

// Client side: sends one command, prints the reply. Returns the exit code.
int ipcSend(const std::string& command);

}  // namespace undershell
