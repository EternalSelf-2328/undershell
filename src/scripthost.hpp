// SPDX-License-Identifier: GPL-3.0-or-later
// The scene widget's side of undershell-script (scripthelper.cpp): starts
// it, hands it the scene's scripts, asks for new values now and then and
// reads them back without ever waiting, so a slow script never holds a
// frame. One per scene that has scripts; the helper ends with it.
#pragma once

#include <glib.h>

#include <map>
#include <string>
#include <vector>

namespace undershell {

class ScriptHost {
public:
  ScriptHost() = default;
  ScriptHost(const ScriptHost&) = delete;
  ScriptHost& operator=(const ScriptHost&) = delete;
  ~ScriptHost();

  // the helper beside the binary or installed; "" when there is none
  static std::string helperPath();
  bool start();
  [[nodiscard]] bool running() const { return m_in >= 0; }
  // a script, the value it starts from and its properties (JSON)
  void load(int id, const std::string& source, const std::string& value, const std::string& propsJson);
  // asks for the next values (nothing is sent while an answer is awaited)
  void update(double runtime, double dt, double timeOfDay, const std::vector<int>& ids);
  // the values that came back since last time; true when any did
  bool poll(std::map<int, std::string>& values);

private:
  void send(const std::string& line);
  void stop();
  GPid m_pid = 0;
  int m_in = -1, m_out = -1;
  std::string m_buf;
  bool m_waiting = false;
};

}  // namespace undershell
