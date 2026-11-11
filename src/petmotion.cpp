// SPDX-License-Identifier: GPL-3.0-or-later
// Motion for the sprite companions. Each one has three drawings (standing,
// a step, resting); everything else is made here, in whole sprite pixels, as
// pixel artists animate with few frames: the figure bobs, squashes and leans
// about its feet, and soft regions of the drawing move on their own — heads
// nod or hold still, tails and capes trail with a wave that travels to the
// tip, wings beat, an orb floats, a serpent's body undulates head to tail.
// Every output pixel takes one source pixel (nearest), so nothing is
// smoothed and alpha stays binary. The timing that drives it is in pet.cpp;
// the research behind the numbers is in docs/PETS.md.
#include "petart.hpp"

#include <algorithm>
#include <cmath>

namespace undershell {
namespace {
constexpr int S = kPetPixels;
constexpr int kDrop = 8;         // the atlases keep 8 empty rows under the feet; frames put the feet on the last row
constexpr float kFeet = S - 1;   // the feet row in a frame
constexpr float kTall = 112;     // the figure's height in the atlases

// A soft region of a drawing (atlas coordinates, facing right): an ellipse
// with a feathered edge, and the point it hangs from (tails, wings).
enum class Part : unsigned char { None, Head, HeadL, HeadR, Tail, Wing, Orb, Visor };
struct Region {
  Part part = Part::None;
  short cx = 0, cy = 0, rx = 0, ry = 0, rootX = 0, rootY = 0;
  float phase = 0;  // a tail of its own: its wave runs a little behind the others
};
struct Spec {
  PetMotion motion;
  Region regions[5];
};
using G = PetGait;
using P = Part;
constexpr Region R(Part p, int cx, int cy, int rx, int ry, int rootX = 0, int rootY = 0, float ph = 0) {
  return {p, static_cast<short>(cx), static_cast<short>(cy), static_cast<short>(rx), static_cast<short>(ry),
          static_cast<short>(rootX), static_cast<short>(rootY), ph};
}

// Speeds and steps: one step covers `step` px, so the cadence follows the
// ground speed and the feet do not slide. A horse walks a ~1.1 s cycle with
// two head nods, a big dog ~1.2 Hz, a fox trots lighter and quicker, a heavy
// mecha takes ~1.5 s, a knight two steps a second (docs/PETS.md, Motion).
const Spec kSpecs[2 * kPetFamilySize] = {
    // ── Actitud ──
    {{G::Serpent, false, 24, 20}, {R(P::Head, 100, 38, 20, 18), R(P::Tail, 45, 28, 34, 18, 92, 30)}},
    {{G::Hoof, false, 30, 16}, {R(P::Head, 100, 28, 18, 18), R(P::Tail, 28, 55, 24, 30, 52, 58)}},
    {{G::Bird, true, 18, 8},
     {R(P::Head, 98, 30, 14, 14), R(P::Tail, 30, 90, 32, 30, 64, 78), R(P::Wing, 50, 42, 36, 26, 78, 55)}},
    {{G::Paw, false, 28, 12},
     {R(P::Head, 77, 30, 16, 18), R(P::HeadL, 30, 46, 16, 12), R(P::HeadR, 106, 46, 14, 12), R(P::Tail, 15, 75, 12, 22, 30, 68)}},
    {{G::Hoof, true, 30, 16},
     {R(P::Wing, 40, 35, 36, 30, 72, 45), R(P::Head, 96, 24, 18, 16), R(P::Tail, 26, 96, 22, 22, 46, 80)}},
    {{G::Paw, true, 28, 12},
     {R(P::Wing, 38, 42, 36, 34, 76, 42), R(P::Head, 100, 34, 18, 20), R(P::Tail, 20, 96, 14, 16, 36, 90)}},
    {{G::Fox, false, 34, 11}, {R(P::Tail, 38, 60, 34, 50, 70, 82), R(P::Head, 90, 30, 14, 16)}},
    {{G::Wolf, false, 30, 15}, {R(P::Head, 108, 72, 17, 18), R(P::Tail, 32, 40, 30, 22, 62, 52)}},
    {{G::Lumber, true, 20, 12},
     {R(P::Wing, 40, 44, 36, 32, 76, 46), R(P::Head, 102, 36, 16, 14), R(P::Tail, 36, 106, 30, 14, 60, 96)}},
    {{G::Hoof, false, 30, 16},
     {R(P::Tail, 28, 78, 24, 38, 56, 62), R(P::Tail, 76, 32, 16, 18, 92, 30, 0.8F), R(P::Head, 96, 26, 18, 16)}},
    {{G::Mech, false, 18, 14}, {R(P::Head, 73, 20, 12, 12), R(P::Visor, 73, 20, 11, 9)}},
    {{G::Mech, false, 24, 12}, {R(P::Tail, 100, 72, 28, 42, 76, 32), R(P::Head, 56, 22, 12, 12), R(P::Visor, 56, 24, 11, 9)}},
    {{G::Hover, false, 22, 20},
     {R(P::Tail, 26, 76, 22, 36, 56, 24), R(P::Tail, 66, 106, 46, 14, 66, 82, 0.6F), R(P::Orb, 100, 24, 11, 11), R(P::Head, 64, 22, 12, 12)}},
    {{G::Biped, false, 26, 13},
     {R(P::Tail, 60, 98, 52, 22, 60, 66), R(P::Tail, 102, 46, 22, 16, 76, 36, 1.1F), R(P::Head, 66, 16, 10, 10)}},
    // ── Chibi: shorter legs, so shorter steps (a quicker cadence) ──
    {{G::Serpent, false, 22, 16}, {R(P::Head, 80, 45, 26, 22), R(P::Tail, 26, 48, 22, 26, 60, 40)}},
    {{G::Hoof, false, 27, 11}, {R(P::Head, 86, 40, 26, 26), R(P::Tail, 26, 66, 20, 22, 46, 72)}},
    {{G::Bird, true, 16, 6},
     {R(P::Head, 90, 45, 20, 20), R(P::Tail, 26, 96, 22, 22, 52, 86), R(P::Wing, 40, 50, 30, 22, 66, 60)}},
    {{G::Paw, false, 25, 9},
     {R(P::Head, 66, 38, 20, 20), R(P::HeadL, 26, 66, 16, 14), R(P::HeadR, 104, 62, 16, 14), R(P::Tail, 16, 96, 12, 12, 28, 92)}},
    {{G::Hoof, true, 27, 11},
     {R(P::Wing, 30, 44, 28, 26, 56, 56), R(P::Head, 86, 40, 22, 22), R(P::Tail, 20, 90, 15, 18, 36, 84)}},
    {{G::Paw, true, 25, 9},
     {R(P::Wing, 34, 50, 32, 28, 62, 56), R(P::Head, 94, 40, 20, 20), R(P::Tail, 20, 96, 14, 12, 36, 96)}},
    {{G::Fox, false, 31, 8}, {R(P::Tail, 34, 76, 32, 40, 66, 92), R(P::Head, 88, 56, 20, 20)}},
    {{G::Wolf, false, 27, 10}, {R(P::Head, 96, 70, 22, 20), R(P::Tail, 34, 50, 28, 18, 60, 60)}},
    {{G::Lumber, true, 18, 9},
     {R(P::Wing, 34, 44, 30, 28, 60, 56), R(P::Head, 96, 44, 20, 18), R(P::Tail, 30, 96, 22, 12, 52, 96)}},
    {{G::Hoof, false, 27, 11}, {R(P::Tail, 30, 66, 28, 40, 66, 44), R(P::Head, 90, 46, 20, 20)}},
    {{G::Mech, false, 16, 10}, {R(P::Head, 80, 56, 18, 16), R(P::Visor, 80, 56, 15, 10)}},
    {{G::Mech, false, 22, 9}, {R(P::Tail, 102, 86, 22, 30, 82, 62), R(P::Head, 66, 50, 20, 18), R(P::Visor, 66, 50, 16, 10)}},
    {{G::Hover, false, 20, 16},
     {R(P::Tail, 24, 80, 18, 30, 50, 40), R(P::Tail, 100, 86, 12, 24, 86, 52, 0.7F), R(P::Orb, 100, 40, 10, 10), R(P::Head, 58, 42, 20, 18)}},
    {{G::Biped, false, 24, 10},
     {R(P::Tail, 96, 82, 26, 20, 80, 62), R(P::Tail, 20, 80, 14, 18, 40, 66, 0.9F), R(P::Head, 60, 30, 30, 18)}},
};

const Spec* specOf(PetKind kind) {
  if (kind == PetKind::Sprout) return nullptr;
  const int i = static_cast<int>(kind) - static_cast<int>(PetKind::LongClassic);
  return i >= 0 && i < 2 * kPetFamilySize ? &kSpecs[i] : nullptr;
}

float weight(const Region& r, float x, float y) {
  if (std::abs(x - r.cx) >= r.rx || std::abs(y - r.cy) >= r.ry) return 0;  // outside its box: no square root
  const float dx = (x - r.cx) / r.rx, dy = (y - r.cy) / r.ry;
  const float d = std::sqrt(dx * dx + dy * dy);
  if (d >= 1) return 0;
  if (d <= 0.65F) return 1;
  const float t = (1 - d) / 0.35F;
  return t * t * (3 - 2 * t);
}
// how far along a hanging part (0 at its root, 1 at its tip)
// (std::hypot is careful about overflow and slow; these numbers are small)
float dist(float x, float y) { return std::sqrt(x * x + y * y); }
float along(const Region& r, float x, float y) {
  const float reach = dist(static_cast<float>(r.cx - r.rootX), static_cast<float>(r.cy - r.rootY)) + std::max(r.rx, r.ry);
  return std::clamp(dist(x - r.rootX, y - r.rootY) / std::max(1.0F, reach), 0.0F, 1.0F);
}

bool cool(const std::uint8_t* p) {  // a visor's cyan or jade
  const int r = p[0], g = p[1], b = p[2];
  return std::max(g, b) > 120 && std::max(g, b) > r + 40;
}

// pixel glyphs: '#' ink, drawn with a one-pixel dark outline so they read on any wallpaper
constexpr const char* kBigZ[] = {"#####", "...#.", "..#..", ".#...", "#####"};
constexpr const char* kSmallZ[] = {"###", ".#.", "###"};
constexpr const char* kNote[] = {"..##.", "..#.#", "..#..", ".##..", "###..", ".#..."};
constexpr const char* kPuffs[3][3] = {{"...", ".#.", "..."}, {".#.", "###", ".#."}, {"#.#", "...", "#.#"}};

void put(PetPixels& p, int x, int y, std::uint32_t c) {
  if (x < 0 || x >= S || y < 0 || y >= S) return;
  auto* v = p.data() + (y * S + x) * 4;
  v[0] = c >> 16, v[1] = c >> 8, v[2] = c, v[3] = 255;
}
template <size_t N>
void glyph(PetPixels& p, const char* const (&rows)[N], int x0, int y0, std::uint32_t ink, bool outline) {
  const int w = static_cast<int>(std::char_traits<char>::length(rows[0]));
  auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < static_cast<int>(N) && rows[y][x] == '#'; };
  if (outline)
    for (int y = -1; y <= static_cast<int>(N); ++y)
      for (int x = -1; x <= w; ++x)
        if (!at(x, y) && (at(x - 1, y) || at(x + 1, y) || at(x, y - 1) || at(x, y + 1))) put(p, x0 + x, y0 + y, 0x241c2c);
  for (int y = 0; y < static_cast<int>(N); ++y)
    for (int x = 0; x < w; ++x)
      if (rows[y][x] == '#') put(p, x0 + x, y0 + y, ink);
}

struct Box { int x0 = S, y0 = S, x1 = -1, y1 = -1; };
Box boxOf(const PetPixels& p, int fromRow = 0) {
  Box b;
  for (int y = fromRow; y < S; ++y)
    for (int x = 0; x < S; ++x)
      if (p[(y * S + x) * 4 + 3]) b.x0 = std::min(b.x0, x), b.x1 = std::max(b.x1, x), b.y0 = std::min(b.y0, y), b.y1 = std::max(b.y1, y);
  return b;
}
}  // namespace

