// SPDX-License-Identifier: GPL-3.0-or-later
// Where the windows are, for widgets that walk on them (the pet): Umbriel's
// `umbriel subscribe windows` streams the whole list as a JSON line each time
// it changes. Started only while some widget asks and the umbriel command is
// there; on other compositors there are no windows to tell, and nothing runs.
#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

namespace undershell {

struct WindowBox {
  std::string output;
  double x = 0, y = 0, w = 0, h = 0;  // global layout coordinates, logical px
};

// the windows a pet could stand on in one `windows` event (visible, not
// minimised, not full screen, not in the scratchpad); false: not that event
bool parseWindowsEvent(const std::string& line, std::vector<WindowBox>& out);

class WindowFeed {
public:
  WindowFeed() = default;
  WindowFeed(const WindowFeed&) = delete;
  WindowFeed& operator=(const WindowFeed&) = delete;
  ~WindowFeed() { stop(); }
  void want(bool on);  // starts or stops the stream
  [[nodiscard]] int fd() const { return m_fd; }
  bool dispatch();     // reads what came; true when the list changed
  [[nodiscard]] const std::vector<WindowBox>& windows() const { return m_windows; }

private:
  void stop();
  int m_fd = -1;
  pid_t m_pid = -1;
  bool m_tried = false;
  std::string m_buf;
  std::vector<WindowBox> m_windows;
};

}  // namespace undershell
