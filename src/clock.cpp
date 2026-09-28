// SPDX-License-Identifier: GPL-3.0-or-later
// Faces translated from ryoku/shell/quickshell/shell/modules/desktop/clock/*.qml
// (GPL-3.0): sizes, weights, spacing and colours are Ryoku's at clockScale 1.
#include "clock.hpp"

#include "clockparts.hpp"

#include <array>
#include <cmath>
#include <glib.h>
#include <numbers>
#include <regex>

namespace undershell {

namespace {

constexpr float PI = std::numbers::pi_v<float>;
const char* FONT = "Space Grotesk";
const char* MONO = "JetBrains Mono";
const char* DISPLAY = "Fraunces 144pt";
const char* INTER = "Inter Display";

Color withAlpha(Color c, float a) {
  c.a *= a;
  return c;
}

std::string upper(const std::string& s) {
  gchar* u = g_utf8_strup(s.c_str(), -1);
  std::string out(u);
  g_free(u);
  return out;
}

std::string capitalize(const std::string& s) {
  if (s.empty()) return s;
  const gchar* next = g_utf8_next_char(s.c_str());
  gchar* first = g_utf8_strup(s.c_str(), next - s.c_str());
  std::string out = std::string(first) + std::string(next);
  g_free(first);
  return out;
}

std::string stripAccents(const std::string& s) {
  gchar* norm = g_utf8_normalize(s.c_str(), -1, G_NORMALIZE_NFD);
  std::string out;
  for (const gchar* p = norm; *p; p = g_utf8_next_char(p)) {
    gunichar c = g_utf8_get_char(p);
    if (g_unichar_combining_class(c) == 0 && c < 128) out += static_cast<char>(c);
  }
  g_free(norm);
  return out;
}

std::string pad2(int v) { return std::format("{:02d}", v); }

// Ryoku's Clk.parts / Clk.dateParts
struct Parts {
  int hours = 0, minutes = 0, seconds = 0, h12 = 12, wday = 0, mday = 1, mon = 0, year = 2026;
  std::string hh, mm, ss, ampm;
  float hourAngle = 0, minuteAngle = 0, secondAngle = 0;
  std::string weekday, weekdayShort, month, monthShort;
};

const char* kEnDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* kEnDaysShort[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* kEnMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                           "July",    "August",   "September", "October", "November", "December"};
const char* kEsDays[] = {"domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado"};
const char* kEsDaysShort[] = {"dom", "lun", "mar", "mié", "jue", "vie", "sáb"};
const char* kEsMonths[] = {"enero", "febrero", "marzo",      "abril",   "mayo",      "junio",
                           "julio", "agosto",  "septiembre", "octubre", "noviembre", "diciembre"};

std::string strftimeLocal(const char* fmt, const std::tm& tm) {
  char buf[128];
  size_t n = std::strftime(buf, sizeof(buf), fmt, &tm);
  std::string s(buf, n);
  if (!s.empty() && s.back() == '.') s.pop_back();  // "sep." -> "sep"
  return s;
}

Parts partsOf(std::time_t t, bool clock24, const std::string& lang) {
  std::tm tm{};
  localtime_r(&t, &tm);
  Parts p;
  p.hours = tm.tm_hour;
  p.minutes = tm.tm_min;
  p.seconds = tm.tm_sec;
  p.h12 = tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12;
  p.hh = clock24 ? pad2(tm.tm_hour) : pad2(p.h12);
  p.mm = pad2(tm.tm_min);
  p.ss = pad2(tm.tm_sec);
  p.ampm = tm.tm_hour < 12 ? "AM" : "PM";
  p.hourAngle = ((tm.tm_hour % 12) + tm.tm_min / 60.0F) * 30;
  p.minuteAngle = (tm.tm_min + tm.tm_sec / 60.0F) * 6;
  p.secondAngle = tm.tm_sec * 6.0F;
  p.wday = tm.tm_wday;
  p.mday = tm.tm_mday;
  p.mon = tm.tm_mon;
  p.year = tm.tm_year + 1900;
  if (lang == "en" || lang == "es") {
    const bool es = lang == "es";
    p.weekday = capitalize(es ? kEsDays[tm.tm_wday] : kEnDays[tm.tm_wday]);
    p.weekdayShort = capitalize(es ? kEsDaysShort[tm.tm_wday] : kEnDaysShort[tm.tm_wday]);
    p.month = capitalize(es ? kEsMonths[tm.tm_mon] : kEnMonths[tm.tm_mon]);
  } else {
    p.weekday = capitalize(strftimeLocal("%A", tm));
    p.weekdayShort = capitalize(strftimeLocal("%a", tm));
    p.month = capitalize(strftimeLocal("%B", tm));
  }
  return p;
}

bool systemSpanish() {
  for (const char* v : {"LC_ALL", "LC_TIME", "LANG"}) {
    const char* s = std::getenv(v);
    if (s && *s) return std::string_view(s).starts_with("es");
  }
  return false;
}

// the good-night greeting, split into two stacked words
std::pair<std::string, std::string> greeting(int hour, bool es) {
  const int idx = (hour >= 5 && hour < 12) ? 0 : (hour >= 12 && hour < 17) ? 1 : (hour >= 17 && hour < 21) ? 2 : 3;
  static const char* en[] = {"Morning", "Afternoon", "Evening", "Night"};
  static const char* esp[] = {"Días", "Tardes", "Tardes", "Noches"};
  return {es ? "Buenas" : "Good", es ? esp[idx] : en[idx]};
}

// Ryoku's faux-kana stroke alphabet (clock/lib/kana.js) plus the letters the
// Spanish weekdays need. Polylines in a 0..1 cell, y down.
using Stroke = std::vector<std::array<float, 2>>;
const std::unordered_map<char, std::vector<Stroke>>& kanaGlyphs() {
  static const std::unordered_map<char, std::vector<Stroke>> g = {
      {'A', {{{0.5F, 0.06F}, {0.12F, 0.94F}}, {{0.5F, 0.06F}, {0.88F, 0.94F}}, {{0.26F, 0.62F}, {0.74F, 0.62F}}}},
      {'S', {{{0.82F, 0.12F}, {0.2F, 0.12F}, {0.2F, 0.5F}, {0.82F, 0.5F}, {0.82F, 0.88F}, {0.2F, 0.88F}}}},
      {'U', {{{0.18F, 0.06F}, {0.18F, 0.9F}, {0.82F, 0.9F}, {0.82F, 0.06F}}}},
      {'N', {{{0.18F, 0.94F}, {0.18F, 0.06F}, {0.82F, 0.94F}, {0.82F, 0.06F}}}},
      {'M', {{{0.12F, 0.94F}, {0.12F, 0.06F}, {0.5F, 0.62F}, {0.88F, 0.06F}, {0.88F, 0.94F}}}},
      {'O', {{{0.2F, 0.1F}, {0.8F, 0.1F}, {0.8F, 0.9F}, {0.2F, 0.9F}, {0.2F, 0.1F}}}},
      {'T', {{{0.1F, 0.22F}, {0.9F, 0.22F}}, {{0.6F, 0.22F}, {0.5F, 0.94F}}}},
      {'E', {{{0.82F, 0.12F}, {0.2F, 0.12F}, {0.2F, 0.9F}, {0.82F, 0.9F}}, {{0.2F, 0.5F}, {0.68F, 0.5F}}}},
      {'W', {{{0.08F, 0.06F}, {0.28F, 0.94F}, {0.5F, 0.4F}, {0.72F, 0.94F}, {0.92F, 0.06F}}}},
      {'D', {{{0.2F, 0.1F}, {0.2F, 0.9F}, {0.62F, 0.9F}, {0.82F, 0.68F}, {0.82F, 0.32F}, {0.62F, 0.1F}, {0.2F, 0.1F}}}},
      {'H', {{{0.2F, 0.06F}, {0.2F, 0.94F}}, {{0.8F, 0.06F}, {0.8F, 0.94F}}, {{0.2F, 0.5F}, {0.8F, 0.5F}}}},
      {'F', {{{0.2F, 0.94F}, {0.2F, 0.06F}, {0.82F, 0.06F}}, {{0.2F, 0.5F}, {0.66F, 0.5F}}}},
      {'R', {{{0.2F, 0.94F}, {0.2F, 0.06F}, {0.7F, 0.06F}, {0.82F, 0.26F}, {0.7F, 0.46F}, {0.2F, 0.46F}}, {{0.48F, 0.46F}, {0.82F, 0.94F}}}},
      {'I', {{{0.5F, 0.06F}, {0.5F, 0.94F}}, {{0.3F, 0.06F}, {0.7F, 0.06F}}, {{0.3F, 0.94F}, {0.7F, 0.94F}}}},
      // additions in the same angular hand
      {'L', {{{0.22F, 0.06F}, {0.22F, 0.9F}, {0.84F, 0.9F}}}},
      {'J', {{{0.3F, 0.1F}, {0.8F, 0.1F}}, {{0.68F, 0.1F}, {0.68F, 0.9F}, {0.2F, 0.9F}, {0.2F, 0.66F}}}},
      {'V', {{{0.1F, 0.06F}, {0.5F, 0.94F}, {0.9F, 0.06F}}}},
      {'B', {{{0.2F, 0.08F}, {0.2F, 0.92F}}, {{0.2F, 0.08F}, {0.7F, 0.08F}, {0.8F, 0.28F}, {0.68F, 0.48F}}, {{0.2F, 0.48F}, {0.72F, 0.48F}, {0.84F, 0.7F}, {0.72F, 0.92F}, {0.2F, 0.92F}}}},
      {'C', {{{0.82F, 0.1F}, {0.2F, 0.1F}, {0.2F, 0.9F}, {0.82F, 0.9F}}}},
      {'G', {{{0.82F, 0.1F}, {0.2F, 0.1F}, {0.2F, 0.9F}, {0.82F, 0.9F}, {0.82F, 0.54F}, {0.52F, 0.54F}}}},
      {'P', {{{0.2F, 0.94F}, {0.2F, 0.06F}, {0.78F, 0.06F}, {0.78F, 0.48F}, {0.2F, 0.48F}}}},
      {'Y', {{{0.12F, 0.06F}, {0.5F, 0.5F}}, {{0.88F, 0.06F}, {0.5F, 0.5F}, {0.5F, 0.94F}}}},
  };
  return g;
}

// Noctalia's weather cache: "Condition , 12°C" (Ryoku's spacing) or "".
std::string weatherClause(bool es, bool fahrenheit) {
  static double at = -1e9;
  static std::string cached;
  static bool cachedEs = false, cachedF = false;
  const double now = nowSeconds();
  if (now - at < 300 && cachedEs == es && cachedF == fahrenheit) return cached;
  at = now;
  cachedEs = es;
  cachedF = fahrenheit;
  cached.clear();
  const std::string json = readFile(expandHome("~/.cache/noctalia/weather.json"));
  const auto cur = json.find("\"current\"");
  if (cur == std::string::npos) return cached;
  const std::string tail = json.substr(cur, 1200);
  std::smatch m;
  double temp = 0;
  int code = -1;
  if (std::regex_search(tail, m, std::regex(R"re("temperature_c"\s*:\s*(-?[\d.]+))re"))) temp = std::stod(m[1]);
  else return cached;
  if (std::regex_search(tail, m, std::regex(R"re("weather_code"\s*:\s*(\d+))re"))) code = std::stoi(m[1]);
  auto name = [&](int c) -> std::string {
    struct N {
      int code;
      const char *en, *es;
    };
    static const N names[] = {{0, "Clear", "Despejado"},       {1, "Mostly clear", "Mayormente despejado"},
                              {2, "Partly cloudy", "Parcialmente nublado"}, {3, "Overcast", "Nublado"},
                              {45, "Fog", "Niebla"},           {48, "Fog", "Niebla"},
                              {51, "Drizzle", "Llovizna"},     {53, "Drizzle", "Llovizna"},
                              {55, "Drizzle", "Llovizna"},     {61, "Rain", "Lluvia"},
                              {63, "Rain", "Lluvia"},          {65, "Heavy rain", "Lluvia fuerte"},
                              {71, "Snow", "Nieve"},           {73, "Snow", "Nieve"},
                              {75, "Heavy snow", "Nevada fuerte"}, {80, "Showers", "Chubascos"},
                              {81, "Showers", "Chubascos"},    {82, "Heavy showers", "Chubascos fuertes"},
                              {95, "Thunderstorm", "Tormenta"}, {96, "Thunderstorm", "Tormenta"},
                              {99, "Thunderstorm", "Tormenta"}};
    for (auto& n : names)
      if (n.code == c) return es ? n.es : n.en;
    return {};
  };
  const std::string cond = name(code);
  if (fahrenheit) temp = temp * 9 / 5 + 32;
  cached = (cond.empty() ? "" : cond + " , ") + std::format("{}°{}", static_cast<int>(std::lround(temp)), fahrenheit ? "F" : "C");
  return cached;
}

}  // namespace

// ── config ──────────────────────────────────────────────────────────────────

ClockConfig ClockConfig::fromTable(const toml::table& t) {
  ClockConfig c;
  c.face = t["face"].value_or(c.face);
  c.date = t["date"].value_or(c.date);
  c.clock24 = t["clock_24h"].value_or(c.clock24);
  c.seconds = t["seconds"].value_or(c.seconds);
  c.accent = t["accent"].value_or(c.accent);
  c.accentColor = t["accent_color"].value_or(c.accentColor);
  c.ink = t["ink"].value_or(c.ink);
  c.language = t["language"].value_or(c.language);
  c.weather = t["weather"].value_or(c.weather);
  c.fahrenheit = t["fahrenheit"].value_or(c.fahrenheit);
  c.opacity = std::clamp(t["opacity"].value_or(c.opacity), 0.0, 1.0);
  return c;
}

const std::vector<std::string>& ClockWidget::faces() {
  static const std::vector<std::string> f = {"digital", "minimal", "analog", "flip",  "rings",   "bighour",
                                             "metal",   "goodnight", "grand", "column", "outline", "banner"};
  return f;
}

void ClockWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  configure(ClockConfig::fromTable(cfg.options), noct);
  m_opts = cfg.options;
}

