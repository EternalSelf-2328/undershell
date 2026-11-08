// SPDX-License-Identifier: GPL-3.0-or-later
// The pet: a selectable pixel-art companion (original sprite atlases in art/pets)
// that lives on your desktop. It walks along the line of its box, hops up
// onto the tops of windows (Umbriel says where they are) and falls when the
// window under it goes; it dances on the beat while music plays and dozes
// off after a minute of silence. Drawn over the windows but never in the
// way: the overlay layer, click-through, a surface the size of its box that
// moves with it.
#include "pet.hpp"

#include "i18n.hpp"

#include <algorithm>
#include <cmath>

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
}  // namespace

PetWidget::~PetWidget() {
  if (m_sprite) glDeleteTextures(1, &m_sprite);
}

void PetWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  const toml::table& t = cfg.options;
  m_species = petKind(get<std::string>(t, "species", "sprout"));
  m_body = noct.color(get<std::string>(t, "color", "primary"), Color::fromHex("#7fd18b"));
  m_sprout = noct.color(get<std::string>(t, "sprout", "#7ed957"), Color::fromHex("#7ed957"));
  m_speed = std::clamp(get<double>(t, "speed", 1.0), 0.2, 3.0);
  m_climb = get<bool>(t, "climb", true);
  m_sleeps = get<bool>(t, "sleep", true);
  m_dances = get<bool>(t, "dance", true);
  m_opacity = std::clamp(get<double>(t, "opacity", 1.0), 0.05, 1.0);
  m_preview = get<std::string>(t, "pet_preview", "");
  if (m_preview != "idle" && m_preview != "sit" && m_preview != "walk" && m_preview != "jump" &&
      m_preview != "dance" && m_preview != "sleep") m_preview.clear();
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
  m_mode = m;
  m_timer = secs;
}

void PetWidget::tick(const TickContext& ctx) {
  if (!m_preview.empty()) return;
  // resting, it is woken only now and then: the time that passed, in steps
  // short enough for the jumps (a long wait is not one giant step)
  const bool kick = ctx.audio.onsets && ctx.audio.kicks != m_kicks;
  m_kicks = ctx.audio.kicks;
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
  }
  if (!m_placed && ctx.box.w > 0) {
    m_x = m_homeX, m_y = m_homeY;
    m_placed = true;
  }
  m_windows = ctx.windows ? *ctx.windows : std::vector<Rect>{};
  const bool music = !ctx.audio.silent && ctx.audio.energy > 0.02;
  m_silence = music ? 0 : m_silence + dt;
  const double under = support(m_x, m_y);
  const bool grounded = m_vy >= 0 && m_y >= under - 0.5;
  // in the air: fall (or fly through a jump) and land on whatever is under it
  if (!grounded) {
    m_vy += kGravity * dt;
    m_x = std::clamp(m_x + m_vx * dt, m_minX, m_maxX);
    const double next = m_y + m_vy * dt;
    const double land = support(m_x, m_y);
    if (m_vy > 0 && next >= land) {
      m_y = land, m_vy = 0, m_vx = 0;
      m_squash = 0.35;  // a little bump on landing
      pick(Mode::Idle, 0.4 + rnd() * 0.6);
    } else {
      m_y = next;
    }
  } else {
    m_y = under;
    m_timer -= dt;
    // music first: it dances; long silence: it sleeps
    if (m_dances && music && m_mode != Mode::Dance && m_mode != Mode::Walk) pick(Mode::Dance, 4 + rnd() * 6);
    if (m_sleeps && m_silence > 60 && m_mode != Mode::Sleep) pick(Mode::Sleep, 1e9);
    if (m_mode == Mode::Sleep && (music || m_silence < 60)) pick(Mode::Idle, 1);
    switch (m_mode) {
      case Mode::Walk: {
        const double v = 46 * m_speed * m_facing;
        const double nx = m_x + v * dt;
        if (nx <= m_minX || nx >= m_maxX) m_facing = -m_facing;  // the edge of the screen: turn
        m_x = std::clamp(nx, m_minX, m_maxX);
        // a window top ahead and within reach: hop onto it
        if (m_climb && rnd() < dt * 0.8)
          for (const Rect& w : m_windows) {
            const double edge = m_facing > 0 ? w.x : w.x + w.w, dist = (edge - m_x) * m_facing, rise = m_y - w.y;
            if (dist > 4 && dist < 140 && rise > 24 && rise < 300 && w.y > 40) {
              const double vy = -std::sqrt(2 * kGravity * (rise + 28)), flight = -vy / kGravity * 1.6;
              m_vy = vy, m_vx = (dist + 24) * m_facing / flight;
              m_mode = Mode::Jump;
              break;
            }
          }
        if (m_timer <= 0) pick(rnd() < 0.5 ? Mode::Idle : Mode::Sit, 1.5 + rnd() * 4);
        break;
      }
      case Mode::Dance:
        if (kick) m_vy = -260 - 140 * std::clamp(ctx.audio.kickStrength, 0.0, 1.0);  // a hop on the kick
        if (m_timer <= 0) pick(music ? Mode::Walk : Mode::Idle, music ? 2 + rnd() * 3 : 2);
        if (m_mode == Mode::Walk && rnd() < 0.5) m_facing = -m_facing;
        break;
      case Mode::Jump: pick(Mode::Idle, 0.5); break;
      case Mode::Sleep: break;
      default:  // idle or sitting: look about, then set off
        if (m_timer <= 0) {
          if (rnd() < 0.7) {
            if (rnd() < 0.35) m_facing = -m_facing;
            pick(Mode::Walk, 2 + rnd() * 6);
          } else {
            pick(rnd() < 0.5 ? Mode::Sit : Mode::Idle, 2 + rnd() * 5);
          }
        }
    }
  }
  m_squash = std::max(0.0, m_squash - dt * 2.5);
  // blinking, now and then
  m_blink -= dt;
  if (m_blink < -0.12) m_blink = 2 + rnd() * 3.5;
}

