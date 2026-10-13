// SPDX-License-Identifier: GPL-3.0-or-later
// iCalendar events (see ics.hpp). Times go through GLib's GDateTime, so a
// repeating event keeps its wall-clock time across daylight-saving changes.
#include "ics.hpp"

#include <glib.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace undershell {

namespace {
struct Prop {
  std::string name, value;
  std::map<std::string, std::string> params;
};

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

// a property line: NAME;PARAM=VALUE;PARAM="VALUE":the value
bool parseLine(const std::string& line, Prop& p) {
  bool quoted = false;
  size_t colon = std::string::npos;
  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '"') quoted = !quoted;
    else if (line[i] == ':' && !quoted) {
      colon = i;
      break;
    }
  }
  if (colon == std::string::npos) return false;
  const std::string head = line.substr(0, colon);
  p.value = line.substr(colon + 1);
  p.params.clear();
  size_t semi = head.find(';');
  p.name = upper(head.substr(0, semi));
  while (semi != std::string::npos) {
    const size_t next = head.find(';', semi + 1);
    const std::string kv = head.substr(semi + 1, next == std::string::npos ? std::string::npos : next - semi - 1);
    const size_t eq = kv.find('=');
    if (eq != std::string::npos) {
      std::string v = kv.substr(eq + 1);
      if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
      p.params[upper(kv.substr(0, eq))] = v;
    }
    semi = next;
  }
  return true;
}

// TEXT values: \n, \, \; and \\ escaped; newlines become spaces here
std::string unescape(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char c = s[++i];
      out += (c == 'n' || c == 'N') ? ' ' : c;
    } else {
      out += s[i];
    }
  }
  return out;
}

using DT = std::unique_ptr<GDateTime, decltype(&g_date_time_unref)>;
DT dt(GDateTime* p) { return DT(p, &g_date_time_unref); }

struct Zones {
  std::map<std::string, GTimeZone*> byId;
  GTimeZone* local = g_time_zone_new_local();
  GTimeZone* utc = g_time_zone_new_utc();
  ~Zones() {
    for (auto& [k, z] : byId)
      if (z) g_time_zone_unref(z);
    g_time_zone_unref(local);
    g_time_zone_unref(utc);
  }
  GTimeZone* get(const std::string& id) {
    if (id.empty()) return local;
    auto it = byId.find(id);
    if (it == byId.end()) {
      // an Olson name ("America/Guayaquil"); anything else (Outlook's
      // "Pacific Standard Time") is read as local time
      GTimeZone* z = g_time_zone_new_identifier(id.c_str());
      it = byId.emplace(id, z).first;
    }
    return it->second ? it->second : local;
  }
};

// a DATE or DATE-TIME value as a date-time in its zone; nullptr if unreadable
DT parseTime(const std::string& v, const std::map<std::string, std::string>& params, Zones& zones, bool& allDay) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
  allDay = false;
  if (v.size() >= 8 && std::sscanf(v.c_str(), "%4d%2d%2d", &y, &mo, &d) == 3) {
    const auto vt = params.find("VALUE");
    if (v.size() == 8 || (vt != params.end() && upper(vt->second) == "DATE")) {
      allDay = true;
      return dt(g_date_time_new(zones.local, y, mo, d, 0, 0, 0));
    }
    if (v.size() >= 15 && v[8] == 'T' && std::sscanf(v.c_str() + 9, "%2d%2d%2d", &h, &mi, &s) == 3) {
      GTimeZone* z = v.back() == 'Z' ? zones.utc : zones.get(params.count("TZID") ? params.at("TZID") : std::string());
      return dt(g_date_time_new(z, y, mo, d, h, mi, s));
    }
  }
  return dt(nullptr);
}