void ClockWidget::configure(const ClockConfig& cfg, const NoctaliaState& noct) {
  m_cfg = cfg;
  m_opts = toml::table{};
  m_noct = noct;
  m_ink = noct.color(cfg.ink, Color::fromHex("#f4f1ea"));
  if (cfg.accent == "brand") m_accent = Color::fromHex("#e2342a");
  else if (cfg.accent == "mono") m_accent = m_ink;
  else if (cfg.accent == "custom") m_accent = noct.color(cfg.accentColor);
  else m_accent = noct.color(cfg.accent);
  m_measures.clear();
}

std::time_t ClockWidget::currentTime() const { return s_fixedTime ? s_fixedTime : std::time(nullptr); }

Canvas::Size ClockWidget::measure(const std::string& text, const TextStyle& st) {
  const std::string key = std::format("{}\x1f{}\x1f{}\x1f{}\x1f{}", text, st.family, st.size, st.weight, st.letterSpacing);
  auto it = m_measures.find(key);
  if (it != m_measures.end()) return it->second;
  if (m_measures.size() > 512) m_measures.clear();
  return m_measures[key] = Canvas::measure(text, st);
}

// ── face builder helpers ────────────────────────────────────────────────────

struct FaceBuilder {
  ClockWidget& w;
  ClockWidget::Scene& sc;
  const ClockConfig& cfg;
  Color ink, inkDim, inkSoft, accent;

