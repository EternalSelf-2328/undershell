// SPDX-License-Identifier: GPL-3.0-or-later
// Original companion designs; their pixel atlases (art/pets) are embedded at build time.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace undershell {

enum class PetKind { Sprout, LongClassic, QilinClassic, FenghuangClassic, CerberusClassic, PegasusClassic, GriffinClassic,
                     KitsuneClassic, FenrirClassic, WyvernClassic, UnicornClassic, SentinelClassic, RoverClassic, StarMageClassic,
                     MoonRangerClassic,
                     LongChibi, QilinChibi, FenghuangChibi, CerberusChibi, PegasusChibi, GriffinChibi,
                     KitsuneChibi, FenrirChibi, WyvernChibi, UnicornChibi, SentinelChibi, RoverChibi,
                     StarMageChibi, MoonRangerChibi,
                     // built on the rig (petrig.hpp, petlife.cpp, petkinds.cpp): jointed, recolourable
                     Long, Qilin, Fenghuang, Cerberus, Pegasus, Griffin, Kitsune, Fenrir, Wyvern, Unicorn,
                     Sentinel, Rover, StarMage, MoonRanger,
                     Cat, Shiba, Corgi, Bunny, Hamster,
                     Fox, Wolf, Deer, Raccoon, RedPanda, Bear,
                     Dragon,
                     Owl, Penguin, Duckling, Slime, Ghost,
                     Skeleton, Wukong, Cultivator, Ninja, Witch };
enum class PetPose { Idle, Sit, Walk, Jump, Dance, Sleep };
inline constexpr int kPetFamilySize = static_cast<int>(PetKind::MoonRangerClassic);
inline constexpr bool isChibiPet(PetKind kind) {
  return kind >= PetKind::LongChibi && kind <= PetKind::MoonRangerChibi;
}

