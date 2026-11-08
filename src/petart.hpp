// SPDX-License-Identifier: GPL-3.0-or-later
// Original companion designs; their pixel atlases (art/pets) are embedded at build time.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace undershell {

enum class PetKind { Sprout, Long, Qilin, Fenghuang, Cerberus, Pegasus, Griffin,
                     Kitsune, Fenrir, Wyvern, Unicorn, Sentinel, Rover, StarMage, MoonRanger,
                     LongChibi, QilinChibi, FenghuangChibi, CerberusChibi, PegasusChibi, GriffinChibi,
                     KitsuneChibi, FenrirChibi, WyvernChibi, UnicornChibi, SentinelChibi, RoverChibi,
                     StarMageChibi, MoonRangerChibi };
enum class PetPose { Idle, Sit, Walk, Jump, Dance, Sleep };
inline constexpr int kPetFamilySize = static_cast<int>(PetKind::MoonRanger);
inline constexpr bool isChibiPet(PetKind kind) {
  return kind >= PetKind::LongChibi && kind <= PetKind::MoonRangerChibi;
}

struct PetSpecies {
  PetKind kind;
  std::string_view id, en, es;
};
inline constexpr std::array kPetSpecies{
    PetSpecies{PetKind::Sprout, "sprout", "Sprout", "Brotito"},
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

}  // namespace undershell