  TextStyle st(const char* family, float size, int weight, float ls = 0, float stroke = 0) {
    TextStyle s;
    s.family = family;
    s.size = size;
    s.weight = weight;
    s.letterSpacing = ls;
    s.stroke = stroke;
    return s;
  }
  Canvas::Size size(const std::string& t, const TextStyle& s) { return w.measure(t, s); }
  // an element's colour: the face's inks by name, else a palette role / #hex
  Color colorOf(const std::string& name) const {
    if (name == "ink") return ink;
    if (name == "accent") return accent;
    if (name == "dim") return inkDim;
    if (name == "soft") return inkSoft;
    return w.m_noct.color(name, ink);
  }
  TextStyle st(const ClockElement& e) { return st(e.family.c_str(), e.spec->size * e.scale, e.weight, e.spacing); }
  const toml::table& opts() const { return w.m_opts; }
  std::string option(const std::string& key, const std::string& def) const {
    if (const toml::node* n = w.m_opts.get(key))
      if (auto v = n->value<std::string>(); v && !v->empty()) return *v;
    return def;
  }
  void text(const std::string& t, const TextStyle& s, float x, float y, Color c, float opacity = 1, float sy = 1,
            float blur = 0) {
    ClockWidget::Item it;
    it.kind = ClockWidget::Item::Text;
    it.text = t;
    it.style = s;
    it.x = x;
    it.y = y;
    it.color = c;
    it.opacity = opacity;
    it.sy = sy;
    it.blur = blur;
    sc.items.push_back(std::move(it));
  }
  void rect(float x, float y, float ww, float hh, float r, Color fill, float strokeW = 0, Color stroke = {}) {
    ClockWidget::Item it;
    it.kind = ClockWidget::Item::Rect;
    it.x = x;
    it.y = y;
    it.w = ww;
    it.h = hh;
    it.r = r;
    it.color = fill;
    it.strokeW = strokeW;
    it.stroke = stroke;
    sc.items.push_back(it);
  }
  void circle(float cx, float cy, float r, Color fill, float strokeW = 0, Color stroke = {}) {
    ClockWidget::Item it;
    it.kind = ClockWidget::Item::Circle;
    it.x = cx;
    it.y = cy;
    it.r = r;
    it.color = fill;
    it.strokeW = strokeW;
    it.stroke = stroke;
    sc.items.push_back(it);
  }
  void segment(float x1, float y1, float x2, float y2, float width, Color c, bool round = true) {
    ClockWidget::Item it;
    it.kind = ClockWidget::Item::Segment;
    it.x = x1;
    it.y = y1;
    it.x2 = x2;
    it.y2 = y2;
    it.w = width;
    it.color = c;
    it.roundCap = round;
    sc.items.push_back(it);
  }
  void arc(float cx, float cy, float r, float width, float a0, float a1, Color c, bool round) {
    ClockWidget::Item it;
    it.kind = ClockWidget::Item::Arc;
    it.x = cx;
    it.y = cy;
    it.r = r;
    it.w = width;
    it.a0 = a0;
    it.a1 = a1;
    it.color = c;
    it.roundCap = round;
    sc.items.push_back(it);
  }
  // a hand: a rounded bar from the centre, rotated `deg` clockwise from 12
  void hand(float cx, float cy, float deg, float width, float len, Color c) {
    const float a = deg * PI / 180;
    const float dx = std::sin(a), dy = -std::cos(a), hw = width / 2;
    segment(cx + dx * hw, cy + dy * hw, cx + dx * (len - hw), cy + dy * (len - hw), width, c);
  }
};

