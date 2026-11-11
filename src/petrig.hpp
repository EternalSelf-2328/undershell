// SPDX-License-Identifier: GPL-3.0-or-later
// Rigged companions: creatures built from parts (a spine, jointed legs, a
// neck, heads with ears, eyes and a mouth, tails, wings, a robe, hair) and
// drawn as pixel art on every frame. Their legs really step, their tails,
// ears, hair and robes follow through, and every colour comes from a
// palette, so they can be recoloured freely.
//
// Geometry is in units of a 64 grid (the ground at the bottom, the creature
// facing right); the picture is drawn on a finer grid (kRigPixels), so the
// outlines stay one pixel and faces get room for detail.
#pragma once

#include "common.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace undershell {

inline constexpr int kRigPixels = 96;              // the art grid
inline constexpr float kRigUnit = kRigPixels / 64.0F;  // pixels per geometry unit
using RigPixels = std::array<std::uint8_t, kRigPixels * kRigPixels * 4>;

// How the creature is built.
enum class RigPlan { Quad, Bird, Blob, Serpent, Biped };
enum class RigEar { Pointed, Round, Floppy, Long, Tuft, Fin, None };
enum class RigTail { Brush, Thin, Puff, Curl, Long, Fan, Plume, Tuft, None };
enum class RigFoot { Paw, Hoof, Talon };
enum class RigHorn { None, Unicorn, Antlers, Nubs, Dragon, Qilin };
enum class RigWing { None, Feather, Membrane };
enum class RigEye { Round, Anime, Socket, Visor, Bead };
// bipeds
enum class RigOutfit { Fur, Robe, Tunic, Armor, Bones };
enum class RigHair { None, Short, Long, Ponytail, Spiky, Bun, Hood, WizardHat, Helmet, Circlet };
enum class RigProp { None, Staff, Sword, Orb, Lance, Fan };

// The colours a companion is painted with. Each one gets a ramp of shades
// (hue-shifted: shadows cooler, lights warmer) and its own outline.
// Bipeds wear them as: body = the outfit, belly = the skin, mark = the hair
// and trims, extra = metal, gold and props.
struct RigPalette {
  Color body{0.9F, 0.5F, 0.25F, 1};
  Color belly{1, 0.95F, 0.88F, 1};   // underside, muzzle, chest, tail tip
  Color mark{0.25F, 0.18F, 0.18F, 1};  // socks, ear backs, stripes, mane
  Color eyes{0.2F, 0.15F, 0.12F, 1};
  Color extra{1, 0.85F, 0.45F, 1};   // horns, hooves, beak, collar
};

struct RigSpec {
  RigPlan plan = RigPlan::Quad;
  // body (units; the ground is the bottom row)
  float shoulderH = 16, hipH = 16, spine = 16;
  float chestRx = 7.5F, chestRy = 6.5F, rumpRx = 6.5F, rumpRy = 6.2F, waist = 5.6F;
  // legs: upper and lower lengths, widths at the joint and the foot
  float frontUp = 7.5F, frontLow = 8.5F, frontW = 2.4F, frontFootW = 1.9F;
  float hindUp = 8, hindLow = 8.5F, hindW = 3, hindFootW = 1.9F;
  float haunchRx = 5, haunchRy = 6.5F, paw = 2.1F;
  RigFoot foot = RigFoot::Paw;
  // neck and head
  float neckLen = 7, neckW = 3.8F, neckAng = -1.05F;
  float headR = 6.8F, headRy = 6.0F;
  float snout = 5.5F, snoutRx = 3.6F, snoutRy = 2.6F, snoutDrop = 1.6F;
  int heads = 1;  // Cerberus
  RigEar ear = RigEar::Pointed;
  float earLen = 6.5F, earW = 4.6F, earTilt = -0.35F;  // tilt: back (-) or forward (+)
  bool cheeks = false;      // a ruff of fur on the cheeks
  bool whiskers = false;    // a dragon's barbels
  RigHorn horn = RigHorn::None;
  RigEye eye = RigEye::Round;
  float eyeX = 2.2F, eyeY = -0.8F;
  // tail: from the rump, in segments; several fan out (a kitsune's)
  RigTail tail = RigTail::Brush;
  float tailLen = 17, tailW = 2.6F, tailFat = 5.5F, tailUp = 0.5F, tailCurl = 0.05F;
  int tailSegs = 8, tails = 1;
  float tailSpread = 0.32F;
  // wings
  RigWing wings = RigWing::None;
  float wingLen = 14;
  // markings
  bool socks = false, earTips = false, tailTip = false, mask = false, stripes = false, spots = false, bib = true, mane = false;
  bool scales = false, flame = false;  // a qilin's or a dragon's scales; flames for a mane
  bool bandit = false;      // a dark band across the eyes (a raccoon's, a panda's patches)
  bool ringTail = false;    // rings down the tail only
  bool muzzleExtra = false; // the muzzle, cheeks and ear rims in the detail colour (a red panda's)
  bool tipsExtra = false;   // ear tips in the detail colour
  bool beak = false;        // an eagle's head on a four-legged body (a griffin's)
  bool frontTalons = false; // and its front feet
  bool headBelly = false;   // the head in the belly colour
  bool tailTipMark = false; // tail tips in the markings' colour (a kitsune's)
  float stripeFreq = 0.9F;
  // a bird or a blob: the body is one round shape
  float bodyRx = 10, bodyRy = 10;
  // a serpent: its length and girth
  float length = 70, girth = 3.4F;
  // a biped: legs, arms, torso; what it wears and carries
  float legUp = 6, legLow = 6, armUp = 5, armLow = 5, torsoH = 9, torsoW = 5, hipW = 4.5F, limbW = 1.6F;
  RigOutfit outfit = RigOutfit::Tunic;
  RigHair hair = RigHair::Short;
  RigProp prop = RigProp::None;
  bool cape = false, robe = false, monkeyTail = false;
  // motion: walking speed, the ground one stride covers (units), the step's lift
  float walk = 18, stride = 12, lift = 2.5F;
  bool hops = false;    // bounds instead of walking (a rabbit, a bird, a slime)
  bool floats = false;  // hovers over the ground (a ghost, a serpent, a mage)
  bool flies = false;   // beats its wings up to windows and glides down
  // how it fits the grid (rigKind measures it once over all it does): a scale under 1 for the
  // big ones, and the middle of its reach in units
  float fitScale = 1, fitX = 32;
};

