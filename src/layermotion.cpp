// SPDX-License-Identifier: GPL-3.0-or-later
#include "layermotion.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <numbers>

namespace undershell {

void LayerMotion::configure(const toml::table& options) {
  auto number = [&](const char* key, double fallback, double low, double high) {
    const double value = options[key].value_or(fallback);
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
  };
  const auto mode = options["scene_motion"].value_or(std::string("none"));
  const auto next = mode == "loop" ? Mode::Loop : mode == "night" ? Mode::Night : Mode::None;
  if (m_mode != next) m_start = -1;
  m_mode = next;
  m_period = number("scene_period", 12, 1, 3600);
  m_preview = number("scene_preview", -1, -1, 100);
  m_from = 60 * number("scene_from", 18, 0, 23.75);
  m_until = 60 * number("scene_until", 6, 0, 23.75);
  m_fade = number("scene_fade", 12, 0, 180);
  m_startOpacity = static_cast<float>(number("scene_start_opacity", 1, 0, 1));
  m_target = {static_cast<float>(number("scene_x", 0, -80, 80) / 100),
              static_cast<float>(number("scene_y", 0, -80, 80) / 100),
              static_cast<float>(number("scene_brightness", 1, 0, 2)),
              static_cast<float>(number("scene_opacity", 1, 0, 1))};
  if (m_target.x == 0 && m_target.y == 0 && m_target.brightness == 1 && m_startOpacity == 1 && m_target.opacity == 1) m_mode = Mode::None;
}

double LayerMotion::nightAmount(double minute, double from, double until, double fade) {
  const double length = std::fmod(1440 + until - from, 1440);
  const double inside = std::fmod(1440 + minute - from, 1440);
  if (inside >= length) return 0;
  if (fade <= 0) return 1;
  const double ramp = std::max(0.0, std::min(inside, length - inside) / std::min(fade, length / 2));
  const double amount = std::min(1.0, ramp);
  return amount * amount * (3 - 2 * amount);
}

void LayerMotion::tick(double now, bool editing) {
  m_pose = {};
  m_next = 1e18;
  if (!enabled()) return;
  if (m_start < 0) m_start = now;
  double amount = 0;
  if (editing && m_preview >= 0) {
    amount = m_preview / 100;
  } else if (m_mode == Mode::Loop) {
    const double t = std::fmod(std::max(0.0, now - m_start), m_period) / m_period;
    amount = (1 - std::cos(2 * std::numbers::pi * t)) / 2;
    m_next = now + 1.0 / 24;
  } else {
    const std::time_t time = std::time(nullptr);
    std::tm local{};
    localtime_r(&time, &local);
    amount = nightAmount(local.tm_hour * 60 + local.tm_min, m_from, m_until, m_fade);
    m_next = now + (60 - local.tm_sec);
  }
  const float a = static_cast<float>(amount);
  m_pose = {m_target.x * a, m_target.y * a, std::lerp(1.0F, m_target.brightness, a), std::lerp(m_startOpacity, m_target.opacity, a)};
}

}  // namespace undershell
