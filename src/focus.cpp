// SPDX-License-Identifier: GPL-3.0-or-later
#include "app.hpp"

#include <sstream>

namespace undershell {

void App::updateFocus(double now) {
  const bool changed = m_focus.update(now);
  if (changed) {
    const bool resting = m_focus.phase() == FocusTimer::Phase::Break;
    const bool es = spanishUi();
    handleCommand(std::string("pet say ") + (resting ? (es ? "Hora de descansar" : "Time for a break")
                                                     : (es ? "Sesión terminada" : "Session complete")));
  }
  const int seconds = m_focus.seconds(now);
  const std::string display = m_focus.phase() == FocusTimer::Phase::Idle
                                  ? std::string()
                                  : std::format("{:02}:{:02}", seconds / 60, seconds % 60);
  if (display != m_focusText) {
    m_focusText = display;
    for (auto& w : m_widgets)
      if (w->cfg.type == "text" && w->cfg.options["text"].value_or(std::string()).find("{focus}") != std::string::npos)
        w->needsRender = true;
  }
}

std::string App::focusCommand(const std::string& args) {
  const double now = nowSeconds();
  updateFocus(now);
  std::istringstream in(args);
  std::string verb, extra;
  in >> verb;
  if (verb == "start") {
    int work = 25, rest = 5;
    in >> std::ws;
    if (!in.eof() && !(in >> work)) return "error: expected work minutes";
    in >> std::ws;
    if (!in.eof() && !(in >> rest)) return "error: expected break minutes";
    if (work < 1 || work > 240 || rest < 0 || rest > 120 || in >> extra)
      return "error: focus start [work minutes 1-240] [break minutes 0-120]";
    m_focus.start(now, work, rest);
  } else {
    if (in >> extra) return "error: unexpected focus argument";
    if (verb == "pause") m_focus.pause(now);
    else if (verb == "resume") m_focus.resume(now);
    else if (verb == "stop") m_focus.stop();
    else if (!verb.empty() && verb != "status") return "error: focus start|pause|resume|stop|status";
  }
  updateFocus(now);
  return std::format("{} {}", m_focus.state(), m_focus.seconds(now));
}

}  // namespace undershell
