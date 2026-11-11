// SPDX-License-Identifier: GPL-3.0-or-later
#include "check.hpp"
#include "i18n.hpp"
#include "offscreen.hpp"
#include "pet.hpp"
#include "petframes.hpp"
#include "schema.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

using namespace undershell;

namespace {
constexpr int S = kPetPixels;
Image render(const std::string& species, const std::string& pose, int size = 128, double opacity = 1) {
  return renderWidget("pet", toml::table{{"species", species}, {"pet_preview", pose}, {"opacity", opacity}},
                      size, size, testPalette());
}

// The optional gallery is made from the same GL rendering as the desktop,
// so it also catches upside-down uploads, filtering and palette regressions.
void gallery(const std::filesystem::path& dir, bool chibi) {
  std::filesystem::create_directories(dir);
  TextRenderer text;
  Canvas canvas;
  constexpr int width = 1280, height = 1576;
  const Color bg = Color::fromHex(chibi?"#f2edf8":"#101318");
  const Color card = Color::fromHex(chibi?"#e3daee":"#1b2028");
  const Color ink = Color::fromHex(chibi?"#30243f":"#e1ded3");
  const Color accent = Color::fromHex(chibi?"#79518a":"#c4a675");
  const Color muted = Color::fromHex(chibi?"#6b597b":"#a6adb5");
  const int first=chibi?static_cast<int>(PetKind::LongChibi):1;
  Image page = renderDrawn(width, height, [&] {
    canvas.begin(width, height, 1, &text);
    canvas.roundRect(0, 0, width, height, 0, bg);
    canvas.text(chibi?"UNDERSHELL  /  CHIBI COLLECTION":"UNDERSHELL  /  GUARDIANS", {.family="Space Grotesk", .size=16, .weight=700}, 36, 26, accent);
    canvas.text(chibi?"Little guardians":"Steel and ash · Actitud", {.family="Space Grotesk", .size=38, .weight=700}, 36, 52, ink);
    canvas.text(chibi?"14 adorable versions · big eyes, small paws and plenty of character":
                      "14 companions · poses with attitude · mythology, mecha and dark fantasy", {.size=17}, 36, 104, muted);
    for (int slot = 0; slot < kPetFamilySize; ++slot) {
      const auto& s = kPetSpecies[first+slot];
      const int x = 24 + static_cast<int>(slot % 4) * 312, y = 152 + static_cast<int>(slot / 4) * 348;
      canvas.roundRect(x, y, 296, 330, 3, card);
      canvas.text(std::string(s.en), {.size=13, .weight=600}, x + 12, y + 282, ink);
      canvas.text(std::string(s.id), {.family="JetBrains Mono", .size=11}, x + 12, y + 307, accent);
    }
    canvas.text(chibi?"128 × 128 px · expressive eyes · walking and resting · transparent sprites":
                      "128 × 128 px · crisp pixels · asymmetric poses · transparent sprites", {.size=14}, 36, 1550, muted);
  });
  for (int slot = 0; slot < kPetFamilySize; ++slot) {
    const auto& s = kPetSpecies[first+slot];
    const Image sprite = render(std::string(s.id), "idle", 256);
    const int ox = 44 + static_cast<int>(slot % 4) * 312, oy = 160 + static_cast<int>(slot / 4) * 348;
    for (int y = 0; y < sprite.h; ++y) for (int x = 0; x < sprite.w; ++x) {
      const size_t src = (y * sprite.w + x) * 4, dst = ((oy + y) * width + ox + x) * 4;
      const int alpha = sprite.rgba[src + 3];
      for (int c = 0; c < 3; ++c) page.rgba[dst + c] = sprite.rgba[src + c] + page.rgba[dst + c] * (255-alpha) / 255;
    }
    CHECK(writePng(sprite, (dir / (std::string(s.id) + ".png")).string(), false));
    // Six behaviours in source resolution, eight frames each on the 12 Hz
    // clock (a whole cycle for the walk): transparent sprite sheets that can
    // also be used outside Undershell.
    if (s.kind != PetKind::Sprout) {
      constexpr int kFrames = 8;
      PetWidget pet;
      WidgetConfig wc;
      wc.type = "pet";
      wc.options = toml::table{{"species", std::string(s.id)}};
      pet.configure(wc, testPalette());
      const PetMotion motion = petMotion(s.kind);
      Image sheet{S * kFrames, S * 6, std::vector<std::uint8_t>(S * kFrames * S * 6 * 4)};
      for (int pose = 0; pose < 6; ++pose) for (int frame = 0; frame < kFrames; ++frame) {
        const auto p = static_cast<PetPose>(pose);
        const double t = p == PetPose::Walk ? 2 * motion.step / motion.speed * frame / kFrames
                                            : p == PetPose::Dance ? frame / 8.0 : frame / 12.0 * 4;
        const auto pixels = petFrame(s.kind, pet.previewLook(p, t));
        for (int y = 0; y < S; ++y) std::copy_n(pixels.data()+y*S*4,S*4,
            sheet.rgba.data()+((pose*S+y)*sheet.w+frame*S)*4);
      }
      CHECK(writePng(sheet, (dir / (std::string(s.id) + "-sheet.png")).string(), false));
    }
  }
  CHECK(writePng(page, (dir / (chibi?"bestiary-chibi.png":"bestiary.png")).string(), false));
  text.releaseGl();
}
}  // namespace

