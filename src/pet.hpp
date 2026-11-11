// SPDX-License-Identifier: GPL-3.0-or-later
// The pet widget (see pet.cpp).
#pragma once

#include "canvas.hpp"
#include "petart.hpp"
#include "petframes.hpp"
#include "petlife.hpp"
#include "widget.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <string>
#include <vector>

namespace undershell {

class PetWidget final : public WidgetImpl {
public:
  ~PetWidget() override;
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] bool animating(const TickContext& ctx) const override;
  [[nodiscard]] double nextWakeup(double now) const override;
  [[nodiscard]] int fps() const override;
  [[nodiscard]] bool usesAudio() const override { return m_dances; }
  [[nodiscard]] bool wantsWindows() const override { return m_climb; }
  [[nodiscard]] bool overlay() const override { return true; }
  [[nodiscard]] bool offset(float& dx, float& dy) const override;
  [[nodiscard]] Color accent() const override { return m_rig ? m_pal.body : m_body; }
  // a pat, a carry, a double click: only on the creature itself, the rest stays click-through
  [[nodiscard]] std::vector<Rect> inputRects() const override;
  bool onPointer(const PointerEvent& ev) override;
  [[nodiscard]] const char* cursor() const override { return m_dragging ? "grabbing" : "pointer"; }
  // `undershell msg pet <verb> <words>`: say, react, notify. False if it does not know the verb.
  bool command(const std::string& verb, const std::string& arg) override;
  // something to say, in a bubble over its head, for `secs`
  void say(const std::string& text, double secs = 0);

  using Mode = PetPose;
  // tests: where it stands and what it does
  [[nodiscard]] double x() const { return m_x; }
  [[nodiscard]] double y() const { return m_y; }
  [[nodiscard]] Mode mode() const { return m_mode; }
  [[nodiscard]] bool carried() const { return m_dragging; }
  [[nodiscard]] std::string bubble() const { return m_bubbles.empty() ? std::string() : m_bubbles.front().text; }
  // the frame a behaviour shows `t` seconds into it, as previews and the gallery draw it
  [[nodiscard]] PetLook previewLook(Mode mode, double t) const;

  // the little things a companion does while it stands about (petQuirks)
  enum class Quirk { None, LookBack, TailSwish, HeadToss, Stamp, Howl, Pant, Shake, Peck, TailFan,
                     WingRuffle, WingStretch, Coil, VisorScan, Servo, CapeBillow, OrbSpin, HeadTilt, HeadSnap, Pounce,
                     Act /* a rigged companion's own (m_act) */ };
  // a rigged companion's frame `t` seconds into a behaviour (previews, the gallery, tests)
  [[nodiscard]] RigPixels previewRig(Mode mode, double t, RigAct act = RigAct::None) const;
  // a painted companion's frame `t` seconds into a behaviour, or false if it has no frames
  bool previewFrames(Mode mode, double t, PetPixels& out) const;
  [[nodiscard]] bool painted() const { return m_frames != nullptr; }

private:
  [[nodiscard]] double rnd();
  [[nodiscard]] double support(double x, double y) const;
  void pick(Mode m, double secs);
  void stepBy(const TickContext& ctx, double dt, bool kick);
  void drawSpecies(const DrawContext& ctx, Mode mode, double time);
  void turn();
  void startQuirk();
  [[nodiscard]] bool floats() const;
  [[nodiscard]] double unitPx() const;
  [[nodiscard]] double walkSpeed() const;
  [[nodiscard]] double crouchTime() const;
  [[nodiscard]] double animHz() const;
  [[nodiscard]] PetLook baseLook(Mode mode, double t, double phase, double kickAge, int beat, double moving) const;
  [[nodiscard]] PetLook liveLook(Mode mode) const;
  [[nodiscard]] LifeIn lifeIn(double dt) const;
  void applyPalette();
  void tintClassic(PetPixels& px);
  void lifeEvents(const TickContext& ctx, double dt);  // notifications, coming back, the battery, the hour, breaks
  void react(const std::string& how);
  void drawBubble(const DrawContext& ctx, float spriteX, float spriteY, float pixel, int side);
  void watchWallpaper(const std::string& path);
  void uploadFrame(const std::uint8_t* pixels, int side);

