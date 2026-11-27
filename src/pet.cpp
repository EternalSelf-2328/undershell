// SPDX-License-Identifier: GPL-3.0-or-later
// The pet: a selectable pixel-art companion that lives on your desktop: a
// rigged creature drawn part by part (petrig, petlife, petkinds) or one of the
// drawn ones (original sprite atlases in art/pets). It walks along the line of
// its box, hops up onto the tops of windows (Umbriel says where they are) and
// falls when the window under it goes; it dances on the beat while music
// plays and sleeps while you are away or after a long silence. It tells you
// of notifications, a low battery, the hour and a break due, in a bubble; a
// click pats it, a drag carries it. Drawn over the windows, click-through but
// for the creature itself: the overlay layer, a surface the size of its box
// that moves with it.
//
// How it moves follows how animals and pixel-art characters move (the
// sources are in docs/PETS.md): steps cover the ground they should, so the
// feet do not slide; the body is lowest just after a foot lands and highest
// as the legs pass; it crouches before it jumps, stretches as it rises and
// squashes on landing as hard as it fell; it turns round instead of
// mirroring at once; and while it stands about it does the small things its
// kind does, now and then and never twice in a row. The frame itself is
// made in petmotion.cpp, on a 12 Hz clock as pixel art is animated (slower
// at rest); the position moves at 30 fps.
#include "pet.hpp"

#include "colorpick.hpp"
#include "notifywatch.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>

namespace undershell {

namespace {
template <typename T>
T get(const toml::table& t, std::string_view key, T def) {
  if (auto v = t[key].value<T>()) return *v;
  return def;
}
Color withA(Color c, float a) {
  c.a *= a;
  return c;
}
constexpr double kGravity = 1500;  // logical px/s²
constexpr double kTurn = 0.2;      // a turn round, s
constexpr double kRigTurn = 0.36;  // a rigged companion turns its head to you on the way round
constexpr double kTau = 6.283185307179586;
using Q = PetWidget::Quirk;
using G = PetGait;

double fract(double v) { return v - std::floor(v); }
double smooth(double a, double b, double v) {
  const double t = std::clamp((v - a) / (b - a), 0.0, 1.0);
  return t * t * (3 - 2 * t);
}
double hump(double age, double dur) { return age <= 0 || age >= dur ? 0 : std::sin(M_PI * age / dur); }
// rises in `in`, holds, falls in the last `out` of `dur`
double plateau(double age, double dur, double in, double out) {
  return smooth(0, in, age) * (1 - smooth(dur - out, dur, age));
}
double pulse(double v, double a, double b) { return v > a && v < b ? std::sin(M_PI * (v - a) / (b - a)) : 0; }

// What each companion does while it stands about. LookBack turns it round
// and back; Pounce is the fox's mousing leap.
std::vector<Q> quirksOf(PetKind kind) {
  if (kind == PetKind::Sprout || kind > PetKind::MoonRangerChibi) return {};
  const int i = (static_cast<int>(kind) - static_cast<int>(PetKind::LongClassic)) % kPetFamilySize;
  switch (i) {
    case 0: return {Q::Coil, Q::LookBack, Q::Coil};
    case 1: return {Q::HeadToss, Q::Stamp, Q::TailSwish, Q::LookBack};
    case 2: return {Q::Peck, Q::TailFan, Q::WingRuffle, Q::LookBack};
    case 3: return {Q::Pant, Q::Shake, Q::TailSwish, Q::LookBack};
    case 4: return {Q::HeadToss, Q::Stamp, Q::WingStretch, Q::WingRuffle};
    case 5: return {Q::HeadSnap, Q::WingRuffle, Q::WingStretch, Q::TailSwish};
    case 6: return {Q::Pounce, Q::HeadTilt, Q::TailSwish, Q::Shake};
    case 7: return {Q::Howl, Q::Shake, Q::TailSwish, Q::LookBack};
    case 8: return {Q::WingStretch, Q::TailSwish, Q::HeadSnap, Q::LookBack};
    case 9: return {Q::HeadToss, Q::Stamp, Q::TailSwish, Q::LookBack};
    case 10: return {Q::VisorScan, Q::Servo, Q::LookBack};
    case 11: return {Q::VisorScan, Q::CapeBillow, Q::Servo, Q::LookBack};
    case 12: return {Q::OrbSpin, Q::CapeBillow, Q::LookBack};
    default: return {Q::CapeBillow, Q::HeadTilt, Q::LookBack};
  }
}
double quirkLength(Q q) {
  switch (q) {
    case Q::LookBack: return 1.4;
    case Q::Howl: return 3.0;
    case Q::Pant: return 1.8;
    case Q::Coil: case Q::OrbSpin: return 2.4;
    case Q::WingStretch: case Q::CapeBillow: return 1.6;
    case Q::VisorScan: return 1.3;
    case Q::Servo: return 0.5;
    case Q::Pounce: return 0;
    case Q::TailSwish: case Q::HeadSnap: return 1.2;
    default: return 0.9;
  }
}
}  // namespace

PetWidget::~PetWidget() {
  if (m_sprite) glDeleteTextures(1, &m_sprite);
}

void PetWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  const toml::table& t = cfg.options;
  const PetKind species = petKind(get<std::string>(t, "species", "sprout"));
  if (species != m_species) m_frameTick = -1, m_quirk = Quirk::None;
  m_species = species;
  m_motion = petMotion(m_species);
  const RigKind* rig = rigKind(m_species);
  if (rig != m_rig) {
    m_rig = rig;
    m_act = RigAct::None;
    if (rig) m_life.reset(rig, std::hash<std::string>{}(cfg.id) | 1);
  }
  if (m_species != PetKind::Sprout) {
    if (m_rig) m_motion.wings = m_rig->spec.flies;
    // painted frames, in the style asked (the other style stands in), else the rig draws it
    m_style = get<std::string>(t, "art", "chibi") == "pixel" ? FrameStyle::Pixel : FrameStyle::Chibi;
    const FrameAtlas* frames = m_rig ? frameAtlasOrOther(get<std::string>(t, "species", ""), m_style) : nullptr;
    if (frames != m_frames) m_frames = frames, m_frameTick = -1, m_cell = -1, m_tintFor = PetKind::Sprout;
    if (m_frames) m_frameCyc = frameCycle(*m_frames, m_rig->spec.plan);
    // a drawn or painted companion keeps its own colours unless asked: its "original" tint
    const bool drawn = !m_rig || m_frames;
    const std::vector<RigPreset>& presets = drawn ? classicTints() : m_rig->presets;
    const std::string variant = get<std::string>(t, "variant", "");
    m_variantPal = presets.front().palette;
    m_original = drawn;
    for (const RigPreset& p : presets)
      if (p.id == variant) m_variantPal = p.palette, m_original = drawn && p.id == "original";
    m_paint = get<std::string>(t, "paint", "variant");
    // the theme's colours: the body in the primary, markings in the secondary, eyes in the tertiary
    const Color primary = noct.color("primary", m_variantPal.body);
    m_themePal = {primary, rigTint(primary, 0.94F), noct.color("secondary", m_variantPal.mark), noct.color("tertiary", m_variantPal.eyes),
                  rigTint(noct.color("tertiary", m_variantPal.extra), 0.82F)};
    static constexpr const char* keys[5] = {"body_color", "belly_color", "mark_color", "eye_color", "detail_color"};
    bool anyOverride = false;
    for (int i = 0; i < 5; ++i) {
      const std::string v = get<std::string>(t, keys[i], "");
      m_overrides[i] = v.empty() ? std::nullopt : std::optional<Color>(noct.color(v, Color{1, 1, 1, 1}));
      if (m_overrides[i]) anyOverride = true;
    }
    if (drawn && (get<std::string>(t, "paint", "variant") != "variant" || anyOverride)) m_original = false;
    applyPalette();
    if (drawn) m_tintLut.clear(), m_frameTick = -1;
  }
  m_body = noct.color(get<std::string>(t, "color", "primary"), Color::fromHex("#7fd18b"));
  m_sprout = noct.color(get<std::string>(t, "sprout", "#7ed957"), Color::fromHex("#7ed957"));
  m_speed = std::clamp(get<double>(t, "speed", 1.0), 0.2, 3.0);
  m_climb = get<bool>(t, "climb", true);
  m_sleeps = get<bool>(t, "sleep", true);
  m_dances = get<bool>(t, "dance", true);
  m_opacity = std::clamp(get<double>(t, "opacity", 1.0), 0.05, 1.0);
  m_notify = get<std::string>(t, "notify", "title");
  m_notifyIgnore.clear();
  {
    // apps it does not tell you about, comma separated, any case
    std::string list = get<std::string>(t, "notify_ignore", ""), word;
    for (char ch : list + ",") {
      if (ch == ',') {
        while (!word.empty() && word.back() == ' ') word.pop_back();
        if (!word.empty()) m_notifyIgnore.push_back(word);
        word.clear();
      } else if (ch != ' ' || !word.empty()) {
        word += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      }
    }
  }
  m_greet = get<bool>(t, "greet", true);
  m_battery = get<bool>(t, "battery", true);
  m_hourly = get<bool>(t, "hourly", false);
  m_interact = get<bool>(t, "interact", true);
  m_breakEvery = std::clamp(get<double>(t, "breaks", 0.0), 0.0, 240.0);
  m_bubbleSecs = std::clamp(get<double>(t, "bubble_seconds", 5.0), 2.0, 20.0);
  m_lively = std::clamp(get<double>(t, "lively", 1.0), 0.2, 3.0);
  m_preview = get<std::string>(t, "pet_preview", "");
  if (m_preview != "idle" && m_preview != "sit" && m_preview != "walk" && m_preview != "jump" &&
      m_preview != "dance" && m_preview != "sleep") m_preview.clear();
  // a phase of its own, from its id: two pets on one desktop never breathe in step
  m_phi = static_cast<double>(std::hash<std::string>{}(cfg.id) % 1000) / 1000.0 * 4.0;
  if (!m_preview.empty()) m_frameTick = -1;
}

double PetWidget::rnd() {
  m_rng ^= m_rng << 13, m_rng ^= m_rng >> 7, m_rng ^= m_rng << 17;
  return static_cast<double>(m_rng >> 11) / static_cast<double>(1ULL << 53);
}