// ── the faces ───────────────────────────────────────────────────────────────

// digital: big tabular mono time; seconds and AM/PM stacked small to the right
static void faceDigital(FaceBuilder& b, const Parts& t) {
  const float px = 88;
  auto big = b.st(MONO, px, 700);
  auto s1 = b.size(t.hh, big), s2 = b.size(":", big), s3 = b.size(t.mm, big);
  float x = 0;
  b.text(t.hh, big, x, 0, b.ink);
  x += s1.w;
  b.text(":", big, x, 0, b.accent);
  x += s2.w;
  b.text(t.mm, big, x, 0, b.ink);
  x += s3.w;
  const float hmH = s1.h;
  const bool side = b.cfg.seconds || !b.cfg.clock24;
  float right = x;
  if (side) {
    x += 14;
    auto ss = b.st(MONO, std::round(px * 0.3F), 600), ap = b.st(MONO, std::round(px * 0.24F), 600, 1);
    std::vector<std::pair<std::string, TextStyle>> col;
    std::vector<Color> cols;
    if (b.cfg.seconds) col.push_back({t.ss, ss}), cols.push_back(b.accent);
    if (!b.cfg.clock24) col.push_back({t.ampm, ap}), cols.push_back(b.inkDim);
    float colH = 0, colW = 0;
    for (auto& [s, st] : col) {
      auto z = b.size(s, st);
      colH += z.h;
      colW = std::max(colW, z.w);
    }
    colH += 4 * (col.size() - 1);
    float y = (hmH - colH) / 2;
    for (size_t i = 0; i < col.size(); ++i) {
      b.text(col[i].first, col[i].second, x, y, cols[i]);
      y += b.size(col[i].first, col[i].second).h + 4;
    }
    right = x + colW;
  }
  b.sc.w = right;
  b.sc.h = hmH;
}

// minimal: thin airy time, wide tracking, a short accent rule, a quiet caption
static void faceMinimal(FaceBuilder& b, const Parts& t) {
  const float px = 82;
  auto ts = b.st(FONT, px, 300, 2);
  const std::string time = t.hh + ":" + t.mm;
  auto sz = b.size(time, ts);
  b.text(time, ts, 0, 0, b.ink);
  float y = sz.h + 10;
  const float ruleH = std::max(2.0F, 3.0F);
  b.rect(0, y, std::round(sz.w * 0.34F), ruleH, ruleH / 2, b.accent);
  y += ruleH;
  std::string caption = (b.cfg.seconds ? t.ss : "") + ((b.cfg.seconds && !b.cfg.clock24) ? "  " : "") +
                        (!b.cfg.clock24 ? std::string(t.ampm == "AM" ? "am" : "pm") : "");
  float w = sz.w;
  if (!caption.empty()) {
    y += 10;
    auto cs = b.st(FONT, std::round(px * 0.22F), 500, 3);
    b.text(caption, cs, 0, y, b.inkDim);
    auto c = b.size(caption, cs);
    y += c.h;
    w = std::max(w, c.w);
  }
  b.sc.w = w;
  b.sc.h = y;
}

// analog: clean dial, twelve ticks (quarters bright), ink hands, accent second hand
static void faceAnalog(FaceBuilder& b, const Parts& t) {
  const float dia = 220, c = dia / 2, s = 1;
  b.circle(c, c, c - 0.5F, Color{0, 0, 0, 0}, 1, withAlpha(b.ink, 0.14F));
  for (int i = 0; i < 12; ++i) {
    const bool major = i % 3 == 0;
    const float w = major ? 4 : 2, h = major ? 15 : 8, y0 = 8;
    const float a = i * 30 * PI / 180, dx = std::sin(a), dy = -std::cos(a);
    const float r0 = c - y0 - w / 2, r1 = c - y0 - h + w / 2;
    b.segment(c + dx * r0, c + dy * r0, c + dx * r1, c + dy * r1, w, major ? b.ink : b.inkDim);
  }
  b.hand(c, c, t.hourAngle, 8 * s, dia * 0.28F, b.ink);
  b.hand(c, c, t.minuteAngle, 6 * s, dia * 0.40F, b.ink);
  b.hand(c, c, t.secondAngle, std::max(2.0F, 2.5F * s), dia * 0.44F, b.accent);
  b.circle(c, c, 7, b.accent, 2, b.ink);
  b.sc.w = dia;
  b.sc.h = dia;
}

