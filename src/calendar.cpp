// SPDX-License-Identifier: GPL-3.0-or-later
// The calendar widget (see calendar.hpp).
#include "calendar.hpp"

#include "i18n.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <format>

namespace undershell {

namespace {
template <typename T>
T get(const toml::table& t, std::string_view key, T def) {
  if (auto v = t[key].value<T>()) return *v;
  return def;
}

const char* kMonthsEs[] = {"Enero", "Febrero", "Marzo", "Abril", "Mayo", "Junio", "Julio", "Agosto", "Septiembre", "Octubre", "Noviembre", "Diciembre"};
const char* kMonthsEn[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
// Sunday first, as tm_wday counts
const char* kDaysEs[] = {"Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado"};
const char* kDaysEn[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* kShortEs[] = {"Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb"};
const char* kShortEn[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* kInitialEs[] = {"D", "L", "M", "X", "J", "V", "S"};
const char* kInitialEn[] = {"S", "M", "T", "W", "T", "F", "S"};

using namespace std::chrono;

year_month_day ymd(int y, int m, int d) { return year_month_day{year{y}, month{static_cast<unsigned>(m)}, day{static_cast<unsigned>(d)}}; }
int weekdayOf(const year_month_day& d) { return static_cast<int>(weekday{sys_days{d}}.c_encoding()); }  // 0 = Sunday
int daysIn(int y, int m) { return static_cast<int>(static_cast<unsigned>((year{y} / month{static_cast<unsigned>(m)} / last).day())); }
// ISO 8601: the week of the year whose Thursday this day's week holds
int isoWeek(const year_month_day& d) {
  const sys_days sd{d};
  const int wd = (weekdayOf(d) + 6) % 7;  // Monday 0
  const sys_days thursday = sd + days{3 - wd};
  const year_month_day ty{thursday};
  const sys_days jan1{ty.year() / January / 1};
  return static_cast<int>((thursday - jan1).count() / 7) + 1;
}

Color withA(Color c, float a) {
  c.a *= a;
  return c;
}
}  // namespace

CalendarConfig CalendarConfig::fromTable(const toml::table& t) {
  CalendarConfig c;
  c.layout = get<std::string>(t, "layout", c.layout);
  c.firstDay = get<std::string>(t, "first_day", c.firstDay);
  c.plate = get<std::string>(t, "plate", c.plate);
  c.accent = get<std::string>(t, "accent", c.accent);
  c.ink = get<std::string>(t, "ink", c.ink);
  c.font = get<std::string>(t, "font", c.font);
  c.weekends = get<bool>(t, "weekends", c.weekends);
  c.weekNumbers = get<bool>(t, "week_numbers", c.weekNumbers);
  c.language = get<std::string>(t, "language", c.language);
  if (auto d = t["opacity"].value<double>()) c.opacity = std::clamp(*d, 0.0, 1.0);
  c.date = get<std::string>(t, "date", c.date);
  return c;
}

void CalendarWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  m_cfg = CalendarConfig::fromTable(cfg.options);
  m_accent = noct.color(m_cfg.accent, Color::fromHex("#e2342a"));
  m_ink = noct.color(m_cfg.ink, Color::fromHex("#f4f1ea"));
  m_surface = noct.color("surface", Color::fromHex("#16161a"));
  const float lum = 0.2126F * m_accent.r + 0.7152F * m_accent.g + 0.0722F * m_accent.b;
  m_onAccent = lum > 0.55F ? Color{0.07F, 0.07F, 0.08F, 1} : Color{1, 1, 1, 1};
  m_es = m_cfg.language == "es" || (m_cfg.language != "en" && spanishUi());
  m_fixed = {};
  int y = 0, m = 0, d = 0;
  if (std::sscanf(m_cfg.date.c_str(), "%d-%d-%d", &y, &m, &d) == 3 && m >= 1 && m <= 12 && d >= 1 && d <= daysIn(y, m)) m_fixed = {y, m, d};
}

void CalendarWidget::tick(const TickContext&) {}

CalendarWidget::Day CalendarWidget::today() const {
  if (m_fixed.y) return m_fixed;
  const std::time_t tt = std::time(nullptr);
  std::tm tm{};
  localtime_r(&tt, &tm);
  return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
}

double CalendarWidget::nextWakeup(double now) const {
  // just after the next local midnight
  const std::time_t tt = std::time(nullptr);
  std::tm tm{};
  localtime_r(&tt, &tm);
  const int left = 86400 - (tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
  return now + left + 1;
}

namespace {
// text whose logical box is centred on (cx, cy)
void centred(Canvas& c, const std::string& s, const TextStyle& st, float cx, float cy, Color col) {
  const auto sz = Canvas::measure(s, st);
  c.text(s, st, cx - sz.w / 2, cy - sz.h / 2, col);
}
}  // namespace

void CalendarWidget::draw(const DrawContext& ctx) {
  Canvas& c = m_canvas;
  c.begin(ctx.w, ctx.h, ctx.scale, ctx.text);
  const float w = ctx.w, h = ctx.h, op = static_cast<float>(m_cfg.opacity);
  const float radius = std::min(w, h) * 0.08F;
  if (m_cfg.plate != "none") {
    const float a = m_cfg.plate == "solid" ? 0.94F : 0.58F;
    c.roundRect(0, 0, w, h, radius, withA(m_surface.mix(m_accent, 0.06F), a * op), 1, withA(m_ink, 0.10F * op));
  }
  const Day t = today();
  if (m_cfg.layout == "week") drawWeek(c, w, h, t);
  else if (m_cfg.layout == "day") drawPage(c, w, h, t);
  else drawMonth(c, w, h, t);
  if (ctx.text) ctx.text->collect(60);
}

void CalendarWidget::drawMonth(Canvas& c, float w, float h, const Day& t) {
  const float op = static_cast<float>(m_cfg.opacity);
  const float pad = std::min(w, h) * 0.07F;
  const float iw = w - 2 * pad, ih = h - 2 * pad;
  const bool monday = m_cfg.firstDay != "sunday";
  const int first = weekdayOf(ymd(t.y, t.m, 1));
  const int lead = monday ? (first + 6) % 7 : first;  // blanks before the 1st
  const int n = daysIn(t.y, t.m);
  const int rows = (lead + n + 6) / 7;
  const float headH = ih * 0.17F, dayRowH = ih * 0.11F;
  const float wkW = m_cfg.weekNumbers ? iw * 0.09F : 0;
  const float cw = (iw - wkW) / 7, ch = (ih - headH - dayRowH) / static_cast<float>(rows);
  const float fs = std::min(cw, ch);
  // the month and the year
  const TextStyle head{.family = m_cfg.font, .size = std::min(headH * 0.62F, iw * 0.09F), .weight = 700};
  const std::string month = m_es ? kMonthsEs[t.m - 1] : kMonthsEn[t.m - 1];
  const auto ms = Canvas::measure(month, head);
  const float hy = pad + (headH * 0.8F - ms.h) / 2;
  c.text(month, head, pad + wkW + cw * 0.18F, hy, withA(m_ink, op));
  c.text(std::to_string(t.y), TextStyle{.family = m_cfg.font, .size = head.size, .weight = 500}, pad + wkW + cw * 0.18F + ms.w + head.size * 0.35F,
         hy, withA(m_accent, op));
  // the days' initials
  const TextStyle ini{.family = m_cfg.font, .size = std::min(dayRowH * 0.55F, cw * 0.4F), .weight = 600};
  for (int k = 0; k < 7; ++k) {
    const int wd = monday ? (k + 1) % 7 : k;
    const bool weekend = wd == 0 || wd == 6;
    const Color col = m_cfg.weekends && weekend ? withA(m_accent, 0.85F * op) : withA(m_ink, 0.5F * op);
    centred(c, m_es ? kInitialEs[wd] : kInitialEn[wd], ini, pad + wkW + cw * (k + 0.5F), pad + headH + dayRowH / 2, col);
  }
  // the days
  const TextStyle num{.family = m_cfg.font, .size = fs * 0.4F, .weight = 500};
  const TextStyle numToday{.family = m_cfg.font, .size = fs * 0.42F, .weight = 700};
  const TextStyle wk{.family = m_cfg.font, .size = fs * 0.28F, .weight = 500};
  const float gy = pad + headH + dayRowH;
  for (int r = 0; r < rows; ++r) {
    if (m_cfg.weekNumbers) {
      const int dayInRow = std::clamp(r * 7 - lead + 1 + 3, 1, n);  // the row's Thursday, or as near as the month allows
      centred(c, std::to_string(isoWeek(ymd(t.y, t.m, dayInRow))), wk, pad + wkW / 2, gy + ch * (r + 0.5F), withA(m_ink, 0.35F * op));
    }
    for (int k = 0; k < 7; ++k) {
      const int d = r * 7 + k - lead + 1;
      if (d < 1 || d > n) continue;
      const int wd = monday ? (k + 1) % 7 : k;
      const float cx = pad + wkW + cw * (k + 0.5F), cy = gy + ch * (r + 0.5F);
      if (d == t.d) {
        c.circle(cx, cy, fs * 0.4F, withA(m_accent, op));
        centred(c, std::to_string(d), numToday, cx, cy, withA(m_onAccent, op));
        continue;
      }
      const bool weekend = wd == 0 || wd == 6;
      const bool past = d < t.d;
      Color col = m_cfg.weekends && weekend ? m_accent.mix(m_ink, 0.35F) : m_ink;
      col = withA(col, (past ? 0.45F : 0.92F) * op);
      centred(c, std::to_string(d), num, cx, cy, col);
    }
  }
}

void CalendarWidget::drawWeek(Canvas& c, float w, float h, const Day& t) {
  const float op = static_cast<float>(m_cfg.opacity);
  const float pad = std::min(w, h) * 0.09F;
  const float iw = w - 2 * pad, ih = h - 2 * pad;
  const bool monday = m_cfg.firstDay != "sunday";
  const auto today = ymd(t.y, t.m, t.d);
  const int wd = weekdayOf(today);
  const int back = monday ? (wd + 6) % 7 : wd;
  const sys_days start = sys_days{today} - days{back};
  // a line over the strip: the month and the week
  const float headH = ih * 0.24F;
  const TextStyle head{.family = m_cfg.font, .size = std::min(headH * 0.6F, iw * 0.05F), .weight = 600};
  const std::string month = m_es ? kMonthsEs[t.m - 1] : kMonthsEn[t.m - 1];
  const std::string line = std::format("{}  ·  {} {}", month, m_es ? "semana" : "week", isoWeek(today));
  c.text(line, head, pad + iw * 0.01F, pad + (headH - Canvas::measure(line, head).h) / 2, withA(m_ink, 0.7F * op));
  const float cw = iw / 7, top = pad + headH, ch = ih - headH;
  const float fs = std::min(cw * 1.2F, ch);
  const TextStyle ini{.family = m_cfg.font, .size = fs * 0.2F, .weight = 600};
  const TextStyle num{.family = m_cfg.font, .size = fs * 0.4F, .weight = 600};
  for (int k = 0; k < 7; ++k) {
    const year_month_day d{start + days{k}};
    const int dwd = weekdayOf(d);
    const bool isToday = d == today, weekend = dwd == 0 || dwd == 6;
    const float cx = pad + cw * (k + 0.5F);
    Color ink = m_cfg.weekends && weekend ? m_accent.mix(m_ink, 0.35F) : m_ink;
    if (isToday) {
      c.roundRect(cx - cw * 0.42F, top + ch * 0.04F, cw * 0.84F, ch * 0.92F, std::min(cw, ch) * 0.3F, withA(m_accent, op));
      ink = m_onAccent;
    }
    const bool past = sys_days{d} < sys_days{today};
    const float a = isToday ? 1.0F : past ? 0.45F : 0.92F;
    centred(c, m_es ? kShortEs[dwd] : kShortEn[dwd], ini, cx, top + ch * 0.3F, withA(ink, a * 0.8F * op));
    centred(c, std::to_string(static_cast<unsigned>(d.day())), num, cx, top + ch * 0.64F, withA(ink, a * op));
  }
}

void CalendarWidget::drawPage(Canvas& c, float w, float h, const Day& t) {
  const float op = static_cast<float>(m_cfg.opacity);
  const float radius = std::min(w, h) * 0.08F;
  const float band = h * 0.24F;
  const std::string month = m_es ? kMonthsEs[t.m - 1] : kMonthsEn[t.m - 1];
  std::string upper;
  for (char ch : month) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  const TextStyle head{.family = m_cfg.font, .size = std::min(band * 0.5F, w * 0.12F), .weight = 700, .letterSpacing = band * 0.04F};
  if (m_cfg.plate != "none") {
    // the page's coloured top, its corners rounded with the plate's
    c.clip(0, 0, w, band);
    c.roundRect(0, 0, w, h, radius, withA(m_accent, op));
    c.clip();
    centred(c, upper, head, w / 2, band / 2, withA(m_onAccent, op));
  } else {
    centred(c, upper, head, w / 2, band / 2, withA(m_accent, op));
  }
  const int wd = weekdayOf(ymd(t.y, t.m, t.d));
  const float rest = h - band;
  const TextStyle big{.family = m_cfg.font, .size = std::min(rest * 0.56F, w * 0.5F), .weight = 700};
  centred(c, std::to_string(t.d), big, w / 2, band + rest * 0.42F, withA(m_ink, op));
  const TextStyle sub{.family = m_cfg.font, .size = std::min(rest * 0.13F, w * 0.09F), .weight = 500};
  const std::string day = std::format("{}  ·  {}", m_es ? kDaysEs[wd] : kDaysEn[wd], t.y);
  centred(c, day, sub, w / 2, band + rest * 0.82F, withA(m_ink, 0.6F * op));
}

}  // namespace undershell