// what is right under the feet: the ground (the box's own bottom line) or
// the top of a window it stands over, whichever is nearest below
double PetWidget::support(double x, double y) const {
  double best = m_ground;
  if (!m_climb) return best;
  for (const Rect& w : m_windows)
    if (x >= w.x + 4 && x <= w.x + w.w - 4 && w.y >= y - 2 && w.y < best && w.y > 40) best = w.y;
  return best;
}

void PetWidget::pick(Mode m, double secs) {
  const bool resting = m_mode == Mode::Sit || m_mode == Mode::Sleep, rests = m == Mode::Sit || m == Mode::Sleep;
  if (!resting && rests) m_trans = 1, m_transAge = 0;                                 // settling down
  else if (resting && !rests) m_trans = m_mode == Mode::Sleep ? 3 : 2, m_transAge = 0;  // getting up (and a stretch)
  if (m != m_mode) m_quirk = Quirk::None;
  if (m_rig) {
    if (m == Mode::Walk && m_mode != Mode::Walk) m_pace = rnd() < 0.18 ? 2.1 : 1;  // now and then it runs
    if (m == Mode::Sit && !resting) m_lie = rnd() < 0.4;
    if (m_mode == Mode::Sleep && m != Mode::Sleep) m_wake = 0;  // a stretch when it gets up
  }
  m_mode = m;
  m_timer = secs;
}

// logical px per geometry unit of the rig, as it is drawn (whole device pixels per art pixel)
double PetWidget::unitPx() const {
  // the rig's geometry unit on screen; a painted frame (128 px) stands for the
  // rig's 96 px grid, so speeds and jumps stay the same size against the figure
  const int side = m_frames ? kPetPixels : kRigPixels;
  double art = m_artPx > 0 ? m_artPx : std::max(0.25, std::floor(std::min(m_boxW, m_boxH) / side));
  if (m_frames) art *= static_cast<double>(kPetPixels) / kRigPixels;
  return art * kRigUnit;
}

bool PetWidget::floats() const {
  if (m_rig) return m_rig->spec.floats;
  return m_species != PetKind::Sprout && (m_motion.gait == G::Serpent || m_motion.gait == G::Hover);
}

// logical px per second: a step covers `step` sprite px, so the cadence
// follows the speed and the size of the sprite
double PetWidget::walkSpeed() const {
  if (m_species == PetKind::Sprout) return 46 * m_speed;
  if (m_rig) return m_rig->spec.walk * unitPx() * m_speed * m_pace;
  return m_motion.speed * m_pixel * m_speed;
}

double PetWidget::crouchTime() const {
  if (m_species == PetKind::Sprout) return 0.1;
  if (m_rig) return m_jumpIsPounce ? 0.42 : 0.2;
  switch (m_motion.gait) {
    case G::Fox: return 0.32;     // the mousing crouch
    case G::Lumber: return 0.26;  // the wyvern vaults on its wings
    case G::Mech: return 0.24;
    case G::Serpent: case G::Hover: return 0.1;
    default: return 0.15;
  }
}

void PetWidget::turn() {
  if (m_turn >= 0) return;
  m_turn = 0;
  m_turnSwapped = false;
}

void PetWidget::startQuirk() {
  if (m_rig) {
    const auto& acts = m_rig->acts;
    if (acts.empty()) return;
    RigAct a = acts[static_cast<size_t>(rnd() * static_cast<double>(acts.size())) % acts.size()];
    if (a == m_lastAct) a = acts[(static_cast<size_t>(std::find(acts.begin(), acts.end(), a) - acts.begin()) + 1) % acts.size()];
    m_lastAct = a;
    if (m_mode == Mode::Sit && (a == RigAct::Pounce || a == RigAct::Stretch || a == RigAct::Sniff || a == RigAct::LieDown || a == RigAct::Roll)) return;
    if (a == RigAct::LieDown) {
      pick(Mode::Sit, 4 + rnd() * 8);
      m_lie = true;
      return;
    }
    if (a == RigAct::Pounce) {
      if (m_x - m_minX < 160 || m_maxX - m_x < 160) return;
      const double g = kGravity, h = 30 * m_pixel, vy = -std::sqrt(2 * g * h);
      m_launchVy = vy, m_launchVx = m_facing * 60 * m_pixel / (-2 * vy / g);
      m_jumpIsPounce = true;
      m_crouch = crouchTime();
      m_mode = Mode::Jump;
      return;
    }
    m_act = a, m_quirk = Q::Act, m_quirkAge = 0, m_quirkDur = PetLife::actLength(a);
    return;
  }
  const auto list = quirksOf(m_species);
  if (list.empty()) return;
  Quirk q = list[static_cast<size_t>(rnd() * static_cast<double>(list.size())) % list.size()];
  if (q == m_lastQuirk) q = list[(static_cast<size_t>(std::find(list.begin(), list.end(), q) - list.begin()) + 1) % list.size()];
  m_lastQuirk = q;
  if (q == Q::Pounce) {
    // listen, crouch, leap high and come down head first
    if (m_x - m_minX < 160 || m_maxX - m_x < 160) return;
    const double g = kGravity, h = 46 * m_pixel, vy = -std::sqrt(2 * g * h);
    m_launchVy = vy, m_launchVx = m_facing * 70 * m_pixel / (-2 * vy / g);
    m_crouch = crouchTime();
    m_mode = Mode::Jump;
    return;
  }
  m_quirk = q, m_quirkAge = 0, m_quirkDur = quirkLength(q);
  if (q == Q::LookBack) turn(), m_backAgain = false;
}

void PetWidget::tick(const TickContext& ctx) {
  if (!m_preview.empty()) return;
  // resting, it is woken only now and then: the time that passed, in steps
  // short enough for the jumps (a long wait is not one giant step)
  const bool kick = ctx.audio.onsets && ctx.audio.kicks != m_kicks;
  m_kicks = ctx.audio.kicks;
  lifeEvents(ctx, std::min(ctx.dt, 2.0));
  double left = std::min(ctx.dt, 2.0);
  bool first = true;
  do {
    const double dt = std::min(left, 0.05);
    stepBy(ctx, dt, first && kick);
    left -= dt;
    first = false;
  } while (left > 1e-6);
}

