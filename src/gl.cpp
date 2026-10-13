// SPDX-License-Identifier: GPL-3.0-or-later
#include "gl.hpp"

#include "common.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace undershell {

const char* kQuadVertexShader = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
out vec2 v_uv;
void main() {
    v_uv = a_pos;
    gl_Position = vec4(a_pos.x * 2.0 - 1.0, 1.0 - a_pos.y * 2.0, 0.0, 1.0);
}
)";

static GLuint compile(GLenum type, const std::string& src, const char* label) {
  GLuint s = glCreateShader(type);
  const char* p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    std::array<char, 4096> log{};
    glGetShaderInfoLog(s, log.size(), nullptr, log.data());
    glDeleteShader(s);
    throw std::runtime_error(std::format("{} shader ({}): {}", label, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log.data()));
  }
  return s;
}

void Program::create(const std::string& vertex, const std::string& fragment, const char* label) {
  destroy();
  GLuint vs = compile(GL_VERTEX_SHADER, vertex, label);
  GLuint fs = compile(GL_FRAGMENT_SHADER, fragment, label);
  m_id = glCreateProgram();
  glAttachShader(m_id, vs);
  glAttachShader(m_id, fs);
  glBindAttribLocation(m_id, 0, "a_pos");
  glLinkProgram(m_id);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(m_id, GL_LINK_STATUS, &ok);
  if (!ok) {
    std::array<char, 4096> log{};
    glGetProgramInfoLog(m_id, log.size(), nullptr, log.data());
    glDeleteProgram(m_id);
    m_id = 0;
    throw std::runtime_error(std::format("{} link: {}", label, log.data()));
  }
}

void Program::destroy() {
  if (m_id) glDeleteProgram(m_id);
  m_id = 0;
}

void drawUnitQuad() {
  static constexpr std::array<GLfloat, 12> kQuad = {0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1};
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kQuad.data());
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(0);
}

// ── small shared utilities (declared in common.hpp) ─────────────────────────

std::string expandHome(std::string_view path) {
  if (!path.empty() && path[0] == '~') {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "") + std::string(path.substr(1));
  }
  return std::string(path);
}

std::string readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// the process's umask, read without changing it (umask() would, for a moment, for every thread)
static mode_t currentUmask() {
  static const mode_t mask = [] {
    std::ifstream f("/proc/self/status");
    for (std::string line; std::getline(f, line);)
      if (line.rfind("Umask:", 0) == 0) return static_cast<mode_t>(std::stoul(line.substr(6), nullptr, 8));
    return static_cast<mode_t>(022);
  }();
  return mask;
}

// A new file written beside the old one and renamed over it: readers see the
// old content or the new, never half. The temporary gets a fresh, unguessable
// name (it cannot be a link someone left there), keeps the old file's
// permissions (a new one gets the umask's), and reaches the disk before the
// rename.
bool writeFileAtomic(const std::string& path, const std::string& content) {
  std::string tmp = path + ".XXXXXX";
  const int fd = mkostemp(tmp.data(), O_CLOEXEC);
  if (fd < 0) return false;
  struct stat st{};
  const mode_t mode = stat(path.c_str(), &st) == 0 ? (st.st_mode & 07777) : (0666 & ~currentUmask());
  bool ok = fchmod(fd, mode) == 0;
  for (size_t done = 0; ok && done < content.size();) {
    const ssize_t n = write(fd, content.data() + done, content.size() - done);
    if (n <= 0) ok = false;
    else done += static_cast<size_t>(n);
  }
  ok = ok && fsync(fd) == 0;
  ok = close(fd) == 0 && ok;
  if (ok && std::rename(tmp.c_str(), path.c_str()) == 0) return true;
  unlink(tmp.c_str());
  return false;
}

// Runs `sh -c cmd`, returns stdout. Kills the child after timeoutSec.
std::string runCommand(const std::string& cmd, int timeoutSec) {
  int fds[2];
  if (pipe2(fds, O_CLOEXEC) != 0) return {};
  pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return {};
  }
  if (pid == 0) {
    dup2(fds[1], STDOUT_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) dup2(devnull, STDERR_FILENO);
    execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  close(fds[1]);
  std::string out;
  auto deadline = Clock::now() + std::chrono::seconds(timeoutSec);
  std::array<char, 8192> buf{};
  while (true) {
    int left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count());
    if (left <= 0) {
      kill(pid, SIGKILL);
      break;
    }
    pollfd p{fds[0], POLLIN, 0};
    int r = poll(&p, 1, left);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) {
      kill(pid, SIGKILL);
      break;
    }
    ssize_t n = read(fds[0], buf.data(), buf.size());
    if (n <= 0) break;
    out.append(buf.data(), static_cast<size_t>(n));
  }
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  return out;
}

}  // namespace undershell
