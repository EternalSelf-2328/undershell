// SPDX-License-Identifier: GPL-3.0-or-later
// The desktop's own file chooser, asked for without blocking the main loop:
// the XDG desktop portal over D-Bus (GTK's, KDE's or GNOME's dialog, whatever
// the desktop set up), else zenity or kdialog as a child process. One request
// at a time; poll fd() and call dispatch() when it is readable.
#pragma once

#include <functional>
#include <string>
#include <sys/types.h>
#include <vector>

struct sd_bus;
struct sd_bus_slot;
struct sd_bus_message;
struct sd_bus_error;

namespace undershell {

class FilePicker {
public:
  struct Request {
    bool folders = false;               // pick folders instead of files
    std::string title;
    std::string filterName;             // the files it shows first…
    std::vector<std::string> mimes;     // …by type (the portal)
    std::vector<std::string> patterns;  // …or by name ("*.gif"; zenity, kdialog)
    std::string allName;                // the second filter: every file
    std::string startDir;               // "" = the chooser's own choice
  };
  // the chosen paths (none when cancelled), or why no chooser could open
  using Done = std::function<void(std::vector<std::string> paths, std::string error)>;

  FilePicker() = default;
  ~FilePicker();
  FilePicker(const FilePicker&) = delete;
  FilePicker& operator=(const FilePicker&) = delete;

  // False (and `done` is never called) while another request is open.
  bool open(Request req, Done done);
  [[nodiscard]] bool busy() const { return static_cast<bool>(m_done); }
  [[nodiscard]] int fd() const;  // -1 when idle
  void dispatch();

  // file:// URI → local path ("" for anything else)
  static std::string pathFromUri(const std::string& uri);

private:
  bool openPortal();
  bool spawnFallback();  // zenity, then kdialog
  void finish(std::vector<std::string> paths, std::string error);
  void closeBus();
  static int callReply(sd_bus_message* m, void* self, sd_bus_error*);
  static int response(sd_bus_message* m, void* self, sd_bus_error*);

  Request m_req;
  Done m_done;
  // the portal
  sd_bus* m_bus = nullptr;
  sd_bus_slot* m_matchSlot = nullptr;
  sd_bus_slot* m_callSlot = nullptr;
  std::string m_expected, m_handle;  // the request's object path, as guessed and as returned
  bool m_fallback = false;           // the portal failed: try a child process
  // zenity / kdialog
  pid_t m_child = -1;
  int m_out = -1;
  std::string m_output;
  // a finished request, delivered at the end of dispatch()
  bool m_finished = false;
  std::vector<std::string> m_paths;
  std::string m_error;
};

}  // namespace undershell
