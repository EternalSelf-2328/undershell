// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

namespace undershell {

struct StereoFrame {
  static constexpr int samples = 128;
  std::array<float, samples * 2> wave{};  // left row, then right row
  float left = 0, right = 0;             // RMS across the latest audio block
};

}  // namespace undershell