int main(int argc, char** argv) {
  CHECK(petKind("") == PetKind::Sprout);
  CHECK(petKind("not-a-species") == PetKind::Sprout);
  std::set<std::string> ids;
  std::set<PetPixels> drawings;
  for (const auto& s : kPetSpecies) {
    CHECK(ids.emplace(s.id).second);
    CHECK(petKind(s.id) == s.kind);
    if (s.kind == PetKind::Sprout || isRigPet(s.kind)) continue;
    const auto idle = petPixels(s.kind, PetPose::Idle, false, false, false);
    CHECK(drawings.insert(idle).second); // each companion has its own artwork
    CHECK(idle != petPixels(s.kind, PetPose::Idle, false, true, false));
    CHECK(idle != petPixels(s.kind, PetPose::Sleep, false, false, false));
    CHECK(petPixels(s.kind, PetPose::Walk, false, false, false) != petPixels(s.kind, PetPose::Walk, true, false, false));
    CHECK(petPixels(s.kind, PetPose::Dance, false, false, false) != petPixels(s.kind, PetPose::Dance, true, false, false));
    for (int p = 0; p < 6; ++p) for (bool frame : {false,true}) {
      const auto pixels = petPixels(s.kind, static_cast<PetPose>(p), frame, false, false);
      const auto flipped = petPixels(s.kind, static_cast<PetPose>(p), frame, false, true);
      size_t opaque = 0;
      for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {
        const int offset = (y * S + x) * 4;
        CHECK(pixels[offset + 3] == 0 || pixels[offset + 3] == 255);
        opaque += pixels[offset + 3] == 255;
        if (x==0 || x==S-1 || y==0 || y==S-1) CHECK(pixels[offset+3] == 0); // no clipped features
        for (int c = 0; c < 4; ++c) CHECK(pixels[offset+c] == flipped[(y*S+S-1-x)*4+c]);
      }
      CHECK(opaque > S*S/8 && opaque < S*S*4/5);
    }
  }
  CHECK(ids.size() == kPetSpecies.size() && ids.size() == 65);
  // The animated frames: crisp (binary alpha), an exact mirror when they face
  // left, the feet on the bottom row (they touch what they stand on), nothing
  // cut at the top or the sides, a walk that alternates its two drawings (they
  // are no longer bent into in-betweens, 2026-11-09), and resting different
  // from standing.
  for (const auto& s : kPetSpecies) {
    if (s.kind == PetKind::Sprout || isRigPet(s.kind)) continue;
    PetWidget pet;
    WidgetConfig wc;
    wc.type = "pet";
    wc.options = toml::table{{"species", std::string(s.id)}};
    pet.configure(wc, testPalette());
    const PetMotion motion = petMotion(s.kind);
    const bool floats = motion.gait == PetGait::Serpent || motion.gait == PetGait::Hover;
    CHECK(motion.speed > 10 && motion.step > 4);
    std::set<PetPixels> walk;
    const double cycle = 2 * motion.step / motion.speed;
    for (int i = 0; i < 12; ++i) walk.insert(petFrame(s.kind, pet.previewLook(PetPose::Walk, cycle * i / 12)));
    CHECK(walk.size() >= 2);
    const PetPixels stand = petFrame(s.kind, pet.previewLook(PetPose::Idle, 0.3));
    for (int p = 0; p < 6; ++p) {
      PetLook look = pet.previewLook(static_cast<PetPose>(p), 0.55);
      const PetPixels frame = petFrame(s.kind, look);
      look.flip = !look.flip;
      const PetPixels mirror = petFrame(s.kind, look);
      if (p == static_cast<int>(PetPose::Sit) || p == static_cast<int>(PetPose::Sleep)) CHECK(frame != stand);
      bool feet = false;
      size_t opaque = 0;
      for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {
        const int o = (y * S + x) * 4;
        CHECK(frame[o + 3] == 0 || frame[o + 3] == 255);
        opaque += frame[o + 3] == 255;
        if (x == 0 || x == S - 1 || y == 0) CHECK(frame[o + 3] == 0);
        if (y == S - 1 && frame[o + 3]) feet = true;
        for (int c = 0; c < 4; ++c) CHECK(frame[o + c] == mirror[(y * S + S - 1 - x) * 4 + c]);
      }
      CHECK(opaque > S * S / 8);
      if (!floats || p == static_cast<int>(PetPose::Sit) || p == static_cast<int>(PetPose::Sleep)) CHECK(feet);
    }
  }
  // The rigged companions: crisp, mirrored exactly, inside their grid, standing on its
  // bottom row; a walk of many different frames; every behaviour, act and colour variant
  // its own picture.
  constexpr int R = kRigPixels;
  auto checkFrame = [&](const RigPixels& f, bool feetDown, size_t& opaque) {
    bool feet = false;
    opaque = 0;
    for (int y = 0; y < R; ++y)
      for (int x = 0; x < R; ++x) {
        const std::uint8_t a = f[(y * R + x) * 4 + 3];
        CHECK(a == 0 || a == 255);
        opaque += a == 255;
        if (x == 0 || x == R - 1 || y == 0) CHECK(a == 0);
        if (y == R - 1 && a) feet = true;
      }
    if (feetDown) CHECK(feet);
  };
  size_t rigKinds = 0;
  for (const auto& s : kPetSpecies) {
    const RigKind* rk = rigKind(s.kind);
    if (!rk) continue;
    ++rigKinds;
    CHECK(rk->presets.size() >= 4 && rk->acts.size() >= 4);
    PetWidget pet;
    WidgetConfig wc;
    wc.type = "pet";
    wc.options = toml::table{{"species", std::string(s.id)}};
    pet.configure(wc, testPalette());
    const bool floats = rk->spec.floats;
    std::set<RigPixels> walk;
    const double cycle = rigCycle(rk->spec, 0) / rk->spec.walk;
    for (int i = 0; i < 12; ++i) walk.insert(pet.previewRig(PetPose::Walk, cycle * i / 12));
    CHECK(walk.size() >= 4);
    const RigPixels stand = pet.previewRig(PetPose::Idle, 0.3);
    for (int p = 0; p < 6; ++p) {
      const auto mode = static_cast<PetPose>(p);
      const RigPixels frame = pet.previewRig(mode, 0.55);
      size_t opaque = 0;
      checkFrame(frame, !floats && mode != PetPose::Jump && !(rk->spec.hops && mode == PetPose::Walk), opaque);
      CHECK(opaque > R * R / 30);
      if (mode != PetPose::Idle) CHECK(frame != stand);
    }
    // facing left, its shape is the exact mirror of facing right
    PetLife life;
    life.reset(rk, 3);
    LifeIn in;
    in.mode = PetPose::Walk, in.pace = 1;
    for (int i = 0; i < 40; ++i) in.dt = 1 / 60.0, in.time = i / 60.0, in.phase = i / 80.0, life.step(in);
    RigPixels right, left;
    life.draw(rk->presets[0].palette, false, in, RigMark::None, 0, right);
    life.draw(rk->presets[0].palette, true, in, RigMark::None, 0, left);
    for (int y = 0; y < R; ++y)
      for (int x = 0; x < R; ++x)
        CHECK(right[(y * R + x) * 4 + 3] == left[(y * R + R - 1 - x) * 4 + 3]);  // the light stays top-left: only the shape mirrors
    // each act, halfway through, stays inside the grid
    for (RigAct a : rk->acts) {
      if (a == RigAct::Pounce || a == RigAct::LieDown) continue;
      CHECK(PetLife::actLength(a) > 0);
      size_t opaque = 0;
      checkFrame(pet.previewRig(PetPose::Idle, PetLife::actLength(a) * 0.5, a), false, opaque);
      CHECK(opaque > R * R / 30);
    }
    // the colour variants: each one paints it differently
    std::set<RigPixels> painted;
    for (const RigPreset& v : rk->presets) {
      CHECK(!v.id.empty() && !v.en.empty() && !v.es.empty());
      RigPixels px;
      life.draw(v.palette, false, in, RigMark::None, 0, px);
      painted.insert(px);
    }
    CHECK(painted.size() == rk->presets.size());
  }
  CHECK(rigKinds == 36);
  // colours from a picture: a lively body, a pale belly, the eyes in another hue
  {
    const RigPalette fallback;
    const RigPalette p = rigPaletteFrom({Color::fromHex("#1a2a3a"), Color::fromHex("#3f7fd6"), Color::fromHex("#e8a03a"), Color::fromHex("#f4f4f4")}, fallback);
    const Oklch body = toOklch(p.body), belly = toOklch(p.belly), eyes = toOklch(p.eyes);
    CHECK(body.C > 0.08F && body.L > 0.45F);
    CHECK(belly.L > 0.9F);
    CHECK(std::abs(std::fmod(eyes.h - body.h + 540.0F, 360.0F) - 180.0F) > 60);
    CHECK(rigPaletteFrom({}, fallback).body == fallback.body);
  }
  CHECK(!isChibiPet(PetKind::Sprout) && !isChibiPet(PetKind::MoonRangerClassic));
  for(int i=1;i<=kPetFamilySize;++i) {
    const auto& original=kPetSpecies[i];
    const auto& chibi=kPetSpecies[kPetFamilySize+i];
    const std::string base = std::string(original.id).substr(0, original.id.size() - std::string_view("_classic").size());
    CHECK(chibi.id==base+"_chibi");
    CHECK(isChibiPet(chibi.kind));
    const auto cfg=toml::parse(Config::defaultBlock("pet","tiny","",0,0,256,256,std::string(chibi.id)));
    CHECK(cfg["widget"][0]["species"].value_or(std::string())==chibi.id);
  }
  const auto block = toml::parse(Config::defaultBlock("pet", "jade", "", 0, 0, 96, 96, "long"));
  CHECK(block["widget"][0]["species"].value_or(std::string()) == "long");
  WidgetConfig config;
  config.type = "pet";
  CHECK(optionApplies(config, "color"));
  config.options.insert_or_assign("species", "qilin");
  CHECK(optionApplies(config, "species"));
  CHECK(!optionApplies(config, "color") && !optionApplies(config, "sprout"));
  CHECK(!optionApplies(config, "colors_from"));
  const auto& schema = schemaFor("pet");
  const auto selector = std::find_if(schema.begin(),schema.end(),[](const auto& p) { return p.key=="species"; });
  CHECK(selector != schema.end());
  if (selector != schema.end()) {
    CHECK(selector->options == petSpeciesIds() && selector->defText == "sprout");
    CHECK(selector->optionNames.size() == kPetSpecies.size());
    for (size_t i=0;i<selector->optionNames.size();++i) {
      CHECK(selector->optionNames[i].first == kPetSpecies[i].en);
      CHECK(selector->optionNames[i].second == kPetSpecies[i].es);
    }
  }
  CHECK(optionLabel("long",true) == "Larga"); // the dragon's name is local to the pet selector

  Headless gl;
  if (!gl.ok()) { std::fputs("no surfaceless EGL: pet render tests skipped\n",stderr); return TEST_RESULT(); }
  CHECK(imageDiff(render("unknown","idle"), render("sprout","idle")) == 0);
  // Reconfiguring a live widget changes the texture (including going back to
  // Sprout), without resetting the physical position or keeping stale pixels.
  PetWidget live;
  auto liveRender = [&](const std::string& id) {
    config.options = toml::table{{"species", id}, {"pet_preview", "idle"}};
    live.configure(config, testPalette());
    return renderDrawn(128,128,[&] { DrawContext ctx; ctx.w=128; ctx.h=128; live.draw(ctx); });
  };
  const Image dragon = liveRender("long"), mecha = liveRender("sentinel");
  CHECK(imageDiff(dragon,mecha) > 5);
  CHECK(imageDiff(dragon,liveRender("long_chibi")) > 5);
  CHECK(imageDiff(dragon,liveRender("long")) == 0);
  CHECK(imageDiff(liveRender("sprout"), render("sprout","idle")) == 0);
  for (const auto& s : kPetSpecies) {
    if (s.kind == PetKind::Sprout) continue;
    const std::string id(s.id);
    const bool rig = isRigPet(s.kind);
    const Image idle=render(id,"idle"), sleep=render(id,"sleep"), faded=render(id,"idle",128,0.5);
    CHECK(imageDiff(idle,sleep) > 1);
    int drawn = 0;
    for (size_t i=3;i<idle.rgba.size();i+=4) {
      // The palette is rendered with nearest filtering, including the border.
      CHECK(idle.rgba[i]==0 || idle.rgba[i]==255);
      if(idle.rgba[i]) { ++drawn; CHECK(faded.rgba[i]>=127 && faded.rgba[i]<=128); }
    }
    CHECK(drawn>idle.w*idle.h/(rig ? 40 : 8));
    const Image tiny=render(id,"jump",24);
    int tinyDrawn=0;
    for (size_t i=3;i<tiny.rgba.size();i+=4) tinyDrawn += tiny.rgba[i]>200;
    CHECK(tinyDrawn>(rig ? 8 : 40)); // tiny boxes fit a visible character, not a cropped corner
  }
  // Touching it: a drag carries it (the surface follows), letting go drops it back on its
  // ground; words wait their turn in a bubble and go; an unknown verb is refused.
  {
    PetWidget pet;
    WidgetConfig wc;
    wc.type = "pet";
    wc.options = toml::table{{"species", "fox"}, {"notify", "off"}, {"battery", false}};
    pet.configure(wc, testPalette());
    TickContext tc;
    tc.box = {300, 200, 288, 288};
    tc.outputW = 1366, tc.outputH = 768, tc.dt = 1 / 30.0;
    for (int i = 0; i < 10; ++i) pet.tick(tc);
    renderDrawn(288, 288, [&] {
      DrawContext ctx;
      ctx.w = 288, ctx.h = 288;
      pet.draw(ctx);
    });
    const auto rects = pet.inputRects();
    CHECK(rects.size() == 1 && rects[0].w > 40 && rects[0].h > 30);
    if (rects.size() == 1) {
      const float cx = rects[0].x + rects[0].w / 2, cy = rects[0].y + rects[0].h / 2;
      const double x0 = pet.x();
      pet.onPointer({PointerEvent::Press, cx, cy, 0x110});
      pet.onPointer({PointerEvent::Motion, cx + 60, cy - 80, 0});
      CHECK(pet.carried() && pet.x() > x0 + 50 && pet.y() < 488 - 50);
      pet.onPointer({PointerEvent::Release, cx + 60, cy - 80, 0x110});
      CHECK(!pet.carried());
      for (int i = 0; i < 60; ++i) pet.tick(tc);
      CHECK(std::abs(pet.y() - 488) < 1);
    }
    CHECK(pet.command("say", "Hola") && pet.command("say", "otra vez"));
    CHECK(pet.bubble() == "Hola");
    for (int i = 0; i < 30 * 6; ++i) pet.tick(tc);
    CHECK(pet.bubble() == "otra vez");
    for (int i = 0; i < 30 * 6; ++i) pet.tick(tc);
    CHECK(pet.bubble().empty());
    CHECK(pet.command("react", "happy") && !pet.command("tango", ""));
  }
  // Painted companions: a frame atlas (made here in memory: each cell a block of
  // its own colour on the ground row) draws a rigged species instead of its rig.
  // The frame is the cell as painted, its ground on the last row, mirrored
  // exactly; the walk steps through its six cells by the ground covered; being
  // carried, jumping, sleeping and petting each show their own cell.
  {
    static FrameAtlas atlas;
    const int row = 6 * kFrameCell * 4;
    atlas.rgba.assign(static_cast<size_t>(row) * 4 * kFrameCell, 0);
    for (int c = 0; c < kFrameCount; ++c)
      for (int y = kFrameGround - 40; y <= kFrameGround; ++y)
        for (int x = 40 + (c % 5); x < 90; ++x) {
          std::uint8_t* q = &atlas.rgba[static_cast<size_t>((c / 6) * kFrameCell + y) * row + ((c % 6) * kFrameCell + x) * 4];
          q[0] = static_cast<std::uint8_t>(10 * c), q[1] = 200, q[2] = static_cast<std::uint8_t>(x == 89 ? 0 : 90), q[3] = 255;
        }
    atlas.idleLeft = 40, atlas.idleRight = 89, atlas.idleTop = kFrameGround - 40;
    frameAtlasOverride("fox", FrameStyle::Pixel, &atlas);
    CHECK(frameAtlasOrOther("fox", FrameStyle::Pixel) == &atlas);
    // a species painted in one style only lends that style to the other (an id
    // of its own, so it holds whatever real atlases are embedded)
    frameAtlasOverride("test-one-style", FrameStyle::Pixel, &atlas);
    CHECK(frameAtlasOrOther("test-one-style", FrameStyle::Chibi) == &atlas);
    frameAtlasOverride("test-one-style", FrameStyle::Pixel, nullptr);
    CHECK(frameAtlasOrOther("test-one-style", FrameStyle::Chibi) == nullptr);
    CHECK(std::find(framedSpecies(FrameStyle::Pixel).begin(), framedSpecies(FrameStyle::Pixel).end(), "fox") != framedSpecies(FrameStyle::Pixel).end());
    PetWidget painted;
    WidgetConfig wc;
    wc.type = "pet";
    wc.options = toml::table{{"species", "fox"}, {"art", "pixel"}};
    painted.configure(wc, testPalette());
    CHECK(painted.painted());
    PetPixels f, m;
    frameCompose(atlas, fcell::Sit, false, f);
    frameCompose(atlas, fcell::Sit, true, m);
    bool ground = false;
    for (int y = 0; y < kPetPixels; ++y)
      for (int x = 0; x < kPetPixels; ++x) {
        const int o = (y * kPetPixels + x) * 4;
        if (y == kPetPixels - 1 && f[o + 3]) ground = true;
        if (f[o + 3]) CHECK(f[o] == 10 * fcell::Sit);
        for (int c = 0; c < 4; ++c) CHECK(f[o + c] == m[(y * kPetPixels + kPetPixels - 1 - x) * 4 + c]);
      }
    CHECK(ground);
    LifeIn in;
    in.mode = PetPose::Walk;
    std::set<int> walk;
    for (int i = 0; i < 12; ++i) {
      in.phase = 3 + i / 12.0;
      walk.insert(frameCell(in, 0, RigPlan::Quad));
    }
    CHECK(walk.size() == 6 && *walk.begin() == fcell::Walk && *walk.rbegin() == fcell::Walk + 5);
    LifeIn held = in;
    held.carried = true;
    CHECK(frameCell(held, 0, RigPlan::Quad) == fcell::Held);
    LifeIn up = in;
    up.grounded = false, up.vy = -50;
    CHECK(frameCell(up, 0, RigPlan::Quad) == fcell::Rise);
    up.vy = 50;
    CHECK(frameCell(up, 0, RigPlan::Quad) == fcell::Fall);
    LifeIn pat = in;
    pat.mode = PetPose::Idle, pat.petted = 0.3;
    CHECK(frameCell(pat, 0, RigPlan::Quad) == fcell::Happy);
    LifeIn sleep = in;
    sleep.mode = PetPose::Sleep;
    CHECK(frameCell(sleep, 0, RigPlan::Quad) == fcell::Sleep || frameCell(sleep, 0, RigPlan::Quad) == fcell::SleepBreathe);
    LifeIn act = in;
    act.mode = PetPose::Idle, act.act = RigAct::Groom, act.actDur = 2;
    std::set<int> acts;
    for (double t : {0.1, 1.0, 1.9}) act.actAge = t, acts.insert(frameCell(act, 0, RigPlan::Quad));
    CHECK(acts.size() == 3 && *acts.begin() == fcell::Act);
    CHECK(frameCycle(atlas, RigPlan::Quad) > 20);
    PetPixels shown;
    CHECK(painted.previewFrames(PetPose::Sleep, 0.5, shown));
    frameAtlasOverride("fox", FrameStyle::Pixel, nullptr);
  }
  // A fractional desktop scale still maps each source cell to whole physical
  // pixels. Exercise a non-square surface and check for filtering and cropping.
  config.options=toml::table{{"species","star_mage_classic"},{"pet_preview","idle"}};
  live.configure(config,testPalette());
  const Image scaled=renderDrawn(126,105,[&] {
    DrawContext ctx; ctx.w=100.8F; ctx.h=84; ctx.scale=1.25F; live.draw(ctx);
  });
  int scaledDrawn=0;
  for(int y=0;y<scaled.h;++y) for(int x=0;x<scaled.w;++x) {
    const auto alpha=scaled.rgba[(y*scaled.w+x)*4+3];
    CHECK(alpha==0 || alpha==255);
    scaledDrawn += alpha==255;
    if(x==0 || y==0 || x==scaled.w-1) CHECK(alpha==0);  // the bottom row is where the feet (or a shadow) are
  }
  CHECK(scaledDrawn>880);
  if (argc == 3 && std::string_view(argv[1]) == "--gallery") {
    gallery(argv[2],false);
    gallery(argv[2],true);
  }
  return TEST_RESULT();
}
