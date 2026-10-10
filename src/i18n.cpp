// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.hpp"

#include <cctype>
#include <cstdlib>
#include <map>
#include <string_view>

namespace undershell {

namespace {
std::string g_lang = "system";

bool localeSpanish(std::initializer_list<const char*> vars) {
  for (const char* v : vars) {
    const char* s = std::getenv(v);
    if (s && *s) return std::string_view(s).starts_with("es");
  }
  return false;
}
}  // namespace

void setUiLanguage(const std::string& lang) { g_lang = lang == "en" || lang == "es" ? lang : "system"; }
const std::string& uiLanguage() { return g_lang; }

bool spanishUi() {
  if (g_lang != "system") return g_lang == "es";
  return localeSpanish({"LC_ALL", "LC_MESSAGES", "LANG"});
}

bool spanishDates() {
  if (g_lang != "system") return g_lang == "es";
  return localeSpanish({"LC_ALL", "LC_TIME", "LANG"});
}

std::string optionLabel(const std::string& value, bool es) {
  static const std::map<std::string_view, const char*> kEs = {
      {"album", "Álbum"}, {"analog", "Analógico"}, {"auto", "Auto"}, {"badge", "Insignia"}, {"banner", "Banner"},
      {"bar", "Barra"}, {"bars", "Barras"}, {"bighour", "Hora grande"}, {"bonfire", "Hoguera"}, {"brand", "Marca"},
      {"burst", "Estallido"}, {"center", "Centro"}, {"circle", "Círculo"}, {"clover4Leaf", "Trébol de 4"},
      {"clover8Leaf", "Trébol de 8"}, {"column", "Columna"}, {"cookie12Sided", "Galleta 12"}, {"cookie4Sided", "Galleta 4"},
      {"cookie6Sided", "Galleta 6"}, {"cookie7Sided", "Galleta 7"}, {"cookie9Sided", "Galleta 9"}, {"cover", "Portada"},
      {"curtain", "Cortina"}, {"custom", "Personalizado"}, {"cycle", "Ciclo"}, {"dash", "Guion"}, {"demo", "Demo"},
      {"digital", "Digital"}, {"dot", "Punto"}, {"dots", "Puntos"}, {"down", "Abajo"}, {"en", "Inglés"}, {"es", "Español"},
      {"fire", "Fuego"}, {"flip", "Flip"}, {"flower", "Flor"}, {"font", "Fuente"}, {"frame", "Marco"}, {"glass", "Vidrio"},
      {"goodnight", "Buenas noches"}, {"gradient", "Degradado"}, {"grand", "Grande"}, {"halo", "Halo"}, {"hide", "Ocultar"},
      {"inline", "En línea"}, {"inside", "Dentro"}, {"inside_out", "Desde dentro"}, {"inward", "Hacia dentro"},
      {"left", "Izquierda"}, {"line", "Línea"}, {"long", "Larga"}, {"metal", "Metal"}, {"minimal", "Mínimo"},
      {"mono", "Mono"}, {"none", "Ninguno"}, {"numeric", "Numérica"}, {"orb", "Orbe"}, {"outline", "Contorno"},
      {"outward", "Hacia fuera"}, {"oval", "Óvalo"}, {"pentagon", "Pentágono"}, {"pill", "Píldora"}, {"plain", "Simple"},
      {"poster", "Póster"}, {"primary", "Primario"}, {"puffyDiamond", "Diamante"}, {"radial", "Radial"}, {"ribbon", "Cinta"},
      {"right", "Derecha"}, {"rings", "Anillos"}, {"rounded", "Redondeado"}, {"secondary", "Secundario"},
      {"segments", "Segmentos"}, {"short", "Corta"}, {"show", "Mostrar"}, {"slash", "Barra /"}, {"softBurst", "Estallido suave"},
      {"space", "Espacio"}, {"spiral", "Espiral"}, {"split", "Dividido"}, {"square", "Cuadrado"}, {"stacked", "Apilado"},
      {"strokes", "Trazos"}, {"sunny", "Sol"}, {"system", "Sistema"}, {"tertiary", "Terciario"}, {"theme", "Tema"},
      {"up", "Arriba"}, {"verySunny", "Sol intenso"}, {"vortex", "Vórtice"}, {"wall", "Muro"}, {"wave", "Onda"},
      {"spotify", "Spotify"}, {"on_surface", "Texto"}, {"tilt", "Inclinar"}, {"muzzle", "Fogonazo"}, {"manga", "Manga"}, {"flash", "Realista"}, {"pin", "Esquinas"}, {"mesh", "Malla"},
      {"flat", "Plana"}, {"arc", "Arco"}, {"bulge", "Abombar"}, {"flag", "Bandera"}, {"cylinder", "Cilindro"},
      {"smooth", "Suave"}, {"text", "Solo texto"}, {"electric", "Electricidad"}, {"bolts", "Rayos"}, {"plasma", "Plasma"}, {"blue", "Azul eléctrico"}, {"sfx", "Onomatopeyas"}, {"mix", "Mezcla"},
      {"english", "Inglés"}, {"japanese", "Japonés"}, {"korean", "Coreano"}, {"chinese", "Chino"}, {"comic", "Cómic"},
      {"classic", "Clásicos"}, {"speedlines", "Líneas manga"}, {"focus", "Enfoque"},
      {"parallel", "Velocidad"}, {"rain", "Lluvia"}, {"snow", "Nieve"}, {"petals", "Pétalos"}, {"black", "Negro"}, {"white", "Blanco"}, {"top", "Arriba"}, {"bottom", "Abajo"}, {"straight", "Recto"}, {"3", "3 × 3"}, {"4", "4 × 4"}, {"5", "5 × 5"},
  };
  static const std::map<std::string_view, const char*> kEn = {
      {"bighour", "Big hour"}, {"goodnight", "Good night"}, {"en", "English"}, {"es", "Spanish"},
      {"clover4Leaf", "Clover 4"}, {"clover8Leaf", "Clover 8"}, {"cookie12Sided", "Cookie 12"}, {"cookie4Sided", "Cookie 4"},
      {"cookie6Sided", "Cookie 6"}, {"cookie7Sided", "Cookie 7"}, {"cookie9Sided", "Cookie 9"}, {"slash", "Slash /"},
      {"on_surface", "Text"}, {"pin", "Corners"}, {"muzzle", "Muzzle flash"}, {"electric", "Electricity"}, {"sfx", "Onomatopoeia"}, {"speedlines", "Speed lines"}, {"petals", "Sakura petals"}, {"comic", "Comic"}, {"blue", "Electric blue"}, {"flash", "Realistic"}, {"3", "3 × 3"}, {"4", "4 × 4"}, {"5", "5 × 5"},
  };
  const auto& table = es ? kEs : kEn;
  if (auto it = table.find(value); it != table.end()) return it->second;
  // camelCase / snake_case -> "Words like this"
  std::string out;
  for (size_t i = 0; i < value.size(); ++i) {
    const char c = value[i];
    if (c == '_' || c == '-') {
      out += ' ';
    } else if (i > 0 && std::isupper(static_cast<unsigned char>(c))) {
      out += ' ';
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else {
      out += c;
    }
  }
  if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}

}  // namespace undershell