bool PetWidget::animating(const TickContext&) const {
  return m_preview.empty() && (m_mode == Mode::Walk || m_mode == Mode::Dance || m_mode == Mode::Jump || m_vy != 0 || m_squash > 0);
}

double PetWidget::nextWakeup(double now) const {
  if (!m_preview.empty()) return 1e18;
  // resting: the next blink, the end of a rest, or a look at the windows now and then
  return now + std::clamp(std::min(m_blink > 0 ? m_blink : 0.12, m_timer > 0 ? m_timer : 0.5), 0.05, 0.5);
}

bool PetWidget::offset(float& dx, float& dy) const {
  dx = static_cast<float>(m_x - m_homeX), dy = static_cast<float>(m_y - m_homeY);
  return m_placed;
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
  if (ctx.text) ctx.text->collect(60);
}

void PetWidget::drawSpecies(const DrawContext& ctx, Mode mode, double time) {
  if (ctx.w <= 0 || ctx.h <= 0 || ctx.scale <= 0) return;
  const bool frame = (mode == Mode::Walk || mode == Mode::Dance) &&
                     std::fmod(time * (mode == Mode::Walk ? 6 * m_speed : 4), 2.0) < 1;
  const bool blink = m_preview.empty() && m_blink < 0, flip = m_facing < 0;
  // Upload only when the discrete animation frame changes.
  const int key = static_cast<int>(m_species) * 64 + static_cast<int>(mode) * 8 + frame * 4 + blink * 2 + flip;
  if (!m_sprite || key != m_spriteKey) {
    const PetPixels pixels = petPixels(m_species, mode, frame, blink, flip);
    if (!m_sprite) glGenTextures(1, &m_sprite);
    glBindTexture(GL_TEXTURE_2D, m_sprite);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kPetPixels, kPetPixels, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    m_spriteKey = key;
  }
  // Snap to physical pixels on fractional-scale displays as well. Very small
  // boxes shrink to fit instead of clipping the face or feet.
  const float available = std::min(ctx.w, ctx.h) * ctx.scale / kPetPixels;
  const float pixel = (available >= 1 ? std::floor(available) : available) / ctx.scale;
  const float size = kPetPixels * pixel;
  const float x = std::floor((ctx.w - size) * 0.5F * ctx.scale) / ctx.scale;
  const float y = std::floor((ctx.h - size) * ctx.scale) / ctx.scale;
  m_canvas.image(m_sprite, kPetPixels, kPetPixels, x, y, size, size, 0, static_cast<float>(m_opacity));
}

}  // namespace undershell
