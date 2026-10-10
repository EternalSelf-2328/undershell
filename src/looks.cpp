// SPDX-License-Identifier: GPL-3.0-or-later
#include "looks.hpp"

namespace undershell {

std::unique_ptr<StrokeLook> makeSpeedLines();
std::unique_ptr<StrokeLook> makeRain();

namespace {
struct Entry {
  const char* style;
  std::unique_ptr<StrokeLook> (*make)();
};
const Entry kLooks[] = {
    {"speedlines", makeSpeedLines},
    {"rain", makeRain},
};
}  // namespace

std::unique_ptr<StrokeLook> makeLook(const std::string& style) {
  for (const Entry& e : kLooks)
    if (style == e.style) return e.make();
  return nullptr;
}

bool isStrokeLook(const std::string& style) {
  for (const Entry& e : kLooks)
    if (style == e.style) return true;
  return false;
}

void setColours(StrokeRenderer::Look& look, Color core, Color halo) {
  look.core[0] = core.r, look.core[1] = core.g, look.core[2] = core.b;
  look.halo[0] = halo.r, look.halo[1] = halo.g, look.halo[2] = halo.b;
}

Color lighter(Color c, float towardWhite) { return c.mix(Color{1, 1, 1, 1}, towardWhite); }

}  // namespace undershell