PetMotion petMotion(PetKind kind) {
  const Spec* s = specOf(kind);
  return s ? s->motion : PetMotion{PetGait::Paw, false, 23, 8};
}

PetPixels petFrame(PetKind kind, const PetLook& in) {
  // The drawings are shown as drawn (2026-11-09): bending them to fake a step
  // (squash, lean, tails and capes waving, a serpent's undulation) read as a
  // wobble. Only whole moves stay: a jump's lift, the flip, the glints.
  PetLook look = in;
  look.sy = 1, look.sx = in.sx < 0 ? -1.0F : 1.0F, look.lean = 0, look.bob = 0;
  look.wave = 0, look.sway = 0, look.flap = 0, look.headX = look.headY = 0;
  look.heads[0] = look.heads[1] = look.heads[2] = 0, look.orbX = look.orbY = 0;
  PetPixels out{};
  const Spec* spec = specOf(kind);
  if (!spec) return out;
  const PetPixels& src = petDrawing(kind, look.drawing, look.blink);
  // Regions follow the standing and stride drawings; the resting drawing only
  // breathes (its head and tail are elsewhere).
  const bool regions = look.drawing != PetDrawing::Rest;
  const Region* head = nullptr;
  for (const Region& r : spec->regions)
    if (r.part == Part::Head) head = &r;
  // Squash and stretch live in the torso: the head keeps its rows (an eye is
  // never dropped) and the feet stay planted; the body widens a little as it
  // squashes, to keep its volume.
  const float give = (1 / std::clamp(look.sy, 0.7F, 1.3F) - 1) * kTall;
  const float sx = std::max(0.08F, std::abs(look.sx)) * (1 + 0.5F * std::clamp(1 - look.sy, -0.2F, 0.3F));
  const float k = 2 * static_cast<float>(M_PI) / 70.0F;  // the serpent's wave, ~0.65 of its body
  auto torso = [](float u) {  // 0 below the hips, 1 from the shoulders up
    const float t = std::clamp((u - 0.12F) / 0.58F, 0.0F, 1.0F);
    return t * t * (3 - 2 * t);
  };
  // The waves only depend on how far along a part (or the serpent's body) a
  // pixel is: tables per frame instead of a sine per pixel.
  constexpr int kSteps = 64;
  float tailSin[5][kSteps + 1], tailCos[5][kSteps + 1];
  for (int i = 0; i < 5; ++i)
    if (spec->regions[i].part == Part::Tail)
      for (int j = 0; j <= kSteps; ++j) {
        const float a = static_cast<float>(j) / kSteps, ph = look.swayPhase + spec->regions[i].phase - 2.4F * a;
        tailSin[i][j] = look.sway * a * std::sqrt(a) * std::sin(ph);
        tailCos[i][j] = 0.35F * look.sway * a * std::cos(ph);
      }
  constexpr int kFar = 184;
  float waveY[kFar], waveX[kFar];
  if (look.wave != 0)
    for (int j = 0; j < kFar; ++j) {
      const float amp = look.wave * (0.3F + 0.7F * std::min(j / 90.0F, 1.0F));
      waveY[j] = amp * std::sin(look.wavePhase - k * j), waveX[j] = 0.5F * amp * std::cos(look.wavePhase - k * j);
    }
  for (int y = 0; y < S; ++y) {
    // back from the frame to the atlas: lift, then squash about the feet
    const float hd = kFeet - y - look.lift;
    const float hs = hd + (give - look.bob) * torso(hd / kTall);  // the bob raises the body, not the feet
    const float ay = kFeet - kDrop - hs, h = std::clamp(hs / kTall, 0.0F, 1.2F);
    for (int x = 0; x < S; ++x) {
      // turn about the middle, lean from the feet up
      const float ax = 63.5F + (x - 63.5F) / sx - look.lean * h;
      float dx = 0, dy = 0;
      if (regions) {
        for (int ri = 0; ri < 5; ++ri) {
          const Region& r = spec->regions[ri];
          if (r.part == Part::None) break;
          const float w = weight(r, ax, ay);
          if (w <= 0) continue;
          switch (r.part) {
            case Part::Head: dx += look.headX * w, dy += (look.headY + look.heads[0]) * w; break;
            case Part::HeadL: dx += look.headX * w, dy += (look.headY + look.heads[1]) * w; break;
            case Part::HeadR: dx += look.headX * w, dy += (look.headY + look.heads[2]) * w; break;
            case Part::Tail: {
              // follow-through: the wave runs out to the tip, growing as it goes
              const int j = static_cast<int>(along(r, ax, ay) * kSteps + 0.5F);
              dx += tailSin[ri][j] * w, dy += tailCos[ri][j] * w;
              break;
            }
            case Part::Wing: {
              const float a = along(r, ax, ay);
              dy += look.flap * a * w, dx -= 0.25F * look.flap * a * w;
              break;
            }
            case Part::Orb: dx += look.orbX * w, dy += look.orbY * w; break;
            default: break;
          }
        }
        if (look.wave != 0) {
          // the serpent: a wave from the head to the tail, growing toward the tail
          const float far = head ? dist(ax - head->cx, ay - head->cy) : 128 - ax;
          const int j = std::clamp(static_cast<int>(far + 0.5F), 0, kFar - 1);
          dy += waveY[j], dx += waveX[j];
        }
      }
      const int fx = static_cast<int>(std::floor(ax - dx + 0.5F)), fy = static_cast<int>(std::floor(ay - dy + 0.5F));
      if (fx < 0 || fx >= S || fy < 0 || fy >= S) continue;
      const std::uint8_t* s = src.data() + (fy * S + fx) * 4;
      if (!s[3]) continue;
      std::uint8_t* d = out.data() + (y * S + x) * 4;
      std::copy_n(s, 4, d);
      if (!regions) continue;
      for (const Region& r : spec->regions) {
        if (r.part == Part::Visor && look.glint >= 0 && weight(r, fx, fy) > 0.3F && cool(s)) {
          // a light crossing the visor
          const float band = r.cx - r.rx + look.glint * 2 * r.rx;
          if (std::abs(fx - band) < 1.6F)
            for (int c = 0; c < 3; ++c) d[c] = static_cast<std::uint8_t>(d[c] + (255 - d[c]) * 3 / 4);
        } else if (r.part == Part::Orb && look.glow > 0 && weight(r, fx, fy) > 0.5F) {
          // the orb lights up in two steps (palette shifts, not a blur)
          const int step = look.glow > 0.66F ? 2 : look.glow > 0.2F ? 1 : 0;
          const std::uint8_t warm[3] = {255, 238, 186};
          for (int c = 0; c < 3; ++c) d[c] = static_cast<std::uint8_t>(d[c] + (warm[c] - d[c]) * step / 5);
        }
      }
    }
  }
  // a dithered shadow under a floating figure: it shrinks as the figure rises
  if (look.shadow > 0) {
    const Box feet = boxOf(src, S - kDrop - 24);
    const float cx = feet.x1 >= 0 ? (feet.x0 + feet.x1) / 2.0F : 63.5F;
    const float half = 26 * std::clamp(look.shadow, 0.0F, 1.0F);
    for (int y = S - 3; y < S; ++y) {
      const float w = half * (y == S - 2 ? 1.0F : 0.75F);
      for (int x = static_cast<int>(cx - w); x <= static_cast<int>(cx + w); ++x)
        if (x >= 0 && x < S && ((x + y) & 1) && !out[(y * S + x) * 4 + 3]) put(out, x, y, 0x1c1824);
    }
  }
  // dust where the feet strike: a dot, a puff, a ring, gone
  if (look.dust >= 0 && look.dust < 0.33F) {
    const Box feet = boxOf(src, S - kDrop - 6);
    if (feet.x1 >= 0) {
      const int stage = look.dust < 0.08F ? 0 : look.dust < 0.19F ? 1 : 2;
      const int spread = static_cast<int>(look.dust * 18), rise = static_cast<int>(look.dust * 9);
      for (int side : {-1, 1}) {
        const int px = std::clamp(side < 0 ? feet.x0 - 3 - spread : feet.x1 + 1 + spread, 1, S - 4);
        glyph(out, kPuffs[stage], px, S - 3 - rise, 0xcfc6b4, false);
      }
    }
  }
  // asleep: z's drifting up from the head end of the resting figure
  if (look.sleepPhase >= 0) {
    const Box b = boxOf(src);
    const int ax = std::min(S - 8, b.x1 - 6), ay = b.y0 + kDrop - 4;
    for (int i = 0; i < 2; ++i) {
      const float ph = std::fmod(look.sleepPhase + i * 0.5F, 1.0F);
      const int zx = ax + static_cast<int>(ph * 8) + i * 2, zy = ay - static_cast<int>(ph * 24);
      if (ph < 0.45F) glyph(out, kSmallZ, std::min(zx, S - 5), std::max(zy, 2), 0xe6d6a6, true);
      else if (ph < 0.9F) glyph(out, kBigZ, std::min(zx, S - 7), std::max(zy, 2), 0xe6d6a6, true);
    }
  }
  // dancing: notes rising and swaying near the head
  if (look.notePhase >= 0) {
    const int hx = head ? head->cx + 10 : 96, hy = head ? head->cy + kDrop - 16 : 24;
    for (int i = 0; i < 2; ++i) {
      const float ph = std::fmod(look.notePhase + i * 0.5F, 1.0F);
      if (ph > 0.85F) continue;
      const int nx = hx + i * 9 + static_cast<int>(std::lround(2 * std::sin(ph * 9.0F + i))), ny = hy - static_cast<int>(ph * 22);
      glyph(out, kNote, std::clamp(nx, 2, S - 7), std::max(ny, 2), i ? 0x9fd8cf : 0xe8c46a, true);
    }
  }
  if (look.flip)
    for (int y = 0; y < S; ++y)
      for (int x = 0; x < S / 2; ++x)
        for (int c = 0; c < 4; ++c) std::swap(out[(y * S + x) * 4 + c], out[(y * S + S - 1 - x) * 4 + c]);
  return out;
}

}  // namespace undershell
