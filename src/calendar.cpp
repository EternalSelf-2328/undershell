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
  c.ics = get<std::string>(t, "ics", c.ics);
  c.khal = get<bool>(t, "khal", c.khal);
  c.evolution = get<bool>(t, "evolution", c.evolution);
  if (auto d = t["agenda_days"].value<int64_t>()) c.agendaDays = static_cast<int>(std::clamp<int64_t>(*d, 1, 60));
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
  m_sources = icsSources(m_cfg.ics);
  if (m_cfg.khal)
    for (auto& s : khalSources()) m_sources.push_back(s);
  if (m_cfg.evolution)
    for (auto& s : evolutionSources()) m_sources.push_back(s);
  m_stamp.clear();  // read them again
}

std::vector<std::string> CalendarWidget::watchedFiles() const {
  std::vector<std::string> out;
  for (const auto& s : m_sources) out.push_back(s.folder ? s.path + "/" : s.path);
  return out;
}

namespace {
int64_t localMidnight(int y, int m, int d) {
  std::tm tm{};
  tm.tm_year = y - 1900;
  tm.tm_mon = m - 1;
  tm.tm_mday = d;
  tm.tm_isdst = -1;
  return static_cast<int64_t>(std::mktime(&tm));
}
}  // namespace

void CalendarWidget::loadEvents(const Day& t) {
  if (m_sources.empty()) {
    m_events.clear();
    return;
  }
  // the month shown, a week round it, and the agenda's days
  const int64_t from = localMidnight(t.y, t.m, 1) - 7 * 86400;
  const int64_t to = std::max(localMidnight(t.y, t.m, daysIn(t.y, t.m)) + 8 * 86400, localMidnight(t.y, t.m, t.d) + (m_cfg.agendaDays + 1) * 86400);
  const std::string window = std::format("{}-{}", from, to);
  const std::string stamp = sourcesStamp(m_sources);
  if (stamp == m_stamp && window == m_window) return;
  m_stamp = stamp;
  m_window = window;
  m_events = undershell::loadEvents(m_sources, from, to);
}

void CalendarWidget::tick(const TickContext&) { loadEvents(today()); }

std::vector<const CalEvent*> CalendarWidget::eventsOn(int y, int m, int d) const {
  std::vector<const CalEvent*> out;
  const int64_t a = localMidnight(y, m, d), b = a + 86400;
  for (const auto& e : m_events)
    if (e.start < b && std::max(e.end, e.start + 1) > a) out.push_back(&e);
  return out;
}