// "P1D", "PT1H30M", "P2W", "-PT15M"
int64_t parseDuration(const std::string& v) {
  int64_t total = 0, n = 0;
  bool neg = false;
  for (char c : v) {
    if (c == '-') neg = true;
    else if (std::isdigit(static_cast<unsigned char>(c))) n = n * 10 + (c - '0');
    else if (c == 'W') total += n * 7 * 86400, n = 0;
    else if (c == 'D') total += n * 86400, n = 0;
    else if (c == 'H') total += n * 3600, n = 0;
    else if (c == 'M') total += n * 60, n = 0;
    else if (c == 'S') total += n, n = 0;
  }
  return neg ? -total : total;
}

int weekdayCode(const std::string& c) {  // GLib's: Monday 1 .. Sunday 7
  static const char* names[] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
  for (int i = 0; i < 7; ++i)
    if (c == names[i]) return i + 1;
  return 0;
}

struct ByDay {
  int nth = 0, weekday = 0;  // nth: 0 every one, 2 the second, -1 the last
};

struct VEvent {
  std::vector<Prop> props;
};
}  // namespace

std::vector<CalEvent> parseIcs(const std::string& text, int64_t from, int64_t to, const std::string& color) {
  std::vector<CalEvent> out;
  // unfold: a line starting with a space or a tab continues the one before
  std::vector<std::string> lines;
  {
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) {
      if (!l.empty() && l.back() == '\r') l.pop_back();
      if (!l.empty() && (l[0] == ' ' || l[0] == '\t') && !lines.empty()) lines.back() += l.substr(1);
      else lines.push_back(std::move(l));
    }
  }
  std::vector<VEvent> events;
  std::string calColor = color;
  int depth = 0;
  bool inEvent = false;
  for (const std::string& l : lines) {
    Prop p;
    if (!parseLine(l, p)) continue;
    if (p.name == "BEGIN") {
      ++depth;
      if (upper(p.value) == "VEVENT") {
        inEvent = true;
        events.emplace_back();
      }
      continue;
    }
    if (p.name == "END") {
      --depth;
      if (upper(p.value) == "VEVENT") inEvent = false;
      continue;
    }
    if (inEvent) events.back().props.push_back(std::move(p));
    else if (depth == 1 && p.name == "X-APPLE-CALENDAR-COLOR" && p.value.size() >= 7 && p.value[0] == '#')
      calColor = p.value.substr(0, 7);
    if (events.size() > 50000) break;
  }

  Zones zones;
  // occurrences moved or cancelled one by one: (uid, the start it replaces)
  std::set<std::pair<std::string, int64_t>> moved;
  for (const auto& e : events)
    for (const auto& p : e.props)
      if (p.name == "RECURRENCE-ID") {
        std::string uid;
        for (const auto& q : e.props)
          if (q.name == "UID") uid = q.value;
        bool ad = false;
        if (DT t = parseTime(p.value, p.params, zones, ad)) moved.insert({uid, g_date_time_to_unix(t.get())});
      }

  for (const auto& e : events) {
    CalEvent ev;
    ev.color = calColor;
    DT start = dt(nullptr), end = dt(nullptr);
    int64_t duration = -1;
    std::string rrule, status;
    std::set<int64_t> exdates;
    bool isOverride = false;
    for (const auto& p : e.props) {
      bool ad = false;
      if (p.name == "DTSTART") {
        start = parseTime(p.value, p.params, zones, ad);
        ev.allDay = ad;
      } else if (p.name == "DTEND") {
        end = parseTime(p.value, p.params, zones, ad);
      } else if (p.name == "DURATION") {
        duration = parseDuration(p.value);
      } else if (p.name == "SUMMARY") {
        ev.title = unescape(p.value);
      } else if (p.name == "LOCATION") {
        ev.place = unescape(p.value);
      } else if (p.name == "UID") {
        ev.uid = p.value;
      } else if (p.name == "RRULE") {
        rrule = upper(p.value);
      } else if (p.name == "STATUS") {
        status = upper(p.value);
      } else if (p.name == "RECURRENCE-ID") {
        isOverride = true;
      } else if (p.name == "COLOR" && p.value.size() >= 7 && p.value[0] == '#') {
        ev.color = p.value.substr(0, 7);
      } else if (p.name == "EXDATE") {
        std::stringstream list(p.value);
        for (std::string one; std::getline(list, one, ',');)
          if (DT t = parseTime(one, p.params, zones, ad)) exdates.insert(g_date_time_to_unix(t.get()));
      }
    }
    if (!start || status == "CANCELLED") continue;
    if (ev.title.empty()) ev.title = "—";
    // how long it lasts: DTEND, DURATION, or a day / nothing
    int64_t length = 0;
    if (end) length = g_date_time_to_unix(end.get()) - g_date_time_to_unix(start.get());
    else if (duration >= 0) length = duration;
    else length = ev.allDay ? 86400 : 0;
    length = std::max<int64_t>(0, length);
    const int allDays = ev.allDay ? static_cast<int>(std::max<int64_t>(1, (length + 43200) / 86400)) : 0;
    auto add = [&](GDateTime* s) {
      const int64_t t = g_date_time_to_unix(s);
      if (exdates.count(t) || (!isOverride && moved.count({ev.uid, t}))) return;
      CalEvent o = ev;
      o.start = t;
      if (ev.allDay) {
        DT e2 = dt(g_date_time_add_days(s, allDays));
        o.end = g_date_time_to_unix(e2.get());
      } else {
        o.end = t + length;
      }
      if (o.start < to && std::max(o.end, o.start + 1) > from) out.push_back(std::move(o));
    };
    if (rrule.empty() || isOverride) {
      add(start.get());
      continue;
    }
    // the rule's parts
    std::map<std::string, std::string> rule;
    {
      std::stringstream parts(rrule);
      for (std::string kv; std::getline(parts, kv, ';');)
        if (const size_t eq = kv.find('='); eq != std::string::npos) rule[kv.substr(0, eq)] = kv.substr(eq + 1);
    }
    const std::string freq = rule["FREQ"];
    const int interval = std::max(1, std::atoi(rule.count("INTERVAL") ? rule["INTERVAL"].c_str() : "1"));
    const int count = rule.count("COUNT") ? std::atoi(rule["COUNT"].c_str()) : -1;
    int64_t until = INT64_MAX;
    if (rule.count("UNTIL")) {
      bool ad = false;
      std::map<std::string, std::string> none;
      if (DT u = parseTime(rule["UNTIL"], none, zones, ad)) until = g_date_time_to_unix(u.get()) + (ad ? 86399 : 0);
    }
    std::vector<ByDay> byDay;
    if (rule.count("BYDAY")) {
      std::stringstream days(rule["BYDAY"]);
      for (std::string d; std::getline(days, d, ',');) {
        if (d.size() < 2) continue;
        ByDay b;
        b.weekday = weekdayCode(d.substr(d.size() - 2));
        b.nth = d.size() > 2 ? std::atoi(d.substr(0, d.size() - 2).c_str()) : 0;
        if (b.weekday) byDay.push_back(b);
      }
    }
    std::vector<int> byMonthDay;
    if (rule.count("BYMONTHDAY")) {
      std::stringstream days(rule["BYMONTHDAY"]);
      for (std::string d; std::getline(days, d, ',');)
        if (const int v = std::atoi(d.c_str()); v != 0) byMonthDay.push_back(v);
    }
    GDateTime* s0 = start.get();
    GTimeZone* tz = g_date_time_get_timezone(s0);
    const int h = g_date_time_get_hour(s0), mi = g_date_time_get_minute(s0), sec = g_date_time_get_second(s0);
    const int64_t first = g_date_time_to_unix(s0);
    int made = 0;
    bool done = false;
    // one candidate day: counted when it is not before the start
    auto take = [&](int y, int m, int d) {
      if (done) return;
      DT c = dt(g_date_time_new(tz, y, m, d, h, mi, sec));
      if (!c) return;
      const int64_t t = g_date_time_to_unix(c.get());
      if (t < first) return;
      if (t > until || (count >= 0 && made >= count) || t >= to) {
        done = true;
        return;
      }
      ++made;
      add(c.get());
    };
    const int y0 = g_date_time_get_year(s0), m0 = g_date_time_get_month(s0), d0 = g_date_time_get_day_of_month(s0);
    // without COUNT, nothing before the window needs counting: start near it
    int startStep = 0;
    if (count < 0 && from > first + length) {
      const double gap = static_cast<double>(from - first - length);
      const double per = freq == "DAILY" ? 86400.0 : freq == "WEEKLY" ? 7 * 86400.0 : freq == "MONTHLY" ? 31 * 86400.0 : 366 * 86400.0;
      startStep = std::max(0, static_cast<int>(gap / per / interval) - 1);
    }
    for (int step = startStep; step < startStep + 5000 && !done; ++step) {
      if (freq == "DAILY") {
        DT c = dt(g_date_time_add_days(s0, step * interval));
        take(g_date_time_get_year(c.get()), g_date_time_get_month(c.get()), g_date_time_get_day_of_month(c.get()));
      } else if (freq == "WEEKLY") {
        // the week (Monday first) step weeks on, and its days the rule names
        DT monday = dt(g_date_time_add_days(s0, step * 7 * interval - (g_date_time_get_day_of_week(s0) - 1)));
        std::vector<int> wds;
        for (const auto& b : byDay) wds.push_back(b.weekday);
        if (wds.empty()) wds.push_back(g_date_time_get_day_of_week(s0));
        std::sort(wds.begin(), wds.end());
        for (int wd : wds) {
          DT c = dt(g_date_time_add_days(monday.get(), wd - 1));
          take(g_date_time_get_year(c.get()), g_date_time_get_month(c.get()), g_date_time_get_day_of_month(c.get()));
        }
      } else if (freq == "MONTHLY") {
        const int mm = m0 - 1 + step * interval;
        const int y = y0 + mm / 12, m = mm % 12 + 1;
        const int len = g_date_get_days_in_month(static_cast<GDateMonth>(m), static_cast<GDateYear>(y));
        std::vector<int> days;
        for (int v : byMonthDay) days.push_back(v > 0 ? v : len + 1 + v);
        for (const auto& b : byDay) {
          // the weekday of the 1st, then every one that matches (or the nth)
          DT firstDay = dt(g_date_time_new(tz, y, m, 1, 12, 0, 0));
          const int wd1 = g_date_time_get_day_of_week(firstDay.get());
          std::vector<int> all;
          for (int d = 1 + ((b.weekday - wd1 + 7) % 7); d <= len; d += 7) all.push_back(d);
          if (b.nth > 0 && b.nth <= static_cast<int>(all.size())) days.push_back(all[static_cast<size_t>(b.nth - 1)]);
          else if (b.nth < 0 && -b.nth <= static_cast<int>(all.size())) days.push_back(all[all.size() - static_cast<size_t>(-b.nth)]);
          else if (b.nth == 0) days.insert(days.end(), all.begin(), all.end());
        }
        if (days.empty()) days.push_back(d0);
        std::sort(days.begin(), days.end());
        for (int d : days)
          if (d >= 1 && d <= len) take(y, m, d);
      } else if (freq == "YEARLY") {
        const int y = y0 + step * interval;
        if (d0 <= g_date_get_days_in_month(static_cast<GDateMonth>(m0), static_cast<GDateYear>(y))) take(y, m0, d0);
      } else {
        add(s0);  // a rule this does not know: the first occurrence only
        break;
      }
    }
  }
  std::sort(out.begin(), out.end(), [](const CalEvent& a, const CalEvent& b) { return a.start < b.start; });
  return out;
}

}  // namespace undershell