  PetKind m_species = PetKind::Sprout;
  // a rigged companion: its kind, its life, its colours, what it is doing
  const RigKind* m_rig = nullptr;
  // painted frames for it, when there are (petframes.hpp): they draw it instead of the rig
  const FrameAtlas* m_frames = nullptr;
  FrameStyle m_style = FrameStyle::Chibi;
  double m_frameCyc = 30;  // frame px per walk cycle
  int m_cell = -1;
  PetLife m_life;
  RigPalette m_pal;
  RigAct m_act = RigAct::None, m_lastAct = RigAct::None;
  bool m_lie = false;     // resting lying down, not sitting
  double m_pace = 1;      // 1 walking, 2 running
  double m_petted = -1, m_alert = -1, m_wake = -1;
  bool m_jumpIsPounce = false;
  // what it tells you: the options and their state
  std::string m_notify = "title";   // off react app title full
  std::vector<std::string> m_notifyIgnore;
  bool m_greet = true, m_awaySleep = true, m_battery = true, m_hourly = false, m_interact = true;
  double m_breakEvery = 0;          // minutes of use between break reminders (0 = never)
  double m_bubbleSecs = 5, m_lively = 1;
  std::uint64_t m_noteCursor = 0;
  bool m_noteStarted = false, m_away = false;
  double m_awayFor = 0, m_used = 0, m_batteryCheck = 0;
  int m_batteryWarned = 101, m_lastHour = -1;
  bool m_charging = false, m_night = false;
  struct Bubble {
    std::string text;
    double secs = 5;
  };
  std::vector<Bubble> m_bubbles;    // waiting; the first one shows
  double m_bubbleAge = 0;
  // being carried: where it was grabbed, how the pointer moved
  bool m_pressed = false, m_dragging = false;
  float m_grabX = 0, m_grabY = 0, m_pressX = 0, m_pressY = 0;
  double m_pressT = 0, m_lastClick = -9, m_dragVx = 0, m_dragVy = 0;
  // its colours: a variant of its kind, the theme's, the wallpaper's or your own, and any
  // colour set one by one over that
  std::string m_paint = "variant";
  RigPalette m_variantPal, m_themePal;
  std::array<std::optional<Color>, 5> m_overrides;  // body belly mark eyes detail
  struct WallColors {
    std::mutex mutex;
    bool ready = false;
    std::string path;
    std::vector<Color> colors;
  };
  std::shared_ptr<WallColors> m_wall;
  // a drawn companion in other colours (tintClassic)
  bool m_original = true;
  PetKind m_tintFor = PetKind::Sprout;
  float m_tintHue1 = 0, m_tintHue2 = -1, m_tintL = 0.6F, m_tintC = 0.1F;
  std::unordered_map<std::uint32_t, std::uint32_t> m_tintLut;
  std::string m_wallPath;
  PetMotion m_motion;
  GLuint m_sprite = 0;
  long m_frameTick = -1;
  std::vector<std::uint8_t> m_frameBytes;  // the frame on the texture
  int m_texSide = kPetPixels;
  mutable float m_artPx = 0;  // set as it draws
  int m_figTop = 0, m_figLeft = 0, m_figRight = 0;  // the creature's bounds in the frame (frame px)
  Rect m_hit;                                       // and in the box (logical px)
  Color m_body, m_sprout;
  double m_speed = 1, m_opacity = 1;
  bool m_climb = true, m_sleeps = true, m_dances = true;
  std::string m_preview;  // a fixed pose for previews and tests: walk, dance, sleep

  Mode m_mode = Mode::Idle;
  double m_timer = 1.5, m_time = 0, m_silence = 0, m_sound = 0, m_blink = 2, m_squash = 0;
  double m_x = 0, m_y = 0, m_vx = 0, m_vy = 0, m_facing = 1;
  double m_homeX = 0, m_homeY = 0, m_ground = 0, m_minX = 0, m_maxX = 1e9, m_boxW = 0, m_boxH = 0;
  // motion: the gait's cycle, the speed it has now, sprite px in logical px,
  // and a phase of its own so two companions never move in step
  double m_phase = 0, m_vel = 0, m_pixel = 1, m_phi = 0;
  double m_turn = -1;                  // a turn under way (s), -1: none
  bool m_turnSwapped = false;
  double m_crouch = -1, m_launchVx = 0, m_launchVy = 0;  // the crouch before a jump
  double m_landAge = 9, m_landHard = 0, m_dustAge = 9, m_airTime = 0;
  bool m_jumped = false, m_grounded = true;
  int m_lastStep = 0;
  int m_trans = 0;                     // 1 settling down, 2 getting up, 3 waking
  double m_transAge = 9;
  Quirk m_quirk = Quirk::None, m_lastQuirk = Quirk::None;
  double m_quirkAge = 0, m_quirkDur = 0, m_quirkNext = 6;
  bool m_backAgain = false;  // LookBack: it has turned back
  double m_kickAge = 9;
  int m_beat = 0;
  bool m_placed = false;
  uint64_t m_kicks = 0;
  uint64_t m_rng = 0x853C49E6748FEA9BULL;
  std::vector<Rect> m_windows;
  Canvas m_canvas;
};

}  // namespace undershell
