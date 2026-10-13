// SPDX-License-Identifier: GPL-3.0-or-later
// The weather, from Open-Meteo (open-meteo.com: free, no account, no key)
// for a place each person writes themselves -- a city ("Quito, Ecuador") or
// coordinates ("-0.18, -78.47"); nothing locates them on its own. What goes
// out: the place's name, once, to Open-Meteo's geocoding (the answer is kept
// on disk), then only its coordinates rounded to 0.01° (about a kilometre)
// every half hour. Answers are kept on disk too, so a restart does not ask
// again. Fetched on a worker thread; the widgets redraw when it lands.
#pragma once

#include "jobs.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace undershell {

struct WeatherReport {
  enum class State { Empty, Loading, Ready, NotFound, Failed } state = State::Empty;
  std::string place;  // as the geocoder names it ("Quito, Ecuador"), or the coordinates
  bool imperial = false;
  double temp = 0, feels = 0, wind = 0;  // °C / °F, km/h / mph
  int humidity = 0, code = 0;            // code: WMO weather interpretation code
  bool day = true;
  struct Day {
    int y = 0, m = 0, d = 0, code = 0, rain = 0;  // rain: chance, %
    double hi = 0, lo = 0;
  };
  std::vector<Day> days;  // today first
  struct Hour {
    int64_t at = 0;  // unix seconds
    int code = 0;
    double temp = 0;
    bool day = true;
  };
  std::vector<Hour> hours;  // the next two days
  int utcOffset = 0;        // the place's, seconds
  int64_t fetched = 0;      // unix seconds
};

// a WMO code as words, and the picture that goes with it
std::string weatherText(int code, bool es);
enum class Sky { Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, Snow, Thunder };
Sky skyOf(int code);

class WeatherService {
public:
  explicit WeatherService(Jobs& jobs) : m_jobs(jobs) {}

  // The report for a place (fetching it when it is missing or older than
  // half an hour). `lang` names places in the geocoder's answer.
  const WeatherReport& want(const std::string& location, bool imperial, bool es);
  [[nodiscard]] uint64_t generation() const { return m_generation; }
  // tests: this report for this place, as if fetched just now
  void setForTest(const std::string& location, bool imperial, bool es, WeatherReport r);

  static constexpr int64_t kRefresh = 30 * 60;  // seconds

  // exposed for tests: an Open-Meteo forecast answer read into a report
  static bool parseForecast(const std::string& json, WeatherReport& out);
  // "lat, lon" written by hand; false for a place name
  static bool parseCoordinates(const std::string& s, double& lat, double& lon);

private:
  struct Entry {
    WeatherReport report;
    bool busy = false;
    int64_t retryAt = 0;  // unix seconds: after a failure
    int failures = 0;
  };
  void fetch(const std::string& key, const std::string& location, bool imperial, bool es);

  Jobs& m_jobs;
  std::map<std::string, Entry> m_entries;
  uint64_t m_generation = 0;
};

}  // namespace undershell