// rings: three concentric arcs sweeping hour / minute / second
static void faceRings(FaceBuilder& b, const Parts& t, const NoctaliaState& noct) {
  const float w = 232, cx = w / 2;
  const float lineW = w * 0.05F, gap = lineW * 1.75F, r0 = w / 2 - lineW * 0.7F - 2;
  const float radii[3] = {r0 - 2 * gap, r0 - gap, r0};
  const float fr[3] = {((t.hours % 12) + t.minutes / 60.0F) / 12.0F, (t.minutes + t.seconds / 60.0F) / 60.0F,
                       t.seconds / 60.0F};
  Color cols[3];
  if (b.cfg.accent == "brand") {
    for (int i = 0; i < 3; ++i) cols[i] = Color::fromHex("#e2342a").mix(Color{1, 1, 1, 1}, i * 0.14F);
  } else if (b.cfg.accent == "mono") {
    for (int i = 0; i < 3; ++i) cols[i] = withAlpha(b.ink, 0.55F + i * 0.22F);
  } else {
    // Ryoku walks the palette ramp at 0.2 / 0.5 / 0.85
    const Color p = b.accent, ter = noct.color("tertiary", p), sec = noct.color("secondary", p);
    cols[0] = ter;
    cols[1] = sec.mix(p, 0.5F);
    cols[2] = p;
  }
  for (int i = 0; i < 3; ++i) {
    b.arc(cx, cx, radii[i], lineW, 0, 2 * PI, withAlpha(b.ink, 0.12F), false);
    if (fr[i] > 0.0001F) b.arc(cx, cx, radii[i], lineW, 0, fr[i] * 2 * PI, cols[i], true);
  }
  auto ts = b.st(MONO, std::round(w * 0.16F), 700);
  const std::string time = t.hh + ":" + t.mm;
  auto sz = b.size(time, ts);
  float total = sz.h;
  TextStyle as = b.st(MONO, std::round(w * 0.066F), 600, 2);
  Canvas::Size az;
  if (!b.cfg.clock24) {
    az = b.size(t.ampm, as);
    total += az.h;
  }
  float y = cx - total / 2;
  b.text(time, ts, cx - sz.w / 2, y, b.ink);
  if (!b.cfg.clock24) b.text(t.ampm, as, cx - az.w / 2, y + sz.h, b.inkDim);
  b.sc.w = w;
  b.sc.h = w;
}

// flip: split-flap cards that fold edge-on, swap and unfold when a digit changes
static void faceFlip(FaceBuilder& b, const Parts& t, const std::vector<std::pair<std::string, float>>& cards, double now) {
  const float ch = 104, cw = std::round(ch * 0.72F), gap = 7;
  const Color card{0.02F, 0.02F, 0.025F, 0.86F};
  auto ds = b.st(MONO, std::round(ch * 0.64F), 700);
  auto cs = b.st(MONO, std::round(ch * 0.5F), 700);
  float x = 0;
  auto drawCard = [&](size_t i) {
    const float sy = cards[i].second;
    const float h = std::max(1.0F, ch * sy);
    b.rect(x, (ch - h) / 2, cw, h, std::round(ch * 0.16F) * std::min(1.0F, sy * 1.4F), card, 1, withAlpha(b.accent, 0.24F));
    auto d = b.size(cards[i].first, ds);
    b.text(cards[i].first, ds, x + (cw - d.w) / 2, (ch - d.h) / 2, b.ink, 1, sy);
    // the fold seam across the middle
    b.rect(x, ch / 2 - 1, cw, 2, 0, Color{0, 0, 0, 0.4F});
    x += cw + gap;
  };
  auto colon = [&](float opacity) {
    auto z = b.size(":", cs);
    b.text(":", cs, x, (ch - z.h) / 2, b.accent, opacity);
    x += z.w + gap;
  };
  const float pulse = 0.65F + 0.35F * static_cast<float>(std::cos(std::fmod(now, 1.24) / 1.24 * 2 * std::numbers::pi));
  drawCard(0);
  drawCard(1);
  colon(pulse);
  drawCard(2);
  drawCard(3);
  if (b.cfg.seconds) {
    colon(1);
    drawCard(4);
    drawCard(5);
  }
  (void)t;
  b.sc.w = x - gap;
  b.sc.h = ch;
}

// big hour: a giant hour; minute over hollow second at its right; weekday/day
// and the hollow month down its left
static void faceBigHour(FaceBuilder& b, const Parts& t) {
  auto wdS = b.st(INTER, 33, 700, 2);
  auto moS = b.st(INTER, 74, 900, 1, 2.0F);
  auto hhS = b.st(INTER, 300, 900, -8);
  auto mmS = b.st(INTER, 116, 900, -4);
  auto ssS = b.st(INTER, 116, 900, 0, 2.6F);
  const std::string wd = upper(t.weekdayShort + ", " + pad2(t.mday)), mo = upper(t.month);
  auto wdZ = b.size(wd, wdS), moZ = b.size(mo, moS), hhZ = b.size(t.hh, hhS), mmZ = b.size(t.mm, mmS);
  auto ssZ = b.size(t.ss, ssS);
  const float leftW = std::max(wdZ.w, moZ.w), leftH = wdZ.h + 2 + moZ.h + 2 + 30;
  const float rightH = mmZ.h + (b.cfg.seconds ? ssZ.h : 0), rightW = std::max(mmZ.w, b.cfg.seconds ? ssZ.w : 0.0F);
  const float H = std::max({hhZ.h, leftH, rightH});
  // left column hangs from the row's bottom
  float y = H - leftH;
  b.text(wd, wdS, 0, y, b.ink);
  b.text(mo, moS, 0, y + wdZ.h + 2, b.ink);
  float x = leftW + 22;
  b.text(t.hh, hhS, x, (H - hhZ.h) / 2, b.ink);
  x += hhZ.w + 22;
  y = (H - rightH) / 2;
  b.text(t.mm, mmS, x, y, b.ink);
  if (b.cfg.seconds) b.text(t.ss, ssS, x, y + mmZ.h, b.ink);
  b.sc.w = x + rightW;
  b.sc.h = H;
}

// metal: a heavy condensed time over one meta line
static void faceMetal(FaceBuilder& b, const Parts& t, bool es) {
  auto ts = b.st(INTER, 132, 900, -2), ms = b.st(INTER, 25, 700, 0.5F);
  std::string meta = (b.cfg.clock24 ? "" : t.ampm + "  |  ") + t.weekday;
  if (b.cfg.weather) {
    const std::string wx = weatherClause(es, b.cfg.fahrenheit);
    if (!wx.empty()) meta += "  |  " + wx;
  }
  const std::string time = t.hh + ":" + t.mm;
  auto tz = b.size(time, ts), mz = b.size(meta, ms);
  b.text(time, ts, 0, 0, b.ink);
  b.text(meta, ms, 0, tz.h + 12, b.ink);
  b.sc.w = std::max(tz.w, mz.w);
  b.sc.h = tz.h + 12 + mz.h;
}

