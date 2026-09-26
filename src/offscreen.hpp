// SPDX-License-Identifier: GPL-3.0-or-later
// Headless rendering (EGL surfaceless + FBO): `undershell --snapshot` and the
// test suite draw widgets without a compositor.
#pragma once

#include "noctalia.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace undershell {

struct Image {
  int w = 0, h = 0;
  std::vector<std::uint8_t> rgba;  // top-down, premultiplied
};

class Headless {
public:
  Headless();
  ~Headless();
  [[nodiscard]] bool ok() const { return m_ok; }

private:
  void* m_dpy = nullptr;
  void* m_ctx = nullptr;
  bool m_ok = false;
};

// A fixed palette so rendering tests do not depend on the current wallpaper.
NoctaliaState testPalette();

// Renders a visualizer look fed a fixed synthetic spectrum (needs Headless).
Image renderLook(const std::string& look, int w, int h, const NoctaliaState& noct);
// Renders a clock face at a fixed wall-clock time (needs Headless).
struct ClockConfig;
Image renderClock(const ClockConfig& cfg, int w, int h, const NoctaliaState& noct, long fixedTime);
// Renders one line of text in white (needs Headless).
Image renderText(const std::string& text, const std::string& family, float size, int weight);

bool writePng(const Image& img, const std::string& path, bool overDark);
bool readPng(const std::string& path, Image& img);
// mean absolute channel difference (0..255), or -1 when sizes differ
double imageDiff(const Image& a, const Image& b);

int snapshotLooks(const std::string& dir);

}  // namespace undershell
