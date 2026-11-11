// SPDX-License-Identifier: GPL-3.0-or-later
// How a rigged companion (petrig.hpp) behaves from moment to moment: the
// pose it aims for in each behaviour, eased so it never snaps from one to
// the next; the gait on its feet; springs in its tail and ears; where it
// looks; and the little marks over its head (z's, notes, hearts, a "!").
#pragma once

#include "petart.hpp"
#include "petrig.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace undershell {

// The things a companion does on its own (and when something happens).
enum class RigAct {
  None, LookAround, LookAtYou, HeadTilt, Groom, Scratch, Stretch, Yawn, Sniff, Shake, Wag, Howl, Pant,
  LieDown, Preen, Flap, Peck, Wobble, Hop, Pounce, Roll,
  Wave, Meditate, Spin, SwordDance, Scan, Rattle, ScratchHead, Bow, Coil, Count
};

// A species built on the rig: its build, its natural colours, the colour
// variants it comes in, and what it likes to do.
struct RigPreset {
  std::string_view id, en, es;
  RigPalette palette;
};
struct RigKind {
  PetKind kind;
  RigSpec spec;
  std::vector<RigPreset> presets;  // the first is its natural colouring
  std::vector<RigAct> acts;
};
const RigKind* rigKind(PetKind kind);  // null: a drawn companion (or Sprout)
// The drawn companions (classic and chibi) come in these tints: their own colours
// ("original", palette unused), or their main colour (body) and second one (mark) moved.
const std::vector<RigPreset>& classicTints();
inline bool isRigPet(PetKind kind) { return rigKind(kind) != nullptr; }

// What the widget tells it each step.
struct LifeIn {
  double dt = 0, time = 0;
  PetPose mode = PetPose::Idle;
  bool lie = false;        // resting lying down instead of sitting
  double phase = 0;        // the gait's cycles (from the ground covered)
  double pace = 0;         // 0 standing, 1 walking, 2 running
  double vy = 0;           // art px/s, + down
  bool grounded = true, jumped = false;
  double crouch = -1;      // 0..1 through the crouch before a jump, -1 none
  double landAge = 9, landHard = 0;
  double turn = -1;        // 0..1 through a turn round, -1 none
  bool blink = false;
  double kickAge = 9;
  int beat = 0;
  RigAct act = RigAct::None;
  double actAge = 0, actDur = 0;
  double petted = -1;      // s since it was patted (-1: not lately)
  double alert = -1;       // s since something called its attention
  double airTime = 0;
  double wake = -1;        // s since it got up from a rest
  bool carried = false;    // hanging from the pointer
};

// The marks it shows over its head.
enum class RigMark { None, Sleep, Notes, Hearts, Alert, Sweat, Question };
// The mark over the figure in an RGBA frame of `side` px (the rig's 96 or a painted 128).
void drawRigMark(std::uint8_t* px, int side, RigMark mark, double age, bool flip, const RigPalette& pal);

class PetLife {
public:
  void reset(const RigKind* kind, std::uint64_t seed);
  void step(const LifeIn& in);
  // The frame: the figure, turned and marked. `t` drives the marks.
  void draw(const RigPalette& pal, bool flip, const LifeIn& in, RigMark mark, double markAge, RigPixels& out) const;
  [[nodiscard]] const RigPose& pose() const { return m_pose; }
  // the pose a behaviour aims for (also the previews')
  [[nodiscard]] RigPose target(const LifeIn& in) const;
  [[nodiscard]] static double actLength(RigAct a);

private:
  [[nodiscard]] double rnd();
  const RigKind* m_kind = nullptr;
  RigPose m_base, m_pose;  // eased towards the target; and as drawn
  bool m_init = false;
  float m_tail[12] = {}, m_tailV[12] = {};
  float m_flop = 0, m_flopV = 0;
  float m_hem[4] = {}, m_hemV[4] = {};
  double m_wave = 0, m_prop = 0;
  float m_gaitW = 0;
  double m_prevVy = 0;
  // looking about: where its head goes next, and when
  double m_lookNext = 1, m_lookYaw = 0, m_lookPitch = 0, m_earTwitch = -1;
  std::uint64_t m_rng = 0x9E3779B97F4A7C15ULL;
};

}  // namespace undershell
