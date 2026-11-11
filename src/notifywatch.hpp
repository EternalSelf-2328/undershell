// SPDX-License-Identifier: GPL-3.0-or-later
// The desktop's notifications, as they are sent: a monitor on the session
// bus (org.freedesktop.DBus.Monitoring.BecomeMonitor) that sees every
// org.freedesktop.Notifications.Notify call on its way to the shell that
// shows it. Nothing is taken over: Noctalia (or whoever owns the name) still
// shows and answers them; a pet only hears that one came.
//
// It runs on a thread of its own, started by the first one who asks, and
// keeps the last few notes, numbered, so each listener reads what is new
// for it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace undershell {

struct Notice {
  std::uint64_t seq = 0;
  std::string app, summary, body;
  int urgency = 1;  // 0 low, 1 normal, 2 critical
};

class NotifyWatch {
public:
  static NotifyWatch& instance();
  // start listening (once); false if the session bus cannot be reached
  bool start();
  // the notes after `cursor` (and the cursor moved past them)
  std::vector<Notice> since(std::uint64_t& cursor);
  // a note of one's own, as if it had come from the bus (the tests, `msg pet notify`)
  void inject(const Notice& n);

private:
  NotifyWatch() = default;
  void loop();
};

}  // namespace undershell
