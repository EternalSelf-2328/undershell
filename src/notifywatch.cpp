// SPDX-License-Identifier: GPL-3.0-or-later
// NotifyWatch (notifywatch.hpp): a session-bus monitor for Notify calls,
// set up as `busctl monitor` does (a monitor connection, then BecomeMonitor
// with one match rule), read on its own thread.
#include "notifywatch.hpp"

#include "common.hpp"

#include <systemd/sd-bus.h>

#include <atomic>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>

namespace undershell {

namespace {
std::mutex g_mutex;
std::deque<Notice> g_notes;
std::uint64_t g_seq = 0;
std::atomic<int> g_state{0};  // 0 not started, 1 listening, -1 failed

void push(Notice n) {
  std::lock_guard lock(g_mutex);
  n.seq = ++g_seq;
  g_notes.push_back(std::move(n));
  while (g_notes.size() > 16) g_notes.pop_front();
}

sd_bus* openMonitor() {
  sd_bus* bus = nullptr;
  if (sd_bus_new(&bus) < 0) return nullptr;
  sd_bus_set_monitor(bus, 1);
  sd_bus_set_bus_client(bus, 1);
  std::string addr;
  if (const char* a = std::getenv("DBUS_SESSION_BUS_ADDRESS"); a && *a) addr = a;
  else if (const char* r = std::getenv("XDG_RUNTIME_DIR"); r && *r) addr = std::string("unix:path=") + r + "/bus";
  if (addr.empty() || sd_bus_set_address(bus, addr.c_str()) < 0 || sd_bus_start(bus) < 0) {
    sd_bus_unref(bus);
    return nullptr;
  }
  sd_bus_message* m = nullptr;
  sd_bus_error err = SD_BUS_ERROR_NULL;
  sd_bus_message* reply = nullptr;
  int r = sd_bus_message_new_method_call(bus, &m, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus.Monitoring", "BecomeMonitor");
  if (r >= 0) r = sd_bus_message_open_container(m, 'a', "s");
  if (r >= 0) r = sd_bus_message_append(m, "s", "type='method_call',interface='org.freedesktop.Notifications',member='Notify'");
  if (r >= 0) r = sd_bus_message_close_container(m);
  if (r >= 0) r = sd_bus_message_append(m, "u", 0u);
  if (r >= 0) r = sd_bus_call(bus, m, 0, &err, &reply);
  sd_bus_message_unref(m);
  sd_bus_message_unref(reply);
  if (r < 0) {
    US_WARN("pet: cannot watch notifications ({})", err.message ? err.message : "BecomeMonitor failed");
    sd_bus_error_free(&err);
    sd_bus_unref(bus);
    return nullptr;
  }
  sd_bus_error_free(&err);
  return bus;
}

void read(sd_bus_message* m) {
  if (!sd_bus_message_is_method_call(m, "org.freedesktop.Notifications", "Notify")) return;
  const char *app = nullptr, *icon = nullptr, *summary = nullptr, *body = nullptr;
  std::uint32_t replaces = 0;
  if (sd_bus_message_read(m, "susss", &app, &replaces, &icon, &summary, &body) < 0) return;
  Notice n;
  n.app = app ? app : "";
  n.summary = summary ? summary : "";
  n.body = body ? body : "";
  // the urgency hint, if any: skip the actions, walk the hints
  if (sd_bus_message_skip(m, "as") >= 0 && sd_bus_message_enter_container(m, 'a', "{sv}") > 0) {
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
      const char* key = nullptr;
      if (sd_bus_message_read(m, "s", &key) < 0) break;
      if (key && std::string_view(key) == "urgency" && sd_bus_message_enter_container(m, 'v', "y") > 0) {
        std::uint8_t u = 1;
        sd_bus_message_read(m, "y", &u);
        n.urgency = u;
        sd_bus_message_exit_container(m);
      } else {
        sd_bus_message_skip(m, "v");
      }
      sd_bus_message_exit_container(m);
    }
  }
  // undershell's own notes (the depth model's download…) are not news to its pets
  if (n.app == "undershell") return;
  push(std::move(n));
}
}  // namespace

NotifyWatch& NotifyWatch::instance() {
  static NotifyWatch w;
  return w;
}

bool NotifyWatch::start() {
  int expect = 0;
  if (g_state.compare_exchange_strong(expect, 1)) std::thread([this] { loop(); }).detach();
  return g_state.load() >= 0;
}

void NotifyWatch::loop() {
  // reconnect if the bus goes away (a session restart), slowly
  for (int tries = 0;; ++tries) {
    sd_bus* bus = openMonitor();
    if (!bus) {
      if (tries > 3) {
        g_state = -1;
        return;
      }
      std::this_thread::sleep_for(std::chrono::seconds(5));
      continue;
    }
    tries = 0;
    for (;;) {
      sd_bus_message* m = nullptr;
      const int r = sd_bus_process(bus, &m);
      if (r < 0) break;
      if (m) {
        read(m);
        sd_bus_message_unref(m);
      }
      if (r > 0) continue;
      if (sd_bus_wait(bus, UINT64_MAX) < 0) break;
    }
    sd_bus_flush_close_unref(bus);
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}

std::vector<Notice> NotifyWatch::since(std::uint64_t& cursor) {
  std::lock_guard lock(g_mutex);
  std::vector<Notice> out;
  for (const Notice& n : g_notes)
    if (n.seq > cursor) out.push_back(n);
  if (cursor < g_seq) cursor = g_seq;
  return out;
}

void NotifyWatch::inject(const Notice& n) { push(n); }

}  // namespace undershell