// good night (the card): a greeting card; the weekday in the angular faux-kana
// alphabet, or in a font. An editable structure: every element's font, size,
// colour, case and place come from clockparts (defaults = Ryoku's design).
static void faceGoodNight(FaceBuilder& b, const Parts& t, bool es) {
  const float W0 = 431, H0 = 765, pad = 120, gap = 26;
  const auto elements = clockElements(b.opts(), *clockStructure("goodnight"));
  const bool glyphDay = b.option("goodnight_day_style", "strokes") != "font";
  auto [good, part] = greeting(t.hours, es);
  auto cased = [](const ClockElement& e, const std::string& s) { return e.upper ? upper(s) : s; };
  // measure first: the card widens for bigger fonts
  float maxW = 0;
  for (const auto& e : elements) {
    if (!e.show) continue;
    if (e.spec->kind == ClockElementSpec::Text) {
      auto s = b.st(e);
      if (std::string_view(e.spec->id) == "greeting") {
        maxW = std::max({maxW, b.size(cased(e, good), s).w, b.size(cased(e, part), s).w});
      } else if (std::string_view(e.spec->id) == "date") {
        maxW = std::max(maxW, b.size(cased(e, pad2(t.mday) + " " + t.month), s).w);
      } else {
        maxW = std::max(maxW, b.size(cased(e, t.hh + ":" + t.mm + (b.cfg.clock24 ? "" : " " + t.ampm)), s).w);
      }
    }
  }
  const float W = std::max(W0, maxW + 40);
  float y = pad;
  bool first = true;
  for (const auto& e : elements) {
    if (!e.show) continue;
    if (!first) y += gap;
    first = false;
    const Color c = b.colorOf(e.color);
    const std::string_view id = e.spec->id;
    if (e.spec->kind == ClockElementSpec::Rule) {
      const float len = e.spec->size * e.scale;
      b.rect(W / 2 - e.thickness / 2, y, e.thickness, len, 0, withAlpha(c, 0.85F));
      y += len;
    } else if (id == "greeting") {
      auto s = b.st(e);
      bool firstLine = true;
      for (const std::string& word : {cased(e, good), cased(e, part)}) {
        if (!firstLine) y += 12;
        firstLine = false;
        auto z = b.size(word, s);
        b.text(word, s, (W - z.w) / 2, y, c);
        y += z.h;
      }
    } else if (id == "day") {
      const std::string wk = e.upper ? upper(stripAccents(t.weekdayShort)) : stripAccents(t.weekdayShort);
      if (glyphDay) {
        const std::string glyphsText = upper(stripAccents(t.weekdayShort));
        const float cell = e.spec->size * e.scale, dgap = e.spacing, lw = 11 * e.scale * (e.weight / 700.0F);
        const float kw = glyphsText.size() * cell + (glyphsText.size() - 1) * dgap;
        float kx = (W - kw) / 2;
        const auto& glyphs = kanaGlyphs();
        for (char ch : glyphsText) {
          auto it = glyphs.find(ch);
          if (it != glyphs.end())
            for (const Stroke& st : it->second)
              for (size_t i = 1; i < st.size(); ++i)
                b.segment(kx + st[i - 1][0] * cell, y + 6 + st[i - 1][1] * cell, kx + st[i][0] * cell, y + 6 + st[i][1] * cell, lw,
                          c, false);
          kx += cell + dgap;
        }
        y += cell + 12;
      } else {
        // the weekday set in a font, sized to match the drawn one
        auto s = b.st(e.family.c_str(), e.spec->size * e.scale * 1.15F, e.weight, e.spacing * 0.5F);
        auto z = b.size(wk, s);
        b.text(wk, s, (W - z.w) / 2, y, c);
        y += z.h;
      }
    } else if (id == "date") {
      const std::string date = cased(e, pad2(t.mday) + " " + t.month);
      auto s = b.st(e);
      auto z = b.size(date, s);
      b.text(date, s, (W - z.w) / 2, y, c);
      y += z.h;
    } else if (id == "time") {
      const std::string time = cased(e, t.hh + ":" + t.mm + (b.cfg.clock24 ? "" : " " + t.ampm));
      auto s = b.st(e);
      auto z = b.size(time, s);
      b.text(time, s, (W - z.w) / 2, y, c);
      y += z.h;
    }
  }
  b.sc.w = W;
  b.sc.h = std::max(H0, y + pad);
}

// grand: one giant time in the display serif; accent colon
static void faceGrand(FaceBuilder& b, const Parts& t) {
  const float px = 168;
  auto big = b.st(DISPLAY, px, 500);
  auto s1 = b.size(t.hh, big), s2 = b.size(":", big), s3 = b.size(t.mm, big);
  b.text(t.hh, big, 0, 0, b.ink);
  b.text(":", big, s1.w, 0, b.accent);
  b.text(t.mm, big, s1.w + s2.w, 0, b.ink);
  float right = s1.w + s2.w + s3.w;
  if (b.cfg.seconds || !b.cfg.clock24) {
    const float x = right + 18;
    auto ss = b.st(DISPLAY, std::round(px * 0.26F), 500), ap = b.st(FONT, std::round(px * 0.15F), 600, 3);
    float colH = 0, colW = 0;
    if (b.cfg.seconds) colH += b.size(t.ss, ss).h, colW = std::max(colW, b.size(t.ss, ss).w);
    if (!b.cfg.clock24) colH += b.size(t.ampm, ap).h, colW = std::max(colW, b.size(t.ampm, ap).w);
    if (b.cfg.seconds && !b.cfg.clock24) colH += 6;
    float y = (s1.h - colH) / 2;
    if (b.cfg.seconds) {
      b.text(t.ss, ss, x, y, b.accent);
      y += b.size(t.ss, ss).h + 6;
    }
    if (!b.cfg.clock24) b.text(t.ampm, ap, x, y, b.inkDim);
    right = x + colW;
  }
  b.sc.w = right;
  b.sc.h = s1.h;
}