struct PetSpecies {
  PetKind kind;
  std::string_view id, en, es;
};
inline constexpr std::array kPetSpecies{
    PetSpecies{PetKind::Sprout, "sprout", "Sprout", "Brotito"},
    PetSpecies{PetKind::LongClassic, "long_classic", "Jade dragon (China) · classic", "Dragón de jade (China) · clásico"},
    PetSpecies{PetKind::QilinClassic, "qilin_classic", "Qilin (China) · classic", "Qilin (China) · clásico"},
    PetSpecies{PetKind::FenghuangClassic, "fenghuang_classic", "Fenghuang (China) · classic", "Fenghuang (China) · clásico"},
    PetSpecies{PetKind::CerberusClassic, "cerberus_classic", "Cerberus (Greece) · classic", "Cerbero (Grecia) · clásico"},
    PetSpecies{PetKind::PegasusClassic, "pegasus_classic", "Pegasus (Greece) · classic", "Pegaso (Grecia) · clásico"},
    PetSpecies{PetKind::GriffinClassic, "griffin_classic", "Griffin (Greece) · classic", "Grifo (Grecia) · clásico"},
    PetSpecies{PetKind::KitsuneClassic, "kitsune_classic", "Kitsune (Japan) · classic", "Kitsune (Japón) · clásico"},
    PetSpecies{PetKind::FenrirClassic, "fenrir_classic", "Fenrir (Norse) · classic", "Fenrir (Nórdico) · clásico"},
    PetSpecies{PetKind::WyvernClassic, "wyvern_classic", "Ember wyvern · classic", "Guiverno de brasa · clásico"},
    PetSpecies{PetKind::UnicornClassic, "unicorn_classic", "Dusk unicorn · classic", "Unicornio del ocaso · clásico"},
    PetSpecies{PetKind::SentinelClassic, "sentinel_classic", "Bastion · mecha · classic", "Bastión · mecha · clásico"},
    PetSpecies{PetKind::RoverClassic, "rover_classic", "Copper · ronin mecha · classic", "Cobre · mecha ronin · clásico"},
    PetSpecies{PetKind::StarMageClassic, "star_mage_classic", "Luma · astral oracle · classic", "Luma · oráculo astral · clásico"},
    PetSpecies{PetKind::MoonRangerClassic, "moon_ranger_classic", "Nox · moon knight · classic", "Nox · caballero lunar · clásico"},
    PetSpecies{PetKind::LongChibi, "long_chibi", "Jade dragon · chibi", "Dragón de jade · chibi"},
    PetSpecies{PetKind::QilinChibi, "qilin_chibi", "Qilin · chibi", "Qilin · chibi"},
    PetSpecies{PetKind::FenghuangChibi, "fenghuang_chibi", "Fenghuang · chibi", "Fenghuang · chibi"},
    PetSpecies{PetKind::CerberusChibi, "cerberus_chibi", "Cerberus · chibi", "Cerbero · chibi"},
    PetSpecies{PetKind::PegasusChibi, "pegasus_chibi", "Pegasus · chibi", "Pegaso · chibi"},
    PetSpecies{PetKind::GriffinChibi, "griffin_chibi", "Griffin · chibi", "Grifo · chibi"},
    PetSpecies{PetKind::KitsuneChibi, "kitsune_chibi", "Kitsune · chibi", "Kitsune · chibi"},
    PetSpecies{PetKind::FenrirChibi, "fenrir_chibi", "Fenrir · chibi", "Fenrir · chibi"},
    PetSpecies{PetKind::WyvernChibi, "wyvern_chibi", "Ember wyvern · chibi", "Guiverno de brasa · chibi"},
    PetSpecies{PetKind::UnicornChibi, "unicorn_chibi", "Dusk unicorn · chibi", "Unicornio del ocaso · chibi"},
    PetSpecies{PetKind::SentinelChibi, "sentinel_chibi", "Bastion · chibi mecha", "Bastión · mecha chibi"},
    PetSpecies{PetKind::RoverChibi, "rover_chibi", "Copper · chibi mecha", "Cobre · mecha chibi"},
    PetSpecies{PetKind::StarMageChibi, "star_mage_chibi", "Luma · chibi", "Luma · chibi"},
    PetSpecies{PetKind::MoonRangerChibi, "moon_ranger_chibi", "Nox · chibi", "Nox · chibi"},
    PetSpecies{PetKind::Long, "long", "Jade dragon (China)", "Dragón de jade (China)"},
    PetSpecies{PetKind::Qilin, "qilin", "Qilin (China)", "Qilin (China)"},
    PetSpecies{PetKind::Fenghuang, "fenghuang", "Fenghuang (China)", "Fenghuang (China)"},
    PetSpecies{PetKind::Cerberus, "cerberus", "Cerberus (Greece)", "Cerbero (Grecia)"},
    PetSpecies{PetKind::Pegasus, "pegasus", "Pegasus (Greece)", "Pegaso (Grecia)"},
    PetSpecies{PetKind::Griffin, "griffin", "Griffin (Greece)", "Grifo (Grecia)"},
    PetSpecies{PetKind::Kitsune, "kitsune", "Kitsune (Japan)", "Kitsune (Japón)"},
    PetSpecies{PetKind::Fenrir, "fenrir", "Fenrir (Norse)", "Fenrir (Nórdico)"},
    PetSpecies{PetKind::Wyvern, "wyvern", "Ember wyvern", "Guiverno de brasa"},
    PetSpecies{PetKind::Unicorn, "unicorn", "Dusk unicorn", "Unicornio del ocaso"},
    PetSpecies{PetKind::Sentinel, "sentinel", "Bastion · mecha", "Bastión · mecha"},
    PetSpecies{PetKind::Rover, "rover", "Copper · ronin mecha", "Cobre · mecha ronin"},
    PetSpecies{PetKind::StarMage, "star_mage", "Luma · astral oracle", "Luma · oráculo astral"},
    PetSpecies{PetKind::MoonRanger, "moon_ranger", "Nox · moon knight", "Nox · caballero lunar"},
    PetSpecies{PetKind::Cat, "cat", "Cat", "Gato"},
    PetSpecies{PetKind::Shiba, "shiba", "Shiba dog", "Perro shiba"},
    PetSpecies{PetKind::Corgi, "corgi", "Corgi", "Corgi"},
    PetSpecies{PetKind::Bunny, "bunny", "Bunny", "Conejo"},
    PetSpecies{PetKind::Hamster, "hamster", "Hamster", "Hámster"},
    PetSpecies{PetKind::Fox, "fox", "Fox", "Zorro"},
    PetSpecies{PetKind::Wolf, "wolf", "Wolf", "Lobo"},
    PetSpecies{PetKind::Deer, "deer", "Deer", "Ciervo"},
    PetSpecies{PetKind::Raccoon, "raccoon", "Raccoon", "Mapache"},
    PetSpecies{PetKind::RedPanda, "red_panda", "Red panda", "Panda rojo"},
    PetSpecies{PetKind::Bear, "bear", "Bear cub", "Osezno"},
    PetSpecies{PetKind::Dragon, "dragon", "Little dragon", "Dragoncito"},
    PetSpecies{PetKind::Owl, "owl", "Owl", "Búho"},
    PetSpecies{PetKind::Penguin, "penguin", "Penguin", "Pingüino"},
    PetSpecies{PetKind::Duckling, "duckling", "Duckling", "Patito"},
    PetSpecies{PetKind::Slime, "slime", "Slime", "Slime"},
    PetSpecies{PetKind::Ghost, "ghost", "Ghost", "Fantasma"},
    PetSpecies{PetKind::Skeleton, "skeleton", "Skeleton", "Esqueleto"},
    PetSpecies{PetKind::Wukong, "wukong", "Sun Wukong · Monkey King", "Sun Wukong · Rey Mono"},
    PetSpecies{PetKind::Cultivator, "cultivator", "Sword cultivator", "Cultivador de la espada"},
    PetSpecies{PetKind::Ninja, "ninja", "Ninja", "Ninja"},
    PetSpecies{PetKind::Witch, "witch", "Little witch", "Brujita"},
};

