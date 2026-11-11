// SPDX-License-Identifier: GPL-3.0-or-later
// Rigged companions' behaviour (petlife.hpp). Every behaviour is a target
// pose; the pose eases towards it (bodies slower, heads and ears quicker,
// eyes at once), so sitting down, lying, getting up and looking round are
// all real movements. The feet are the exception: while it walks they
// follow the gait exactly, so they stay planted.
#include "petlife.hpp"

#include <algorithm>
#include <cmath>

namespace undershell {

namespace {
constexpr double kTau = 6.283185307179586;
double smooth(double a, double b, double v) {
  const double t = std::clamp((v - a) / (b - a), 0.0, 1.0);
  return t * t * (3 - 2 * t);
}
double plateau(double age, double dur, double in, double out) { return smooth(0, in, age) * (1 - smooth(dur - out, dur, age)); }
float f(double v) { return static_cast<float>(v); }
using A = RigAct;
}  // namespace

double PetLife::actLength(RigAct a) {
  switch (a) {
    case A::LookAround: return 2.4;
    case A::LookAtYou: return 2.6;
    case A::HeadTilt: return 1.4;
    case A::Groom: return 3.2;
    case A::Scratch: return 2.2;
    case A::Stretch: return 2.4;
    case A::Yawn: return 1.8;
    case A::Sniff: return 2.0;
    case A::Shake: return 1.0;
    case A::Wag: return 2.0;
    case A::Howl: return 3.0;
    case A::Pant: return 2.2;
    case A::Preen: return 2.2;
    case A::Flap: return 1.4;
    case A::Peck: return 1.0;
    case A::Wobble: return 1.4;
    case A::Hop: return 0.8;
    case A::Roll: return 2.6;
    case A::Wave: return 2.0;
    case A::Meditate: return 6.0;
    case A::Spin: return 2.2;
    case A::SwordDance: return 3.2;
    case A::Scan: return 1.6;
    case A::Rattle: return 1.2;
    case A::ScratchHead: return 1.8;
    case A::Bow: return 1.8;
    case A::Coil: return 3.0;
    default: return 0;
  }
}

double PetLife::rnd() {
  m_rng ^= m_rng << 13, m_rng ^= m_rng >> 7, m_rng ^= m_rng << 17;
  return static_cast<double>(m_rng >> 11) / static_cast<double>(1ULL << 53);
}

void PetLife::reset(const RigKind* kind, std::uint64_t seed) {
  m_kind = kind;
  m_rng = seed | 1;
  m_init = false;
  for (int i = 0; i < 12; ++i) m_tail[i] = m_tailV[i] = 0;
  for (int i = 0; i < 4; ++i) m_hem[i] = m_hemV[i] = 0;
  m_flop = m_flopV = 0;
  m_gaitW = 0;
  m_wave = 0, m_prop = 0;
}

// ── Target poses ────────────────────────────────────────────────────────────

namespace {
using A = RigAct;

RigPose liePose(const RigSpec& s);

RigPose sitPose(const RigSpec& s) {
  if (s.plan == RigPlan::Quad && s.foot == RigFoot::Hoof) return liePose(s);  // a horse, a deer rests lying down
  RigPose p = rigStand(s);
  switch (s.plan) {
    case RigPlan::Quad:
      p.hipH = s.rumpRy + 0.3F;
      p.shoulderH = s.shoulderH + 4;
      p.neck = -1.35F;
      p.foot[0] = {-1, 0}, p.foot[1] = {0, 0}, p.foot[2] = {6, 0}, p.foot[3] = {7, 0};
      p.tailUp = -0.2F;
      for (float& t : p.tail) t = -0.1F;
      break;
    case RigPlan::Bird: p.hipH = 0.6F, p.shoulderH = 0.6F, p.squash = 0.1F; break;
    case RigPlan::Blob: p.squash = 0.15F; break;
    case RigPlan::Serpent: p.lift = -6, p.wave = 0.9F; break;
    case RigPlan::Biped:
      // cross-legged on the ground (or on the air, for one who floats)
      p.hipH = 1.8F;
      p.foot[2] = {3.4F, 0.2F}, p.foot[3] = {-2.6F, 0.2F};
      p.hand[0] = {2.6F, s.armUp + s.armLow - 3.2F};
      p.hand[1] = {1.6F, s.armUp + s.armLow - 3.4F};
      break;
  }
  return p;
}

RigPose liePose(const RigSpec& s) {
  RigPose p = rigStand(s);
  switch (s.plan) {
    case RigPlan::Quad:
      p.hipH = s.rumpRy * 0.85F;
      p.shoulderH = s.chestRy * 0.85F;
      p.neck = -0.95F;
      p.foot[0] = {9, 0}, p.foot[1] = {10, 0}, p.foot[2] = {6, 0}, p.foot[3] = {7, 0};
      p.tailUp = -0.3F;
      break;
    case RigPlan::Bird: p.hipH = 0.4F, p.shoulderH = 0.4F, p.squash = 0.15F; break;
    case RigPlan::Blob: p.squash = 0.3F; break;
    case RigPlan::Serpent: p.lift = -9, p.wave = 1.0F; break;
    case RigPlan::Biped:
      p = sitPose(s);
      p.lean = 0.22F;
      p.head = 0.3F;
      break;
  }
  return p;
}
}  // namespace

RigPose PetLife::target(const LifeIn& in) const {
  const RigSpec& s = m_kind->spec;
  const bool quad = s.plan == RigPlan::Quad, biped = s.plan == RigPlan::Biped;
  RigPose p = rigStand(s);
  const double t = in.time;
  const float reach = s.armUp + s.armLow;
  // breathing: about 1 px over ~3 s, deeper and slower asleep
  const double breath = std::sin(kTau * t / (in.mode == PetPose::Sleep ? 4.2 : 3.0));
  if (biped) p.hipH += f(0.2 * breath), p.hand[0].y += f(0.15 * breath), p.hand[1].y += f(0.15 * breath);
  if (s.plan == RigPlan::Blob) p.squash = f(0.04 * breath);
  // a floater bobs on the air
  if (s.floats) p.lift += f(0.9 * std::sin(kTau * t / 2.6));

  switch (in.mode) {
    case PetPose::Sit:
      p = in.lie ? liePose(s) : sitPose(s);
      p.shoulderH += f(0.3 * breath);
      if (s.floats) p.lift += f(1.5 + 0.9 * std::sin(kTau * t / 2.6));
      break;
    case PetPose::Sleep: {
      p = liePose(s);
      if (quad) p.neck = -0.15F, p.head = 0.5F;
      if (s.plan == RigPlan::Bird) p.neck += 0.6F, p.head = 0.6F;
      if (biped) p.head = 0.55F, p.lean = 0.25F;
      p.eyes = 0, p.eyeStyle = 2;
      p.ears = -0.6F;
      p.shoulderH += f(0.45 * breath), p.hipH += f(0.2 * breath);
      p.squash += f(0.03 * breath);
      for (int i = 0; i < 12; ++i) p.tail[i] = -0.05F;
      break;
    }
    case PetPose::Dance: {
      const double e = std::exp(-in.kickAge * 6);
      p.squash = f(0.22 * e - 0.04);
      p.hipH -= f((biped ? 1.0 : 1.6) * e), p.shoulderH -= f(1.6 * e);
      p.eyeStyle = 1, p.mouth = 0.45F, p.blush = 1;
      p.head = f(0.18 * std::sin(kTau * t * 2));
      p.yaw = biped ? 0.8F : 0.35F;
      p.ears = f(0.4 * std::sin(kTau * t * 2 + 1));
      const int side = in.beat % 2;
      if (biped) {
        // an arm up on each beat, the hips swaying
        p.hand[side] = {f(1.5 - side * 2.5), f(-reach * 0.8 * std::max(0.3, 1 - in.kickAge * 1.5))};
        p.hand[1 - side] = {f(2.5), f(reach * 0.45)};
        p.lean = f(0.1 * std::sin(kTau * t));
        p.foot[2 + side].y = f(1.5 * std::max(0.0, 1 - in.kickAge * 3));
      } else {
        p.foot[side].y = f(3.5 * std::max(0.0, 1 - in.kickAge * 2.2));
        p.foot[side].x += 1.5F;
        p.pawCurl[side] = 0.8F;
      }
      for (int i = 0; i < 12; ++i) p.tail[i] = f(0.3 * std::sin(kTau * 3 * t - i * 0.7) * (i + 1) / 8.0);
      if (s.plan == RigPlan::Bird) p.wingOpen = 0.6F, p.wing = f(std::sin(kTau * 3 * t));
      if (s.plan == RigPlan::Serpent) p.wave = 1.2F, p.lift += f(2 * e);
      break;
    }
    default: break;
  }

  if (in.mode == PetPose::Walk && in.pace > 0.05) {
    const double run = std::clamp(in.pace - 1, 0.0, 1.0);
    if (quad) {
      p.neck += f(0.12 + 0.2 * run);
      p.head += f(0.08 + 0.1 * run);
      p.tailUp -= f(0.25 + 0.25 * run);
      p.mouth = f(0.35 * run), p.tongue = f(run > 0.5 ? 1 : 0);
    }
    p.ears += f(0.3 - 0.8 * run);
    if (biped) p.yaw = 0.3F, p.lean += f(0.04 * run);
    if (s.plan == RigPlan::Serpent) p.wave = f(0.7 + 0.3 * run);
  }

  // in the air: the crouch, the leap stretched out, reaching for the ground
  if (in.crouch >= 0) {
    const double c = smooth(0, 1, in.crouch);
    p.hipH -= f((biped ? 2.2 : 3) * c), p.shoulderH -= f(3.5 * c);
    p.squash = f(0.25 * c);
    p.ears = f(-0.6 * c);
    p.tailUp -= f(0.4 * c);
    p.neck += f(0.3 * c);
    if (biped) p.lean = f(0.25 * c), p.hand[0].x -= f(2 * c), p.hand[1].x -= f(2 * c);
    if (s.wings != RigWing::None) p.wingOpen = f(c), p.wing = f(-c);
  } else if (!in.grounded) {
    const bool up = in.vy < 0;
    if (quad) {
      p.foot[0] = {f(up ? 5 : 3), f(up ? 6 : 3)};
      p.foot[1] = {f(up ? 6 : 4), f(up ? 5 : 2.5)};
      p.foot[2] = {f(up ? -6 : 1), f(up ? 2 : 5)};
      p.foot[3] = {f(up ? -5 : 2), f(up ? 2.5 : 5.5)};
      p.pawCurl[0] = p.pawCurl[1] = up ? 0.2F : 0.6F;
      p.pawCurl[2] = p.pawCurl[3] = 0.4F;
      p.squash = f(up ? -0.2 : -0.1);
      p.neck += f(up ? -0.2 : 0.25);
      p.head += f(in.jumped && !up ? 0.35 : 0);
      p.ears = f(up ? -0.6 : 0.6);
      p.tailUp += f(up ? -0.3 : 0.5);
    } else if (biped) {
      // knees up, arms out; coming down, the feet reach for the ground
      p.foot[2] = {f(up ? 2.5 : 1.5), f(up ? 3.5 : 1.0)};
      p.foot[3] = {f(up ? -1.5 : -0.5), f(up ? 2.0 : 0.6)};
      p.hand[0] = {f(up ? 3.5 : 4.0), f(up ? -reach * 0.5 : reach * 0.2)};
      p.hand[1] = {f(up ? -3.0 : -3.5), f(up ? -reach * 0.3 : reach * 0.25)};
      p.lean = f(up ? -0.08 : 0.08);
      p.eyes = 1, p.mouth = up ? 0.3F : 0.0F;
    } else if (s.plan == RigPlan::Blob) {
      p.squash = f(up ? -0.25 : -0.1);
    } else {
      p.foot[2].y = p.foot[3].y = 2;
      p.squash = f(up ? -0.15 : 0.05);
    }
    if (s.wings != RigWing::None) {
      p.wingOpen = 1;
      // beating up, the downstroke 60 % of the beat; gliding down, spread and still
      const double b = std::fmod(in.airTime * 3.2, 1.0);
      p.wing = f(up ? (b < 0.6 ? -1 + 2 * smooth(0, 0.6, b) : 1 - 2 * smooth(0.6, 1, b)) : -0.5 + 0.15 * std::sin(kTau * 1.2 * in.airTime));
    }
  } else if (in.landAge < 0.35 && in.landHard > 0) {
    const double k = in.landAge < 0.06 ? 1 : std::max(0.0, 1 - (in.landAge - 0.06) / 0.29);
    p.hipH -= f((biped ? 2 : 3) * in.landHard * k), p.shoulderH -= f(3.5 * in.landHard * k);
    p.squash += f(0.3 * in.landHard * k);
    p.ears += f(0.8 * in.landHard * k);
    if (biped) p.lean += f(0.2 * in.landHard * k), p.hand[0].x += f(1.5 * k), p.hand[1].x -= f(1.5 * k);
  }

  // what it does on its own
  const double a = in.actAge, d = std::max(0.01, in.actDur), e = plateau(a, d, std::min(0.35, d * 0.3), std::min(0.4, d * 0.3));
  const RigPose sit = sitPose(s);
  switch (in.act) {
    case A::LookAtYou: p.yaw = f(std::max<double>(p.yaw, e)), p.ears += f(0.5 * e), p.blush = f(e > 0.6 ? 1 : 0); break;
    case A::HeadTilt: p.head += f(0.4 * e), p.yaw = f(std::max<double>(p.yaw, 0.55 * e)), p.ears += f(0.8 * e); break;
    case A::Groom: {
      p = rigMix(p, sit, f(smooth(0, 0.3, a) * (1 - smooth(d - 0.3, d, a))));
      const double lick = e * (0.5 + 0.5 * std::sin(kTau * 3 * a));
      p.foot[0] = {f(2.5 * e), f(9 * e)};
      p.pawCurl[0] = f(e);
      p.neck += f(0.55 * e), p.head += f(0.55 * e + 0.15 * lick);
      p.mouth = f(0.3 * e), p.tongue = f(e > 0.5 ? 1 : 0);
      p.eyes = f(1 - e), p.eyeStyle = e > 0.5 ? 2 : 0;
      break;
    }
    case A::Scratch: {
      p = rigMix(p, sit, f(smooth(0, 0.25, a) * (1 - smooth(d - 0.25, d, a))));
      const double fast = std::sin(kTau * 9 * a);
      p.foot[2] = {f(10 * e), f((11 + 1.4 * fast) * e)};
      p.pawCurl[2] = f(e);
      p.head += f(0.35 * e), p.yaw = f(0.3 * e);
      p.eyeStyle = e > 0.5 ? 1 : 0;
      p.ears += f(-0.4 * e + 0.2 * fast * e);
      p.mouth = f(0.25 * e);
      break;
    }
    case A::Stretch: {
      const double y = plateau(a - d * 0.3, d * 0.5, 0.2, 0.2);
      if (biped) {
        // up on its toes, both arms high
        p.hand[0] = {f(0.6 + (1 - e) * p.hand[0].x), f(p.hand[0].y + (-reach * 0.95 - p.hand[0].y) * e)};
        p.hand[1] = {f(-0.4), f(p.hand[1].y + (-reach * 0.95 - p.hand[1].y) * e)};
        p.hipH += f(0.8 * e), p.lean -= f(0.08 * e), p.head -= f(0.2 * e);
      } else if (quad) {
        // the play bow: forelegs out in front, chest down, rump and tail up, a yawn
        p.shoulderH -= f((s.shoulderH * 0.55) * e);
        p.hipH += f(0.8 * e);
        p.foot[0].x += f(8 * e), p.foot[1].x += f(8.5 * e);
        p.neck += f(0.5 * e), p.head -= f(0.2 * e);
        p.tailUp += f(0.6 * e);
        p.ears -= f(0.6 * e);
      } else {
        p.squash -= f(0.2 * e), p.wingOpen = f(e * (s.wings != RigWing::None ? 1 : 0));
      }
      p.mouth = f(y), p.eyes = f(1 - y), p.eyeStyle = y > 0.4 ? 2 : 0;
      break;
    }
    case A::Yawn:
      p.mouth = f(e), p.eyes = f(1 - e), p.eyeStyle = e > 0.4 ? 2 : 0;
      p.neck -= f(0.25 * e), p.head -= f(0.35 * e), p.ears -= f(0.7 * e);
      if (biped) p.hand[0] = {f(p.hand[0].x + (1.5 - p.hand[0].x) * e), f(p.hand[0].y + (-s.headR * 0.9 - p.hand[0].y) * e)};
      break;
    case A::Sniff:
      p.neck += f(0.75 * e), p.head += f(0.6 * e);
      p.head += f(0.05 * std::sin(kTau * 9 * a) * e);
      p.ears += f(0.6 * e), p.tailUp += f(0.2 * e);
      break;
    case A::Shake: {
      const double w = std::sin(kTau * 4.5 * a) * e;
      p.x += f(0.8 * w), p.head += f(0.25 * w), p.yaw = f(0.4 * std::abs(w));
      p.eyeStyle = 1, p.ears = f(-0.6 + 0.6 * w);
      for (int i = 0; i < 12; ++i) p.tail[i] += f(0.35 * std::sin(kTau * 4.5 * a - i * 0.6) * e);
      break;
    }
    case A::Wag:
      p.eyeStyle = 1, p.mouth = f(0.4 * e), p.ears += f(0.4 * e), p.yaw = f(0.5 * e);
      for (int i = 0; i < 12; ++i) p.tail[i] += f(0.45 * std::sin(kTau * 3.2 * a - i * 0.6) * e * (i + 1) / 6.0);
      break;
    case A::Howl:
      p = rigMix(p, sit, f(e));
      p.neck -= f(0.35 * e), p.head -= f(0.75 * e), p.mouth = f(0.75 * e), p.eyes = f(1 - e), p.eyeStyle = 2, p.ears -= f(0.5 * e);
      for (int i = 0; i < 3; ++i) p.heads[i] -= f(0.2 * e * std::sin(i * 1.3 + 0.4));
      break;
    case A::Pant:
      p.mouth = f(0.6 * e), p.tongue = f(e), p.shoulderH += f(0.5 * std::sin(kTau * 4 * a) * e), p.eyeStyle = 1;
      for (int i = 0; i < 3; ++i) p.heads[i] += f(0.12 * std::sin(kTau * 2.5 * a + i * 2.09) * e);
      break;
    case A::Preen: p.neck += f(1.2 * e), p.head += f(0.9 * e), p.yaw = 0, p.wing = f(0.4 * std::sin(kTau * 3 * a) * e), p.eyes = f(1 - e); break;
    case A::Flap: {
      p.wingOpen = f(e), p.wing = f(std::sin(kTau * 3 * a));
      if (s.plan != RigPlan::Quad) p.lift = f(1.5 * e * std::max(0.0, std::sin(kTau * 3 * a)));
      break;
    }
    case A::Peck: {
      const double k = std::max(0.0, std::sin(kTau * 2.5 * a)) * e;
      p.neck += f(1.0 * k), p.head += f(0.9 * k);
      break;
    }
    case A::Wobble: p.squash = f(0.25 * std::sin(kTau * 3 * a) * e), p.eyeStyle = 1, p.x += f(0.6 * std::sin(kTau * 1.5 * a) * e); break;
    case A::Hop: {
      const double h = std::max(0.0, std::sin(M_PI * a / d));
      p.lift = f(p.lift + 4 * h), p.squash = f(a < 0.12 ? 0.25 : -0.15 * h);
      if (biped) p.hand[0].y -= f(reach * 0.6 * h), p.hand[1].y -= f(reach * 0.6 * h), p.eyeStyle = 1;
      break;
    }
    case A::LieDown: p = rigMix(p, liePose(s), f(e)); break;
    case A::Roll: {
      p = rigMix(p, liePose(s), f(e));
      const double r = std::sin(kTau * 1.5 * a) * e;
      p.foot[0].y = p.foot[1].y = f(4 + 3 * r), p.foot[2].y = p.foot[3].y = f(3 - 2 * r);
      p.pawCurl[0] = p.pawCurl[1] = 1;
      p.eyeStyle = 1, p.mouth = 0.4F, p.yaw = 0.6F, p.blush = 1;
      break;
    }
    case A::Wave: {
      // hello: the near hand up, waving
      const double w = std::sin(kTau * 2.8 * a);
      p.hand[0] = {f(p.hand[0].x + (2.0 + 1.2 * w - p.hand[0].x) * e), f(p.hand[0].y + (-reach * 0.75 - p.hand[0].y) * e)};
      p.yaw = f(std::max<double>(p.yaw, 0.9 * e)), p.eyeStyle = e > 0.5 ? 1 : 0, p.blush = f(e), p.mouth = f(0.3 * e);
      break;
    }
    case A::Meditate: {
      // seated in the air, eyes shut, hands together, qi gathering round it
      p = rigMix(p, sit, f(e));
      p.lift += f((s.floats || biped ? 3.0 : 0) * e + 0.6 * std::sin(kTau * a / 2.4) * e);
      p.hand[0] = {f(2.2), f(reach * 0.5)}, p.hand[1] = {f(1.8), f(reach * 0.5)};
      p.hand[0] = {p.hand[0].x * f(e) + sit.hand[0].x * f(1 - e), p.hand[0].y};
      p.eyes = f(1 - e), p.eyeStyle = e > 0.5 ? 2 : 0;
      p.aura = f(e);
      p.yaw = 0.75F;
      break;
    }
    case A::Spin:
      // a staff twirled, an orb turned in the hand
      p.hand[0] = {f(p.hand[0].x + (3.0 - p.hand[0].x) * e), f(p.hand[0].y + (reach * 0.3 - p.hand[0].y) * e)};
      p.aura = f(s.prop == RigProp::Orb ? e : 0), p.eyeStyle = e > 0.5 ? 1 : 0, p.yaw = f(std::max<double>(p.yaw, 0.7 * e));
      if (s.plan == RigPlan::Blob) p.x += f(0.8 * std::sin(kTau * 2 * a) * e), p.squash = f(0.1 * std::sin(kTau * 4 * a) * e);
      break;
    case A::SwordDance:
      p.hand[0] = {f(p.hand[0].x + (reach * 0.8 - p.hand[0].x) * e), f(p.hand[0].y + (-1.0 - p.hand[0].y) * e)};
      p.aura = f(e), p.lean = f(0.08 * e), p.yaw = f(0.6 * e);
      break;
    case A::Scan:
      p.aura = f(std::fmod(a / 0.8, 1.0));
      p.yaw = f(0.5 + 0.5 * std::sin(kTau * a / d));
      p.head += f(0.08 * std::sin(kTau * 2 * a / d));
      break;
    case A::Rattle: {
      const double w = std::sin(kTau * 9 * a) * e;
      p.x += f(0.5 * w), p.head += f(0.15 * w), p.squash = f(0.06 * std::abs(w)), p.mouth = f(0.5 * e);
      p.hand[0].y -= f(1.5 * std::abs(w)), p.hand[1].y -= f(1.5 * std::abs(w));
      break;
    }
    case A::ScratchHead: {
      const double w = std::sin(kTau * 5 * a) * e;
      p.hand[0] = {f(p.hand[0].x + (0.8 + 0.5 * w - p.hand[0].x) * e), f(p.hand[0].y + (-s.headR * 1.55 - p.hand[0].y) * e)};
      p.head += f(0.12 * e), p.yaw = f(0.8 * e), p.eyeStyle = e > 0.5 ? 1 : 0, p.blush = f(e);
      break;
    }
    case A::Bow:
      p.lean = f(p.lean + 0.55 * e), p.head += f(0.3 * e), p.eyes = f(1 - e), p.eyeStyle = e > 0.5 ? 2 : 0;
      p.hand[0].x -= f(0.8 * e), p.hand[1].x -= f(0.8 * e);
      break;
    case A::Coil: p.wave = f(0.6 + 0.7 * e), p.head += f(0.25 * std::sin(kTau * a / d) * e), p.lift -= f(3 * e); break;
    default: break;
  }

  // carried: it hangs, legs dangling and kicking a little, tail down, looking at you
  if (in.carried) {
    const double k = std::sin(kTau * 2.2 * t);
    if (quad) {
      p.hipH += 3, p.shoulderH += 4;
      for (int i = 0; i < 4; ++i) p.foot[i] = {f(0.4 + 0.8 * k * (i % 2 ? 1 : -1)), f(-1.5)}, p.pawCurl[i] = 0.3F;
      p.tailUp = -1.1F, p.neck = -1.25F;
    } else if (biped) {
      p.foot[2] = {f(0.8 * k), -1.5F}, p.foot[3] = {f(-0.8 * k), -1.5F};
      p.hand[0] = {2.5F, -reach * 0.6F}, p.hand[1] = {-2.0F, -reach * 0.5F};
    } else {
      p.squash = f(-0.15 + 0.05 * k);
    }
    p.ears = -0.4F, p.yaw = 0.8F, p.eyes = 1, p.eyeStyle = 0, p.mouth = 0.35F, p.blush = 1;
    if (s.wings != RigWing::None) p.wingOpen = 0.8F, p.wing = f(0.6 * k);
  }

  // something called it: ears up, it looks at you
  if (in.alert >= 0 && in.alert < 2.5) {
    const double k = plateau(in.alert, 2.5, 0.15, 0.6);
    p.ears = f(p.ears + (1 - p.ears) * k), p.yaw = f(std::max<double>(p.yaw, k)), p.eyes = 1, p.eyeStyle = 0;
    p.head -= f(0.15 * k);
    if (biped) p.hand[1] = {f(p.hand[1].x + (1.5 - p.hand[1].x) * k), p.hand[1].y};
  }
  // patted: eyes shut in contentment, ears back, a wag
  if (in.petted >= 0 && in.petted < 2.2) {
    const double k = plateau(in.petted, 2.2, 0.15, 0.6);
    p.eyeStyle = 1, p.blush = 1, p.ears = f(p.ears - 0.7 * k), p.yaw = f(std::max<double>(p.yaw, 0.5 * k));
    p.head += f(0.15 * k * std::sin(kTau * 1.5 * in.petted));
    for (int i = 0; i < 12; ++i) p.tail[i] += f(0.35 * std::sin(kTau * 3 * in.petted - i * 0.6) * k);
    if (s.plan == RigPlan::Blob) p.squash = f(0.15 * k * std::sin(kTau * 2 * in.petted));
  }
  if (in.blink && p.eyeStyle == 0) p.eyes = 0;
  return p;
}

// ── Stepping ────────────────────────────────────────────────────────────────

void PetLife::step(const LifeIn& in) {
  if (!m_kind) return;
  const RigSpec& s = m_kind->spec;
  RigPose tgt = target(in);
  // looking about while it stands or sits: now here, now there, now at you
  m_lookNext -= in.dt;
  if (m_lookNext <= 0) {
    m_lookNext = 1.2 + rnd() * 3.5;
    const double r = rnd();
    m_lookYaw = r < 0.18 ? 1 : r < 0.35 ? 0.45 : 0;
    m_lookPitch = (rnd() - 0.55) * 0.35;
    if (rnd() < 0.4) m_earTwitch = 0;
  }
  const bool calm = in.mode == PetPose::Idle || (in.mode == PetPose::Sit && !in.lie);
  if (calm && in.act == RigAct::None && in.alert < 0 && in.petted < 0 && in.turn < 0) {
    const float rest = rigStand(s).yaw;
    tgt.yaw = f(std::max<double>(tgt.yaw, rest + (1 - rest) * m_lookYaw));
    tgt.head += f(m_lookPitch);
  }
  if (m_earTwitch >= 0) {
    m_earTwitch += in.dt;
    if (m_earTwitch < 0.25) tgt.ears += f(0.9 * std::sin(M_PI * m_earTwitch / 0.25));
    else m_earTwitch = -1;
  }
  if (in.turn >= 0) tgt.yaw = f(std::max<double>(tgt.yaw, std::sin(M_PI * in.turn)));

  if (!m_init) {
    m_base = tgt;
    m_init = true;
  }
  auto ease = [&](float& v, float to, double tau) { v += f((to - v) * (1 - std::exp(-in.dt / tau))); };
  const double body = in.mode == PetPose::Dance || !in.grounded || in.crouch >= 0 || in.landAge < 0.35 ? 0.05 : 0.16;
  ease(m_base.x, tgt.x, 0.06);
  ease(m_base.shoulderH, tgt.shoulderH, body);
  ease(m_base.hipH, tgt.hipH, body * 1.15);
  ease(m_base.lean, tgt.lean, body);
  ease(m_base.neck, tgt.neck, 0.12);
  ease(m_base.head, tgt.head, 0.09);
  for (int i = 0; i < 3; ++i) ease(m_base.heads[i], tgt.heads[i], 0.07 + 0.03 * i);
  ease(m_base.yaw, tgt.yaw, 0.11);
  ease(m_base.ears, tgt.ears, 0.06);
  ease(m_base.mouth, tgt.mouth, 0.06);
  ease(m_base.tongue, tgt.tongue, 0.08);
  ease(m_base.blush, tgt.blush, 0.2);
  ease(m_base.tailUp, tgt.tailUp, 0.18);
  ease(m_base.squash, tgt.squash, 0.04);
  ease(m_base.lift, tgt.lift, 0.07);
  ease(m_base.wingOpen, tgt.wingOpen, 0.08);
  ease(m_base.wing, tgt.wing, 0.03);
  ease(m_base.wave, tgt.wave, 0.4);
  ease(m_base.aura, tgt.aura, 0.25);
  m_base.eyes = tgt.eyes, m_base.eyeStyle = tgt.eyeStyle;
  for (int i = 0; i < 4; ++i) {
    ease(m_base.foot[i].x, tgt.foot[i].x, body * 0.8);
    ease(m_base.foot[i].y, tgt.foot[i].y, body * 0.6);
    ease(m_base.pawCurl[i], tgt.pawCurl[i], 0.06);
  }
  for (int i = 0; i < 2; ++i) {
    ease(m_base.hand[i].x, tgt.hand[i].x, 0.09);
    ease(m_base.hand[i].y, tgt.hand[i].y, 0.09);
  }
  // what shows: the eased pose, with the gait and the springs over it (never fed back into the easing)
  m_pose = m_base;
  // the gait owns the feet (and a biped's arms) while it walks: exact, so they stay planted
  const bool walking = in.mode == PetPose::Walk && in.grounded && in.crouch < 0;
  ease(m_gaitW, walking && in.pace > 0.05 ? 1.0F : 0.0F, 0.08);
  if (m_gaitW > 0.01) {
    const double trot = std::clamp(in.pace - 1, 0.0, 1.0);
    RigPose still = m_pose, g = m_pose;
    rigGait(s, g, in.phase, trot);
    const float w = m_gaitW;
    for (int i = 0; i < 4; ++i) {
      m_pose.foot[i] = {still.foot[i].x + (g.foot[i].x - still.foot[i].x) * w, still.foot[i].y + (g.foot[i].y - still.foot[i].y) * w};
      m_pose.pawCurl[i] += (g.pawCurl[i] - still.pawCurl[i]) * w;
    }
    for (int i = 0; i < 2; ++i) m_pose.hand[i] = {still.hand[i].x + (g.hand[i].x - still.hand[i].x) * w, still.hand[i].y + (g.hand[i].y - still.hand[i].y) * w};
    m_pose.hipH += (g.hipH - still.hipH) * w;
    m_pose.shoulderH += (g.shoulderH - still.shoulderH) * w;
    m_pose.head += (g.head - still.head) * w;
    m_pose.lean += (g.lean - still.lean) * w;
    m_pose.lift += (g.lift - still.lift) * w;
    m_pose.squash += (g.squash - still.squash) * w;
  }
  // a serpent's body, a ghost's hem, flames: a wave that never stops, quicker as it goes
  m_wave += in.dt * (1.6 + 3.2 * std::clamp(in.pace, 0.0, 2.0) + (in.act == RigAct::Coil ? 2.5 : 0));
  m_pose.wavePhase = f(std::fmod(m_wave, 1000.0));
  // props: spun while it plays with them, then settled back where they rest
  const bool spinning = in.act == RigAct::Spin || in.act == RigAct::SwordDance;
  if (spinning) m_prop += in.dt * (in.act == RigAct::SwordDance ? 0.9 : 1.3) * plateau(in.actAge, in.actDur, 0.3, 0.3) * 1.6;
  else m_prop += (std::round(m_prop) - m_prop) * (1 - std::exp(-in.dt / 0.15));
  m_pose.prop = f(m_prop - std::floor(m_prop));

  // springs: the tail, the ears, a robe's hem, a cape, hair and whiskers lag
  // the body and overshoot. Driven by the body's vertical acceleration (a hop,
  // a landing, the walk's bob) and by its pace.
  const double accY = (in.vy - m_prevVy) / std::max(1e-3, in.dt);
  m_prevVy = in.vy;
  const double drive = std::clamp(accY, -4000.0, 4000.0);
  const int n = std::clamp(s.tailSegs, 2, 12);
  const double pace = std::clamp(in.mode == PetPose::Walk ? in.pace : 0.0, 0.0, 2.0) + (in.grounded ? 0 : 0.6);
  double left = in.dt;
  while (left > 1e-6) {
    const double h = std::min(left, 1.0 / 120);
    left -= h;
    for (int i = 0; i < n; ++i) {
      const double k = 150 * (1 - 0.055 * i), c = 2 * std::sqrt(k) * 0.32;
      const double parent = i > 0 ? m_tail[i - 1] : 0;
      double acc = k * (tgt.tail[i] - m_tail[i]) - c * m_tailV[i] + 7.5 * (parent - m_tail[i]);
      acc += drive * 0.0009 * (i + 1) / n;
      acc += (in.mode == PetPose::Walk ? 0.4 * std::sin(kTau * in.phase * 2 - i * 0.5) : 0) * 12;
      m_tailV[i] += f(acc * h);
      m_tail[i] = std::clamp(m_tail[i] + m_tailV[i] * f(h), -0.9F, 0.9F);
    }
    for (int i = 0; i < 4; ++i) {
      // the hem trails the pace and flutters with it
      const double k = 90 - 12 * i, c = 2 * std::sqrt(k) * 0.3;
      const double to = 0.22 * pace + 0.05 * pace * std::sin(kTau * 1.8 * in.time + i * 0.9) + 0.03 * std::sin(m_wave * 0.7 + i);
      const double parent = i > 0 ? m_hem[i - 1] : 0;
      double acc = k * (to - m_hem[i]) - c * m_hemV[i] + 6 * (parent - m_hem[i]) - drive * 0.0006;
      m_hemV[i] += f(acc * h);
      m_hem[i] = std::clamp(m_hem[i] + m_hemV[i] * f(h), -0.8F, 0.8F);
    }
    const double k = 220, c = 2 * std::sqrt(k) * 0.28;
    const double acc = -k * m_flop - c * m_flopV + drive * 0.0018;
    m_flopV += f(acc * h);
    m_flop = std::clamp(m_flop + m_flopV * f(h), -0.8F, 0.8F);
  }
  // No travelling waves (2026-11-09): a tail swings as one stiff piece from its
  // root, hems and capes hang still, a serpent keeps its shape.
  for (int i = 0; i < 12; ++i) m_pose.tail[i] = i == 0 ? m_tail[0] : 0;
  for (int i = 0; i < 4; ++i) m_pose.hem[i] = 0;
  m_pose.wavePhase = 0;  // the serpent's curve and the ghost's hem hold their shape
  m_pose.earFlop = m_flop;
}

// ── Drawing ─────────────────────────────────────────────────────────────────

namespace {
// marks: 1 outline, 2 fill, 3 light
constexpr const char* kZBig[] = {"11111", "12221", "11121", ".1211", "11211", "12111", "12221", "11111"};
constexpr const char* kZSmall[] = {"1111", "1221", "1121", "1211", "1221", "1111"};
constexpr const char* kNote[] = {"..111", "..121", "..121", "..121", "11121", "12221", "12221", "1111."};
constexpr const char* kHeart[] = {".11.11.", "1221221", "1232221", "1222221", ".12221.", "..121..", "...1..."};
constexpr const char* kAlert[] = {"111", "121", "121", "121", "111", "121", "111"};
constexpr const char* kQuestion[] = {"11111", "12221", "11121", ".1221", ".121.", ".111.", ".121.", ".111."};
constexpr const char* kSweat[] = {".1.", "121", "131", "121", ".1."};

void stamp(std::uint8_t* out, int side, const char* const* rows, int nrows, int x, int y, Color fill, float fade) {
  if (fade <= 0.05F) return;
  const Color line{0.14F, 0.11F, 0.17F, 1};
  const Color light{std::min(1.0F, fill.r + 0.25F), std::min(1.0F, fill.g + 0.25F), std::min(1.0F, fill.b + 0.25F), 1};
  // whole, inside the grid with a pixel to spare (never cut at the edge)
  const int w = static_cast<int>(std::string_view(rows[0]).size());
  x = std::clamp(x, 1, side - 1 - w), y = std::clamp(y, 1, side - 1 - nrows);
  for (int r = 0; r < nrows; ++r)
    for (int c = 0; rows[r][c]; ++c) {
      const char ch = rows[r][c];
      if (ch == '.') continue;
      const int px = x + c, py = y + r;
      if (px < 0 || py < 0 || px >= side || py >= side) continue;
      // fading out: drop pixels in a fixed dither pattern (the alpha stays binary)
      static constexpr float bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
      if (fade < 1 && (bayer[py & 3][px & 3] + 0.5F) / 16.0F > fade) continue;
      const Color col = ch == '1' ? line : ch == '3' ? light : fill;
      std::uint8_t* o = &out[(py * side + px) * 4];
      o[0] = static_cast<std::uint8_t>(col.r * 255), o[1] = static_cast<std::uint8_t>(col.g * 255), o[2] = static_cast<std::uint8_t>(col.b * 255), o[3] = 255;
    }
}

// the topmost and leftmost/rightmost opaque pixel: where to hang the marks
void bounds(const std::uint8_t* px, int side, int& top, int& left, int& right) {
  top = side, left = side, right = 0;
  for (int y = 0; y < side; ++y)
    for (int x = 0; x < side; ++x)
      if (px[(y * side + x) * 4 + 3]) top = std::min(top, y), left = std::min(left, x), right = std::max(right, x);
}
}  // namespace

void PetLife::draw(const RigPalette& pal, bool flip, const LifeIn& in, RigMark mark, double age, RigPixels& out) const {
  rigDraw(m_kind->spec, m_pose, pal, flip, out);
  // turning round: the picture narrows to an edge and opens the other way
  if (in.turn >= 0) {
    const double sx = std::max(0.35, std::abs(std::cos(M_PI * in.turn)));
    RigPixels src = out;
    const float cx = kRigPixels * 0.5F;
    for (int y = 0; y < kRigPixels; ++y)
      for (int x = 0; x < kRigPixels; ++x) {
        const int sxp = static_cast<int>(std::floor(cx + (x + 0.5F - cx) / sx));
        std::uint8_t* o = &out[(y * kRigPixels + x) * 4];
        if (sxp < 0 || sxp >= kRigPixels) {
          o[0] = o[1] = o[2] = o[3] = 0;
          continue;
        }
        const std::uint8_t* q = &src[(y * kRigPixels + sxp) * 4];
        o[0] = q[0], o[1] = q[1], o[2] = q[2], o[3] = q[3];
      }
  }
  drawRigMark(out.data(), kRigPixels, mark, age, flip, pal);
}

void drawRigMark(std::uint8_t* out, int frameSide, RigMark mark, double age, bool flip, const RigPalette& pal) {
  if (mark == RigMark::None) return;
  int top, left, right;
  bounds(out, frameSide, top, left, right);
  if (top >= frameSide) return;
  // over the head: the head is at the front, the side it faces
  const int headX = flip ? left + 2 : right - 9;
  const int side = flip ? -1 : 1;
  switch (mark) {
    case RigMark::Sleep:
      for (int k = 0; k < 2; ++k) {
        const double ph = std::fmod(age * 0.45 + k * 0.5, 1.0);
        const int x = headX + side * static_cast<int>(2 + ph * 7) + (flip ? -4 : 2);
        const int y = top - 4 - static_cast<int>(ph * 12);
        const float fade = f(std::min(1.0, (1 - ph) * 2.5) * std::min(1.0, ph * 6));
        if (k == 0) stamp(out, frameSide, kZBig, 8, x, y, pal.belly, fade);
        else stamp(out, frameSide, kZSmall, 6, x, y, pal.belly, fade);
      }
      break;
    case RigMark::Notes: {
      const double ph = std::fmod(age * 0.8, 1.0);
      stamp(out, frameSide, kNote, 8, headX + side * static_cast<int>(3 + ph * 5), top - 6 - static_cast<int>(ph * 9), pal.body, f(std::min(1.0, (1 - ph) * 2.5)));
      break;
    }
    case RigMark::Hearts:
      for (int k = 0; k < 2; ++k) {
        const double ph = age * 0.7 - k * 0.45;
        if (ph < 0 || ph > 1) continue;
        stamp(out, frameSide, kHeart, 7, headX - 2 + side * k * 6 + static_cast<int>(std::sin(ph * 9) * 1.5), top - 4 - static_cast<int>(ph * 12), Color{0.93F, 0.36F, 0.47F, 1},
              f(std::min(1.0, (1 - ph) * 3)));
      }
      break;
    case RigMark::Alert: {
      const int bounce = age < 0.25 ? static_cast<int>(3 - age * 12) : 0;
      stamp(out, frameSide, kAlert, 7, headX + 2, top - 9 - bounce, Color{0.98F, 0.8F, 0.25F, 1}, f(std::min(1.0, (2.6 - age) * 2)));
      break;
    }
    case RigMark::Question:
      stamp(out, frameSide, kQuestion, 8, headX + 1, top - 10, Color{0.6F, 0.82F, 1.0F, 1}, f(std::min(1.0, (2.6 - age) * 2)));
      break;
    case RigMark::Sweat: {
      const double ph = std::fmod(age * 1.2, 1.0);
      stamp(out, frameSide, kSweat, 5, headX + side * 7, top + 2 + static_cast<int>(ph * 5), Color{0.55F, 0.8F, 1.0F, 1}, f(1 - ph));
      break;
    }
    default: break;
  }
}

}  // namespace undershell
