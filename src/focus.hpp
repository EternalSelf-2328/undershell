// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

namespace undershell {

class FocusTimer {
public:
  enum class Phase { Idle, Work, Break };

  void start(double now, int workMinutes, int breakMinutes) {
    m_phase = Phase::Work;
    m_end = now + workMinutes * 60.0;
    m_break = breakMinutes * 60.0;
    m_paused = -1;
  }

  bool pause(double now) {
    if (m_phase == Phase::Idle || m_paused >= 0) return false;
    m_paused = std::max(0.0, m_end - now);
    return true;
  }

  bool resume(double now) {
    if (m_paused < 0) return false;
    m_end = now + m_paused;
    m_paused = -1;
    return true;
  }

  void stop() { *this = {}; }

  bool update(double now) {
    if (!running() || now < m_end) return false;
    if (m_phase == Phase::Work && m_break > 0 && now < m_end + m_break) {
      m_phase = Phase::Break;
      m_end += m_break;
    } else {
      stop();
    }
    return true;
  }

  [[nodiscard]] int seconds(double now) const {
    if (m_phase == Phase::Idle) return 0;
    return static_cast<int>(std::ceil(m_paused >= 0 ? m_paused : std::max(0.0, m_end - now)));
  }

  [[nodiscard]] double next(double now) const {
    if (!running()) return 1e18;
    return std::max(now, m_end - std::max(0, seconds(now) - 1));
  }

  [[nodiscard]] std::string_view state() const {
    if (m_phase == Phase::Idle) return "idle";
    if (m_paused >= 0) return "paused";
    return m_phase == Phase::Work ? "work" : "break";
  }

  [[nodiscard]] Phase phase() const { return m_phase; }
  [[nodiscard]] bool running() const { return m_phase != Phase::Idle && m_paused < 0; }

private:
  double m_end = 0, m_break = 0, m_paused = -1;
  Phase m_phase = Phase::Idle;
};

}  // namespace undershell
