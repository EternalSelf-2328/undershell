// SPDX-License-Identifier: GPL-3.0-or-later
#include "filepicker.hpp"

#include "common.hpp"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <glib.h>
#include <systemd/sd-bus.h>
#include <sys/wait.h>
#include <unistd.h>

namespace undershell {

namespace {
constexpr const char* kPortal = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
}  // namespace

FilePicker::~FilePicker() {
  closeBus();
  if (m_out >= 0) close(m_out);
  if (m_child > 0) {
    kill(m_child, SIGTERM);
    waitpid(m_child, nullptr, 0);
  }
}

bool FilePicker::open(Request req, Done done) {
  if (busy()) return false;
  m_req = std::move(req);
  m_done = std::move(done);
  m_finished = m_fallback = false;
  m_paths.clear();
  m_error.clear();
  if (!openPortal() && !spawnFallback()) m_finished = true, m_error = "no file chooser";
  if (m_finished) {  // nothing to wait for: report now
    Done d = std::move(m_done);
    m_done = nullptr;
    closeBus();
    d({}, m_error);
  }
  return true;
}

int FilePicker::fd() const {
  if (m_out >= 0) return m_out;
  return m_bus ? sd_bus_get_fd(m_bus) : -1;
}

void FilePicker::closeBus() {
  if (m_callSlot) m_callSlot = sd_bus_slot_unref(m_callSlot);
  if (m_matchSlot) m_matchSlot = sd_bus_slot_unref(m_matchSlot);
  if (m_bus) m_bus = sd_bus_flush_close_unref(m_bus);
}

// ── the portal ──────────────────────────────────────────────────────────────

bool FilePicker::openPortal() {
  if (sd_bus_open_user(&m_bus) < 0) {
    m_bus = nullptr;
    return false;
  }
  // the reply names an object for the dialog, but the signal may come first:
  // listen before asking, at the path the portal will use for this token
  static unsigned serial = 0;
  const std::string token = "undershell" + std::to_string(getpid()) + "_" + std::to_string(++serial);
  const char* unique = nullptr;
  if (sd_bus_get_unique_name(m_bus, &unique) < 0 || !unique) {
    closeBus();
    return false;
  }
  std::string sender = unique + 1;  // ":1.42" → "1_42"
  for (char& c : sender)
    if (c == '.') c = '_';
  m_expected = std::string(kPortalPath) + "/request/" + sender + "/" + token;
  m_handle.clear();
  if (sd_bus_match_signal_async(m_bus, &m_matchSlot, kPortal, nullptr, "org.freedesktop.portal.Request", "Response",
                                &FilePicker::response, nullptr, this) < 0) {
    closeBus();
    return false;
  }

  sd_bus_message* msg = nullptr;
  int r = sd_bus_message_new_method_call(m_bus, &msg, kPortal, kPortalPath, "org.freedesktop.portal.FileChooser", "OpenFile");
  if (r >= 0) r = sd_bus_message_append(msg, "ss", "", m_req.title.c_str());
  if (r >= 0) r = sd_bus_message_open_container(msg, 'a', "{sv}");
  if (r >= 0) r = sd_bus_message_append(msg, "{sv}", "handle_token", "s", token.c_str());
  if (r >= 0) r = sd_bus_message_append(msg, "{sv}", "multiple", "b", 1);
  if (r >= 0 && m_req.folders) r = sd_bus_message_append(msg, "{sv}", "directory", "b", 1);
  if (r >= 0 && !m_req.folders && !m_req.mimes.empty()) {
    // filters a(sa(us)): (name, [(0 = glob | 1 = mime type, pattern)])
    r = sd_bus_message_open_container(msg, 'e', "sv");
    if (r >= 0) r = sd_bus_message_append(msg, "s", "filters");
    if (r >= 0) r = sd_bus_message_open_container(msg, 'v', "a(sa(us))");
    if (r >= 0) r = sd_bus_message_open_container(msg, 'a', "(sa(us))");
    if (r >= 0) r = sd_bus_message_open_container(msg, 'r', "sa(us)");
    if (r >= 0) r = sd_bus_message_append(msg, "s", m_req.filterName.c_str());
    if (r >= 0) r = sd_bus_message_open_container(msg, 'a', "(us)");
    for (const std::string& m : m_req.mimes)
      if (r >= 0) r = sd_bus_message_append(msg, "(us)", 1u, m.c_str());
    if (r >= 0) r = sd_bus_message_close_container(msg);
    if (r >= 0) r = sd_bus_message_close_container(msg);
    if (r >= 0 && !m_req.allName.empty()) r = sd_bus_message_append(msg, "(sa(us))", m_req.allName.c_str(), 1, 0u, "*");
    if (r >= 0) r = sd_bus_message_close_container(msg);
    if (r >= 0) r = sd_bus_message_close_container(msg);
    if (r >= 0) r = sd_bus_message_close_container(msg);
  }
  if (r >= 0 && !m_req.startDir.empty()) {  // a byte string, NUL included
    r = sd_bus_message_open_container(msg, 'e', "sv");
    if (r >= 0) r = sd_bus_message_append(msg, "s", "current_folder");
    if (r >= 0) r = sd_bus_message_open_container(msg, 'v', "ay");
    if (r >= 0) r = sd_bus_message_append_array(msg, 'y', m_req.startDir.c_str(), m_req.startDir.size() + 1);
    if (r >= 0) r = sd_bus_message_close_container(msg);
    if (r >= 0) r = sd_bus_message_close_container(msg);
  }
  if (r >= 0) r = sd_bus_message_close_container(msg);
  if (r >= 0) r = sd_bus_call_async(m_bus, &m_callSlot, msg, &FilePicker::callReply, this, 0);
  sd_bus_message_unref(msg);
  if (r >= 0) r = sd_bus_flush(m_bus);  // on its way now, not at the next wakeup
  if (r < 0) {
    US_WARN("file chooser: could not ask the portal ({})", r);
    closeBus();
    return false;
  }
  return true;
}

int FilePicker::callReply(sd_bus_message* m, void* self, sd_bus_error*) {
  auto* p = static_cast<FilePicker*>(self);
  if (sd_bus_message_is_method_error(m, nullptr)) {
    // no portal, or none with a file chooser: the old way
    const sd_bus_error* e = sd_bus_message_get_error(m);
    US_INFO("file chooser: no portal ({}), trying zenity/kdialog", e && e->name ? e->name : "?");
    p->m_fallback = true;
    return 0;
  }
  const char* handle = nullptr;
  if (sd_bus_message_read(m, "o", &handle) >= 0 && handle) p->m_handle = handle;
  return 0;
}

int FilePicker::response(sd_bus_message* m, void* self, sd_bus_error*) {
  auto* p = static_cast<FilePicker*>(self);
  const char* path = sd_bus_message_get_path(m);
  if (!path || (path != p->m_expected && path != p->m_handle) || p->m_finished) return 0;
  uint32_t code = 2;
  if (sd_bus_message_read(m, "u", &code) < 0) return 0;
  std::vector<std::string> paths;
  if (code == 0 && sd_bus_message_enter_container(m, 'a', "{sv}") > 0) {
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
      const char* key = nullptr;
      sd_bus_message_read(m, "s", &key);
      if (key && std::string_view(key) == "uris" && sd_bus_message_enter_container(m, 'v', "as") > 0) {
        if (sd_bus_message_enter_container(m, 'a', "s") > 0) {
          const char* uri = nullptr;
          while (sd_bus_message_read(m, "s", &uri) > 0)
            if (uri)
              if (std::string f = pathFromUri(uri); !f.empty()) paths.push_back(f);
          sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
      } else {
        sd_bus_message_skip(m, "v");
      }
      sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
  }
  // 1: cancelled; 2: ended some other way (no dialog could be shown)
  p->m_finished = true;
  p->m_paths = std::move(paths);
  if (code == 2) p->m_error = "the file chooser failed";
  return 0;
}

std::string FilePicker::pathFromUri(const std::string& uri) {
  gchar* f = g_filename_from_uri(uri.c_str(), nullptr, nullptr);
  std::string out = f ? f : "";
  g_free(f);
  return out;
}

// ── zenity, kdialog ─────────────────────────────────────────────────────────

bool FilePicker::spawnFallback() {
  std::string dir = m_req.startDir;
  if (!dir.empty() && dir.back() != '/') dir += '/';
  std::string globs;
  for (const std::string& p : m_req.patterns) globs += (globs.empty() ? "" : " ") + p;
  const std::vector<std::vector<std::string>> tries = {
      [&] {
        std::vector<std::string> a = {"zenity", "--file-selection", "--multiple", "--separator=\n", "--title=" + m_req.title};
        if (m_req.folders) a.push_back("--directory");
        if (!dir.empty()) a.push_back("--filename=" + dir);
        if (!m_req.folders && !globs.empty()) {
          a.push_back("--file-filter=" + m_req.filterName + " | " + globs);
          if (!m_req.allName.empty()) a.push_back("--file-filter=" + m_req.allName + " | *");
        }
        return a;
      }(),
      [&] {
        std::vector<std::string> a = {"kdialog", "--title", m_req.title};
        if (m_req.folders) {
          a.insert(a.end(), {"--getexistingdirectory", dir.empty() ? std::string(".") : dir});
        } else {
          a.insert(a.end(), {"--multiple", "--separate-output", "--getopenfilename", dir.empty() ? std::string(".") : dir});
          if (!globs.empty()) a.push_back(globs + "|" + m_req.filterName);
        }
        return a;
      }(),
  };
  for (const auto& args : tries) {
    std::vector<char*> argv;
    for (const std::string& s : args) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);
    GPid pid = 0;
    gint out = -1;
    const auto flags = static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL |
                                                G_SPAWN_CLOEXEC_PIPES);
    if (!g_spawn_async_with_pipes(nullptr, argv.data(), nullptr, flags, nullptr, nullptr, &pid, nullptr, &out, nullptr, nullptr))
      continue;  // not installed
    fcntl(out, F_SETFL, fcntl(out, F_GETFL) | O_NONBLOCK);
    m_child = pid;
    m_out = out;
    m_output.clear();
    return true;
  }
  return false;
}