// column: the hour stacked over the minute, huge and tight; accent minute
static void faceColumn(FaceBuilder& b, const Parts& t) {
  const float px = 150;
  auto big = b.st(FONT, px, 700, -2);
  auto h1 = b.size(t.hh, big), h2 = b.size(t.mm, big);
  const float step = std::round(-0.2F * px);
  b.text(t.hh, big, 0, 0, b.ink);
  float y = h1.h + step;
  b.text(t.mm, big, 0, y, b.accent);
  y += h2.h;
  float w = std::max(h1.w, h2.w);
  if (b.cfg.seconds || !b.cfg.clock24) {
    y += std::round(0.1F * px) + step;
    auto sm = b.st(FONT, std::round(px * 0.2F), 600), ap = b.st(FONT, std::round(px * 0.2F), 600, 2);
    float x = 0, rowH = 0;
    if (b.cfg.seconds) {
      b.text(t.ss, sm, x, y, b.inkDim);
      x += b.size(t.ss, sm).w + 8;
      rowH = b.size(t.ss, sm).h;
    }
    if (!b.cfg.clock24) {
      b.text(t.ampm, ap, x, y, b.inkDim);
      x += b.size(t.ampm, ap).w;
      rowH = std::max(rowH, b.size(t.ampm, ap).h);
    }
    y += rowH;
    w = std::max(w, x);
  }
  b.sc.w = w;
  b.sc.h = y;
}

// outline: giant hollow-stroked numerals and a filled accent colon
static void faceOutline(FaceBuilder& b, const Parts& t) {
  const float px = 190;
  auto hollow = b.st(FONT, px, 700, 0, 3), solid = b.st(FONT, px, 700);
  auto s1 = b.size(t.hh, hollow), s2 = b.size(":", solid), s3 = b.size(t.mm, hollow);
  const float H = std::max(s1.h, s2.h);
  b.text(t.hh, hollow, 0, (H - s1.h) / 2, b.ink);
  b.text(":", solid, s1.w + 6, (H - s2.h) / 2, b.accent);
  b.text(t.mm, hollow, s1.w + 6 + s2.w + 6, (H - s3.h) / 2, b.ink);
  b.sc.w = s1.w + 6 + s2.w + 6 + s3.w;
  b.sc.h = H;
}

// banner: one wide line "hh:mm AP – MM/DD" with a soft adaptive halo
static void faceBanner(FaceBuilder& b, const Parts& t) {
  const float px = 150, track = std::round(px * -0.01F);
  auto big = b.st(FONT, px, 700, track), dash = b.st(FONT, px, 700), ap = b.st(FONT, std::round(px * 0.24F), 600, 2);
  const std::string md = pad2(t.mon + 1) + "/" + pad2(t.mday);
  // halo colour: dark behind light ink, light behind dark ink
  const float lum = 0.2126F * b.ink.r + 0.7152F * b.ink.g + 0.0722F * b.ink.b;
  const Color halo = lum > 0.5F ? Color{0, 0, 0, 0.55F} : Color{1, 1, 1, 0.6F};
  const float blur = px * 0.07F;
  auto both = [&](const std::string& s, const TextStyle& st, float x, float y, Color c) {
    b.text(s, st, x, y, halo, 1, 1, blur);
    b.text(s, st, x, y, c);
  };
  auto s1 = b.size(t.hh, big), s2 = b.size(":", big), s3 = b.size(t.mm, big);
  const float hmH = s1.h;
  float x = 0;
  both(t.hh, big, x, 0, b.ink);
  x += s1.w;
  both(":", big, x, 0, b.accent);
  x += s2.w;
  both(t.mm, big, x, 0, b.ink);
  x += s3.w + px * 0.16F;
  if (!b.cfg.clock24) {
    auto a = b.size(t.ampm, ap);
    both(t.ampm, ap, x, hmH - px * 0.14F - a.h, b.inkDim);
    x += a.w + px * 0.16F;
  }
  auto dz = b.size("–", dash);
  both("–", dash, x, (hmH - dz.h) / 2, b.inkDim);
  x += dz.w + px * 0.16F;
  auto mz = b.size(md, big);
  both(md, big, x, (hmH - mz.h) / 2, b.ink);
  x += mz.w;
  b.sc.w = x;
  b.sc.h = hmH;
}

// ── date strips ─────────────────────────────────────────────────────────────

static void dateStrip(FaceBuilder& b, const Parts& t, ClockWidget::Scene& ds) {
  FaceBuilder d{b.w, ds, b.cfg, b.ink, b.inkDim, b.inkSoft, b.accent};
  if (b.cfg.date == "badge") {
    auto domS = d.st(MONO, 40, 700), wdS = d.st(FONT, 15, 600, 2), moS = d.st(FONT, 15, 500);
    const std::string dom = std::to_string(t.mday), wd = upper(t.weekdayShort);
    auto a = d.size(dom, domS), w1 = d.size(wd, wdS), w2 = d.size(t.month, moS);
    const float colW = std::max(w1.w, w2.w), colH = w1.h + 2 + w2.h;
    const float innerW = a.w + 12 + colW, innerH = std::max(a.h, colH);
    const float W = innerW + 28, H = innerH + 16;
    d.rect(0, 0, W, H, 14, withAlpha(b.accent, 0.16F), 1, withAlpha(b.accent, 0.42F));
    d.text(dom, domS, 14, 8 + (innerH - a.h) / 2, b.ink);
    const float cx = 14 + a.w + 12, cy = 8 + (innerH - colH) / 2;
    d.text(wd, wdS, cx, cy, b.accent);
    d.text(t.month, moS, cx, cy + w1.h + 2, b.inkSoft);
    ds.w = W;
    ds.h = H;
  } else if (b.cfg.date == "stacked") {
    auto domS = d.st(MONO, 52, 700), wdS = d.st(FONT, 22, 600), myS = d.st(FONT, 17, 500);
    const std::string dom = std::to_string(t.mday), my = t.month + " " + std::to_string(t.year);
    auto a = d.size(dom, domS), w1 = d.size(t.weekday, wdS), w2 = d.size(my, myS);
    const float colH = w1.h + 3 + w2.h, H = std::max(a.h, colH);
    d.text(dom, domS, 0, (H - a.h) / 2, b.ink);
    const float cx = a.w + 14, cy = (H - colH) / 2;
    d.text(t.weekday, wdS, cx, cy, b.accent);
    d.text(my, myS, cx, cy + w1.h + 3, b.inkDim);
    ds.w = cx + std::max(w1.w, w2.w);
    ds.h = H;
  } else {  // inline
    auto wdS = d.st(FONT, 22, 600), dotS = d.st(FONT, 22, 700), mdS = d.st(FONT, 22, 500);
    const std::string md = t.month + " " + std::to_string(t.mday);
    auto a = d.size(t.weekday, wdS), dot = d.size("·", dotS), m = d.size(md, mdS);
    d.text(t.weekday, wdS, 0, 0, b.accent);
    d.text("·", dotS, a.w + 8, 0, b.inkDim);
    d.text(md, mdS, a.w + 8 + dot.w + 8, 0, b.inkSoft);
    ds.w = a.w + 8 + dot.w + 8 + m.w;
    ds.h = std::max({a.h, dot.h, m.h});
  }
}

