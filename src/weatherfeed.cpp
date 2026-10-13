// SPDX-License-Identifier: GPL-3.0-or-later
// The weather feed (see weatherfeed.hpp).
#include "weatherfeed.hpp"

#include "common.hpp"
#include "net.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>
#include <nlohmann/json.hpp>
#include <regex>

namespace undershell {

namespace {
using json = nlohmann::json;

int64_t unixNow() { return static_cast<int64_t>(std::time(nullptr)); }

std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  return s.substr(i);
}

// "2026-10-13T14:00" (or "2026-10-13") in the place's own time, as unix seconds
int64_t localToUnix(const std::string& t, int utcOffset) {
  std::tm tm{};
  int y = 0, mo = 0, d = 0, h = 0, mi = 0;
  if (std::sscanf(t.c_str(), "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) < 3) return 0;
  tm.tm_year = y - 1900;
  tm.tm_mon = mo - 1;
  tm.tm_mday = d;
  tm.tm_hour = h;
  tm.tm_min = mi;
  return static_cast<int64_t>(timegm(&tm)) - utcOffset;
}

template <typename T>
T at(const json& a, size_t i, T def) {
  if (!a.is_array() || i >= a.size() || a[i].is_null()) return def;
  try {
    return a[i].get<T>();
  } catch (const std::exception&) {
    return def;
  }
}

template <typename T>
T field(const json& o, const char* key, T def) {
  if (!o.is_object() || !o.contains(key) || o[key].is_null()) return def;
  try {
    return o[key].get<T>();
  } catch (const std::exception&) {
    return def;
  }
}
}  // namespace

std::string weatherText(int c, bool es) {
  struct Row {
    std::initializer_list<int> codes;
    const char* en;
    const char* es;
  };
  static const Row rows[] = {
      {{0}, "Clear", "Despejado"},
      {{1}, "Mostly clear", "Casi despejado"},
      {{2}, "Partly cloudy", "Parcialmente nublado"},
      {{3}, "Overcast", "Nublado"},
      {{45, 48}, "Fog", "Niebla"},
      {{51, 53, 55}, "Drizzle", "Llovizna"},
      {{56, 57}, "Freezing drizzle", "Llovizna helada"},
      {{61}, "Light rain", "Lluvia ligera"},
      {{63}, "Rain", "Lluvia"},
      {{65}, "Heavy rain", "Lluvia fuerte"},
      {{66, 67}, "Freezing rain", "Lluvia helada"},
      {{71}, "Light snow", "Nevada ligera"},
      {{73}, "Snow", "Nieve"},
      {{75}, "Heavy snow", "Nevada fuerte"},
      {{77}, "Snow grains", "Granizo fino"},
      {{80, 81}, "Showers", "Chubascos"},
      {{82}, "Heavy showers", "Chubascos fuertes"},
      {{85, 86}, "Snow showers", "Chubascos de nieve"},
      {{95}, "Thunderstorm", "Tormenta"},
      {{96, 99}, "Thunderstorm, hail", "Tormenta con granizo"},
  };
  for (const auto& r : rows)
    for (int k : r.codes)
      if (k == c) return es ? r.es : r.en;
  return es ? "—" : "—";
}

Sky skyOf(int c) {
  if (c == 0 || c == 1) return Sky::Clear;
  if (c == 2) return Sky::PartlyCloudy;
  if (c == 3) return Sky::Cloudy;
  if (c == 45 || c == 48) return Sky::Fog;
  if (c >= 51 && c <= 57) return Sky::Drizzle;
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return Sky::Rain;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return Sky::Snow;
  if (c >= 95) return Sky::Thunder;
  return Sky::Cloudy;
}

bool WeatherService::parseCoordinates(const std::string& s, double& lat, double& lon) {
  static const std::regex re(R"(^\s*(-?\d{1,2}(?:\.\d+)?)\s*[,; ]\s*(-?\d{1,3}(?:\.\d+)?)\s*$)");
  std::smatch m;
  if (!std::regex_match(s, m, re)) return false;
  lat = std::stod(m[1]);
  lon = std::stod(m[2]);
  return std::abs(lat) <= 90 && std::abs(lon) <= 180;
}

bool WeatherService::parseForecast(const std::string& text, WeatherReport& r) {
  json j;
  try {
    j = json::parse(text);
  } catch (const std::exception&) {
    return false;
  }
  if (!j.is_object() || !j.contains("current")) return false;
  r.utcOffset = field(j, "utc_offset_seconds", 0);
  const json& c = j["current"];
  r.temp = field(c, "temperature_2m", 0.0);
  r.feels = field(c, "apparent_temperature", r.temp);
  r.humidity = field(c, "relative_humidity_2m", 0);
  r.wind = field(c, "wind_speed_10m", 0.0);
  r.code = field(c, "weather_code", 0);
  r.day = field(c, "is_day", 1) != 0;
  r.hours.clear();
  if (j.contains("hourly")) {
    const json& h = j["hourly"];
    const json& t = h.contains("time") ? h["time"] : json::array();
    for (size_t i = 0; i < t.size(); ++i) {
      WeatherReport::Hour hr;
      hr.at = localToUnix(at<std::string>(t, i, ""), r.utcOffset);
      hr.temp = at(h.value("temperature_2m", json::array()), i, 0.0);
      hr.code = at(h.value("weather_code", json::array()), i, 0);
      hr.day = at(h.value("is_day", json::array()), i, 1) != 0;
      if (hr.at) r.hours.push_back(hr);
    }
  }
  r.days.clear();
  if (j.contains("daily")) {
    const json& d = j["daily"];
    const json& t = d.contains("time") ? d["time"] : json::array();
    for (size_t i = 0; i < t.size(); ++i) {
      WeatherReport::Day day;
      if (std::sscanf(at<std::string>(t, i, "").c_str(), "%d-%d-%d", &day.y, &day.m, &day.d) != 3) continue;
      day.code = at(d.value("weather_code", json::array()), i, 0);
      day.hi = at(d.value("temperature_2m_max", json::array()), i, 0.0);
      day.lo = at(d.value("temperature_2m_min", json::array()), i, 0.0);
      day.rain = at(d.value("precipitation_probability_max", json::array()), i, 0);
      r.days.push_back(day);
    }
  }
  return true;
}

const WeatherReport& WeatherService::want(const std::string& raw, bool imperial, bool es) {
  static const WeatherReport kEmpty;
  const std::string location = trim(raw);
  if (location.empty()) return kEmpty;
  const std::string key = std::format("{}\x1f{}\x1f{}", location, imperial ? 1 : 0, es ? "es" : "en");
  const bool fresh = !m_entries.count(key);
  Entry& e = m_entries[key];
  const int64_t now = unixNow();
  if (fresh) {
    // what an earlier run fetched
    try {
      const std::string cached = readFile(cacheDir("weather") + "/" + hashOf(key) + ".json");
      if (!cached.empty()) {
        const json c = json::parse(cached);
        WeatherReport r;
        if (parseForecast(c.value("forecast", std::string()), r)) {
          r.state = WeatherReport::State::Ready;
          r.place = c.value("place", location);
          r.fetched = c.value("fetched", int64_t{0});
          r.imperial = imperial;
          e.report = std::move(r);
        }
      }
    } catch (const std::exception&) {
    }
  }
  const bool stale = e.report.state != WeatherReport::State::Ready || now - e.report.fetched >= kRefresh;
  if (stale && !e.busy && now >= e.retryAt) {
    e.busy = true;
    if (e.report.state != WeatherReport::State::Ready) e.report.state = WeatherReport::State::Loading;
    fetch(key, location, imperial, es);
  }
  return e.report;
}

void WeatherService::setForTest(const std::string& location, bool imperial, bool es, WeatherReport r) {
  const std::string key = std::format("{}\x1f{}\x1f{}", trim(location), imperial ? 1 : 0, es ? "es" : "en");
  r.fetched = unixNow();
  r.state = WeatherReport::State::Ready;
  m_entries[key].report = std::move(r);
}

void WeatherService::fetch(const std::string& key, const std::string& location, bool imperial, bool es) {
  m_jobs.run([this, key, location, imperial, es]() -> Jobs::Done {
    WeatherReport r;
    r.imperial = imperial;
    double lat = 0, lon = 0;
    bool found = parseCoordinates(location, lat, lon);
    r.place = location;
    if (!found) {
      // the place's name, once: its answer is kept
      const std::string geoFile = cacheDir("weather") + "/geo-" + hashOf(location + "\x1f" + (es ? "es" : "en")) + ".json";
      std::string body = readFile(geoFile);
      const bool cached = !body.empty();
      if (!cached && !httpGet("https://geocoding-api.open-meteo.com/v1/search?count=1&format=json&language=" +
                                  std::string(es ? "es" : "en") + "&name=" + urlEncode(location),
                              body))
        body.clear();
      try {
        const json g = json::parse(body.empty() ? "{}" : body);
        if (g.contains("results") && g["results"].is_array() && !g["results"].empty()) {
          const json& p = g["results"][0];
          lat = field(p, "latitude", 0.0);
          lon = field(p, "longitude", 0.0);
          const std::string name = field(p, "name", location), country = field(p, "country", std::string());
          r.place = country.empty() ? name : name + ", " + country;
          found = true;
          if (!cached) writeFileAtomic(geoFile, body);
        } else if (!body.empty()) {
          r.state = WeatherReport::State::NotFound;  // asked, and no such place
        }
      } catch (const std::exception&) {
      }
    }
    std::string forecast;
    if (found) {
      // about a kilometre is all the forecast needs to know
      const double rlat = std::round(lat * 100) / 100, rlon = std::round(lon * 100) / 100;
      const std::string url = std::format(
          "https://api.open-meteo.com/v1/forecast?latitude={:.2f}&longitude={:.2f}"
          "&current=temperature_2m,apparent_temperature,relative_humidity_2m,is_day,weather_code,wind_speed_10m"
          "&hourly=temperature_2m,weather_code,is_day&daily=weather_code,temperature_2m_max,temperature_2m_min,"
          "precipitation_probability_max&timezone=auto&forecast_days=7&forecast_hours=48{}",
          rlat, rlon, imperial ? "&temperature_unit=fahrenheit&wind_speed_unit=mph" : "");
      if (httpGet(url, forecast) && parseForecast(forecast, r)) {
        r.state = WeatherReport::State::Ready;
        r.fetched = unixNow();
        json c = {{"fetched", r.fetched}, {"place", r.place}, {"forecast", forecast}};
        writeFileAtomic(cacheDir("weather") + "/" + hashOf(key) + ".json", c.dump());
      } else {
        r.state = WeatherReport::State::Failed;
      }
    } else if (r.state != WeatherReport::State::NotFound) {
      r.state = WeatherReport::State::Failed;  // no answer from the geocoder
    }
    return [this, key, r = std::move(r)]() mutable {
      Entry& e = m_entries[key];
      e.busy = false;
      const int64_t now = unixNow();
      if (r.state == WeatherReport::State::Ready) {
        e.report = std::move(r);
        e.failures = 0;
        e.retryAt = 0;
      } else {
        // keep what was there (old weather beats none); try again later and later
        e.failures = std::min(e.failures + 1, 6);
        e.retryAt = now + (r.state == WeatherReport::State::NotFound ? 6 * 3600 : int64_t{60} << e.failures);
        if (e.report.state != WeatherReport::State::Ready) e.report.state = r.state, e.report.place = r.place;
      }
      ++m_generation;
    };
  });
}

}  // namespace undershell