// up to three dots under a day, one per event, in its calendar's colour
void CalendarWidget::dots(Canvas& c, int y, int m, int d, float cx, float cy, float r, Color over) const {
  const auto ev = eventsOn(y, m, d);
  const int n = std::min<int>(3, static_cast<int>(ev.size()));
  const float op = static_cast<float>(m_cfg.opacity);
  for (int k = 0; k < n; ++k) {
    Color col = over.a > 0 ? over : (ev[static_cast<size_t>(k)]->color.empty() ? m_accent : Color::fromHex(ev[static_cast<size_t>(k)]->color));
    col.a = op;
    c.circle(cx + (static_cast<float>(k) - (n - 1) / 2.0F) * r * 2.8F, cy, r, col);
  }
}

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
  double next = now + left + 1;
  // Evolution writes its cache without closing it: looked at every 10 minutes
  if (std::ranges::any_of(m_sources, [](const CalSource& s) { return s.sqlite; })) next = std::min(next, now + 600);
  // the agenda and the page drop an event once it is over
  if (m_cfg.layout == "agenda" || m_cfg.layout == "day")
    for (const auto& e : m_events)
      if (!e.allDay && e.end > tt) {
        next = std::min(next, now + static_cast<double>(e.end - tt) + 1);
        break;
      }
  return next;
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
  if (m_events.empty() && !m_sources.empty()) loadEvents(t);  // drawn before its first tick
  if (m_cfg.layout == "week") drawWeek(c, w, h, t);
  else if (m_cfg.layout == "day") drawPage(c, w, h, t);
  else if (m_cfg.layout == "agenda") drawAgenda(c, w, h, t);
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
        centred(c, std::to_string(d), numToday, cx, cy - fs * 0.04F, withA(m_onAccent, op));
        dots(c, t.y, t.m, d, cx, cy + fs * 0.26F, fs * 0.04F, m_onAccent);
        continue;
      }
      dots(c, t.y, t.m, d, cx, cy + fs * 0.3F, fs * 0.045F, Color{0, 0, 0, 0});
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
    centred(c, std::to_string(static_cast<unsigned>(d.day())), num, cx, top + ch * 0.62F, withA(ink, a * op));
    dots(c, static_cast<int>(d.year()), static_cast<int>(static_cast<unsigned>(d.month())), static_cast<int>(static_cast<unsigned>(d.day())), cx,
         top + ch * 0.85F, fs * 0.035F, isToday ? m_onAccent : Color{0, 0, 0, 0});
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
  // today's events under the date, when there are any: the date makes room
  std::vector<const CalEvent*> ev;
  const auto now = static_cast<int64_t>(std::time(nullptr));
  for (const CalEvent* e : eventsOn(t.y, t.m, t.d))
    if (e->allDay || e->end > now || m_fixed.y) ev.push_back(e);
  const float k = ev.empty() ? 1.0F : 0.62F;
  const TextStyle big{.family = m_cfg.font, .size = std::min(rest * 0.56F * k, w * 0.5F), .weight = 700};
  centred(c, std::to_string(t.d), big, w / 2, band + rest * 0.42F * k, withA(m_ink, op));
  const TextStyle sub{.family = m_cfg.font, .size = std::min(rest * 0.13F, w * 0.09F) * (ev.empty() ? 1.0F : 0.85F), .weight = 500};
  const std::string day = std::format("{}  ·  {}", m_es ? kDaysEs[wd] : kDaysEn[wd], t.y);
  centred(c, day, sub, w / 2, band + rest * (ev.empty() ? 0.82F : 0.5F), withA(m_ink, 0.6F * op));
  if (ev.empty()) return;
  const float pad = w * 0.09F, rowH = rest * 0.13F;
  const TextStyle row{.family = m_cfg.font, .size = rowH * 0.62F, .weight = 500, .maxWidth = w - 2 * pad - rowH * 2.6F, .maxLines = 1};
  const TextStyle time{.family = m_cfg.font, .size = rowH * 0.56F, .weight = 600};
  float y = band + rest * 0.62F;
  // as many rows as fit; when some are left over, the last row says how many
  const auto fit = static_cast<size_t>(std::max(1.0F, std::floor((h - rest * 0.04F - y) / rowH)));
  for (size_t i = 0; i < ev.size() && i < fit; ++i, y += rowH) {
    const CalEvent& e = *ev[i];
    if (i + 1 == fit && ev.size() > fit) {
      const size_t more = ev.size() - i;
      c.text(m_es ? std::format("y {} más", more) : std::format("and {} more", more), time, pad + rowH * 0.35F,
             y + (rowH - Canvas::measure("y", time).h) / 2, withA(m_ink, 0.5F * op));
      break;
    }
    const Color col = e.color.empty() ? m_accent : Color::fromHex(e.color);
    c.roundRect(pad, y + rowH * 0.18F, std::max(2.0F, rowH * 0.1F), rowH * 0.64F, 1, withA(col, op));
    std::tm tm{};
    const std::time_t st = static_cast<std::time_t>(e.start);
    localtime_r(&st, &tm);
    const std::string when = e.allDay ? std::string("·") : std::format("{:02}:{:02}", tm.tm_hour, tm.tm_min);
    c.text(when, time, pad + rowH * 0.35F, y + (rowH - Canvas::measure(when, time).h) / 2, withA(m_ink, 0.65F * op));
    c.text(e.title, row, pad + rowH * 2.6F, y + (rowH - Canvas::measure(e.title, row).h) / 2, withA(m_ink, op));
  }
}

