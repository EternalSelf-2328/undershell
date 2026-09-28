// SPDX-License-Identifier: GPL-3.0-or-later
// Editable clock structures: a face made of named elements (the time, the
// date, a greeting, a rule…), each with a font, size, weight, spacing, colour,
// case and visibility, in an order. Defaults are the face's own design, so an
// untouched structure looks exactly as drawn; changes live in the widget's
// config as flat keys: "<face>_<element>_<prop>" and "<face>_order".
#pragma once

#include <string>
#include <vector>

#include <toml++/toml.hpp>

namespace undershell {

struct ClockElementSpec {
  enum Kind { Text, Rule, Glyphs } kind = Text;  // Glyphs: the drawn weekday of the card
  const char* id;
  const char* labelEs;
  const char* labelEn;
  const char* family;  // design font
  float size;          // design size (Rule: length)
  int weight;
  float spacing;
  const char* color;   // ink | accent | dim | soft, or a palette role / #hex
  bool upper;          // design case
};

struct ClockStructure {
  const char* face;
  std::vector<ClockElementSpec> elements;
};

// the editable structure behind a face, or nullptr for the classic faces
const ClockStructure* clockStructure(const std::string& face);

// an element as configured
struct ClockElement {
  const ClockElementSpec* spec = nullptr;
  bool show = true;
  std::string family;
  float scale = 1;      // on the design size
  int weight = 400;
  float spacing = 0;
  std::string color;
  bool upper = false;
  float thickness = 2;  // Rule
};

ClockElement resolveElement(const toml::table& opts, const std::string& face, const ClockElementSpec& spec);
// the elements in the configured order (unknown ids dropped, missing appended)
std::vector<ClockElement> clockElements(const toml::table& opts, const ClockStructure& s);
// "<face>_<element>_<prop>"
std::string elementKey(const std::string& face, const std::string& element, const char* prop);

// font weights by name, for the editor
const std::vector<std::string>& weightNames();
int weightValue(const std::string& name);
std::string weightName(int weight);

}  // namespace undershell