void PetWidget::stepBy(const TickContext& ctx, double dt, bool kick) {
  m_time += dt;
  if (ctx.box.w > 0) {
    m_boxW = ctx.box.w, m_boxH = ctx.box.h;
    m_homeX = ctx.box.x + ctx.box.w / 2, m_homeY = ctx.box.y + ctx.box.h;
    m_ground = m_homeY;
    m_minX = ctx.box.w / 2, m_maxX = std::max(m_minX, static_cast<double>(ctx.outputW) - ctx.box.w / 2);
    m_pixel = std::max(0.25, static_cast<double>(std::min(ctx.box.w, ctx.box.h)) / kPetPixels);
  }
  if (!m_placed && ctx.box.w > 0) {
    m_x = m_homeX, m_y = m_homeY;
    m_placed = true;
  }
  m_windows = ctx.windows ? *ctx.windows : std::vector<Rect>{};
  const bool music = !ctx.audio.silent && ctx.audio.energy > 0.02;
  m_silence = music ? 0 : m_silence + dt;
  m_sound = music ? m_sound + dt : 0;
  if (kick) m_kickAge = 0, ++m_beat;
  m_kickAge += dt, m_landAge += dt, m_dustAge += dt, m_transAge += dt, m_quirkAge += dt;
  if (m_quirk != Quirk::None && m_quirkAge >= m_quirkDur) m_quirk = Quirk::None, m_quirkNext = (5 + rnd() * 9) / m_lively;
  if (m_quirk == Q::LookBack && m_quirkAge >= 0.9 && m_turn < 0 && !m_backAgain) turn(), m_backAgain = true;  // and back again
  // a turn round: it squeezes to an edge and opens the other way, halfway through
  if (m_turn >= 0) {
    m_turn += dt;
    const double turnLen = m_rig ? kRigTurn : kTurn;
    if (!m_turnSwapped && m_turn >= turnLen / 2) m_facing = -m_facing, m_turnSwapped = true;
    if (m_turn >= turnLen) m_turn = -1;
  }

  if (m_dragging) {
    // carried: it hangs from the pointer, legs dangling; no gravity, no plans
    m_vy = 0, m_vx = 0, m_vel = 0, m_grounded = false, m_airTime = 0;
    if (m_rig) m_life.step(lifeIn(dt));
    return;
  }
  if (ctx.rainShelter.w > 0 && ctx.box.w > 0) {
    const float ratio = ctx.rainShelter.w / ctx.rainShelter.h / 1.6F;
    const double shift = std::min(1.0F, ratio) * 0.36 * ctx.rainShelter.h;
    const double floorY = std::clamp(ctx.rainShelter.y + ctx.rainShelter.h * 0.5 + shift,
                                     static_cast<double>(ctx.box.h), std::max<double>(ctx.outputH, ctx.box.h));
    const double center = std::clamp(ctx.rainShelter.x + ctx.rainShelter.w * 0.54, m_minX, m_maxX);
    const double ease = std::min(1.0, dt * 2);
    m_x += (center - m_x) * ease;
    m_y += (floorY - m_y) * ease;
    m_vx = m_vy = m_vel = 0;
    m_crouch = -1;
    m_grounded = true;
    if (m_mode != Mode::Sit) pick(Mode::Sit, 1);
    if (m_rig) m_life.step(lifeIn(dt));
    return;
  }
  m_timer -= dt;  // in the air too: a dance of hops still ends
  const double gravity = kGravity * (floats() ? 0.45 : 1.0);
  const double under = support(m_x, m_y);
  const bool grounded = m_vy >= 0 && m_y >= under - 0.5;
  m_grounded = grounded;
  if (!grounded) {
    // in the air: fall (or fly through a jump) and land on whatever is under it
    m_airTime += dt;
    m_vy += gravity * dt;
    if (m_motion.wings && m_species != PetKind::Sprout && m_vy > 240) m_vy = 240;  // wings spread: it glides down
    if (floats() && m_vy > 300) m_vy = 300;
    m_x = std::clamp(m_x + m_vx * dt, m_minX, m_maxX);
    const double next = m_y + m_vy * dt;
    const double land = support(m_x, m_y);
    if (m_vy > 0 && next >= land) {
      // the landing squash goes with how hard it came down
      m_landHard = floats() ? 0 : std::clamp(m_vy / 900, 0.25, 1.0);
      m_landAge = 0;
      if (m_landHard > 0.3) m_dustAge = 0;
      m_y = land, m_vy = 0, m_vx = 0, m_vel = 0;
      m_jumped = false;
      m_jumpIsPounce = false;
      m_squash = 0.35;  // a little bump on landing (Sprout)
      // a hop on the beat lands back in the dance; anything else stands a moment
      if (m_mode != Mode::Dance) pick(Mode::Idle, 0.4 + rnd() * 0.6 + m_landHard * 0.5);
    } else {
      m_y = next;
    }
  } else {
    m_y = under;
    m_airTime = 0;
    if (m_crouch >= 0) {
      // the crouch before a jump, then off it goes
      m_crouch -= dt;
      if (m_crouch < 0) {
        m_vy = m_launchVy, m_vx = m_launchVx;
        m_jumped = true;
        m_y -= 0.01;
      }
      m_squash = std::max(0.0, m_squash - dt * 2.5);
      if (m_rig) m_life.step(lifeIn(dt));
      return;
    }
    // music first: it dances once the music has gone on a moment (not at
    // every blip); long silence: it sleeps
    if (m_dances && music && m_sound > 0.4 && m_mode != Mode::Dance && m_mode != Mode::Walk) pick(Mode::Dance, 4 + rnd() * 6);
    // asleep while you are away, or after a long silence (sooner at night); up when you come
    // back or the music starts
    const bool sleepy = m_sleeps && ((m_away && m_awayFor > 20) || m_silence > (m_night ? 90 : 300));
    if (sleepy && m_mode != Mode::Sleep && !m_dragging) pick(Mode::Sleep, 1e9);
    if (m_mode == Mode::Sleep && (music || !sleepy)) pick(Mode::Idle, 1.4);
    if (m_mode != Mode::Walk) m_vel = std::max(0.0, m_vel - walkSpeed() * 8 * dt);
    switch (m_mode) {
      case Mode::Walk: {
        // ease into the stride and out of it; stop for a turn
        const double target = m_turn >= 0 ? 0 : walkSpeed();
        m_vel += (target - m_vel) * std::min(1.0, dt * 7);
        const double nx = m_x + m_vel * m_facing * dt;
        if (nx <= m_minX || nx >= m_maxX) turn();  // the edge of the screen: turn round
        m_x = std::clamp(nx, m_minX, m_maxX);
        // a window top ahead and within reach: crouch, then hop onto it
        if (m_climb && m_turn < 0 && rnd() < dt * 0.8)
          for (const Rect& w : m_windows) {
            const double edge = m_facing > 0 ? w.x : w.x + w.w, dist = (edge - m_x) * m_facing, rise = m_y - w.y;
            if (dist > 4 && dist < 140 && rise > 24 && rise < 300 && w.y > 40) {
              // clear the top by ~28 px (+12 for the 20 Hz steps, which lose height), and come
              // down ~30 px inside the window: the flight time is when it falls back to the top
              const double vy = -std::sqrt(2 * gravity * (rise + 40));
              const double flight = (-vy + std::sqrt(std::max(0.0, vy * vy - 2 * gravity * rise))) / gravity;
              m_launchVy = vy, m_launchVx = (dist + 30) * m_facing / flight;
              m_crouch = crouchTime();
              m_vel = 0;
              m_mode = Mode::Jump;
              break;
            }
          }
        if (m_mode == Mode::Walk && m_timer <= 0) pick(rnd() < 0.5 ? Mode::Idle : Mode::Sit, 1.5 + rnd() * 4);
        break;
      }
      case Mode::Dance:
        if (kick && m_motion.gait != G::Mech) m_vy = -260 - 140 * std::clamp(ctx.audio.kickStrength, 0.0, 1.0);  // a hop on the kick
        if (kick && m_motion.gait == G::Mech) m_dustAge = 0;  // a mecha stomps instead
        if (m_timer <= 0) {
          pick(music ? Mode::Walk : Mode::Idle, music ? 2 + rnd() * 3 : 2);
          if (m_mode == Mode::Walk && rnd() < 0.5) turn();
        }
        break;
      case Mode::Jump: pick(Mode::Idle, 0.5); break;
      case Mode::Sleep: break;
      default:  // idle or sitting: look about, then set off
        if ((m_mode == Mode::Idle || (m_rig && m_mode == Mode::Sit && !m_lie)) && m_quirk == Quirk::None && m_turn < 0) {
          m_quirkNext -= dt;
          if (m_quirkNext <= 0 && m_timer > 1) {
            m_quirkNext = (5 + rnd() * 9) / m_lively;
            startQuirk();
            if (m_mode == Mode::Jump) break;
          }
        }
        if (m_timer <= 0 && m_quirk == Quirk::None) {
          if (rnd() < 0.7) {
            if (rnd() < 0.35) turn();
            pick(Mode::Walk, 2 + rnd() * 6);
          } else {
            pick(rnd() < 0.5 ? Mode::Sit : Mode::Idle, 2 + rnd() * 5);
          }
        }
    }
    // the gait's cycle runs with the ground covered: two steps per cycle
    if (m_rig) {
      // one cycle covers rigCycle art px, an art px being two sprite px: the feet stay planted
      if (m_frames) {
        // six painted frames cover m_frameCyc frame px: the feet stay planted
        const double px = m_artPx > 0 ? m_artPx : std::max(0.25, std::floor(std::min(m_boxW, m_boxH) / kPetPixels));
        m_phase += m_vel / std::max(0.1, px * m_frameCyc) * dt;
      } else {
        m_phase += m_vel / std::max(0.1, unitPx() * rigCycle(m_rig->spec, std::clamp(m_pace - 1, 0.0, 1.0))) * dt;
      }
    } else if (m_species != PetKind::Sprout) {
      m_phase += m_vel / std::max(0.1, m_pixel * 2 * m_motion.step) * dt;
      const int step = static_cast<int>(std::floor(m_phase * 2));
      if (step != m_lastStep && m_motion.gait == G::Mech && m_vel > 1) m_dustAge = 0;  // every footfall raises dust
      m_lastStep = step;
    }
  }
  m_squash = std::max(0.0, m_squash - dt * 2.5);
  // blinking: 2–7 s apart, now and then twice
  m_blink -= dt;
  if (m_blink < -0.12) m_blink = rnd() < 0.15 ? 0.2 : 2 + rnd() * 5;
  if (m_petted >= 0) m_petted = m_petted + dt > 2.4 ? -1 : m_petted + dt;
  if (m_alert >= 0) m_alert = m_alert + dt > 2.8 ? -1 : m_alert + dt;
  if (m_rig) {
    if (m_wake >= 0) {
      // up from a sleep: a long stretch first
      if (m_grounded && m_mode == Mode::Idle && m_quirk == Quirk::None && m_wake > 0.3)
        m_act = RigAct::Stretch, m_quirk = Q::Act, m_quirkAge = 0, m_quirkDur = PetLife::actLength(RigAct::Stretch), m_wake = -1, m_timer = std::max(m_timer, 2.6);
      else m_wake = m_wake + dt > 3 ? -1 : m_wake + dt;
    }
    if (m_quirk != Q::Act) m_act = RigAct::None;
    m_life.step(lifeIn(dt));
  }
}

bool PetWidget::animating(const TickContext&) const {
  if (!m_preview.empty()) return false;
  if (m_species != PetKind::Sprout) return true;  // it breathes and its tail sways, on the 12 Hz clock
  return m_mode == Mode::Walk || m_mode == Mode::Dance || m_mode == Mode::Jump || m_vy != 0 || m_squash > 0;
}

// The frame clock, as pixel art is timed: 12 frames a second while it does
// something, held longer (8) while it stands or sits about and longer still
// (4) asleep, when only its breath and the z's move.
double PetWidget::animHz() const {
  const bool busy = m_vel > 0.5 || !m_grounded || m_vy != 0 || m_mode == Mode::Dance || m_mode == Mode::Walk ||
                    m_turn >= 0 || m_crouch >= 0 || m_landAge < 0.4 || m_dustAge < 0.35 || m_transAge < 1.6 || m_quirk != Quirk::None;
  if (m_rig) return busy || m_petted >= 0 || m_alert >= 0 ? 30 : m_mode == Mode::Sleep ? 8 : 15;  // its head and tail ease
  return busy ? 12 : m_mode == Mode::Sleep ? 4 : 8;
}

int PetWidget::fps() const {
  if (m_species == PetKind::Sprout) return 30;
  // the surface moves smoothly while it goes somewhere; otherwise only the frame clock
  const bool moving = m_vel > 0.5 || !m_grounded || m_vy != 0 || m_mode == Mode::Dance;
  return moving ? 30 : static_cast<int>(animHz());
}

double PetWidget::nextWakeup(double now) const {
  if (!m_preview.empty()) return 1e18;
  if (m_species != PetKind::Sprout) return now + 1 / animHz();
  // resting: the next blink, the end of a rest, or a look at the windows now and then
  return now + std::clamp(std::min(m_blink > 0 ? m_blink : 0.12, m_timer > 0 ? m_timer : 0.5), 0.05, 0.5);
}

bool PetWidget::offset(float& dx, float& dy) const {
  dx = static_cast<float>(m_x - m_homeX), dy = static_cast<float>(m_y - m_homeY);
  return m_placed;
}

