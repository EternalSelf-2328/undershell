// SPDX-License-Identifier: GPL-3.0-or-later
// Noctalia's wallpaper_depth plugin writes a grayscale foreground mask per
// wallpaper and hands it to Noctalia, which erases desktop-widget pixels under
// the foreground (DestinationOut). undershell finds the same cached mask
// (sha256 of the wallpaper + the plugin's threshold/feather) and applies it
// with the same sampling maths, so its widgets pass behind the scenery too.
#pragma once

#include "noctalia.hpp"

#include <GLES3/gl3.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace undershell {

struct DepthMask {
  std::string wallpaper;
  std::string maskPath;
  int width = 0, height = 0;
  GLuint texture = 0;
  std::vector<std::uint8_t> pixels;  // pending upload (R8)
};

class DepthMasks {
public:
  static std::string maskDir();

  // Re-resolves which mask belongs to each output. Returns true on change.
  bool update(const NoctaliaState& st, const std::vector<std::string>& outputs);
  // Uploads pending pixels (needs a current GL context) and returns the mask
  // for an output, or nullptr when there is none.
  const DepthMask* get(const std::string& output);
  void releaseGl();
  // true when the plugin is active but some output still has no mask
  [[nodiscard]] bool missing() const { return m_missing; }
  // sha256 of a file, cached by mtime and size (also keys wallpaper profiles)
  std::string sha256Of(const std::string& path);

private:
  static std::string findMask(const std::string& sha, double threshold, double feather);
  static bool loadPng(const std::string& path, DepthMask& out);

  std::map<std::string, DepthMask> m_masks;
  struct HashEntry {
    std::int64_t mtime = 0, size = 0;
    std::string sha;
  };
  std::map<std::string, HashEntry> m_hashes;
  bool m_missing = false;
};

}  // namespace undershell
