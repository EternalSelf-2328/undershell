// SPDX-License-Identifier: GPL-3.0-or-later
// Painted companions: each species drawn frame by frame (24 frames, two
// styles), played as drawn. Nothing is bent or squashed; the motion comes from
// choosing the frame: the walk by the ground covered, so the feet never slide.
//
// The frames come from sprite sheets painted over pose guides (the rig's
// mannequins, build/testtools/petguide) and are cut by
// tools/import-pet-frames.py into art/pets/frames/<style>/<id>.png: 6x4 cells
// of 128 px, facing right, the ground on row 116 of each cell. The build
// embeds whatever atlases exist (tools/embed-pet-frames.py); a species without
// one is drawn by the rig (petrig.hpp).
#pragma once

#include "petart.hpp"
#include "petlife.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace undershell {

enum class FrameStyle { Chibi, Pixel };

inline constexpr int kFrameCell = 128;   // a frame's side
inline constexpr int kFrameGround = 116; // the ground row inside a cell
inline constexpr int kFrameCount = 24;
// The cells, in the order they are painted (tools/import-pet-frames.py, the guides).
namespace fcell {
inline constexpr int Idle = 0, Breathe = 1, Look = 2, Blink = 3, Sit = 4, SitLook = 5, Walk = 6 /* ..11 */, Crouch = 12,
                     Rise = 13, Fall = 14, Land = 15, Alert = 16, Happy = 17, Sleep = 18, SleepBreathe = 19, Held = 20,
                     Act = 21 /* ..23 */;
}

struct FrameAtlas {
  std::vector<std::uint8_t> rgba;  // 6x4 cells of kFrameCell, RGBA, binary alpha
  // measured once: where the figure stands in the idle frame, for the walk's
  // cycle and the size of a step
  int idleLeft = 0, idleRight = 0, idleTop = 0;
};

// the atlas of a species in a style, decoded on first use; nullptr if it was never painted
const FrameAtlas* frameAtlas(std::string_view id, FrameStyle style);
// a species painted in some style (the other one stands in if the asked one is missing)
const FrameAtlas* frameAtlasOrOther(std::string_view id, FrameStyle style);
// the species that have frames in a style (the gallery, the tests)
std::vector<std::string_view> framedSpecies(FrameStyle style);

// The cell for what it is doing now. `time` drives the slow alternations
// (breathing, sleeping), `plan` is the rig plan it moves like.
int frameCell(const LifeIn& in, double time, RigPlan plan);
// The ground covered by one walk cycle (six frames), in frame px.
double frameCycle(const FrameAtlas& atlas, RigPlan plan);
// One frame as a picture with the ground on its last row, mirrored if it faces left.
void frameCompose(const FrameAtlas& atlas, int cell, bool flip, PetPixels& out);
// Test hook: an atlas made in memory (nullptr clears it).
void frameAtlasOverride(std::string_view id, FrameStyle style, const FrameAtlas* atlas);

}  // namespace undershell