// ── widget ──────────────────────────────────────────────────────────────────

ClockWidget::Scene ClockWidget::build(std::time_t now) {
  Scene face;
  const std::string lang = m_cfg.language == "system" ? "system" : m_cfg.language;
  const bool es = m_cfg.language == "es" || (m_cfg.language == "system" && systemSpanish());
  const Parts t = partsOf(now, m_cfg.clock24, lang);
  FaceBuilder b{*this, face, m_cfg, m_ink, withAlpha(m_ink, 0.62F), withAlpha(m_ink, 0.8F), m_accent};
  const std::string& f = m_cfg.face;
  if (f == "minimal") faceMinimal(b, t);
  else if (f == "analog") faceAnalog(b, t);
  else if (f == "rings") faceRings(b, t, m_noct);
  else if (f == "flip") {
    // hours always two cards, so the look holds in 12 h and 24 h
    const std::string hh = m_cfg.clock24 ? t.hh : pad2(t.h12);
    std::string digits = hh + t.mm + (m_cfg.seconds ? t.ss : "");
    if (m_cards.size() != digits.size()) m_cards.assign(digits.size(), {});
    std::vector<std::pair<std::string, float>> cards;
    for (size_t i = 0; i < digits.size(); ++i) {
      Card& c = m_cards[i];
      const std::string d(1, digits[i]);
      if (c.shown.empty()) c.shown = c.target = d;
      if (d != c.target) {
        c.target = d;
        if (c.start < 0) c.start = m_now;
      }
      float sy = 1;
      if (c.start >= 0) {
        // fold edge-on (120 ms, ease-in), swap, unfold (150 ms, ease-out)
        const double el = m_now - c.start;
        if (el < 0.12) {
          const double k = el / 0.12;
          sy = static_cast<float>(std::cos(k * k * 88 * std::numbers::pi / 180));
        } else if (el < 0.27) {
          c.shown = c.target;
          const double k = (el - 0.12) / 0.15;
          const double e = 1 - (1 - k) * (1 - k);
          sy = static_cast<float>(std::cos((1 - e) * 88 * std::numbers::pi / 180));
        } else {
          c.shown = c.target;
          c.start = -1;
        }
      }
      cards.push_back({c.shown, std::max(0.02F, sy)});
    }
    faceFlip(b, t, cards, m_now);
  } else if (f == "bighour") faceBigHour(b, t);
  else if (f == "metal") faceMetal(b, t, es);
  else if (f == "goodnight") faceGoodNight(b, t, es);
  else if (f == "grand") faceGrand(b, t);
  else if (f == "column") faceColumn(b, t);
  else if (f == "outline") faceOutline(b, t);
  else if (f == "banner") faceBanner(b, t);
  else faceDigital(b, t);

  if (m_cfg.date == "none") return face;
  Scene date;
  dateStrip(b, t, date);
  // the date strip centred under the face, 14 apart
  Scene all;
  all.w = std::max(face.w, date.w);
  const float fx = (all.w - face.w) / 2, dx = (all.w - date.w) / 2, dy = face.h + 14;
  for (auto& it : face.items) {
    it.x += fx;
    it.x2 += fx;
    all.items.push_back(std::move(it));
  }
  for (auto& it : date.items) {
    it.x += dx;
    it.x2 += dx;
    it.y += dy;
    it.y2 += dy;
    all.items.push_back(std::move(it));
  }
  all.h = dy + date.h;
  return all;
}

void ClockWidget::tick(const TickContext& ctx) { m_now = ctx.now; }

bool ClockWidget::animating(const TickContext& ctx) const {
  (void)ctx;
  if (m_cfg.face != "flip") return false;
  return true;  // the colon breathes; cards fold on change
}

double ClockWidget::nextWakeup(double now) const {
  // redraw on the next wall-clock second (or minute when nothing shows seconds)
  const bool perSecond = m_cfg.seconds || m_cfg.face == "analog" || m_cfg.face == "rings";
  const auto sys = std::chrono::system_clock::now().time_since_epoch();
  const double wall = std::chrono::duration<double>(sys).count();
  const double period = perSecond ? 1.0 : 60.0;
  const double toNext = period - std::fmod(wall, period) + 0.002;
  return now + toNext;
}

void ClockWidget::draw(const DrawContext& ctx) {
  Scene sc = build(currentTime());
  if (sc.w <= 0 || sc.h <= 0) return;
  // fit the design into the box, centred, with a small margin
  const float margin = 4;
  const float k = std::min((ctx.w - 2 * margin) / sc.w, (ctx.h - 2 * margin) / sc.h);
  const float ox = (ctx.w - sc.w * k) / 2, oy = (ctx.h - sc.h * k) / 2;
  m_canvas.begin(ctx.w, ctx.h, ctx.scale, ctx.text);
  m_canvas.setTransform(k, ox, oy);
  const float op = static_cast<float>(m_cfg.opacity);
  for (const Item& it : sc.items) {
    Color c = withAlpha(it.color, op);
    switch (it.kind) {
      case Item::Text: m_canvas.text(it.text, it.style, it.x, it.y, c, it.opacity, it.sy, it.blur); break;
      case Item::Rect: m_canvas.roundRect(it.x, it.y, it.w, it.h, it.r, c, it.strokeW, withAlpha(it.stroke, op)); break;
      case Item::Circle: m_canvas.circle(it.x, it.y, it.r, c, it.strokeW, withAlpha(it.stroke, op)); break;
      case Item::Segment: m_canvas.segment(it.x, it.y, it.x2, it.y2, it.w, c, it.roundCap); break;
      case Item::Arc: m_canvas.arc(it.x, it.y, it.r, it.w, it.a0, it.a1, c, it.roundCap); break;
    }
  }
  if (ctx.text) ctx.text->collect(10);
}

}  // namespace undershell