inline PetKind petKind(std::string_view id) {
  for (const auto& s : kPetSpecies) if (s.id == id) return s.kind;
  return PetKind::Sprout;  // old configs and unknown/future names remain usable
}
inline std::vector<std::string> petSpeciesIds() {
  std::vector<std::string> ids;
  for (const auto& s : kPetSpecies) ids.emplace_back(s.id);
  return ids;
}

inline constexpr int kPetPixels = 128;
using PetPixels = std::array<std::uint8_t, kPetPixels * kPetPixels * 4>;
// Integer grid, binary alpha and transparent background. `frame` selects
// the alternate stride pose; blink and mirroring are applied after decoding.
PetPixels petPixels(PetKind kind, PetPose pose, bool frame, bool blink, bool flip);

// ── Motion (petmotion.cpp) ──────────────────────────────────────────────────
// Each companion has three drawings. Its movement is made on top of them, in
// whole sprite pixels: the figure bobs, squashes and leans about its feet,
// and soft regions of the drawing (heads, tails, manes, capes, wings, an
// orb) move on their own, so a tail trails the body and wings beat.
enum class PetDrawing { Stand, Stride, Rest };
// How a companion moves: hooves trot, paws pad, a fox springs, a bird thrusts
// its head, a serpent swims through the air, a mage hovers, a mecha stomps, a
// knight walks and a wyvern lumbers.
enum class PetGait { Hoof, Paw, Fox, Wolf, Bird, Serpent, Hover, Mech, Biped, Lumber };
struct PetMotion {
  PetGait gait = PetGait::Paw;
  bool wings = false;       // flaps up to windows and glides down
  float speed = 30;         // walking speed, sprite px per second
  float step = 12;          // ground covered by one step, sprite px (no foot sliding)
};
PetMotion petMotion(PetKind kind);

// One frame, everything in sprite pixels of the 128 grid. Positive lean
// moves the top of the figure forward; positive flap lowers the wing tips.
struct PetLook {
  PetDrawing drawing = PetDrawing::Stand;
  bool blink = false, flip = false;
  float lift = 0;             // the whole figure up (a hover, a jump)
  float bob = 0;              // the body up over planted feet (a walk's bob)
  float sy = 1, sx = 1;       // squash and stretch about the feet; sx also turns it
  float lean = 0;             // the top of the figure, forward
  float wave = 0, wavePhase = 0;  // the serpent's body: amplitude, phase (rad)
  float sway = 0, swayPhase = 0;  // tails, manes, capes, hair: tip amplitude, phase (rad)
  float flap = 0;             // wings: tip displacement
  float headX = 0, headY = 0;     // every head, forward and down
  float heads[3] = {0, 0, 0};     // and each head on its own (Cerberus): down
  float orbX = 0, orbY = 0;       // a held orb floats on its own
  float glow = 0;             // the orb lights up, 0..1
  float glint = -1;           // a light crossing the visor, 0..1 (-1: none)
  float shadow = 0;           // a dithered shadow under a floating figure, 0..1
  float sleepPhase = -1;      // z's rising (0..1 loops; -1: none)
  float notePhase = -1;       // notes rising while it dances
  float dust = -1;            // a puff at the feet, seconds since it landed or stomped
};
// The frame as an RGBA picture: binary alpha, the feet on the bottom row.
PetPixels petFrame(PetKind kind, const PetLook& look);
// A drawing as it is in the atlas (blink applied), for the frame composer.
const PetPixels& petDrawing(PetKind kind, PetDrawing drawing, bool blink);  // valid until the next call

}  // namespace undershell