// ── the main loop ───────────────────────────────────────────────────────────

void FilePicker::dispatch() {
  if (m_bus) {
    while (sd_bus_process(m_bus, nullptr) > 0) {
    }
    if (m_fallback) {  // the portal said no: free it, then the old way
      m_fallback = false;
      closeBus();
      if (!spawnFallback()) m_finished = true, m_error = "no file chooser";
    }
  }
  if (m_out >= 0) {
    char buf[4096];
    for (;;) {
      const ssize_t n = read(m_out, buf, sizeof(buf));
      if (n > 0) {
        m_output.append(buf, static_cast<size_t>(n));
        continue;
      }
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) break;
      // the end: it closed its output, so it is exiting
      close(m_out);
      m_out = -1;
      int status = 0;
      waitpid(m_child, &status, 0);
      m_child = -1;
      std::vector<std::string> paths;
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {  // 1 = cancelled
        size_t at = 0;
        while (at < m_output.size()) {
          size_t nl = m_output.find('\n', at);
          if (nl == std::string::npos) nl = m_output.size();
          std::string line = m_output.substr(at, nl - at);
          if (!line.empty() && line.back() == '\r') line.pop_back();
          if (!line.empty()) paths.push_back(line);
          at = nl + 1;
        }
      }
      m_finished = true;
      m_paths = std::move(paths);
      break;
    }
  }
  if (m_finished && m_done) {
    closeBus();
    Done d = std::move(m_done);
    m_done = nullptr;
    m_finished = false;
    d(std::move(m_paths), std::move(m_error));
    m_paths.clear();
    m_error.clear();
  }
}

}  // namespace undershell