// What is coming: the next days' events, under a line for each day
void CalendarWidget::drawAgenda(Canvas& c, float w, float h, const Day& t) {
  const float op = static_cast<float>(m_cfg.opacity);
  const float pad = std::min(w, h) * 0.08F;
  const float unit = std::clamp(std::min(w * 0.06F, h * 0.055F), 9.0F, 26.0F);
  const TextStyle title{.family = m_cfg.font, .size = unit * 1.35F, .weight = 700};
  const TextStyle dayStyle{.family = m_cfg.font, .size = unit * 0.85F, .weight = 700, .letterSpacing = unit * 0.04F};
  const TextStyle time{.family = m_cfg.font, .size = unit * 0.85F, .weight = 600};
  const TextStyle what{.family = m_cfg.font, .size = unit, .weight = 500, .maxWidth = w - 2 * pad - unit * 4.6F, .maxLines = 1};
  const TextStyle where{.family = m_cfg.font, .size = unit * 0.78F, .weight = 500, .maxWidth = w - 2 * pad - unit * 4.6F, .maxLines = 1};
  float y = pad;
  c.text(m_es ? "Agenda" : "Agenda", title, pad, y, withA(m_ink, op));
  y += Canvas::measure("Ag", title).h + unit * 0.5F;
  const auto now = static_cast<int64_t>(std::time(nullptr));
  bool any = false;
  for (int k = 0; k < m_cfg.agendaDays && y < h - pad; ++k) {
    const year_month_day day{sys_days{ymd(t.y, t.m, t.d)} + days{k}};
    const int dy = static_cast<int>(day.year()), dm = static_cast<int>(static_cast<unsigned>(day.month())),
              dd = static_cast<int>(static_cast<unsigned>(day.day()));
    std::vector<const CalEvent*> ev;
    for (const CalEvent* e : eventsOn(dy, dm, dd))
      if (k > 0 || e->allDay || e->end > now || m_fixed.y) ev.push_back(e);
    if (ev.empty()) continue;
    any = true;
    const int wd = weekdayOf(day);
    std::string label = k == 0 ? (m_es ? "HOY" : "TODAY") : k == 1 ? (m_es ? "MAÑANA" : "TOMORROW")
                                                                    : std::format("{} {}", m_es ? kShortEs[wd] : kShortEn[wd], dd);
    for (char& ch : label) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    c.text(label, dayStyle, pad, y, withA(k == 0 ? m_accent : m_ink, (k == 0 ? 1.0F : 0.55F) * op));
    y += unit * 1.35F;
    for (const CalEvent* e : ev) {
      const float rh = e->place.empty() ? unit * 1.6F : unit * 2.35F;
      if (y + rh > h - pad * 0.6F) {
        y = h;  // out of room
        break;
      }
      const Color col = e->color.empty() ? m_accent : Color::fromHex(e->color);
      c.roundRect(pad, y + unit * 0.2F, std::max(2.5F, unit * 0.16F), rh - unit * 0.55F, 1.5F, withA(col, op));
      std::tm tm{};
      const std::time_t st = static_cast<std::time_t>(std::max(e->start, localMidnight(dy, dm, dd)));
      localtime_r(&st, &tm);
      const std::string when = e->allDay ? (m_es ? "Todo el día" : "All day") : std::format("{:02}:{:02}", tm.tm_hour, tm.tm_min);
      c.text(when, e->allDay ? TextStyle{.family = m_cfg.font, .size = unit * 0.66F, .weight = 600} : time, pad + unit * 0.6F, y + unit * 0.12F,
             withA(m_ink, 0.6F * op));
      c.text(e->title, what, pad + unit * 4.4F, y, withA(m_ink, op));
      if (!e->place.empty()) c.text(e->place, where, pad + unit * 4.4F, y + unit * 1.25F, withA(m_ink, 0.5F * op));
      y += rh;
    }
    y += unit * 0.45F;
  }
  if (!any) {
    const std::string none = m_sources.empty() ? (m_es ? "Sin calendarios: elige .ics, khal o Evolution en el inspector"
                                                       : "No calendars: pick .ics, khal or Evolution in the inspector")
                                               : (m_es ? "Nada en los próximos días" : "Nothing in the days ahead");
    const TextStyle st{.family = m_cfg.font, .size = unit * 0.9F, .weight = 500, .maxWidth = w - 2 * pad, .maxLines = 3};
    c.text(none, st, pad, y + unit * 0.4F, withA(m_ink, 0.55F * op));
  }
}

}  // namespace undershell
