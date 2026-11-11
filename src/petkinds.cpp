// SPDX-License-Identifier: GPL-3.0-or-later
// The rigged companions (petlife.hpp): each one's build on the rig, the
// colours it comes in (the first is its natural colouring) and what it likes
// to do when it is left to itself. All original designs; the mythical ones
// follow the public-domain legends (Journey to the West, the Shan Hai Jing,
// the Greek and Norse myths), not any particular modern rendition.
#include "petlife.hpp"

namespace undershell {

namespace {
using A = RigAct;
using E = RigEar;
using T = RigTail;

Color c(const char* h) { return Color::fromHex(h); }
RigPreset v(std::string_view id, std::string_view en, std::string_view es, const char* body, const char* belly, const char* mark, const char* eyes,
            const char* extra) {
  return {id, en, es, {c(body), c(belly), c(mark), c(eyes), c(extra)}};
}

// a horse: long legs on hooves, a long neck and face, a mane and a long tail
RigSpec horse() {
  RigSpec s;
  s.shoulderH = 20, s.hipH = 20, s.spine = 17;
  s.chestRx = 7.5F, s.chestRy = 6.6F, s.rumpRx = 7, s.rumpRy = 6.5F, s.waist = 6;
  s.frontUp = 9.5F, s.frontLow = 10.5F, s.frontW = 2.2F, s.frontFootW = 1.5F;
  s.hindUp = 10, s.hindLow = 10.5F, s.hindW = 3, s.hindFootW = 1.5F;
  s.haunchRx = 5.5F, s.haunchRy = 7, s.paw = 1.6F, s.foot = RigFoot::Hoof;
  s.neckLen = 9.5F, s.neckW = 3.4F, s.neckAng = -1.2F;
  s.headR = 5.2F, s.headRy = 4.4F, s.snout = 7, s.snoutRx = 3.8F, s.snoutRy = 2.7F, s.snoutDrop = 2.4F;
  s.ear = E::Pointed, s.earLen = 4.2F, s.earW = 2.8F, s.earTilt = 0.1F;
  s.eyeX = 0.8F, s.eyeY = -0.8F;
  s.tail = T::Long, s.tailLen = 15, s.tailW = 2.6F, s.tailUp = -0.4F, s.tailCurl = -0.04F, s.tailSegs = 8;
  s.mane = true, s.bib = false;
  s.walk = 20, s.stride = 15, s.lift = 3.2F;
  return s;
}

RigSpec dogLike() {
  RigSpec s;
  s.cheeks = true, s.mask = true;
  s.snout = 4.8F, s.snoutRx = 3.6F, s.snoutRy = 2.7F;
  s.earLen = 5.6F, s.earW = 5;
  s.tail = T::Curl, s.tailLen = 12, s.tailW = 2.3F, s.tailFat = 3.8F, s.tailUp = 1.55F, s.tailCurl = 0.33F;
  return s;
}

std::vector<RigKind> build() {
  std::vector<RigKind> k;
  auto add = [&](PetKind kind, RigSpec s, std::vector<RigPreset> presets, std::vector<RigAct> acts) {
    k.push_back({kind, s, std::move(presets), std::move(acts)});
  };

  // ── Domestic ──
  {
    RigSpec s;
    s.shoulderH = 14.5F, s.hipH = 15, s.spine = 15;
    s.frontUp = 7, s.frontLow = 7.5F, s.hindUp = 7.5F, s.hindLow = 7.5F;
    s.chestRx = 6.8F, s.chestRy = 6, s.rumpRx = 6.4F, s.rumpRy = 6, s.waist = 5.2F;
    s.frontW = 2.1F, s.hindW = 2.8F;
    s.headR = 6.8F, s.headRy = 6.0F, s.snout = 2.0F, s.snoutRx = 2.6F, s.snoutRy = 2.0F, s.snoutDrop = 2.2F;
    s.earLen = 5.4F, s.earW = 5.4F, s.earTilt = -0.15F;
    s.tail = T::Thin, s.tailLen = 17, s.tailW = 1.7F, s.tailUp = 1.2F, s.tailCurl = 0.12F;
    s.stripes = true, s.eyeX = 2.0F, s.eyeY = -0.4F;
    s.walk = 17, s.stride = 10, s.lift = 2.2F;
    add(PetKind::Cat, s,
        {v("tabby", "Grey tabby", "Atigrado gris", "#9aa0a8", "#f4f1ec", "#555963", "#7fbf5a", "#f2c14e"),
         v("ginger", "Ginger", "Naranja", "#e8a05a", "#fff3e3", "#b8642f", "#d9a32b", "#f2c14e"),
         v("black", "Black", "Negro", "#33313b", "#4a4754", "#26242c", "#e9c53a", "#f2c14e"),
         v("white", "White", "Blanco", "#f3f1ee", "#ffffff", "#d4d0cc", "#4f8fd6", "#f2c14e"),
         v("siamese", "Siamese", "Siamés", "#efe3cf", "#fbf6ee", "#5b4337", "#5aa0e0", "#f2c14e"),
         v("lilac", "Lilac", "Lila", "#b9a7d6", "#f3eefb", "#8a76ad", "#f0b84a", "#f2c14e")},
        {A::LookAround, A::LookAtYou, A::HeadTilt, A::Groom, A::Stretch, A::Yawn, A::Scratch, A::LieDown, A::Pounce, A::Roll});
  }
  {
    RigSpec s = dogLike();
    s.walk = 19;
    add(PetKind::Shiba, s,
        {v("red", "Red", "Rojo", "#d98c4a", "#fbf0df", "#7a4128", "#3a2a22", "#e04a3a"),
         v("sesame", "Sesame", "Sésamo", "#c9925a", "#fbf0df", "#4a3529", "#3a2a22", "#3a7bd5"),
         v("black_tan", "Black and tan", "Negro y fuego", "#2f2a2e", "#f0e2cf", "#c98a4a", "#2a2224", "#e0b23a"),
         v("cream", "Cream", "Crema", "#efe0c4", "#fffaf0", "#cdb48c", "#3a2a22", "#5ab07a")},
        {A::LookAround, A::LookAtYou, A::HeadTilt, A::Wag, A::Shake, A::Scratch, A::Sniff, A::Stretch, A::Roll, A::Pant, A::LieDown});
  }
  {
    RigSpec s = dogLike();
    s.shoulderH = 10.5F, s.hipH = 10.5F, s.spine = 17;
    s.frontUp = 4.2F, s.frontLow = 4.6F, s.hindUp = 4.6F, s.hindLow = 4.6F, s.frontW = 2.5F, s.hindW = 3.1F;
    s.chestRx = 7.2F, s.chestRy = 6.2F, s.rumpRx = 6.8F, s.rumpRy = 6, s.waist = 5.8F, s.haunchRx = 4.6F, s.haunchRy = 5.2F;
    s.earLen = 7.2F, s.earW = 5.6F, s.earTilt = -0.05F;
    s.tail = T::Puff, s.tailFat = 2.6F, s.socks = true;
    s.walk = 17, s.stride = 9, s.lift = 1.8F;
    add(PetKind::Corgi, s,
        {v("red", "Red and white", "Rojo y blanco", "#e3984f", "#fff6ea", "#fff6ea", "#3a2a22", "#3a7bd5"),
         v("tricolor", "Tricolour", "Tricolor", "#3a3236", "#fff6ea", "#fff6ea", "#2a2224", "#d94a3a"),
         v("sable", "Sable", "Sable", "#b98a5a", "#fff6ea", "#fff6ea", "#3a2a22", "#5ab07a"),
         v("merle", "Blue merle", "Mirlo azul", "#9aa4b0", "#fff6ea", "#fff6ea", "#5a8fd0", "#e08a3a")},
        {A::LookAround, A::LookAtYou, A::HeadTilt, A::Wag, A::Shake, A::Sniff, A::Stretch, A::Roll, A::Pant, A::LieDown});
  }
  {
    RigSpec s;
    s.shoulderH = 9, s.hipH = 10.5F, s.spine = 11.5F;
    s.frontUp = 4.2F, s.frontLow = 4.6F, s.frontW = 2, s.hindUp = 6, s.hindLow = 6.2F, s.hindW = 3;
    s.chestRx = 6, s.chestRy = 5.6F, s.rumpRx = 7.2F, s.rumpRy = 7, s.waist = 5.6F, s.haunchRx = 5.6F, s.haunchRy = 6.2F;
    s.neckLen = 5, s.neckW = 3.6F, s.neckAng = -1.15F;
    s.headR = 6.3F, s.headRy = 5.7F, s.snout = 2.2F, s.snoutRx = 2.7F, s.snoutRy = 2.2F, s.snoutDrop = 1.8F;
    s.ear = E::Long, s.earLen = 10.5F, s.earW = 3.8F, s.earTilt = -0.2F;
    s.tail = T::Puff, s.tailFat = 2.7F, s.tailTip = true;
    s.bib = true, s.eyeX = 2.0F;
    s.walk = 16, s.stride = 13, s.lift = 3.6F, s.hops = true;
    add(PetKind::Bunny, s,
        {v("white", "White", "Blanco", "#f4f2ef", "#ffffff", "#d9cfd2", "#c2405a", "#f0a0b0"),
         v("brown", "Wild brown", "Pardo", "#a57a55", "#f4ead9", "#6d4a33", "#3a2a22", "#f0a0b0"),
         v("grey", "Grey", "Gris", "#9a9aa4", "#f0eff2", "#6a6a76", "#2a2a33", "#f0a0b0"),
         v("dutch", "Dutch", "Holandés", "#3a3438", "#ffffff", "#2a2428", "#3a6fd0", "#f0a0b0")},
        {A::LookAround, A::LookAtYou, A::Groom, A::Sniff, A::Hop, A::LieDown, A::Stretch, A::Yawn, A::HeadTilt});
  }
  {
    RigSpec s;
    s.shoulderH = 7.5F, s.hipH = 7.5F, s.spine = 7.5F;
    s.frontUp = 3, s.frontLow = 3.2F, s.frontW = 1.6F, s.hindUp = 3.2F, s.hindLow = 3.2F, s.hindW = 2, s.paw = 1.5F;
    s.chestRx = 7.6F, s.chestRy = 7, s.rumpRx = 8.2F, s.rumpRy = 7.6F, s.waist = 7.2F, s.haunchRx = 3.4F, s.haunchRy = 3.6F;
    s.neckLen = 3.5F, s.neckW = 5, s.neckAng = -0.9F;
    s.headR = 6.8F, s.headRy = 6.2F, s.snout = 2, s.snoutRx = 2.6F, s.snoutRy = 2.1F, s.snoutDrop = 2;
    s.ear = E::Round, s.earLen = 3.2F, s.earW = 3.2F, s.earTilt = -0.3F;
    s.tail = T::None, s.cheeks = true, s.eyeX = 2.2F;
    s.walk = 15, s.stride = 6, s.lift = 1.4F;
    add(PetKind::Hamster, s,
        {v("golden", "Golden", "Dorado", "#e6b06e", "#fff5e6", "#b7804a", "#2a2224", "#f0a0b0"),
         v("white", "White", "Blanco", "#f6f3ef", "#ffffff", "#ddd6cf", "#c2405a", "#f0a0b0"),
         v("grey", "Dove grey", "Gris paloma", "#a8a4ab", "#f3f1f4", "#77727c", "#2a2224", "#f0a0b0"),
         v("panda", "Panda", "Panda", "#f6f3ef", "#ffffff", "#2e2b30", "#2a2224", "#f0a0b0")},
        {A::LookAround, A::LookAtYou, A::Groom, A::Sniff, A::Roll, A::Yawn, A::LieDown, A::HeadTilt});
  }

  // ── Forest ──
  {
    RigSpec s;
    s.tail = T::Brush;
    s.tailTip = s.socks = s.earTips = s.cheeks = true;
    s.walk = 20, s.stride = 12;
    add(PetKind::Fox, s,
        {v("red", "Red", "Rojo", "#e9803c", "#fbf1e2", "#3d2b2e", "#6b4a2a", "#f2c14e"),
         v("arctic", "Arctic", "Ártico", "#eef1f4", "#ffffff", "#9aa3b5", "#3b6fa8", "#f2c14e"),
         v("silver", "Silver", "Plateado", "#55586a", "#d9dbe4", "#22232c", "#c79a3b", "#f2c14e"),
         v("fennec", "Fennec", "Fénec", "#efd2a2", "#fff8ea", "#b98a5a", "#4a3424", "#f2c14e"),
         v("spirit", "Spirit", "Espíritu", "#7fd6e8", "#f1fbff", "#3e6fb8", "#f5d76e", "#f2c14e")},
        {A::LookAround, A::LookAtYou, A::HeadTilt, A::Groom, A::Stretch, A::Yawn, A::Sniff, A::Pounce, A::Scratch, A::LieDown});
  }
  {
    RigSpec s;
    s.shoulderH = 18, s.hipH = 17.5F, s.spine = 18;
    s.frontUp = 8.5F, s.frontLow = 9.5F, s.hindUp = 9, s.hindLow = 9.5F;
    s.chestRx = 8, s.chestRy = 7, s.rumpRx = 7, s.rumpRy = 6.5F;
    s.neckLen = 7.5F, s.headR = 7, s.headRy = 6.2F, s.snout = 6.5F, s.snoutRx = 4.2F, s.snoutRy = 2.8F;
    s.earLen = 6, s.earW = 5;
    s.tail = T::Brush, s.tailLen = 15, s.tailUp = -0.15F, s.tailFat = 4.5F, s.tailTip = false;
    s.cheeks = s.mask = true;
    s.walk = 22, s.stride = 14;
    add(PetKind::Wolf, s,
        {v("grey", "Grey", "Gris", "#8d8f99", "#eceae6", "#4b4c56", "#d9a032", "#f2c14e"),
         v("arctic", "Arctic", "Ártico", "#eef0f2", "#ffffff", "#b5bcc8", "#5a9fe0", "#f2c14e"),
         v("black", "Black", "Negro", "#34323a", "#5b5862", "#1f1e24", "#e8c040", "#f2c14e"),
         v("timber", "Timber", "Pardo", "#a08268", "#efe4d6", "#5b4434", "#d9a032", "#f2c14e")},
        {A::Howl, A::LookAround, A::LookAtYou, A::Shake, A::Sniff, A::Stretch, A::Scratch, A::LieDown, A::Pant});
  }
  {
    RigSpec s = horse();
    s.shoulderH = 18, s.hipH = 18.5F, s.spine = 14.5F;
    s.chestRx = 6.3F, s.chestRy = 5.8F, s.rumpRx = 6.4F, s.rumpRy = 5.9F, s.waist = 5.1F;
    s.frontUp = 8.5F, s.frontLow = 9.5F, s.frontW = 1.8F, s.frontFootW = 1.3F, s.hindUp = 9, s.hindLow = 9.5F, s.hindW = 2.5F, s.hindFootW = 1.3F;
    s.neckLen = 8, s.neckW = 2.9F;
    s.headR = 5.4F, s.headRy = 4.6F, s.snout = 4.8F, s.snoutRx = 3.0F, s.snoutRy = 2.3F, s.snoutDrop = 1.6F;
    s.earLen = 6, s.earW = 4.4F, s.earTilt = 0.55F;
    s.tail = T::Puff, s.tailFat = 2.0F, s.tailTip = true;
    s.mane = false, s.spots = true, s.bib = true;
    s.horn = RigHorn::Antlers;
    s.walk = 18, s.stride = 13;
    add(PetKind::Deer, s,
        {v("fawn", "Fawn", "Cervatillo", "#c98a55", "#f7ecdc", "#f7ecdc", "#2a2224", "#d8c3a0"),
         v("stag", "Red stag", "Ciervo rojo", "#a6683e", "#efe1cc", "#e8d8bf", "#2a2224", "#e6d2b0"),
         v("white", "White hart", "Ciervo blanco", "#f3f0ea", "#ffffff", "#ffffff", "#5a8fd0", "#f2d58a"),
         v("autumn", "Autumn", "Otoño", "#c0703a", "#f6dcb8", "#f2b65a", "#3a2a22", "#e8b04a")},
        {A::LookAround, A::LookAtYou, A::Sniff, A::HeadTilt, A::Stretch, A::LieDown, A::Scratch, A::Hop});
  }
  {
    RigSpec s;
    s.shoulderH = 12, s.hipH = 13, s.spine = 14;
    s.frontUp = 5.5F, s.frontLow = 6.5F, s.hindUp = 6, s.hindLow = 6.5F;
    s.chestRx = 7, s.chestRy = 6.5F, s.rumpRx = 7.5F, s.rumpRy = 7, s.waist = 6.2F;
    s.headR = 6.6F, s.headRy = 5.7F, s.snout = 4, s.snoutRx = 3.0F, s.snoutRy = 2.2F;
    s.ear = E::Round, s.earLen = 4, s.earW = 3.8F, s.earTips = true, s.tipsExtra = true;
    s.tail = T::Brush, s.tailLen = 14, s.tailFat = 4.4F, s.tailUp = 0.2F, s.ringTail = true;
    s.bandit = true, s.mask = true, s.socks = true;
    s.walk = 17, s.stride = 10;
    add(PetKind::Raccoon, s,
        {v("grey", "Grey", "Gris", "#8e8a8a", "#ece8e4", "#2f2c30", "#2a2224", "#f4f1ee"),
         v("brown", "Brown", "Pardo", "#9a7a5e", "#efe4d6", "#3a2c24", "#2a2224", "#f4ecdf"),
         v("silver", "Silver", "Plateado", "#c4c6cc", "#ffffff", "#454852", "#3a6fd0", "#ffffff"),
         v("midnight", "Midnight", "Medianoche", "#4e4f66", "#9c9db5", "#1f2030", "#f2c14e", "#d8dcf0")},
        {A::LookAround, A::LookAtYou, A::Groom, A::Sniff, A::Scratch, A::HeadTilt, A::LieDown, A::Roll});
  }
  {
    RigSpec s;
    s.shoulderH = 12.5F, s.hipH = 13.5F, s.spine = 14;
    s.frontUp = 5.8F, s.frontLow = 6.6F, s.hindUp = 6.4F, s.hindLow = 6.6F;
    s.chestRx = 7, s.chestRy = 6.4F, s.rumpRx = 7.2F, s.rumpRy = 6.8F, s.waist = 6;
    s.headR = 7, s.headRy = 6.2F, s.snout = 3.2F, s.snoutRx = 2.8F, s.snoutRy = 2.2F;
    s.ear = E::Round, s.earLen = 4.6F, s.earW = 4.2F, s.earTips = true, s.tipsExtra = true;
    s.tail = T::Brush, s.tailLen = 16, s.tailFat = 4.6F, s.tailUp = 0.25F, s.ringTail = true;
    s.muzzleExtra = true, s.socks = true, s.bib = false;
    s.walk = 16, s.stride = 10;
    add(PetKind::RedPanda, s,
        {v("rust", "Rust", "Óxido", "#c75a2a", "#3d2622", "#3d2622", "#2a2224", "#f5e9d8"),
         v("golden", "Golden", "Dorado", "#e09a3a", "#4a3020", "#4a3020", "#2a2224", "#fff3e0"),
         v("snow", "Snow", "Nieve", "#e9e3dc", "#7a6e68", "#7a6e68", "#3a6fd0", "#ffffff"),
         v("ember", "Ember", "Brasa", "#b8402e", "#2a1c22", "#2a1c22", "#f2a33a", "#ffd9a0")},
        {A::LookAround, A::LookAtYou, A::Groom, A::Sniff, A::HeadTilt, A::Stretch, A::Yawn, A::LieDown});
  }
  {
    RigSpec s;
    s.shoulderH = 13, s.hipH = 13.5F, s.spine = 13;
    s.frontUp = 5.5F, s.frontLow = 6, s.frontW = 3.2F, s.frontFootW = 2.6F, s.hindUp = 5.8F, s.hindLow = 6, s.hindW = 3.8F, s.hindFootW = 2.6F, s.paw = 2.4F;
    s.chestRx = 8.5F, s.chestRy = 7.5F, s.rumpRx = 8.5F, s.rumpRy = 7.6F, s.waist = 7, s.haunchRx = 5.4F, s.haunchRy = 6.6F;
    s.neckLen = 5.5F, s.neckW = 5, s.neckAng = -0.85F;
    s.headR = 7.5F, s.headRy = 6.8F, s.snout = 3.8F, s.snoutRx = 3.2F, s.snoutRy = 2.6F;
    s.ear = E::Round, s.earLen = 3.6F, s.earW = 3.6F;
    s.tail = T::Puff, s.tailFat = 1.8F, s.mask = true, s.bib = false;
    s.walk = 15, s.stride = 10, s.lift = 2.2F;
    add(PetKind::Bear, s,
        {v("brown", "Brown", "Pardo", "#8a5a3c", "#d8b48e", "#5a3a28", "#2a2224", "#f2c14e"),
         v("polar", "Polar", "Polar", "#f1efe9", "#ffffff", "#d6d0c6", "#2a2224", "#5ab0e0"),
         v("black", "Black", "Negro", "#2e2b30", "#c9a37a", "#1e1c20", "#2a2224", "#f2c14e"),
         v("honey", "Honey", "Miel", "#d39a4a", "#f6dcae", "#a06a2e", "#2a2224", "#e04a3a")},
        {A::LookAround, A::LookAtYou, A::Scratch, A::Sniff, A::Yawn, A::Stretch, A::Roll, A::LieDown, A::HeadTilt});
  }

  // ── Mythical ──
  {
    RigSpec s;
    s.shoulderH = 14, s.hipH = 15, s.spine = 16;
    s.frontUp = 6.5F, s.frontLow = 7, s.hindUp = 7, s.hindLow = 7.5F, s.frontW = 2.6F, s.hindW = 3.4F;
    s.chestRx = 7.6F, s.chestRy = 6.8F, s.rumpRx = 7.2F, s.rumpRy = 6.6F;
    s.headR = 6.6F, s.headRy = 5.6F, s.snout = 5.2F, s.snoutRx = 3.8F, s.snoutRy = 2.6F;
    s.ear = E::Fin, s.earLen = 4.5F, s.horn = RigHorn::Dragon;
    s.tail = T::Long, s.tailLen = 19, s.tailW = 3.2F, s.tailUp = 0.25F, s.tailCurl = -0.02F, s.tailSegs = 10;
    s.wings = RigWing::Membrane, s.wingLen = 15, s.flies = true, s.scales = true;
    s.walk = 17, s.stride = 11;
    add(PetKind::Dragon, s,
        {v("green", "Forest", "Bosque", "#5fae5a", "#f2e2a6", "#2f6b49", "#f2b33a", "#f4ead0"),
         v("red", "Scarlet", "Escarlata", "#d8463a", "#f6d58a", "#7a2230", "#f2d04a", "#f4ead0"),
         v("blue", "Sapphire", "Zafiro", "#3f7fd6", "#cfe6ff", "#22407a", "#f2d04a", "#eef2ff"),
         v("violet", "Amethyst", "Amatista", "#8a5ad0", "#f0d8ff", "#46307a", "#7af0c8", "#f4ead0"),
         v("onyx", "Onyx and gold", "Ónice y oro", "#2f2c36", "#e8c25a", "#16141c", "#ff5a3a", "#f2c14e")},
        {A::LookAround, A::LookAtYou, A::Flap, A::Stretch, A::Yawn, A::Shake, A::Sniff, A::LieDown, A::HeadTilt});
  }
  {
    RigSpec s;
    s.plan = RigPlan::Serpent;
    s.length = 60, s.girth = 3.8F;
    s.headR = 5.4F, s.headRy = 4.6F, s.snout = 6.5F, s.snoutRx = 4.2F, s.snoutRy = 2.5F, s.snoutDrop = 1.3F;
    s.ear = E::Fin, s.earLen = 3.6F, s.horn = RigHorn::Antlers, s.whiskers = true, s.mane = true, s.scales = true;
    s.eyeX = 1.2F, s.eyeY = -1.2F;
    s.floats = true, s.walk = 22, s.stride = 30;
    add(PetKind::Long, s,
        {v("jade", "Jade", "Jade", "#3fa77a", "#f3d77c", "#e9c05b", "#e8463a", "#f7e3a1"),
         v("imperial", "Imperial gold", "Oro imperial", "#e2a83a", "#fff0c0", "#d0402e", "#2a2224", "#fff3d0"),
         v("azure", "Azure", "Celeste", "#3f8fd6", "#e6f2ff", "#f2f6ff", "#f2c14e", "#ffffff"),
         v("crimson", "Crimson", "Carmesí", "#c7372f", "#f6d27a", "#f2c14e", "#f2c14e", "#fff0c0"),
         v("ink", "Ink", "Tinta", "#2c2a33", "#c9c3b4", "#e8e2d2", "#e8463a", "#f2ecdc")},
        {A::LookAround, A::LookAtYou, A::Coil, A::HeadTilt, A::Yawn, A::Shake});
  }
  {
    RigSpec s = horse();
    s.shoulderH = 18, s.hipH = 18.5F, s.spine = 15.5F;
    s.neckLen = 8, s.headR = 5.6F, s.headRy = 4.8F, s.snout = 5.5F, s.snoutRx = 3.4F;
    s.horn = RigHorn::Qilin, s.ear = E::Pointed, s.earLen = 3.8F;
    s.tail = T::Tuft, s.tailLen = 12, s.tailW = 1.8F, s.tailFat = 3.6F, s.tailUp = 0.3F;
    s.mane = true, s.flame = true, s.scales = true, s.whiskers = true;
    add(PetKind::Qilin, s,
        {v("gold", "Gold and jade", "Oro y jade", "#d9a640", "#f4e5b8", "#3fa77a", "#2a2224", "#c9e7ff"),
         v("azure", "Azure flame", "Llama celeste", "#5a8fd6", "#e6f0ff", "#7af0ff", "#f2c14e", "#ffffff"),
         v("crimson", "Crimson flame", "Llama carmesí", "#c8463a", "#f8e0b0", "#f2b33a", "#2a2224", "#fff0c0"),
         v("white", "White jade", "Jade blanco", "#f2efe6", "#ffffff", "#7ad0a8", "#3a7bd5", "#f2c14e")},
        {A::LookAround, A::LookAtYou, A::HeadTilt, A::Stretch, A::Shake, A::Sniff, A::LieDown, A::Hop});
  }
  {
    RigSpec s;
    s.plan = RigPlan::Bird;
    s.bodyRx = 8.5F, s.bodyRy = 7.2F, s.neckLen = 7, s.neckW = 2.6F, s.neckAng = -1.25F;
    s.headR = 4.4F, s.headRy = 4.0F, s.snout = 3, s.snoutDrop = 0.6F, s.ear = E::None, s.mane = true;
    s.hindUp = 6, s.hindLow = 6.5F, s.foot = RigFoot::Talon;
    s.tail = T::Plume, s.tailLen = 24, s.tailW = 1.3F, s.tailFat = 2.8F, s.tailSegs = 10, s.tailUp = -0.45F, s.tailCurl = 0.07F;
    s.wings = RigWing::Feather, s.wingLen = 15, s.flies = true;
    s.eyeX = 0.6F, s.eyeY = -0.6F;
    s.walk = 14, s.stride = 9, s.lift = 2.6F;
    add(PetKind::Fenghuang, s,
        {v("crimson", "Crimson and gold", "Carmesí y oro", "#e2533b", "#f8d27a", "#3d9fa8", "#2a2224", "#f6c445"),
         v("azure", "Azure", "Celeste", "#3f8fd6", "#cfe6ff", "#f2c14e", "#2a2224", "#ffffff"),
         v("violet", "Violet dusk", "Ocaso violeta", "#8a4ac8", "#f6c4e0", "#f2a33a", "#2a2224", "#ffe08a"),
         v("white", "White gold", "Blanco y oro", "#f5f2ec", "#ffffff", "#e9b84a", "#c2405a", "#f6c445")},
        {A::LookAround, A::LookAtYou, A::Preen, A::Flap, A::Peck, A::HeadTilt, A::Stretch});
  }
  {
    RigSpec s;
    s.shoulderH = 17, s.hipH = 16.5F, s.spine = 17;
    s.frontUp = 8, s.frontLow = 9, s.hindUp = 8.5F, s.hindLow = 9, s.frontW = 2.8F, s.hindW = 3.4F;
    s.chestRx = 8.6F, s.chestRy = 7.4F, s.rumpRx = 7, s.rumpRy = 6.6F;
    s.neckLen = 7.5F, s.headR = 6, s.headRy = 5.2F, s.snout = 5.5F, s.snoutRx = 3.8F, s.snoutRy = 2.7F;
    s.earLen = 5, s.earW = 4.2F, s.heads = 3;
    s.tail = T::Thin, s.tailLen = 14, s.tailW = 1.9F, s.tailUp = 0.1F;
    s.mane = true, s.mask = false, s.bib = false;
    s.walk = 19, s.stride = 13;
    add(PetKind::Cerberus, s,
        {v("obsidian", "Obsidian", "Obsidiana", "#3a3340", "#6a5560", "#d1452f", "#ff8a2a", "#d1a14a"),
         v("bronze", "Bronze", "Bronce", "#7a5a40", "#c9a07a", "#3a2622", "#ff5a3a", "#e0b24a"),
         v("ash", "Ash", "Ceniza", "#8e8a90", "#d8d4d6", "#3a3440", "#7af0ff", "#c9ccd6"),
         v("hellfire", "Hellfire", "Fuego infernal", "#5a1f24", "#d86a3a", "#f2b33a", "#ffe06a", "#f2c14e")},
        {A::Pant, A::Shake, A::LookAround, A::LookAtYou, A::Howl, A::Sniff, A::Scratch, A::LieDown});
  }
  {
    RigSpec s = horse();
    s.wings = RigWing::Feather, s.wingLen = 16, s.flies = true;
    add(PetKind::Pegasus, s,
        {v("white", "Cloud", "Nube", "#f4f3f6", "#ffffff", "#b9c9ef", "#3a6fd0", "#c6b48a"),
         v("palomino", "Palomino", "Palomino", "#e6c27a", "#fff3d6", "#fff8ea", "#3a2a22", "#8a6a4a"),
         v("night", "Night", "Noche", "#2e2c3a", "#5a5870", "#8fa8ff", "#8fd8ff", "#c6b48a"),
         v("rose", "Rose", "Rosa", "#f4d6e4", "#fff4fa", "#e07aa8", "#7a4ab0", "#f2c14e")},
        {A::LookAround, A::LookAtYou, A::HeadTilt, A::Flap, A::Shake, A::Sniff, A::Stretch, A::LieDown});
  }
  {
    RigSpec s;
    s.shoulderH = 16, s.hipH = 15.5F, s.spine = 16;
    s.chestRx = 8, s.chestRy = 7, s.rumpRx = 6.8F, s.rumpRy = 6.4F;
    s.headR = 5.8F, s.headRy = 5.4F, s.snout = 2.5F, s.beak = true, s.headBelly = true, s.frontTalons = true;
    s.ear = E::Tuft, s.earLen = 3.6F, s.earW = 3.2F, s.earTilt = -0.6F;
    s.tail = T::Tuft, s.tailLen = 15, s.tailW = 1.7F, s.tailFat = 2.8F, s.tailUp = 0.1F;
    s.wings = RigWing::Feather, s.wingLen = 16, s.flies = true, s.bib = true;
    s.eyeX = 1.4F, s.eyeY = -1.0F;
    s.walk = 18, s.stride = 12;
    add(PetKind::Griffin, s,
        {v("gold", "Golden", "Dorado", "#c99a52", "#f6f1e6", "#5a3a28", "#f2b33a", "#f2c14e"),
         v("storm", "Storm", "Tormenta", "#7a8090", "#f0f2f6", "#2e3240", "#7af0ff", "#f2c14e"),
         v("royal", "Royal", "Real", "#5a4a8a", "#fff4e0", "#f2c14e", "#f2c14e", "#f2c14e"),
         v("sand", "Desert", "Desierto", "#e0c08a", "#ffffff", "#8a5a3a", "#3a2a22", "#e8a03a")},
        {A::LookAround, A::LookAtYou, A::HeadTilt, A::Preen, A::Flap, A::Stretch, A::Sniff, A::LieDown});
  }
  {
    RigSpec s;
    s.tail = T::Brush, s.tails = 3, s.tailSpread = 0.34F, s.tailLen = 16, s.tailFat = 4.6F;
    s.tailTip = s.tailTipMark = s.socks = s.earTips = s.cheeks = true;
    s.walk = 20, s.stride = 12;
    add(PetKind::Kitsune, s,
        {v("spirit", "White spirit", "Espíritu blanco", "#f5f1ee", "#ffffff", "#d8443a", "#d8443a", "#f2c14e"),
         v("flame", "Flame", "Llama", "#ef8a3a", "#fff1d8", "#7a2a2a", "#f2c14e", "#ffe08a"),
         v("moon", "Moon", "Luna", "#6a7ac8", "#eef0ff", "#2a2f5a", "#f2e08a", "#ffffff"),
         v("shadow", "Shadow", "Sombra", "#3a3440", "#8a7a90", "#c03a5a", "#f2c14e", "#f2c14e")},
        {A::LookAround, A::LookAtYou, A::HeadTilt, A::Groom, A::Pounce, A::Stretch, A::Yawn, A::Sniff, A::LieDown});
  }
  {
    RigSpec s;
    s.shoulderH = 19.5F, s.hipH = 19, s.spine = 19;
    s.frontUp = 9, s.frontLow = 10, s.hindUp = 9.5F, s.hindLow = 10, s.frontW = 2.9F, s.hindW = 3.6F;
    s.chestRx = 8.8F, s.chestRy = 7.8F, s.rumpRx = 7.4F, s.rumpRy = 6.8F;
    s.neckLen = 7.5F, s.headR = 7.2F, s.headRy = 6.2F, s.snout = 7, s.snoutRx = 4.4F, s.snoutRy = 2.9F;
    s.earLen = 6.4F, s.earW = 5;
    s.tail = T::Brush, s.tailLen = 16, s.tailUp = -0.1F, s.tailFat = 5;
    s.cheeks = s.mask = s.mane = true;
    s.walk = 23, s.stride = 15;
    add(PetKind::Fenrir, s,
        {v("storm", "Storm", "Tormenta", "#4a5263", "#c8cdd8", "#22262f", "#7af0ff", "#c9ccd6"),
         v("ash", "Ash", "Ceniza", "#8a8a90", "#e6e4e2", "#3a3a40", "#ffb03a", "#c9ccd6"),
         v("night", "Night", "Noche", "#1f1e26", "#4a4858", "#3a3850", "#ff3a3a", "#c9ccd6"),
         v("frost", "Frost", "Escarcha", "#dfe8f2", "#ffffff", "#7aa0c8", "#4fd0ff", "#ffffff")},
        {A::Howl, A::LookAround, A::LookAtYou, A::Shake, A::Sniff, A::Stretch, A::LieDown, A::Pant});
  }
  {
    RigSpec s;
    s.shoulderH = 15, s.hipH = 16.5F, s.spine = 15;
    s.frontUp = 6.8F, s.frontLow = 7.5F, s.hindUp = 8, s.hindLow = 8.5F, s.frontW = 2.2F, s.hindW = 3.4F;
    s.chestRx = 7, s.chestRy = 6.2F, s.rumpRx = 6.8F, s.rumpRy = 6.2F, s.waist = 5;
    s.neckLen = 8.5F, s.neckW = 3, s.neckAng = -1.1F;
    s.headR = 5.6F, s.headRy = 4.6F, s.snout = 6, s.snoutRx = 3.8F, s.snoutRy = 2.2F;
    s.ear = E::Fin, s.earLen = 3.6F, s.horn = RigHorn::Nubs;
    s.tail = T::Long, s.tailLen = 22, s.tailW = 2.8F, s.tailUp = 0.15F, s.tailSegs = 10;
    s.wings = RigWing::Membrane, s.wingLen = 19, s.flies = true, s.scales = true, s.bib = false;
    s.walk = 16, s.stride = 11;
    add(PetKind::Wyvern, s,
        {v("ember", "Ember", "Brasa", "#3b2a2e", "#f08a3c", "#c43a2a", "#ffcf4f", "#f2c14e"),
         v("ash", "Ash", "Ceniza", "#6a6670", "#e0d6c6", "#3a3640", "#ff6a3a", "#e8e2d2"),
         v("frost", "Frost", "Escarcha", "#bcd6ec", "#ffffff", "#5a8fc8", "#4fd0ff", "#ffffff"),
         v("venom", "Venom", "Veneno", "#3a5a2e", "#d8f07a", "#1e3a1e", "#f2e04a", "#e8f0c0")},
        {A::LookAround, A::LookAtYou, A::Flap, A::Stretch, A::Shake, A::Yawn, A::Sniff, A::LieDown});
  }
  {
    RigSpec s = horse();
    s.horn = RigHorn::Unicorn;
    add(PetKind::Unicorn, s,
        {v("dusk", "Dusk", "Ocaso", "#f1e8f7", "#ffffff", "#b98ad8", "#7a4ab0", "#f2c97a"),
         v("dawn", "Dawn", "Alba", "#fff2e6", "#ffffff", "#f2a0b8", "#3a7bd5", "#f6d06a"),
         v("midnight", "Midnight", "Medianoche", "#2c2a3e", "#4e4a6a", "#7af0ff", "#7af0ff", "#e8e2ff"),
         v("sea", "Sea foam", "Espuma", "#e6f6f4", "#ffffff", "#5ac8c0", "#2a7a9a", "#f2c14e")},
        {A::LookAround, A::LookAtYou, A::HeadTilt, A::Shake, A::Sniff, A::Stretch, A::LieDown, A::Hop});
  }

  // ── Birds and creatures ──
  {
    RigSpec s;
    s.plan = RigPlan::Bird;
    s.bodyRx = 9, s.bodyRy = 9.5F, s.neckLen = 0, s.headR = 6.6F, s.headRy = 6;
    s.mask = true, s.snout = 1.8F, s.snoutDrop = 1.4F;
    s.ear = E::Tuft, s.earLen = 3.2F, s.earW = 3, s.earTilt = -0.15F;
    s.hindUp = 2.6F, s.hindLow = 2.6F, s.foot = RigFoot::Talon;
    s.tail = T::Fan, s.tailLen = 5, s.tailW = 1.4F, s.tailFat = 2.6F, s.tailSegs = 3, s.tailUp = -0.4F;
    s.wings = RigWing::Feather, s.wingLen = 13, s.flies = true;
    s.eyeX = 1.6F, s.eyeY = -0.4F;
    s.walk = 11, s.stride = 6, s.lift = 2.2F, s.hops = true;
    add(PetKind::Owl, s,
        {v("barn", "Barn owl", "Lechuza", "#d9b07a", "#fff3e2", "#8a6040", "#2a2224", "#d8b080"),
         v("snowy", "Snowy", "Nival", "#f4f4f2", "#ffffff", "#5a5a64", "#f2c14e", "#8a8a90"),
         v("horned", "Great horned", "Cornudo", "#8a6a4a", "#d8c0a0", "#3a2a22", "#f2a33a", "#5a4a3a"),
         v("eagle", "Eagle owl", "Búho real", "#b07a3a", "#f0d8a8", "#4a3020", "#ff8a2a", "#5a4a3a")},
        {A::LookAround, A::LookAtYou, A::HeadTilt, A::Preen, A::Flap, A::Yawn, A::Hop});
  }
  {
    RigSpec s;
    s.plan = RigPlan::Bird;
    s.bodyRx = 8.6F, s.bodyRy = 12.2F, s.neckLen = 0, s.headR = 6.4F, s.headRy = 6.0F;
    s.snout = 2.6F, s.snoutDrop = 1.2F, s.ear = E::None;
    s.hindUp = 1.8F, s.hindLow = 1.8F, s.foot = RigFoot::Paw;
    s.tail = T::None, s.wings = RigWing::None;
    s.eyeX = 1.6F, s.eyeY = -0.6F;
    s.walk = 9, s.stride = 4, s.lift = 1.2F;
    add(PetKind::Penguin, s,
        {v("emperor", "Emperor", "Emperador", "#2c3140", "#fbfbfb", "#f2b33a", "#2a2224", "#f2a33a"),
         v("blue", "Little blue", "Azul pequeño", "#4a6aa8", "#f4f8ff", "#2a3a6a", "#2a2224", "#3a3440"),
         v("gentoo", "Gentoo", "Papúa", "#2a2c34", "#ffffff", "#ffffff", "#2a2224", "#ff6a2a"),
         v("rockhopper", "Rockhopper", "De penacho", "#3a3c46", "#ffffff", "#f2d04a", "#d83a3a", "#ff7a3a")},
        {A::LookAround, A::LookAtYou, A::HeadTilt, A::Flap, A::Preen, A::Hop, A::Yawn});
  }
  {
    RigSpec s;
    s.plan = RigPlan::Bird;
    s.bodyRx = 8.6F, s.bodyRy = 7.4F, s.neckLen = 4.2F, s.neckW = 3.8F, s.neckAng = -1.25F, s.headR = 5.8F, s.headRy = 5.3F;
    s.snout = 3.4F, s.snoutDrop = 1.0F, s.ear = E::None;
    s.hindUp = 2.6F, s.hindLow = 2.6F, s.foot = RigFoot::Paw;
    s.tail = T::Fan, s.tailLen = 3.5F, s.tailW = 1.2F, s.tailFat = 2, s.tailSegs = 2, s.tailUp = 0.6F;
    s.wings = RigWing::None;
    s.eyeX = 1.4F, s.eyeY = -0.7F;
    s.walk = 11, s.stride = 5, s.lift = 1.6F;
    add(PetKind::Duckling, s,
        {v("yellow", "Yellow", "Amarillo", "#f6d65a", "#fff3b0", "#e8b83a", "#2a2224", "#f28a2a"),
         v("mallard", "Mallard", "Silvestre", "#9a7a4a", "#f0dca0", "#3a2a1a", "#2a2224", "#3a3440"),
         v("white", "White", "Blanco", "#f6f4ee", "#ffffff", "#dcd6c8", "#2a2224", "#f28a2a"),
         v("rubber", "Rubber duck", "Patito de goma", "#f6d63a", "#ffe86a", "#f2b33a", "#2a2224", "#ff6a2a")},
        {A::LookAround, A::LookAtYou, A::HeadTilt, A::Preen, A::Peck, A::Hop, A::Flap});
  }
  {
    RigSpec s;
    s.plan = RigPlan::Blob;
    s.bodyRx = 10, s.bodyRy = 8.2F, s.ear = E::None, s.tail = T::None, s.bib = false;
    s.eyeY = 0.4F;
    s.walk = 14, s.stride = 12, s.lift = 3.5F, s.hops = true;
    add(PetKind::Slime, s,
        {v("green", "Green", "Verde", "#7ad86a", "#c8f8b0", "#3a9a4a", "#1e2a22", "#ffffff"),
         v("blue", "Blue", "Azul", "#5aa8f0", "#c8e8ff", "#2a5aa8", "#1e2230", "#ffffff"),
         v("pink", "Pink", "Rosa", "#f08ab8", "#ffd8ea", "#b84a7a", "#2a1e26", "#ffffff"),
         v("gold", "Gold", "Oro", "#f2c84a", "#fff0b0", "#c08a2a", "#2a2218", "#ffffff"),
         v("void", "Void", "Vacío", "#4a3a6a", "#8a7ab0", "#2a1e40", "#f2e04a", "#ffffff")},
        {A::LookAround, A::LookAtYou, A::Wobble, A::Hop, A::Yawn});
  }
  {
    RigSpec s;
    s.plan = RigPlan::Blob;
    s.bodyRx = 8.6F, s.bodyRy = 9.4F, s.ear = E::None, s.tail = T::None, s.bib = false, s.floats = true, s.eye = RigEye::Round;
    s.eyeY = -0.6F;
    s.walk = 13, s.stride = 20, s.lift = 1;
    add(PetKind::Ghost, s,
        {v("white", "Sheet", "Sábana", "#f2f2f6", "#ffffff", "#c8c8d8", "#1e1e2a", "#ffffff"),
         v("mint", "Mint", "Menta", "#b8f0dc", "#e8fff6", "#6ad0b0", "#1e2a26", "#ffffff"),
         v("lavender", "Lavender", "Lavanda", "#d6c8f6", "#f4f0ff", "#9a84d8", "#2a1e3a", "#ffffff"),
         v("pumpkin", "Pumpkin", "Calabaza", "#f2a24a", "#ffe0b0", "#c0602a", "#2a1a10", "#ffffff")},
        {A::LookAround, A::LookAtYou, A::Wobble, A::Wave, A::Yawn, A::Spin});
  }

  // ── People and spirits ──
  auto person = [] {
    RigSpec s;
    s.plan = RigPlan::Biped;
    s.legUp = 7, s.legLow = 7, s.armUp = 5.8F, s.armLow = 5.6F, s.torsoH = 12, s.torsoW = 7.2F, s.hipW = 6, s.limbW = 2.0F;
    s.headR = 9.4F, s.eye = RigEye::Anime, s.eyeX = 0.6F, s.eyeY = 0.6F, s.ear = E::None;
    s.walk = 19, s.stride = 10, s.lift = 2.4F;
    return s;
  };
  {
    RigSpec s = person();
    s.outfit = RigOutfit::Bones, s.hair = RigHair::None, s.eye = RigEye::Socket, s.limbW = 1;
    s.headR = 9.6F;
    add(PetKind::Skeleton, s,
        {v("bone", "Bone", "Hueso", "#ece6d6", "#ece6d6", "#3a3440", "#7ef0ff", "#c9a87a"),
         v("ember", "Ember", "Brasa", "#e8dccb", "#e8dccb", "#3a2a2a", "#ff6a2a", "#c9a87a"),
         v("cursed", "Cursed", "Maldito", "#cfd8c8", "#cfd8c8", "#2a3a2a", "#7aff7a", "#a8b89a"),
         v("royal", "Royal", "Real", "#f0ead8", "#f0ead8", "#5a3a8a", "#c88aff", "#f2c14e")},
        {A::LookAround, A::LookAtYou, A::Rattle, A::Wave, A::ScratchHead, A::Stretch, A::Bow});
  }
  {
    RigSpec s = person();
    s.outfit = RigOutfit::Fur, s.hair = RigHair::Circlet, s.prop = RigProp::Staff, s.monkeyTail = true, s.ear = E::Round, s.eye = RigEye::Round;
    s.headR = 9.0F, s.eyeX = 0.8F, s.eyeY = 0.3F;
    s.tailUp = 0;
    add(PetKind::Wukong, s,
        {v("classic", "Golden monkey", "Mono dorado", "#c8402f", "#f0c9a0", "#c08a40", "#f2c14e", "#f4c84a"),
         v("stone", "Stone monkey", "Mono de piedra", "#5a6a7a", "#d8c8b0", "#8a8478", "#f2c14e", "#e8c25a"),
         v("victor", "Victorious Buddha", "Buda victorioso", "#e8b83a", "#f6dcb8", "#d8a050", "#c8402f", "#fff0b0"),
         v("shadow", "Six-eared", "De seis orejas", "#2e2a36", "#c8a888", "#4a3a3a", "#ff4a3a", "#c9a14a")},
        {A::LookAround, A::LookAtYou, A::Spin, A::ScratchHead, A::Wave, A::Hop, A::Stretch, A::Bow, A::Meditate});
  }
  {
    RigSpec s = person();
    s.outfit = RigOutfit::Robe, s.robe = true, s.hair = RigHair::Bun, s.prop = RigProp::Sword;
    add(PetKind::Cultivator, s,
        {v("azure", "Azure cloud", "Nube celeste", "#e9eef5", "#f2d5bf", "#1e2230", "#3a3a52", "#7fc8e8"),
         v("crimson", "Crimson lotus", "Loto carmesí", "#b8343a", "#f2d5bf", "#1e1a22", "#3a2222", "#f2c14e"),
         v("jade", "Jade peak", "Pico de jade", "#3f8f6a", "#f2d5bf", "#2a2230", "#2a3a32", "#e8f0d0"),
         v("night", "Night sect", "Secta nocturna", "#2a2838", "#e8d0c0", "#d8d8e8", "#8a7aff", "#b88aff")},
        {A::LookAround, A::LookAtYou, A::SwordDance, A::Meditate, A::Bow, A::Wave, A::Stretch});
  }
  {
    RigSpec s = person();
    s.outfit = RigOutfit::Tunic, s.hair = RigHair::Hood, s.cape = true;
    add(PetKind::Ninja, s,
        {v("night", "Night", "Noche", "#2b2d3a", "#f0d0b8", "#3a3c4c", "#3a3a52", "#c43c3c"),
         v("crimson", "Crimson", "Carmesí", "#3a2a30", "#f0d0b8", "#b8343a", "#3a2222", "#e8e2d8"),
         v("snow", "Snow", "Nieve", "#e8ecf2", "#f0d0b8", "#c8d0dc", "#3a4a6a", "#5a8fd0"),
         v("leaf", "Leaf", "Hoja", "#3a5a3a", "#f0d0b8", "#2a3a2a", "#3a3a22", "#e8c040")},
        {A::LookAround, A::LookAtYou, A::Hop, A::Bow, A::Wave, A::Stretch, A::ScratchHead});
  }
  {
    RigSpec s = person();
    s.outfit = RigOutfit::Robe, s.robe = true, s.hair = RigHair::WizardHat, s.prop = RigProp::Staff;
    add(PetKind::Witch, s,
        {v("violet", "Violet", "Violeta", "#5b3f8f", "#f4d6c4", "#f08a3c", "#5ab07a", "#f2c14e"),
         v("midnight", "Midnight", "Medianoche", "#22223a", "#f0d0c0", "#e8e8f8", "#8a7aff", "#c0c8ff"),
         v("pumpkin", "Pumpkin", "Calabaza", "#e07a2a", "#f4d6c4", "#3a2a3a", "#7a3ab0", "#2a2a2a"),
         v("forest", "Forest", "Bosque", "#2f5a3a", "#f4d6c4", "#c8402f", "#d8a030", "#e8d8a0")},
        {A::LookAround, A::LookAtYou, A::Spin, A::Wave, A::Yawn, A::Bow, A::Stretch});
  }
  // the Codex companions, rebuilt
  {
    RigSpec s = person();
    s.outfit = RigOutfit::Armor, s.hair = RigHair::Helmet, s.eye = RigEye::Visor;
    s.legUp = 8.2F, s.legLow = 8.2F, s.armUp = 7, s.armLow = 6.8F, s.torsoH = 13.5F, s.torsoW = 9.4F, s.hipW = 7.8F, s.limbW = 2.7F, s.headR = 7;
    s.walk = 15, s.stride = 11, s.lift = 1.8F;
    add(PetKind::Sentinel, s,
        {v("steel", "Steel", "Acero", "#8794a8", "#8794a8", "#3b4252", "#4fe3ff", "#5c6678"),
         v("vanguard", "Vanguard red", "Vanguardia roja", "#c8463a", "#c8463a", "#3a2a30", "#ffd04a", "#6a5a60"),
         v("olive", "Olive", "Oliva", "#7a8a5a", "#7a8a5a", "#3a402a", "#ffb03a", "#5a5a4a"),
         v("white", "White knight", "Caballero blanco", "#eef0f4", "#eef0f4", "#3a4a6a", "#4fa8ff", "#a8b0c0")},
        {A::LookAround, A::LookAtYou, A::Scan, A::Stretch, A::Wave, A::Bow});
  }
  {
    RigSpec s = person();
    s.outfit = RigOutfit::Armor, s.hair = RigHair::Helmet, s.eye = RigEye::Visor, s.cape = true, s.prop = RigProp::Lance;
    s.legUp = 7.8F, s.legLow = 7.8F, s.armUp = 6.5F, s.armLow = 6.5F, s.torsoH = 12.6F, s.torsoW = 8, s.hipW = 6.8F, s.limbW = 2.3F, s.headR = 6.8F;
    s.walk = 17, s.stride = 11;
    add(PetKind::Rover, s,
        {v("copper", "Copper", "Cobre", "#c87a4a", "#c87a4a", "#3d2e36", "#ffcf4f", "#6b5a52"),
         v("ronin", "Ronin black", "Ronin negro", "#2e2c34", "#2e2c34", "#a8343a", "#ff4a3a", "#5a5660"),
         v("jade", "Jade", "Jade", "#3a8a6a", "#3a8a6a", "#e8e2d2", "#f2e04a", "#4a5a52"),
         v("sakura", "Sakura", "Sakura", "#f0c8d8", "#f0c8d8", "#5a3a5a", "#ff7ab0", "#8a7a8a")},
        {A::LookAround, A::LookAtYou, A::Scan, A::Spin, A::Bow, A::Stretch});
  }
  {
    RigSpec s = person();
    s.outfit = RigOutfit::Robe, s.robe = true, s.hair = RigHair::Long, s.prop = RigProp::Orb, s.floats = true;
    add(PetKind::StarMage, s,
        {v("astral", "Astral", "Astral", "#3b3f8f", "#f4dcc8", "#e8e4ff", "#8fd8ff", "#f6d36b"),
         v("solar", "Solar", "Solar", "#e8a03a", "#f4dcc8", "#fff4d0", "#ff6a3a", "#fff0a0"),
         v("nebula", "Nebula", "Nebulosa", "#7a3a8a", "#f4dcc8", "#ff9ad8", "#7af0ff", "#c8f0ff"),
         v("void", "Void", "Vacío", "#1e1c2a", "#e8d0c0", "#8a7aff", "#c88aff", "#e0d8ff")},
        {A::LookAround, A::LookAtYou, A::Spin, A::Meditate, A::Wave, A::Bow});
  }
  {
    RigSpec s = person();
    s.outfit = RigOutfit::Tunic, s.hair = RigHair::Short, s.cape = true, s.prop = RigProp::Lance;
    add(PetKind::MoonRanger, s,
        {v("night", "Night", "Noche", "#2c3350", "#f1d6c0", "#c9d4ff", "#9fb6ff", "#c0c7d6"),
         v("eclipse", "Eclipse", "Eclipse", "#2a2228", "#ead0bc", "#d8463a", "#ff8a4a", "#c9a14a"),
         v("silver", "Silver moon", "Luna de plata", "#c8ccd8", "#f1d6c0", "#4a5a8a", "#4a6aff", "#eef0f8"),
         v("harvest", "Harvest moon", "Luna de cosecha", "#7a3a2a", "#f1d6c0", "#f2b33a", "#f2c14e", "#e8d8b0")},
        {A::LookAround, A::LookAtYou, A::Spin, A::Bow, A::Wave, A::Stretch, A::ScratchHead});
  }
  return k;
}

// How much room a species takes over all it does (standing, walking, sitting, lying,
// asleep, dancing, leaping, carried, each of its acts): drawn small, measured, then
// scaled down only if it would not fit and centred on its own middle.
void fit(RigKind& k) {
  RigKind probe = k;
  probe.spec.fitScale = 0.5F, probe.spec.fitX = 32;
  const float kk = kRigUnit * 0.5F, ground = (kRigPixels - 1) / kRigUnit;
  float minX = 1e9F, maxX = -1e9F, minY = 1e9F;
  auto measure = [&](LifeIn in, double secs) {
    PetLife life;
    life.reset(&probe, 11);
    const int steps = static_cast<int>(secs * 60);
    for (int i = 0; i < steps; ++i) {
      in.dt = 1 / 60.0, in.time = i / 60.0;
      if (in.act != RigAct::None) in.actAge = std::max(0.0, in.time - 0.5);
      if (in.pace > 0) in.phase = in.time * probe.spec.walk / rigCycle(probe.spec, in.pace - 1);
      life.step(in);
      if (i % 15 != 14) continue;  // four looks a second are enough to find its reach
      RigPixels px;
      life.draw(probe.presets.front().palette, false, in, RigMark::None, 0, px);
      for (int y = 0; y < kRigPixels; ++y)
        for (int x = 0; x < kRigPixels; ++x)
          if (px[(y * kRigPixels + x) * 4 + 3]) {
            minX = std::min(minX, (x - kRigPixels * 0.5F) / kk + 32), maxX = std::max(maxX, (x + 1 - kRigPixels * 0.5F) / kk + 32);
            minY = std::min(minY, ground - ((kRigPixels - 1) - y) / kk);
          }
    }
  };
  for (PetPose m : {PetPose::Idle, PetPose::Sit, PetPose::Sleep, PetPose::Dance}) {
    LifeIn in;
    in.mode = m;
    measure(in, 1.5);
    if (m == PetPose::Sit) in.lie = true, measure(in, 1.5);
  }
  for (double pace : {1.0, 2.1}) {
    LifeIn in;
    in.mode = PetPose::Walk, in.pace = pace;
    measure(in, 1.5);
  }
  for (double vy : {-60.0, 60.0}) {
    LifeIn in;
    in.grounded = false, in.vy = vy, in.airTime = 0.3;
    measure(in, 1.0);
  }
  {
    LifeIn in;
    in.carried = true;
    measure(in, 1.0);
  }
  for (RigAct a : k.acts) {
    LifeIn in;
    in.act = a, in.actDur = PetLife::actLength(a);
    measure(in, 0.5 + in.actDur);
  }
  if (maxX < minX) return;
  // a pixel of margin each side; the top below the marks' room
  const float width = (maxX - minX) * kRigUnit + 2, height = (ground - minY) * kRigUnit;
  k.spec.fitScale = std::min({1.0F, (kRigPixels - 4) / width, (kRigPixels - 9) / std::max(1.0F, height)});
  k.spec.fitX = (minX + maxX) * 0.5F;
}
}  // namespace

const std::vector<RigPreset>& classicTints() {
  static const std::vector<RigPreset> tints = {
      v("original", "Original", "Original", "#000000", "#ffffff", "#000000", "#000000", "#000000"),
      v("rose", "Rose", "Rosa", "#e0607e", "#fff0f4", "#f2b33a", "#3a2a32", "#f2c14e"),
      v("jade", "Jade", "Jade", "#3fa77a", "#effaf2", "#e9c05b", "#2a3a32", "#f2c14e"),
      v("azure", "Azure", "Celeste", "#3f8fd6", "#eef6ff", "#f2c14e", "#2a2f3a", "#f2c14e"),
      v("violet", "Violet", "Violeta", "#8a5ad0", "#f6f0ff", "#5ad0c0", "#2a2238", "#f2c14e"),
      v("gold", "Gold", "Oro", "#e2b23a", "#fff8e0", "#c8402f", "#3a2a1a", "#f2c14e"),
      v("obsidian", "Obsidian", "Obsidiana", "#3a3640", "#8a8490", "#d8463a", "#ff8a2a", "#f2c14e"),
      v("frost", "Frost", "Escarcha", "#dfe8f2", "#ffffff", "#5a8fc8", "#2a3a5a", "#f2c14e"),
  };
  return tints;
}

const RigKind* rigKind(PetKind kind) {
  static std::vector<RigKind> all = build();
  static std::vector<bool> fitted(all.size(), false);
  for (size_t i = 0; i < all.size(); ++i)
    if (all[i].kind == kind) {
      if (!fitted[i]) fitted[i] = true, fit(all[i]);
      return &all[i];
    }
  return nullptr;
}

}  // namespace undershell
