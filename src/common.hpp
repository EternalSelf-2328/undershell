// undershell: desktop widgets that live under any shell, independent of it
// (the looks are ports of Ryoku's).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

namespace undershell {

enum class LogLevel { Debug, Info, Warn, Error };
inline LogLevel g_logLevel = LogLevel::Info;

template <typename... Args>
void log(LogLevel lvl, std::format_string<Args...> fmt, Args&&... args) {
  if (lvl < g_logLevel) return;
  static constexpr const char* kTag[] = {"DBG", "INF", "WRN", "ERR"};
  std::fprintf(stderr, "[undershell] [%s] %s\n", kTag[static_cast<int>(lvl)],
               std::format(fmt, std::forward<Args>(args)...).c_str());
}
#define US_DEBUG(...) ::undershell::log(::undershell::LogLevel::Debug, __VA_ARGS__)
#define US_INFO(...) ::undershell::log(::undershell::LogLevel::Info, __VA_ARGS__)
#define US_WARN(...) ::undershell::log(::undershell::LogLevel::Warn, __VA_ARGS__)
#define US_ERROR(...) ::undershell::log(::undershell::LogLevel::Error, __VA_ARGS__)

using Clock = std::chrono::steady_clock;

inline double nowSeconds() {
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

struct Color {
  float r = 1, g = 1, b = 1, a = 1;

  static Color fromHex(std::string_view hex) { return fromHex(hex, Color{1, 1, 1, 1}); }
  static Color fromHex(std::string_view hex, Color fallback) {
    if (!hex.empty() && hex[0] == '#') hex.remove_prefix(1);
    auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    auto byte = [&](size_t i) -> int {
      int h = nib(hex[i]), l = nib(hex[i + 1]);
      return (h < 0 || l < 0) ? -1 : h * 16 + l;
    };
    if (hex.size() != 6 && hex.size() != 8) return fallback;
    int r = byte(0), g = byte(2), b = byte(4), a = hex.size() == 8 ? byte(6) : 255;
    if (r < 0 || g < 0 || b < 0 || a < 0) return fallback;
    return {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
  }

  Color mix(const Color& o, float t) const {
    return {r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, a + (o.a - a) * t};
  }
};

std::string expandHome(std::string_view path);
std::string readFile(const std::string& path);
bool writeFileAtomic(const std::string& path, const std::string& content);
std::string runCommand(const std::string& cmd, int timeoutSec = 10);

}  // namespace undershell
