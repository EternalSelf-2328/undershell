// SPDX-License-Identifier: GPL-3.0-or-later
// Each wallpaper's depth: undershell's map (depthgen.hpp) refined against the
// image (depthfield.hpp) and uploaded as a texture; a widget is erased where
// the wallpaper is nearer than its plane (DestinationOut), so it passes behind
// the scenery.
#pragma once

#include "depthfield.hpp"
#include "noctalia.hpp"

#include <GLES3/gl3.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace undershell {

struct DepthMask {
  std::string wallpaper;
  // the refined depth field (depthfield.hpp), cut at each widget's plane
  std::string fieldNpy;
  std::string fieldKey;  // npy + its mtime and size: a map rewritten in place is a new map
  int fieldW = 0, fieldH = 0;    // texture size
  int imageW = 0, imageH = 0;    // the wallpaper's size (sampling maths)
  GLuint field = 0;
  std::vector<std::uint16_t> fieldPixels;  // pending upload (R16F)
  bool fieldStale = false;                 // texture belongs to a previous wallpaper
  // kept for the depth brush (CPU copies at the field's size)
  std::string sha;
  DepthField base;              // the refined field, before hand corrections
  std::vector<float> guide;     // smoothed luma, for the smart brush
  DepthEdits edits;             // hand corrections (depth-edits/<sha>.usde)
  bool hasEdits = false;
  std::vector<std::uint8_t> colour;  // the wallpaper at the field's size (the brush's zoom)
  GLuint wallTexture = 0;
  bool wallStale = false;
  // the effect masks (depthfield.hpp): four painted channels at the field's
  // size, where wallpaper effects apply; empty until something is painted
  std::vector<std::uint8_t> fxMask;
  GLuint fxMaskTex = 0;
  bool fxMaskUpload = false;  // the whole texture is (re)uploaded on next use
};

class Jobs;

class DepthMasks {
public:
  // depth fields are refined off-thread; `ready` runs (main thread) when one lands
  void setJobs(Jobs* jobs, std::function<void()> ready) {
    m_jobs = jobs;
    m_fieldReady = std::move(ready);
  }
  // where hand corrections and painted effect masks are kept (one file per wallpaper)
  void setEditsDir(std::string dir) { m_editsDir = std::move(dir); }
  void setMasksDir(std::string dir) { m_masksDir = std::move(dir); }
  // the painted effect masks of an output's wallpaper as a texture (needs
  // GL; 0 when none is painted), re-uploading a box after a stroke, saving
  GLuint effectMasks(const std::string& output);
  void uploadMaskRect(DepthMask& m, PixelBox box);
  bool saveMasks(DepthMask& m);
  // the depth brush: a mask whose field is loaded, or nullptr
  DepthMask* paintable(const std::string& output);
  // re-uploads `box` of the field (needs GL), previewing `stroke` if given
  void uploadRect(DepthMask& m, PixelBox box, const std::vector<float>* stroke = nullptr, DepthTool tool = DepthTool::Front,
                  float value = 0, const std::vector<float>* perPixel = nullptr);
  bool saveEdits(DepthMask& m);
  // the wallpaper as a texture, for the zoomed brush view (needs GL)
  GLuint wallpaperTexture(DepthMask& m);

  // off: no wallpaper is followed (no model, a moving wallpaper, or
  // [general] depth_model = false)
  void setEnabled(bool on) { m_enabled = on; }
  // Re-resolves which depth belongs to each output. Returns true on change.
  bool update(const NoctaliaState& st, const std::vector<std::string>& outputs);
  // Uploads pending pixels (needs a current GL context) and returns the depth
  // for an output, or nullptr while there is none.
  const DepthMask* get(const std::string& output);
  void releaseGl();
  // sha256 of a file, cached by mtime and size (also keys wallpaper profiles)
  std::string sha256Of(const std::string& path);

private:
  // per output; outputs showing the same wallpaper share one (one refine,
  // one set of CPU copies and textures)
  std::map<std::string, std::shared_ptr<DepthMask>> m_masks;
  std::vector<std::shared_ptr<DepthMask>> m_retired;  // left by every output: textures to free with GL current
  void freeRetired();
  struct HashEntry {
    std::int64_t mtime = 0, size = 0;
    std::string sha;
  };
  std::map<std::string, HashEntry> m_hashes;
  bool m_enabled = false;
  Jobs* m_jobs = nullptr;
  std::string m_editsDir, m_masksDir;
  std::function<void()> m_fieldReady;
};

}  // namespace undershell
