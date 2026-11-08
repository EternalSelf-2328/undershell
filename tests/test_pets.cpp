// SPDX-License-Identifier: GPL-3.0-or-later
#include "check.hpp"
#include "i18n.hpp"
#include "offscreen.hpp"
#include "pet.hpp"
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
    // Six poses in source resolution, two frames each. These PNGs are also
    // usable as transparent sprite sheets outside Undershell.
    if (s.kind != PetKind::Sprout) {
      Image sheet{S * 2, S * 6, std::vector<std::uint8_t>(S * 2 * S * 6 * 4)};
      for (int pose = 0; pose < 6; ++pose) for (int frame = 0; frame < 2; ++frame) {
        const auto pixels = petPixels(s.kind, static_cast<PetPose>(pose), frame, false, false);
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
    if (s.kind == PetKind::Sprout) continue;
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
  CHECK(ids.size() == 29);
  CHECK(!isChibiPet(PetKind::Sprout) && !isChibiPet(PetKind::MoonRanger));
  for(int i=1;i<=kPetFamilySize;++i) {
    const auto& original=kPetSpecies[i];
    const auto& chibi=kPetSpecies[kPetFamilySize+i];
    CHECK(chibi.id==std::string(original.id)+"_chibi");
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
    const Image idle=render(id,"idle"), sleep=render(id,"sleep"), faded=render(id,"idle",128,0.5);
    CHECK(imageDiff(idle,sleep) > 1);
    int drawn = 0;
    for (size_t i=3;i<idle.rgba.size();i+=4) {
      // The palette is rendered with nearest filtering, including the border.
      CHECK(idle.rgba[i]==0 || idle.rgba[i]==255);
      if(idle.rgba[i]) { ++drawn; CHECK(faded.rgba[i]>=127 && faded.rgba[i]<=128); }
    }
    CHECK(drawn>idle.w*idle.h/8);
    const Image tiny=render(id,"jump",24);
    int tinyDrawn=0;
    for (size_t i=3;i<tiny.rgba.size();i+=4) tinyDrawn += tiny.rgba[i]>200;
    CHECK(tinyDrawn>40); // tiny boxes fit a visible character, not a cropped corner
  }
  // A fractional desktop scale still maps each source cell to whole physical
  // pixels. Exercise a non-square surface and check for filtering and cropping.
  config.options=toml::table{{"species","star_mage"},{"pet_preview","idle"}};
  live.configure(config,testPalette());
  const Image scaled=renderDrawn(126,105,[&] {
    DrawContext ctx; ctx.w=100.8F; ctx.h=84; ctx.scale=1.25F; live.draw(ctx);
  });
  int scaledDrawn=0;
  for(int y=0;y<scaled.h;++y) for(int x=0;x<scaled.w;++x) {
    const auto alpha=scaled.rgba[(y*scaled.w+x)*4+3];
    CHECK(alpha==0 || alpha==255);
    scaledDrawn += alpha==255;
    if(x==0 || y==0 || x==scaled.w-1 || y==scaled.h-1) CHECK(alpha==0);
  }
  CHECK(scaledDrawn>880);
  if (argc == 3 && std::string_view(argv[1]) == "--gallery") {
    gallery(argv[2],false);
    gallery(argv[2],true);
  }
  return TEST_RESULT();
}