// ── the frame ───────────────────────────────────────────────────────────────
// A behaviour at time t: the gait's cycle (phase), the last beat (kickAge,
// beat) and how fast it goes now (moving, 0..1). Numbers in sprite px.
PetLook PetWidget::baseLook(Mode mode, double t, double phase, double kickAge, int beat, double moving) const {
  PetLook L;
  const G g = m_motion.gait;
  const bool chibi = isChibiPet(m_species), eagle = m_species == PetKind::GriffinClassic || m_species == PetKind::GriffinChibi;
  const double tp = t + m_phi, bobScale = chibi ? 1.25 : 1.0;
  L.flip = m_facing < 0;
  L.blink = m_blink < 0 && m_blink > -0.12;
  // standing about: it breathes (the torso, ~1 px, every 3.3 s) and the tails drift (2.5 s)
  L.sy = 1 + 0.011F * std::sin(kTau * tp / 3.3);
  L.sway = g == G::Fox ? 1.8F : 1.2F;
  L.swayPhase = static_cast<float>(kTau * tp / 2.5);
  if (g == G::Serpent) {
    // the dragon swims in the air: a wave from head to tail, a slow hover, its shadow below
    L.sy = 1;
    L.lift = static_cast<float>(6 + 2 * std::sin(kTau * tp / 3.2));
    L.wave = 1.6F;
    L.wavePhase = static_cast<float>(kTau * (tp / 3.0 + phase));
  } else if (g == G::Hover) {
    // the mage floats: 3.3 s up and down, the orb on its own 5 s drift and glowing
    L.lift = static_cast<float>(5 + 2 * std::sin(kTau * tp / 2.9));
    L.orbX = static_cast<float>(0.8 * std::cos(kTau * tp / 5 + 1));
    L.orbY = static_cast<float>(1.5 * std::sin(kTau * tp / 5 + 1));
    L.glow = static_cast<float>(0.35 + 0.35 * std::sin(kTau * tp / 2.2));
    L.sway = 1.3F;
    L.swayPhase = static_cast<float>(kTau * tp / 2.4);
  }
  if (floats()) L.shadow = std::clamp(1.0F - (L.lift - 3) / 10.0F, 0.3F, 1.0F);

  switch (mode) {
    case Mode::Walk: {
      if (floats()) {
        L.lean = static_cast<float>((g == G::Hover ? 2.0 : 1.0) * moving);
        L.wave += static_cast<float>(0.8 * moving);
        L.sway += static_cast<float>(1.2 * moving);
        L.swayPhase += static_cast<float>(kTau * phase * 0.5);
        break;
      }
      // Two drawings make four beats, as Shimeji does (stand, step, stand,
      // step): the step around the footfall, standing as the legs pass. The
      // body drops into the footfall, then rises quickly and settles slowly
      // (a triangle, not a sine).
      const double u = fract(phase * 2);
      L.drawing = u < 0.45 ? PetDrawing::Stride : PetDrawing::Stand;
      L.blink = false;
      L.sy = 1;
      const double tri = u < 0.25 ? 0.5 - 2 * u : u < 0.55 ? (u - 0.25) / 0.3 : 1 - 0.5 * (u - 0.55) / 0.45;
      double bob = 1.4;
      L.sway = 2.0F;
      L.swayPhase = static_cast<float>(kTau * phase - M_PI / 2);  // tails a quarter behind the body
      switch (g) {
        case G::Hoof:  // two nods per stride: the head drops as each forefoot lands
          bob = 1.6, L.headY = static_cast<float>(1.3 * std::cos(kTau * u)), L.sway = 1.8F;
          break;
        case G::Paw: bob = 1.4, L.headY = static_cast<float>(0.6 * std::cos(kTau * u)); break;
        case G::Fox: bob = 2.0, L.headY = static_cast<float>(0.5 * std::cos(kTau * u)), L.sway = 2.6F; break;
        case G::Wolf:  // a level, ground-eating trot, the head carried low
          bob = 0.9, L.headY = static_cast<float>(0.6 * std::cos(kTau * u)), L.headX = 0.5F, L.sway = 1.5F;
          break;
        case G::Bird: {
          // the head holds still in the air while the body walks under it,
          // then snaps forward: one head cycle per step, 60 % hold
          const double a = chibi ? 1.2 : 1.6;
          L.headX = static_cast<float>(u < 0.6 ? a - 2 * a * (u / 0.6) : -a + 2 * a * smooth(0.6, 1.0, u));
          bob = 1.0, L.sway = 1.4F, L.swayPhase = static_cast<float>(kTau * tp / 2.4);
          break;
        }
        case G::Mech: {
          // heavy: the foot comes down fast, the body sinks into it and rebounds a pixel
          bob = 1.0;
          L.drawing = u < 0.3 ? PetDrawing::Stride : PetDrawing::Stand;
          L.sy = static_cast<float>(1 - 0.03 * std::exp(-u * 9) + 0.01 * std::exp(-std::pow((u - 0.3) / 0.08, 2)));
          L.sway = 1.6F;
          L.dust = static_cast<float>(u * m_motion.step / std::max(1.0F, m_motion.speed));
          break;
        }
        case G::Biped: bob = 1.5, L.sway = 2.0F; break;
        case G::Lumber:  // the wyvern waddles: it leans over each foot in turn, wings twitching
          bob = 1.0, L.lean = static_cast<float>(1.4 * std::sin(M_PI * phase * 2)), L.sway = 2.0F;
          L.flap = static_cast<float>(0.8 * std::sin(kTau * phase * 2));
          break;
        default: break;
      }
      L.bob = static_cast<float>(bob * bobScale * tri);
      if (eagle) L.headY = L.bob;  // an eagle's head holds steady while its body bobs
      break;
    }
    case Mode::Sit:
    case Mode::Sleep:
      // resting: the third drawing, breathing slower in sleep; z's drift up
      L = PetLook{};
      L.flip = m_facing < 0;
      L.drawing = PetDrawing::Rest;
      L.sy = mode == Mode::Sleep ? 1 + 0.017F * static_cast<float>(std::sin(kTau * tp / 4.6))
                                 : 1 + 0.011F * static_cast<float>(std::sin(kTau * tp / 3.3));
      if (mode == Mode::Sleep) L.sleepPhase = static_cast<float>(fract(tp * 0.4));
      break;
    case Mode::Jump:
      L.drawing = PetDrawing::Stride;
      L.blink = false;
      L.sy = 1.05F, L.lean = 1;
      if (m_motion.wings) L.flap = -3;
      break;
    case Mode::Dance: {
      // on the beat: a squash and a rebound, a lean to one side then the
      // other, the head bobbing, the step drawing on every other beat
      const double e = std::exp(-kickAge * 10), e2 = std::exp(-kickAge * 9);
      const double side = (beat & 1) ? 1 : -1;
      L.drawing = (beat & 1) && g != G::Serpent && g != G::Hover ? PetDrawing::Stride : PetDrawing::Stand;
      L.blink = false;
      if (!floats()) L.sy = static_cast<float>(1 - 0.08 * e + 0.03 * std::exp(-std::pow((kickAge - 0.16) / 0.06, 2)));
      L.lean = static_cast<float>(1.8 * (side * (1 - e) - side * e));
      L.headY += static_cast<float>(1.5 * e2);
      L.heads[beat % 3] += static_cast<float>(1.2 * e2);  // Cerberus: one head at a time
      L.sway = 2.6F;
      L.swayPhase = static_cast<float>(M_PI * beat + kTau * kickAge * 2);
      if (m_motion.wings) L.flap = static_cast<float>(2.5 * e2);
      if (floats()) L.lift += static_cast<float>(2.5 * e2);
      if (g == G::Serpent) L.wave += 1.2F, L.wavePhase += static_cast<float>(M_PI * beat);
      if (g == G::Mech) L.dust = static_cast<float>(kickAge);
      L.notePhase = static_cast<float>(fract(t * 0.7));
      break;
    }
    default: break;
  }
  return L;
}

PetLook PetWidget::previewLook(Mode mode, double t) const {
  const double cadence = m_motion.speed / std::max(1.0F, 2 * m_motion.step);
  return baseLook(mode, t, t * cadence, fract(t * 2) / 2, static_cast<int>(t * 2), 1);
}