struct V2 {
  float x = 0, y = 0;
};

// One instant of the rig. Every field blends: behaviours ease from one pose
// to the next, the gait writes the feet, the springs write tails, ears, hair.
struct RigPose {
  float x = 0;                 // the body forward (units)
  float shoulderH = 16, hipH = 16;  // joint heights over the ground (a biped's: the hips)
  float lean = 0;              // a biped's torso, forward (rad)
  float neck = -1.05F;         // neck angle (0 forward, -pi/2 up)
  float head = 0;              // head pitch (+ nose down)
  float heads[3] = {};         // each head on its own (+ down)
  float yaw = 0;               // 0 in profile .. 1 facing you
  float ears = 0;              // -1 flat back .. 1 perked forward
  float earFlop = 0;           // the ears' spring (+ down)
  float mouth = 0, tongue = 0; // 0..1
  float eyes = 1;              // 1 open .. 0 shut
  int eyeStyle = 0;            // 0 open, 1 happy (^ ^), 2 sleepy
  float blush = 0;
  float tail[12] = {};         // each tail segment's bend (rad, + up), from the springs
  float tailUp = 0.5F;         // the root's angle (+ up)
  float hem[4] = {};           // a robe's or a cape's sway (springs)
  V2 foot[4];                  // near front, far front, near hind, far hind (a biped: 2 near, 3 far):
                               // forward of the joint and up off the ground
  float pawCurl[4] = {};       // a lifted paw folds (0..1)
  V2 hand[2];                  // a biped's hands: near, far, from the shoulder
  float wing = 0, wingOpen = 0;  // wing beat (-1 up .. 1 down) and spread (0 folded .. 1 open)
  float squash = 0;            // the body squashes (+) or stretches (-), a blob's bounce
  float lift = 0;              // the whole figure up (a hop in place, a hover)
  float wave = 0, wavePhase = 0;  // a serpent's undulation
  float prop = 0;              // a prop's own motion (a staff's spin, a sword's flight), 0..1 cycles
  float aura = 0;              // a glow round it (qi, an orb, a visor), 0..1
};

// The creature at rest, its feet under its joints.
RigPose rigStand(const RigSpec& spec);
// The feet over one stride: `phase` 0..1, `trot` 0 walk .. 1 trot/bound.
void rigGait(const RigSpec& spec, RigPose& pose, double phase, double trot);
// Ground covered by one full cycle of `rigGait` (units): what keeps the feet planted.
float rigCycle(const RigSpec& spec, double trot);
// Blend b over a by w.
RigPose rigMix(const RigPose& a, const RigPose& b, float w);

// Draws one frame: binary alpha, the feet on the bottom row, facing right
// unless `flip`. The light stays up and to the left either way.
void rigDraw(const RigSpec& spec, const RigPose& pose, const RigPalette& pal, bool flip, RigPixels& out);
// Draw in flat colours by part instead of shaded (the pose guides handed to an
// artist): near legs dark, far legs light, tail orange, wings purple. Per thread.
void rigGuideMode(bool on);

// Colour helpers (OKLCH), shared with the recolouring of the drawn companions.
struct Oklch {
  float L = 0, C = 0, h = 0;  // h in degrees
};
Oklch toOklch(Color c);
Color fromOklch(Oklch o);
// the same hue, pale (a belly, a muzzle): lightness L, a whisper of chroma
Color rigTint(Color c, float L);
// A companion's colours from a picture's dominant colours (the most common
// first): the body in the liveliest of the common ones, markings in another
// hue (or a deeper shade), eyes in a contrasting one.
RigPalette rigPaletteFrom(const std::vector<Color>& dominant, const RigPalette& fallback);

}  // namespace undershell
