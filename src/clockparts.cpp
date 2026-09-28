// SPDX-License-Identifier: GPL-3.0-or-later
#include "clockparts.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace undershell {

namespace {
const char* INTER = "Inter Display";
const char* FONT = "Space Grotesk";
const char* MONO = "JetBrains Mono";
}

const ClockStructure* clockStructure(const std::string& face) {
  using K = ClockElementSpec::Kind;
  // the card (Ryoku's good-night face): rule, greeting, drawn weekday, date,
  // time, rule, stacked and centred
  static const ClockStructure card{"goodnight",
                                   {
                                       {K::Rule, "rule_top", "Línea superior", "Top rule", "", 70, 400, 0, "ink", false},
                                       {K::Text, "greeting", "Saludo", "Greeting", INTER, 26, 500, 10, "ink", true},
                                       {K::Glyphs, "day", "Día", "Weekday", INTER, 62, 700, 20, "ink", true},
                                       {K::Text, "date", "Fecha", "Date", INTER, 22, 600, 4, "ink", true},
                                       {K::Text, "time", "Hora", "Time", INTER, 22, 500, 3, "ink", false},
                                       {K::Rule, "rule_bottom", "Línea inferior", "Bottom rule", "", 70, 400, 0, "ink", false},
                                   }};
  // column: hours over minutes, seconds and AM/PM small underneath
  static const ClockStructure column{"column",
                                     {
                                         {K::Text, "hours", "Horas", "Hours", FONT, 150, 700, -2, "ink", false},
                                         {K::Text, "minutes", "Minutos", "Minutes", FONT, 150, 700, -2, "accent", false},
                                         {K::Text, "extra", "Segundos y AM/PM", "Seconds & AM/PM", FONT, 30, 600, 0, "dim", false},
                                     }};
  // flip: split-flap cards; the layout is fixed, the parts restyle
  static const ClockStructure flip{"flip",
                                   {
                                       {K::Box, "cards", "Tarjetas", "Cards", "", 104, 400, 0, "card", false},
                                       {K::Text, "digits", "Dígitos", "Digits", MONO, 67, 700, 0, "ink", false},
                                       {K::Text, "colon", "Separador", "Separator", MONO, 52, 700, 0, "accent", false},
                                   },
                                   false};
  // metal: a heavy time over one line of details
  static const ClockStructure metal{"metal",
                                    {
                                        {K::Text, "time", "Hora", "Time", INTER, 132, 900, -2, "ink", false},
                                        {K::Text, "ampm", "AM/PM", "AM/PM", INTER, 25, 700, 0.5F, "ink", false},
                                        {K::Text, "weekday", "Día", "Weekday", INTER, 25, 700, 0.5F, "ink", false},
                                        {K::Text, "date", "Fecha", "Date", INTER, 25, 700, 0.5F, "ink", false, false},
                                        {K::Text, "weather", "Clima", "Weather", INTER, 25, 700, 0.5F, "ink", false},
                                    }};
  // stacked: the Modern Clock plugin — a big weekday, the date, the time
  static const ClockStructure stacked{"stacked",
                                      {
                                          {K::Text, "day", "Día", "Weekday", FONT, 70, 700, 0, "secondary", true},
                                          {K::Text, "date", "Fecha", "Date", FONT, 20, 400, 0, "ink", false},
                                          {K::Text, "time", "Hora", "Time", FONT, 20, 200, 0, "accent", false},
                                      }};
  for (const ClockStructure* s : {&card, &column, &flip, &metal, &stacked})
    if (face == s->face) return s;
  return nullptr;
}

std::string elementKey(const std::string& face, const std::string& element, const char* prop) {
  return face + "_" + element + "_" + prop;
}

const std::vector<std::string>& weightNames() {
  static const std::vector<std::string> n = {"thin", "extralight", "light", "regular", "medium", "semibold", "bold", "extrabold", "black"};
  return n;
}

int weightValue(const std::string& name) {
  const auto& n = weightNames();
  const auto it = std::find(n.begin(), n.end(), name);
  return it == n.end() ? 400 : static_cast<int>(it - n.begin() + 1) * 100;
}

std::string weightName(int weight) {
  const int i = std::clamp(static_cast<int>(std::lround(weight / 100.0)) - 1, 0, 8);
  return weightNames()[static_cast<size_t>(i)];
}

ClockElement resolveElement(const toml::table& opts, const std::string& face, const ClockElementSpec& spec) {
  ClockElement e;
  e.spec = &spec;
  e.show = spec.shown;
  auto num = [&](const char* prop, double def) {
    const toml::node* n = opts.get(elementKey(face, spec.id, prop));
    if (!n) return def;
    if (auto v = n->value<double>()) return *v;
    return def;
  };
  auto str = [&](const char* prop, const std::string& def) {
    const toml::node* n = opts.get(elementKey(face, spec.id, prop));
    if (!n) return def;
    if (auto v = n->value<std::string>(); v && !v->empty()) return *v;
    return def;
  };
  if (const toml::node* n = opts.get(elementKey(face, spec.id, "show")))
    if (auto v = n->value<bool>()) e.show = *v;
  e.family = str("font", spec.family);
  e.scale = static_cast<float>(std::clamp(num("size", 1.0), 0.2, 5.0));
  const std::string w = str("weight", "");
  e.weight = w.empty() ? spec.weight : weightValue(w);
  e.spacing = static_cast<float>(num("spacing", spec.spacing));
  e.color = str("color", spec.color);
  const std::string c = str("case", "auto");
  e.upper = c == "upper" ? true : c == "normal" ? false : spec.upper;
  e.thickness = static_cast<float>(std::clamp(num("thickness", 2.0), 0.5, 20.0));
  return e;
}

std::vector<ClockElement> clockElements(const toml::table& opts, const ClockStructure& s) {
  std::vector<std::string> order;
  if (const toml::node* n = opts.get(std::string(s.face) + "_order"))
    if (auto v = n->value<std::string>()) {
      std::stringstream in(*v);
      for (std::string id; std::getline(in, id, ',');) {
        id.erase(0, id.find_first_not_of(' '));
        id.erase(id.find_last_not_of(' ') + 1);
        const bool known = std::any_of(s.elements.begin(), s.elements.end(), [&](const auto& e) { return id == e.id; });
        if (known && std::find(order.begin(), order.end(), id) == order.end()) order.push_back(id);
      }
    }
  for (const auto& e : s.elements)
    if (std::find(order.begin(), order.end(), e.id) == order.end()) order.push_back(e.id);
  std::vector<ClockElement> out;
  for (const auto& id : order)
    for (const auto& spec : s.elements)
      if (id == spec.id) out.push_back(resolveElement(opts, s.face, spec));
  return out;
}

}  // namespace undershell