PetLook PetWidget::liveLook(Mode mode) const {
  const double hz = animHz(), t = std::floor(m_time * hz) / hz;  // the frame clock
  const double moving = std::clamp(m_vel / std::max(1.0, walkSpeed()), 0.0, 1.0);
  const bool air = !m_grounded;
  PetLook L = baseLook(air && mode != Mode::Dance ? Mode::Idle : mode, t, m_phase, m_kickAge, m_beat, moving);
  const G g = m_motion.gait;
  if (m_mode == Mode::Walk && moving < 0.15 && !floats()) L.drawing = PetDrawing::Stand, L.bob = 0;
  if (m_turn >= 0) {
    // turning round: it narrows to an edge and opens the other way
    L.sx = static_cast<float>(std::max(0.12, std::abs(std::cos(M_PI * m_turn / kTurn))));
    if (mode != Mode::Walk) L.drawing = PetDrawing::Stand;
  }
  if (m_crouch >= 0) {
    // the crouch before the leap: down, wings raised, the fox rearing back
    const double p = 1 - m_crouch / crouchTime();
    L.drawing = PetDrawing::Stand, L.bob = 0;
    L.sy = static_cast<float>(1 - (floats() ? 0.04 : 0.1) * smooth(0, 1, p));
    if (g == G::Fox) L.lean = static_cast<float>(-1.5 * p), L.headY = static_cast<float>(1.5 * p);
    if (m_motion.wings) L.flap = static_cast<float>(-2.5 * p);
  } else if (air) {
    const double speed = std::abs(m_vy);
    if (!floats()) {
      L.drawing = m_vy < 0 ? PetDrawing::Stride : PetDrawing::Stand;
      L.blink = false;
      // stretched while it goes fast, round at the top of the arc
      L.sy = static_cast<float>(1 + 0.06 * std::clamp((speed - 120) / 600, 0.0, 1.0));
      L.lean = m_vy < 0 ? 1.0F : (g == G::Fox && m_jumped ? 2.2F : 0.5F);  // the fox dives head first
    } else {
      L.sway += 1, L.wave += 0.8F;
    }
    if (m_motion.wings && m_species != PetKind::Sprout) {
      if (m_vy < 0) {
        // beating up: the downstroke takes 60 % of the beat
        const double hz = g == G::Bird ? 3.5 : 2.4, b = fract(m_airTime * hz);
        L.flap = static_cast<float>(4 * (b < 0.6 ? -1 + 2 * smooth(0, 0.6, b) : 1 - 2 * smooth(0.6, 1, b)));
      } else {
        L.flap = static_cast<float>(-1.5 + 0.6 * std::sin(kTau * 1.2 * m_airTime));  // gliding down, wings spread
        L.drawing = PetDrawing::Stand;
      }
    } else if (!m_jumped && !floats()) {
      // it lost its footing: legs going, tail flying
      L.drawing = fract(m_airTime * 7) < 0.5 ? PetDrawing::Stride : PetDrawing::Stand;
      L.sway += 2, L.swayPhase = static_cast<float>(kTau * 3 * m_airTime);
    }
  } else if (m_landAge < 0.4 && m_landHard > 0) {
    // the landing: down at once, as hard as it came, back up in a fifth of a second
    const double k = m_landAge < 0.08 ? 0 : (m_landAge - 0.08) / 0.22;
    L.sy *= static_cast<float>(1 - 0.11 * m_landHard * std::max(0.0, 1 - k) + 0.02 * m_landHard * hump(k - 1, 0.4));
    L.drawing = m_landAge < 0.2 ? PetDrawing::Stride : L.drawing;
  }
  if (m_dustAge < 0.33 && !floats()) L.dust = static_cast<float>(m_dustAge);
  else if (g == G::Mech && mode == Mode::Walk) L.dust = -1;
  // settling down, getting up, and the stretch on waking
  if (m_trans && m_transAge < 1.6) {
    const double a = m_transAge;
    if (m_trans == 1) {
      if (a < 0.16) L.drawing = PetDrawing::Stand, L.sy = static_cast<float>(1 - 0.1 * a / 0.16), L.lift *= static_cast<float>(1 - a / 0.16), L.sleepPhase = -1;
      else if (a < 0.3) L.sy = static_cast<float>(0.94 + 0.06 * (a - 0.16) / 0.14);
    } else {
      if (a < 0.12) {
        L = baseLook(Mode::Sit, t, m_phase, 9, 0, 0);
        L.sy = static_cast<float>(1 - 0.08 * a / 0.12);
      } else if (a < 0.5) {
        L.sy *= static_cast<float>(0.92 + 0.08 * smooth(0.12, 0.35, a) + 0.03 * hump(a - 0.3, 0.2));
        L.lift *= static_cast<float>(smooth(0.12, 0.5, a));
      }
      if (m_trans == 3 && a > 0.5) {
        // awake: a long stretch, head up
        const double e = plateau(a - 0.5, 1.1, 0.35, 0.4);
        L.sy *= static_cast<float>(1 + 0.05 * e);
        L.lean -= static_cast<float>(1.2 * e), L.headY -= static_cast<float>(1.5 * e);
      }
    }
  }
  // the small things it does while it stands about
  const double a = m_quirkAge, d = m_quirkDur, e = hump(a, d);
  switch (m_quirk) {
    case Q::TailSwish: L.sway += static_cast<float>(3 * e), L.swayPhase += static_cast<float>(kTau * 2.5 * a); break;
    case Q::HeadToss:
      L.headY -= static_cast<float>(2.5 * (a < 0.25 ? a / 0.25 : std::max(0.0, 1 - (a - 0.25) / 0.35)));
      L.sway += static_cast<float>(2 * e), L.swayPhase += static_cast<float>(kTau * 2 * a);
      break;
    case Q::Stamp:
      if ((a > 0.1 && a < 0.25) || (a > 0.45 && a < 0.6)) L.drawing = PetDrawing::Stride;
      break;
    case Q::Howl: {
      // muzzle to the sky: half a second up, held, half a second down
      const double up = plateau(a, d, 0.5, 0.5);
      L.headY -= static_cast<float>(3 * up), L.headX -= static_cast<float>(up), L.lean -= static_cast<float>(up);
      break;
    }
    case Q::Pant:
      for (int i = 0; i < 3; ++i) L.heads[i] += static_cast<float>(std::max(0.0, std::sin(kTau * 2.5 * a + i * 2.09)) * plateau(a, d, 0.2, 0.2));
      break;
    case Q::Shake:  // a dog's shake, about 4 Hz, nose to tail
      L.lean += static_cast<float>(1.5 * std::sin(kTau * 4 * a) * e);
      L.sway += static_cast<float>(2.5 * e), L.swayPhase += static_cast<float>(kTau * 4 * a);
      break;
    case Q::Peck: {
      const double p = pulse(a, 0.1, 0.3) + pulse(a, 0.45, 0.65);
      L.headY += static_cast<float>(3 * p), L.headX += static_cast<float>(1.5 * p);
      break;
    }
    case Q::TailFan: L.sway += static_cast<float>(1.5 * e), L.swayPhase += static_cast<float>(kTau * 6 * a); break;
    case Q::WingRuffle: L.flap += static_cast<float>(2 * std::sin(kTau * 5 * a) * e); break;
    case Q::WingStretch: L.flap -= static_cast<float>(4 * plateau(a, d, 0.4, 0.4)); break;
    case Q::Coil: L.wave += static_cast<float>(2 * e), L.wavePhase += static_cast<float>(kTau * 0.9 * a); break;
    case Q::VisorScan: L.glint = a < 0.6 ? static_cast<float>(a / 0.6) : a < 1.2 ? static_cast<float>((a - 0.6) / 0.6) : -1; break;
    case Q::Servo: L.sy *= static_cast<float>(1 - (a < 0.08 ? 0.02 : 0.02 * std::max(0.0, 1 - (a - 0.08) / 0.15))); break;
    case Q::CapeBillow: L.sway += static_cast<float>(3 * e), L.swayPhase += static_cast<float>(kTau * 1.5 * a); break;
    case Q::OrbSpin:
      L.orbX += static_cast<float>(3 * std::cos(kTau * a / 1.2) * e), L.orbY += static_cast<float>(2 * std::sin(kTau * a / 1.2) * e);
      L.glow = std::max(L.glow, static_cast<float>(e));
      break;
    case Q::HeadTilt: {
      const double p = plateau(a, d, 0.15, 0.2);
      L.headX -= static_cast<float>(p), L.headY += static_cast<float>(p);
      break;
    }
    case Q::HeadSnap:  // a raptor's head moves in snaps, not sweeps
      L.headX += a < 0.4 ? -1.5F : a < 0.8 ? 0.5F : 0.0F;
      L.headY += a < 0.8 ? -1.0F : 0.0F;
      break;
    default: break;
  }
  return L;
}

void PetWidget::draw(const DrawContext& ctx) {
  Canvas& c = m_canvas;
  c.begin(ctx.w, ctx.h, ctx.scale, ctx.text);
  const float op = static_cast<float>(m_opacity);
  // the creature on a 16 x 14 grid, scaled to the box, standing on its bottom middle
  const float px = std::max(1.0F, std::floor(std::min(ctx.w / 24.0F, ctx.h / 20.0F)));  // room round it for the notes and the z's
  const Mode mode = m_preview == "sleep" ? Mode::Sleep : m_preview == "walk" ? Mode::Walk : m_preview == "dance" ? Mode::Dance :
                    m_preview == "sit" ? Mode::Sit : m_preview == "jump" ? Mode::Jump : m_preview == "idle" ? Mode::Idle : m_mode;
  const double t = m_preview.empty() ? m_time : 0.3;
  if (m_species != PetKind::Sprout) {
    drawSpecies(ctx, mode, t);
    if (ctx.text) ctx.text->collect(60);
    return;
  }
  const bool sitting = mode == Mode::Sit || mode == Mode::Sleep;
  // squash and stretch: on landing, when sitting, and bobbing to the music
  double squash = m_squash + (sitting ? 0.12 : 0);
  if (mode == Mode::Dance) squash += 0.08 * std::sin(t * 9);
  const int bodyH = std::clamp(static_cast<int>(std::lround(10 * (1 - squash))), 7, 12), bodyW = 14 + (squash > 0.15 ? 1 : 0);
  const float baseX = std::floor(ctx.w / 2 - bodyW * px / 2), feetY = ctx.h - px;  // feet on the last row
  const float top = feetY - static_cast<float>(bodyH) * px;
  const Color outline = m_body.mix(Color{0.05F, 0.05F, 0.1F, 1}, 0.55F), belly = m_body.mix(Color{1, 1, 1, 1}, 0.35F);
  auto cell = [&](float gx, float gy, Color col) { c.roundRect(baseX + gx * px, top + gy * px, px, px, 0, withA(col, op)); };
  const bool flip = m_facing < 0;
  auto fx = [&](int x) { return static_cast<float>(flip ? bodyW - 1 - x : x); };
  // the body: a rounded blob, row by row (the corners cut away), with an outline and a lighter belly
  for (int y = 0; y < bodyH; ++y) {
    const int cut = y == 0 || y == bodyH - 1 ? 3 : y == 1 || y == bodyH - 2 ? 1 : 0;
    for (int x = cut; x < bodyW - cut; ++x) {
      const bool edge = x == cut || x == bodyW - 1 - cut || y == 0 || y == bodyH - 1 || (cut && (x == cut + 1 || x == bodyW - 2 - cut) && (y == 0 || y == bodyH - 1));
      const bool bellyHere = y >= bodyH / 2 && x > 3 && x < bodyW - 4;
      cell(fx(x), static_cast<float>(y), edge ? outline : bellyHere ? belly : m_body);
    }
  }
  // the sprout on its head, swaying
  const float sway = mode == Mode::Dance ? static_cast<float>(std::sin(t * 9)) : static_cast<float>(std::sin(t * 2) * 0.4);
  const float sx = static_cast<float>(bodyW / 2) + (sway > 0.5F ? 1 : sway < -0.5F ? -1 : 0);
  cell(sx, -1, m_sprout.mix(outline, 0.3F));
  cell(sx, -2, m_sprout);
  cell(sx - 1, -3, m_sprout);
  cell(sx + 1, -3, m_sprout.mix(Color{1, 1, 1, 1}, 0.25F));
  // the face: two eyes (closed asleep or blinking), a highlight, a small mouth, rosy cheeks
  const int ey = std::max(2, bodyH / 2 - 2);
  const bool closed = mode == Mode::Sleep || (m_preview.empty() && m_blink < 0);
  const Color ink{0.07F, 0.07F, 0.12F, 1}, rose = m_body.mix(Color{1, 0.45F, 0.55F, 1}, 0.6F);
  for (int e : {4, 9}) {
    if (closed) {
      cell(fx(e), static_cast<float>(ey + 1), ink);
      cell(fx(e + 1), static_cast<float>(ey + 1), ink);
    } else {
      cell(fx(e), static_cast<float>(ey), ink);
      cell(fx(e), static_cast<float>(ey + 1), ink);
      cell(fx(e + 1), static_cast<float>(ey + 1), ink);
      cell(fx(e + 1), static_cast<float>(ey), Color{1, 1, 1, 1});
    }
    cell(fx(e - 1), static_cast<float>(ey + 2), rose);
  }
  if (mode != Mode::Sleep) cell(fx(7), static_cast<float>(ey + 2), ink);
  // feet: stepping while it walks, tucked in when it sits
  if (!sitting) {
    const bool step = (mode == Mode::Walk && std::fmod(t * 6 * m_speed, 2.0) < 1.0) || (mode == Mode::Dance && std::fmod(t * 4, 2.0) < 1.0);
    for (int f : {3, 9}) {
      const float lift = (step == (f == 3)) && mode != Mode::Idle ? -1 : 0;
      cell(fx(f), static_cast<float>(bodyH) + lift, outline);
      cell(fx(f + 1), static_cast<float>(bodyH) + lift, outline);
    }
  }
  // asleep: z's rising; dancing: notes
  if (mode == Mode::Sleep) {
    const TextStyle st{.family = "Space Grotesk", .size = px * 4, .weight = 700};
    for (int k = 0; k < 2; ++k) {
      const double ph = std::fmod(t * 0.6 + k * 0.5, 1.0);
      c.text("z", st, baseX + bodyW * px + static_cast<float>(ph) * px * 3, top - static_cast<float>(ph) * px * 7 - px * 2, withA(outline, op * static_cast<float>(1 - ph)));
    }
  } else if (mode == Mode::Dance) {
    const double ph = std::fmod(t * 0.9, 1.0);
    c.note(baseX + (flip ? -2 : bodyW + 2) * px, top - static_cast<float>(ph) * px * 6, px * 4, withA(m_body, op * static_cast<float>(1 - ph)));
  }
  m_hit = {baseX - px * 2, top - px * 4, static_cast<float>(bodyW + 4) * px, static_cast<float>(bodyH + 5) * px};
  m_figLeft = 0, m_figRight = bodyW, m_figTop = 0;
  drawBubble(ctx, baseX, top, px, bodyH);
  if (ctx.text) ctx.text->collect(60);
}

// ── what it tells you ───────────────────────────────────────────────────────

namespace {
// a notification's text without its markup (<b>, <a href…>, &amp;…), on one line
std::string plain(const std::string& in) {
  std::string out;
  bool tag = false;
  for (size_t i = 0; i < in.size(); ++i) {
    const char ch = in[i];
    if (ch == '<') tag = true;
    else if (ch == '>') tag = false;
    else if (!tag) out += ch == '\n' || ch == '\t' ? ' ' : ch;
  }
  for (auto [from, to] : {std::pair{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&apos;", "'"}})
    for (size_t at; (at = out.find(from)) != std::string::npos;) out.replace(at, std::string_view(from).size(), to);
  return out;
}

// the laptop's battery: percent and whether it charges (-1: no battery)
int batteryLevel(bool& charging) {
  namespace fs = std::filesystem;
  std::error_code ec;
  for (fs::directory_iterator it("/sys/class/power_supply", ec), end; !ec && it != end; it.increment(ec)) {
    const auto& e = *it;
    std::ifstream type(e.path() / "type");
    std::string t;
    if (!(type >> t) || t != "Battery") continue;
    std::ifstream scope(e.path() / "scope");
    std::string sc;
    if ((scope >> sc) && sc == "Device") continue;
    std::ifstream cap(e.path() / "capacity"), st(e.path() / "status");
    int c = -1;
    std::string status;
    if (!(cap >> c)) continue;
    st >> status;
    charging = status == "Charging" || status == "Full";
    return c;
  }
  return -1;
}
}  // namespace

void PetWidget::say(const std::string& text, double secs) {
  if (text.empty()) return;
  if (m_bubbles.size() >= 4) m_bubbles.erase(m_bubbles.begin() + 1);  // a backlog: keep the newest
  if (m_bubbles.empty()) m_bubbleAge = 0;
  m_bubbles.push_back({text, secs > 0 ? secs : m_bubbleSecs});
}

void PetWidget::react(const std::string& how) {
  if (how == "alert") {
    m_alert = 0;
    if (m_mode == Mode::Sleep) pick(Mode::Idle, 2);
    m_quirk = Quirk::None;
    // a little hop of surprise
    if (m_grounded && !floats() && m_crouch < 0) m_vy = -160 - 60 * m_speed, m_y -= 0.01;
  } else if (how == "happy") {
    m_petted = 0;
    if (m_mode == Mode::Sleep) pick(Mode::Idle, 2);
  } else if (how == "dance") {
    pick(Mode::Dance, 6);
  } else if (how == "jump") {
    if (m_grounded && m_crouch < 0) {
      m_launchVy = -std::sqrt(2 * kGravity * 70 * std::max(0.5, m_pixel)), m_launchVx = 0;
      m_crouch = crouchTime();
      m_mode = Mode::Jump;
    }
  } else if (how == "sleep") {
    pick(Mode::Sleep, 1e9);
  } else if (how == "wake") {
    if (m_mode == Mode::Sleep) pick(Mode::Idle, 2);
    m_silence = 0;
  }
}

bool PetWidget::command(const std::string& verb, const std::string& arg) {
  if (verb == "say") {
    say(arg);
    return true;
  }
  if (verb == "react") {
    react(arg.empty() ? "alert" : arg);
    return true;
  }
  if (verb == "notify") {  // as if a notification had come
    react("alert");
    if (m_notify != "react" && m_notify != "off") say(arg.empty() ? (spanishUi() ? "¡Un aviso!" : "A notification!") : arg);
    return true;
  }
  return false;
}

void PetWidget::lifeEvents(const TickContext& ctx, double dt) {
  const bool es = spanishUi();
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  m_night = tm.tm_hour >= 23 || tm.tm_hour < 6;

  // notifications: a hop, the "!" and (if you want) what it says
  if (m_notify != "off") {
    NotifyWatch& watch = NotifyWatch::instance();
    if (!m_noteStarted) {
      m_noteStarted = true;
      watch.start();
      watch.since(m_noteCursor);  // what came before it was here is old news
    }
    for (const Notice& n : watch.since(m_noteCursor)) {
      std::string app = n.app;
      for (char& ch : app) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      if (std::find(m_notifyIgnore.begin(), m_notifyIgnore.end(), app) != m_notifyIgnore.end()) continue;
      react("alert");
      if (m_notify == "react") continue;
      const std::string who = n.app.empty() ? (es ? "Aviso" : "Notification") : n.app, title = plain(n.summary), body = plain(n.body);
      std::string text = who;
      if (m_notify == "title" || m_notify == "full") text += title.empty() ? "" : ": " + title;
      if (m_notify == "full" && !body.empty()) text += " — " + body;
      say(text, m_bubbleSecs + (n.urgency >= 2 ? 3 : 0));
    }
  }

  // you went away, and came back
  if (ctx.userAway != m_away) {
    m_away = ctx.userAway;
    if (m_away) {
      m_awayFor = 0;
    } else {
      if (m_greet && m_awayFor > 120 && m_preview.empty()) {
        react("alert");
        m_petted = 0.6;  // a happy face, after the surprise
        say(m_night ? (es ? "¡Hola! Es tarde…" : "Hi! It's late…") : (es ? "¡Hola de nuevo!" : "Welcome back!"), 3.5);
      }
      m_silence = 0;  // up it gets
      if (m_awayFor > 300) m_used = 0;  // that was a real break
    }
  }
  if (m_away) m_awayFor += dt;
  else m_used += dt;

  // the battery: a warning at 20, 10 and 5 % while it runs down; glad when it charges again
  m_batteryCheck -= dt;
  if (m_battery && m_batteryCheck <= 0) {
    m_batteryCheck = 30;
    bool charging = false;
    const int level = batteryLevel(charging);
    if (level >= 0) {
      if (!charging) {
        for (int mark : {20, 10, 5})
          if (level <= mark && m_batteryWarned > mark) {
            m_batteryWarned = mark;
            react("alert");
            say(es ? std::format("Batería al {} %", level) : std::format("Battery at {} %", level), 7);
            break;
          }
      } else {
        if (!m_charging && m_batteryWarned <= 20) react("happy"), say(es ? "¡Cargando, gracias!" : "Charging, thanks!", 3);
        m_batteryWarned = 101;
      }
      m_charging = charging;
    }
  }

  // the hour, on the hour
  if (m_lastHour < 0) m_lastHour = tm.tm_hour;
  if (tm.tm_min == 0 && tm.tm_hour != m_lastHour) {
    m_lastHour = tm.tm_hour;
    if (m_hourly && !m_away) react("happy"), say(std::format("{:02}:00", tm.tm_hour), 4);
  }

  // a break, after so long at the computer
  if (m_breakEvery > 0 && m_used > m_breakEvery * 60 && !m_away) {
    m_used = 0;
    react("alert");
    say(es ? "¡Hora de una pausa! Estírate un poco." : "Time for a break! Have a stretch.", 8);
    if (m_rig && m_grounded) m_act = RigAct::Stretch, m_quirk = Q::Act, m_quirkAge = 0, m_quirkDur = PetLife::actLength(RigAct::Stretch);
  }

  // the bubble on show, then the next
  if (!m_bubbles.empty()) {
    m_bubbleAge += dt;
    if (m_bubbleAge > m_bubbles.front().secs) m_bubbles.erase(m_bubbles.begin()), m_bubbleAge = 0;
  }
}

// ── touching it ─────────────────────────────────────────────────────────────

std::vector<Rect> PetWidget::inputRects() const {
  if (!m_interact || m_hit.w <= 0 || !m_preview.empty()) return {};
  // rounded out to 8 px, so the input region is not rebuilt for every pixel it moves
  const float x0 = std::floor(m_hit.x / 8) * 8, y0 = std::floor(m_hit.y / 8) * 8;
  const float x1 = std::ceil((m_hit.x + m_hit.w) / 8) * 8, y1 = std::ceil((m_hit.y + m_hit.h) / 8) * 8;
  return {{x0, y0, x1 - x0, y1 - y0}};
}

bool PetWidget::onPointer(const PointerEvent& ev) {
  if (!m_interact) return false;
  switch (ev.type) {
    case PointerEvent::Press:
      if (ev.button != 0x110) return false;  // BTN_LEFT
      m_pressed = true, m_dragging = false;
      m_pressX = m_grabX = ev.x, m_pressY = m_grabY = ev.y;
      m_pressT = m_time;
      return true;
    case PointerEvent::Motion:
      if (!m_pressed) return false;
      if (!m_dragging && std::hypot(ev.x - m_pressX, ev.y - m_pressY) > 6) {
        m_dragging = true;
        m_quirk = Quirk::None, m_crouch = -1, m_turn = -1;
        if (m_mode == Mode::Sleep || m_mode == Mode::Sit) pick(Mode::Idle, 2);
      }
      if (m_dragging) {
        // the surface moves with it, so the pointer is always near where it took hold
        const double dx = ev.x - m_grabX, dy = ev.y - m_grabY;
        m_x = std::clamp(m_x + dx, m_minX, m_maxX);
        m_y = std::max(m_boxH * 0.5, m_y + dy);
        m_dragVx = m_dragVx * 0.6 + dx * 30 * 0.4, m_dragVy = m_dragVy * 0.6 + dy * 30 * 0.4;
      }
      return true;
    case PointerEvent::Release:
      if (!m_pressed) return false;
      m_pressed = false;
      if (m_dragging) {
        // let go: it falls (thrown, if you flung it) and lands on whatever is under it
        m_dragging = false;
        m_vx = std::clamp(m_dragVx, -900.0, 900.0), m_vy = std::clamp(m_dragVy, -900.0, 600.0);
        m_dragVx = m_dragVy = 0;
        m_y -= 0.01;
        m_mode = Mode::Jump;
        m_jumped = false;
        return true;
      }
      if (m_time - m_lastClick < 0.4) {  // a double click: a jump for joy
        react("jump");
        m_lastClick = -9;
      } else {  // a click: a pat
        react("happy");
        m_lastClick = m_time;
      }
      return true;
    case PointerEvent::Leave:
      return false;
    default: return false;
  }
}

void PetWidget::drawBubble(const DrawContext& ctx, float spriteX, float spriteY, float pixel, int side) {
  if (m_bubbles.empty() || !ctx.text) return;
  (void)side;
  const Bubble& b = m_bubbles.front();
  const double age = m_bubbleAge;
  const float alpha = static_cast<float>(std::min({1.0, age / 0.15, (b.secs - age) / 0.3})) * static_cast<float>(m_opacity);
  if (alpha <= 0.01F) return;
  Canvas& c = m_canvas;
  const TextStyle st{.family = "Space Grotesk", .size = 13, .weight = 600};
  const float maxW = std::min(ctx.w - 16, 250.0F);
  // the words, wrapped into at most three lines (the last cut short with an ellipsis)
  std::vector<std::string> lines;
  {
    std::string line, word;
    auto fits = [&](const std::string& t) { return Canvas::measure(t, st).w <= maxW - 16; };
    auto popUtf8 = [](std::string& s) {
      while (!s.empty()) {
        const unsigned char c = static_cast<unsigned char>(s.back());
        s.pop_back();
        if ((c & 0xC0) != 0x80) break;  // stopped at UTF-8 lead/ASCII byte
      }
    };
    auto flush = [&] {
      if (word.empty()) return;
      const std::string tryLine = line.empty() ? word : line + " " + word;
      if (fits(tryLine)) line = tryLine;
      else {
        if (!line.empty()) lines.push_back(line);
        line = word;
        while (!fits(line) && line.size() > 4) popUtf8(line);
      }
      word.clear();
    };
    for (char ch : b.text) {
      if (ch == ' ') flush();
      else word += ch;
    }
    flush();
    if (!line.empty()) lines.push_back(line);
    if (lines.size() > 3) {
      lines.resize(3);
      while (!lines[2].empty() && !fits(lines[2] + "…")) popUtf8(lines[2]);
      lines[2] += "…";
    }
  }
  if (lines.empty()) return;
  float tw = 0;
  for (const auto& l : lines) tw = std::max(tw, Canvas::measure(l, st).w);
  const float lineH = 16, bw = std::max(20.0F, std::ceil(tw + 16)), bh = static_cast<float>(lines.size()) * lineH + 10;
  // over its head: the top of the figure in this frame, on the side it faces
  const float headX = spriteX + static_cast<float>(m_facing > 0 ? m_figRight - 8 : m_figLeft + 8) * pixel;
  const float topY = spriteY + static_cast<float>(m_figTop) * pixel;
  const float rise = static_cast<float>(4 * (1 - std::min(1.0, age / 0.15)));
  float bx = std::clamp(headX - bw / 2, 4.0F, std::max(4.0F, ctx.w - bw - 4));
  float by = std::max(2.0F, topY - bh - 12 + rise);
  const Color ink{0.14F, 0.11F, 0.17F, 1}, paper{0.99F, 0.97F, 0.94F, 1};
  auto withA2 = [alpha](Color k) { return Color{k.r, k.g, k.b, k.a * alpha}; };
  c.roundRect(bx + 2, by + 3, bw, bh, 5, withA2(Color{0, 0, 0, 0.22F}));
  c.roundRect(bx, by, bw, bh, 5, withA2(paper), 2, withA2(ink));
  // the tail, in steps like the pixels below it
  const float tx = std::clamp(headX, bx + 10, bx + bw - 10);
  for (int k = 0; k < 4; ++k) {
    const float w = 10 - k * 2.5F;
    c.roundRect(tx - w / 2 - 1, by + bh - 1 + k * 2.5F, w + 2, 3, 0, withA2(ink));
    if (k < 3) c.roundRect(tx - w / 2 + 1, by + bh - 2 + k * 2.5F, std::max(0.0F, w - 2), 2.5F, 0, withA2(paper));
  }
  for (size_t i = 0; i < lines.size(); ++i) c.text(lines[i], st, bx + 8, by + 5 + static_cast<float>(i) * lineH, ink, alpha);
}

// ── a drawn companion in other colours ──────────────────────────────────────
// Its main colour (the most common lively hue in its drawing) moves to the body
// colour and its second one (another hue, if it has one) to the markings', each
// pixel keeping its own shading; greys, whites, outlines and eyes stay as they
// are. Done through a table of the drawing's colours, so a frame costs a lookup
// per pixel.

void PetWidget::tintClassic(PetPixels& px) {
  if (m_tintFor != m_species) {
    // what its drawing is made of: the strongest hue and the next one apart from it
    PetPixels painted{};
    if (m_frames) frameCompose(*m_frames, fcell::Idle, false, painted);
    const PetPixels& d = m_frames ? painted : petDrawing(m_species, PetDrawing::Stand, false);
    float weight[36] = {};
    for (int i = 0; i < kPetPixels * kPetPixels; ++i) {
      if (!d[i * 4 + 3]) continue;
      const Oklch o = toOklch({d[i * 4] / 255.0F, d[i * 4 + 1] / 255.0F, d[i * 4 + 2] / 255.0F, 1});
      if (o.C > 0.04F) weight[static_cast<int>(o.h / 10) % 36] += o.C;
    }
    auto peak = [&](int skipNear) {
      int best = -1;
      for (int b = 0; b < 36; ++b) {
        if (skipNear >= 0 && std::min(std::abs(b - skipNear), 36 - std::abs(b - skipNear)) < 5) continue;
        const float w = weight[(b + 35) % 36] * 0.5F + weight[b] + weight[(b + 1) % 36] * 0.5F;
        if (best < 0 || w > weight[(best + 35) % 36] * 0.5F + weight[best] + weight[(best + 1) % 36] * 0.5F) best = b;
      }
      return best;
    };
    const int p1 = peak(-1), p2 = peak(p1);
    auto mass = [&](int b) { return b < 0 ? 0 : weight[(b + 35) % 36] * 0.5F + weight[b] + weight[(b + 1) % 36] * 0.5F; };
    m_tintHue1 = p1 * 10.0F + 5, m_tintHue2 = mass(p2) > mass(p1) * 0.25F ? p2 * 10.0F + 5 : -1;
    // its typical lightness and chroma there
    float sl = 0, sc = 0, n = 0;
    for (int i = 0; i < kPetPixels * kPetPixels; ++i) {
      if (!d[i * 4 + 3]) continue;
      const Oklch o = toOklch({d[i * 4] / 255.0F, d[i * 4 + 1] / 255.0F, d[i * 4 + 2] / 255.0F, 1});
      if (o.C > 0.04F && std::abs(std::fmod(o.h - m_tintHue1 + 540.0F, 360.0F) - 180.0F) < 25) sl += o.L, sc += o.C, ++n;
    }
    m_tintL = n > 0 ? sl / n : 0.6F, m_tintC = n > 0 ? sc / n : 0.1F;
    m_tintFor = m_species;
    m_tintLut.clear();
  }
  const Oklch body = toOklch(m_pal.body), mark = toOklch(m_pal.mark);
  for (int i = 0; i < kPetPixels * kPetPixels; ++i) {
    std::uint8_t* q = &px[i * 4];
    if (!q[3]) continue;
    const std::uint32_t key = q[0] | q[1] << 8 | q[2] << 16;
    auto it = m_tintLut.find(key);
    if (it == m_tintLut.end()) {
      Oklch o = toOklch({q[0] / 255.0F, q[1] / 255.0F, q[2] / 255.0F, 1});
      std::uint32_t out = key;
      if (o.C > 0.035F) {
        auto gap = [](float a, float b) { return std::abs(std::fmod(a - b + 540.0F, 360.0F) - 180.0F); };
        const float w1 = std::exp(-std::pow(gap(o.h, m_tintHue1) / 40.0F, 2.0F));
        const float w2 = m_tintHue2 >= 0 ? std::exp(-std::pow(gap(o.h, m_tintHue2) / 40.0F, 2.0F)) : 0;
        const bool second = w2 > w1;
        const Oklch& to = second ? mark : body;
        const float from = second ? m_tintHue2 : m_tintHue1, w = std::clamp(std::max(w1, w2) * 1.3F, 0.0F, 1.0F);
        Oklch n = o;
        n.h = std::fmod(o.h + (to.h - from) + 720.0F, 360.0F);
        n.C = std::min(0.32F, o.C * (to.C + 0.005F) / std::max(0.02F, m_tintC));
        n.L = std::clamp(o.L + (to.L - m_tintL) * 0.75F, 0.05F, 0.98F);
        // blend by how much of that hue it was
        Oklch m = o;
        const float dh = std::fmod(n.h - o.h + 540.0F, 360.0F) - 180.0F;
        m.h = std::fmod(o.h + dh * w + 360.0F, 360.0F), m.C = o.C + (n.C - o.C) * w, m.L = o.L + (n.L - o.L) * w;
        const Color c = fromOklch(m);
        auto b = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255)); };
        out = b(c.r) | b(c.g) << 8 | b(c.b) << 16;
      }
      it = m_tintLut.emplace(key, out).first;
    }
    q[0] = it->second & 0xFF, q[1] = it->second >> 8 & 0xFF, q[2] = it->second >> 16 & 0xFF;
  }
}

// ── rigged companions ───────────────────────────────────────────────────────

void PetWidget::applyPalette() {
  RigPalette p = m_variantPal;
  if (m_paint == "theme") p = m_themePal;
  if (m_paint == "wallpaper" && m_wall) {
    std::lock_guard lock(m_wall->mutex);
    if (m_wall->ready) p = rigPaletteFrom(m_wall->colors, m_variantPal);
  }
  Color* slots[5] = {&p.body, &p.belly, &p.mark, &p.eyes, &p.extra};
  for (int i = 0; i < 5; ++i)
    if (m_overrides[i]) *slots[i] = *m_overrides[i];
  if (!(p.body == m_pal.body && p.belly == m_pal.belly && p.mark == m_pal.mark && p.eyes == m_pal.eyes && p.extra == m_pal.extra)) {
    m_pal = p;
    m_frameTick = -1;
    m_tintLut.clear();
  }
}

// The wallpaper's colours, read off the main thread (a 4K PNG takes ~0.1 s
// to decode) whenever the wallpaper changes, then the palette follows.
void PetWidget::watchWallpaper(const std::string& path) {
  if (path.empty() || path == m_wallPath) return;
  m_wallPath = path;
  auto wall = std::make_shared<WallColors>();
  wall->path = path;
  m_wall = wall;
  std::thread([wall, path] {
    WallpaperSample sample;
    std::vector<Color> colors;
    if (sampleWallpaper(path, 160, sample)) colors = sample.dominant;
    std::lock_guard lock(wall->mutex);
    wall->colors = std::move(colors);
    wall->ready = true;
  }).detach();
}

LifeIn PetWidget::lifeIn(double dt) const {
  LifeIn in;
  in.dt = dt;
  in.time = m_time + m_phi;
  in.mode = m_mode;
  in.lie = m_lie;
  in.phase = m_phase;
  const double base = m_rig ? m_rig->spec.walk * unitPx() * m_speed : 1;
  in.pace = m_mode == Mode::Walk ? m_vel / std::max(1e-3, base) : 0;
  const double art = std::max(1e-3, unitPx());
  in.vy = m_grounded ? 0 : m_vy / art;
  in.grounded = m_grounded;
  in.jumped = m_jumped;
  in.crouch = m_crouch >= 0 ? 1 - m_crouch / std::max(1e-3, crouchTime()) : -1;
  in.landAge = m_landAge, in.landHard = m_landHard;
  in.turn = m_turn >= 0 ? m_turn / kRigTurn : -1;
  in.blink = m_blink < 0;
  in.kickAge = m_kickAge, in.beat = m_beat;
  in.act = m_quirk == Q::Act ? m_act : RigAct::None;
  in.actAge = m_quirkAge, in.actDur = m_quirkDur;
  in.petted = m_petted, in.alert = m_alert;
  in.airTime = m_airTime;
  in.wake = m_wake;
  in.carried = m_dragging;
  return in;
}

// A behaviour as it looks `t` seconds in, from a standing start, for previews
// and the gallery: a life of its own, stepped at 60 Hz up to t.
bool PetWidget::previewFrames(Mode mode, double t, PetPixels& out) const {
  if (!m_frames) return false;
  LifeIn in;
  in.mode = mode;
  in.phase = mode == Mode::Walk ? t * 1.2 : 0;
  in.beat = mode == Mode::Dance ? static_cast<int>(t * 2) : 0;
  if (mode == Mode::Jump) in.grounded = false, in.vy = std::fmod(t, 1.0) < 0.5 ? -60 : 60;
  frameCompose(*m_frames, frameCell(in, t, m_rig->spec.plan), false, out);
  return true;
}

RigPixels PetWidget::previewRig(Mode mode, double t, RigAct act) const {
  RigPixels out{};
  if (!m_rig) return out;
  PetLife life;
  life.reset(m_rig, 7);
  LifeIn in;
  in.mode = mode;
  in.lie = false;
  in.act = act;
  in.actDur = PetLife::actLength(act);
  const double pace = mode == Mode::Walk ? 1 : 0;
  const double cycle = rigCycle(m_rig->spec, 0);
  const int steps = std::max(1, static_cast<int>(std::lround((t + 1.0) * 60)));
  for (int i = 0; i < steps; ++i) {
    in.dt = 1.0 / 60;
    in.time = i / 60.0;
    // a second to settle into the behaviour, then t into it
    const double into = in.time - 1.0;
    in.pace = pace;
    in.phase = into > 0 ? into * m_rig->spec.walk / cycle : 0;
    in.actAge = std::max(0.0, into);
    in.act = into >= 0 ? act : RigAct::None;
    in.kickAge = mode == Mode::Dance ? std::fmod(std::max(0.0, into), 0.5) : 9;
    in.beat = mode == Mode::Dance ? static_cast<int>(std::max(0.0, into) * 2) : 0;
    if (mode == Mode::Jump) {
      in.grounded = false;
      in.vy = into < 0.3 ? -60 : 60;
      in.airTime = std::max(0.0, into);
    }
    life.step(in);
  }
  const RigMark mark = mode == Mode::Sleep ? RigMark::Sleep : mode == Mode::Dance ? RigMark::Notes : RigMark::None;
  life.draw(m_pal, false, in, mark, t, out);
  return out;
}

void PetWidget::uploadFrame(const std::uint8_t* pixels, int side) {
  const size_t bytes = static_cast<size_t>(side) * side * 4;
  if (m_sprite && m_texSide == side && m_frameBytes.size() == bytes && std::equal(pixels, pixels + bytes, m_frameBytes.begin())) return;
  if (!m_sprite) glGenTextures(1, &m_sprite);
  glBindTexture(GL_TEXTURE_2D, m_sprite);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  // two clear rows under the feet: the quad's soft edge falls there, below the box,
  // and the feet keep their full opacity on the bottom row
  std::vector<std::uint8_t> padded(static_cast<size_t>(side) * (side + 2) * 4, 0);
  std::copy(pixels, pixels + bytes, padded.begin());
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, side, side + 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, padded.data());
  m_frameBytes.assign(pixels, pixels + bytes);
  m_texSide = side;
  m_figTop = side, m_figLeft = side, m_figRight = 0;
  for (int yy = 0; yy < side; ++yy)
    for (int xx = 0; xx < side; ++xx)
      if (pixels[(static_cast<size_t>(yy) * side + xx) * 4 + 3])
        m_figTop = std::min(m_figTop, yy), m_figLeft = std::min(m_figLeft, xx), m_figRight = std::max(m_figRight, xx);
}

void PetWidget::drawSpecies(const DrawContext& ctx, Mode mode, double time) {
  // Compose a new frame on the frame clock only, and upload it only when it
  // changed (a resting companion often shows the same frame for a while).
  if (ctx.w <= 0 || ctx.h <= 0 || ctx.scale <= 0) return;
  const long tick = m_preview.empty() ? static_cast<long>(std::floor(m_time * animHz())) * 16 + static_cast<long>(animHz()) : 0;
  if (m_paint == "wallpaper" && m_species != PetKind::Sprout) {
    watchWallpaper(ctx.wallpaper);
    applyPalette();
  }
  if (m_frames) {
    if (!m_sprite || tick != m_frameTick) {
      PetPixels px;
      if (!m_preview.empty()) {
        previewFrames(mode, time, px);
      } else {
        const LifeIn in = lifeIn(0);
        const int cell = frameCell(in, m_time + m_phi, m_rig->spec.plan);
        frameCompose(*m_frames, cell, m_facing < 0, px);
        if (!m_original) tintClassic(px);
        RigMark mark = RigMark::None;
        double age = m_time;
        if (m_petted >= 0) mark = RigMark::Hearts, age = m_petted;
        else if (m_alert >= 0) mark = RigMark::Alert, age = m_alert;
        else if (m_mode == Mode::Sleep) mark = RigMark::Sleep;
        else if (m_mode == Mode::Dance) mark = RigMark::Notes;
        drawRigMark(px.data(), kPetPixels, mark, age, m_facing < 0, m_pal);
        m_cell = cell;
      }
      uploadFrame(px.data(), kPetPixels);
      m_frameTick = tick;
    }
  } else if (m_rig) {
    if (!m_sprite || tick != m_frameTick) {
      RigPixels px;
      if (!m_preview.empty()) {
        px = previewRig(mode, time);
      } else {
        const LifeIn in = lifeIn(0);
        RigMark mark = RigMark::None;
        double age = m_time;
        if (m_petted >= 0) mark = RigMark::Hearts, age = m_petted;
        else if (m_alert >= 0) mark = RigMark::Alert, age = m_alert;
        else if (m_mode == Mode::Sleep) mark = RigMark::Sleep;
        else if (m_mode == Mode::Dance) mark = RigMark::Notes;
        m_life.draw(m_pal, m_facing < 0, in, mark, age, px);
      }
      uploadFrame(px.data(), kRigPixels);
      m_frameTick = tick;
    }
  } else if (!m_sprite || tick != m_frameTick) {
    const PetLook look = m_preview.empty() ? liveLook(mode) : previewLook(mode, time);
    PetPixels px = petFrame(m_species, look);
    if (!m_original) tintClassic(px);
    uploadFrame(px.data(), kPetPixels);
    m_frameTick = tick;
  }
  // Snap to physical pixels on fractional-scale displays as well. Very small
  // boxes shrink to fit instead of clipping the face or feet. The frame has
  // the feet on its last row: they touch the bottom of the box, which is the
  // ground or the top of the window it stands on.
  const int side = m_texSide;
  const float available = std::min(ctx.w, ctx.h) * ctx.scale / static_cast<float>(side);
  const float pixel = (available >= 1 ? std::floor(available) : available) / ctx.scale;
  const float size = static_cast<float>(side) * pixel;
  m_artPx = pixel;  // what a pixel of the art measures on screen: the steps are paced to it
  const float x = std::floor((ctx.w - size) * 0.5F * ctx.scale) / ctx.scale;
  const float y = std::floor((ctx.h - size) * ctx.scale) / ctx.scale;
  m_canvas.image(m_sprite, side, side + 2, x, y, size, size * static_cast<float>(side + 2) / static_cast<float>(side), 0, static_cast<float>(m_opacity));
  // where the creature is in its box: what a click can touch, where a bubble goes
  m_hit = m_figTop < side ? Rect{x + static_cast<float>(m_figLeft) * pixel, y + static_cast<float>(m_figTop) * pixel,
                                 static_cast<float>(m_figRight - m_figLeft + 1) * pixel, static_cast<float>(side - m_figTop) * pixel}
                          : Rect{};
  drawBubble(ctx, x, y, pixel, side);
}

}  // namespace undershell
