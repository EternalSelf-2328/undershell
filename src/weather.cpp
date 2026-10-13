// SPDX-License-Identifier: GPL-3.0-or-later
// The weather widget (see weather.hpp).
#include "weather.hpp"

#include "i18n.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>

namespace undershell {

namespace {
template <typename T>
T get(const toml::table& t, std::string_view key, T def) {
  if (auto v = t[key].value<T>()) return *v;
  return def;
}

Color withA(Color c, float a) {
  c.a *= a;
  return c;
}

void centred(Canvas& c, const std::string& s, const TextStyle& st, float cx, float cy, Color col) {
  const auto sz = Canvas::measure(s, st);
  c.text(s, st, cx - sz.w / 2, cy - sz.h / 2, col);
}

std::string degrees(double t) { return std::format("{}°", static_cast<int>(std::lround(t))); }

const char* kShortEs[] = {"Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb"};
const char* kShortEn[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

int weekdayOf(int y, int m, int d) {
  std::tm tm{};
  tm.tm_year = y - 1900;
  tm.tm_mon = m - 1;
  tm.tm_mday = d;
  tm.tm_hour = 12;
  timegm(&tm);
  return tm.tm_wday;
}
}  // namespace

WeatherConfig WeatherConfig::fromTable(const toml::table& t) {
  WeatherConfig c;
  c.location = get<std::string>(t, "location", c.location);
  c.layout = get<std::string>(t, "layout", c.layout);
  c.units = get<std::string>(t, "units", c.units);
  c.plate = get<std::string>(t, "plate", c.plate);
  c.accent = get<std::string>(t, "accent", c.accent);
  c.ink = get<std::string>(t, "ink", c.ink);
  c.font = get<std::string>(t, "font", c.font);
  c.icons = get<std::string>(t, "icons", c.icons);
  c.language = get<std::string>(t, "language", c.language);
  c.place = get<bool>(t, "place", c.place);
  if (auto d = t["opacity"].value<double>()) c.opacity = std::clamp(*d, 0.0, 1.0);
  return c;
}

void WeatherWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  m_cfg = WeatherConfig::fromTable(cfg.options);
  m_accent = noct.color(m_cfg.accent, Color::fromHex("#e2342a"));
  m_ink = noct.color(m_cfg.ink, Color::fromHex("#f4f1ea"));
  m_surface = noct.color("surface", Color::fromHex("#16161a"));
  m_es = m_cfg.language == "es" || (m_cfg.language != "en" && spanishUi());
}

void WeatherWidget::tick(const TickContext& ctx) {
  if (ctx.weather) m_report = ctx.weather->want(m_cfg.location, m_cfg.units == "imperial", m_es);
}

double WeatherWidget::nextWakeup(double now) const {
  // the next hour (the hours move on), or when the report gets old
  const auto t = static_cast<int64_t>(std::time(nullptr));
  int64_t next = (t / 3600 + 1) * 3600 + 2;
  if (m_report.state == WeatherReport::State::Ready) next = std::min(next, m_report.fetched + WeatherService::kRefresh + 1);
  return now + static_cast<double>(std::max<int64_t>(5, next - t));
}

void WeatherWidget::drawSky(Canvas& c, Sky sky, bool day, float cx, float cy, float r, bool color, Color ink, Color accent, float op) {
  const Color sun = color ? Color{1.0F, 0.78F, 0.27F, op} : withA(ink, op);
  const Color moon = color ? Color{0.93F, 0.91F, 0.80F, op} : withA(ink, op);
  const Color cloud = withA(color ? Color{0.90F, 0.92F, 0.96F, 1} : ink, op);
  const Color dark = withA(color ? Color{0.62F, 0.66F, 0.74F, 1} : ink, 0.85F * op);
  const Color drop = color ? Color{0.42F, 0.70F, 1.0F, op} : withA(ink, op);
  const Color bolt = color ? Color{1.0F, 0.85F, 0.25F, op} : withA(accent, op);
  (void)accent;
  auto body = [&](float x, float y, float s) {  // the sun or the moon
    if (day) {
      c.circle(x, y, s * 0.5F, sun);
      for (int k = 0; k < 8; ++k) {
        const float a = static_cast<float>(k) * 0.7854F;
        c.segment(x + std::cos(a) * s * 0.7F, y + std::sin(a) * s * 0.7F, x + std::cos(a) * s * 0.92F, y + std::sin(a) * s * 0.92F,
                  std::max(1.2F, s * 0.12F), sun);
      }
    } else {
      // a crescent: a thick arc
      c.arc(x, y, s * 0.42F, s * 0.38F, 3.6F, 3.6F + 3.5F, moon, true);
    }
  };
  auto cloudAt = [&](float x, float y, float s, Color col) {  // a cloud, (x, y) its middle, s its width
    c.circle(x - s * 0.22F, y + s * 0.02F, s * 0.2F, col);
    c.circle(x + 0.02F * s, y - s * 0.1F, s * 0.27F, col);
    c.circle(x + s * 0.26F, y + s * 0.04F, s * 0.18F, col);
    c.roundRect(x - s * 0.42F, y + s * 0.02F, s * 0.84F, s * 0.2F, s * 0.1F, col);
  };
  switch (sky) {
    case Sky::Clear: body(cx, cy, r * 1.1F); break;
    case Sky::PartlyCloudy:
      body(cx - r * 0.3F, cy - r * 0.3F, r * 0.85F);
      cloudAt(cx + r * 0.15F, cy + r * 0.2F, r * 1.5F, cloud);
      break;
    case Sky::Cloudy:
      cloudAt(cx + r * 0.3F, cy - r * 0.25F, r * 1.1F, dark);
      cloudAt(cx - r * 0.1F, cy + r * 0.1F, r * 1.6F, cloud);
      break;
    case Sky::Fog:
      for (int k = 0; k < 3; ++k) {
        const float y = cy - r * 0.45F + static_cast<float>(k) * r * 0.45F, inset = (k % 2) * r * 0.25F;
        c.segment(cx - r * 0.85F + inset, y, cx + r * 0.85F - inset * 0.5F, y, std::max(1.5F, r * 0.16F), cloud);
      }
      break;
    case Sky::Drizzle:
    case Sky::Rain:
    case Sky::Snow:
    case Sky::Thunder: {
      cloudAt(cx, cy - r * 0.3F, r * 1.7F, sky == Sky::Thunder ? dark : cloud);
      const float y0 = cy + r * 0.25F;
      for (int k = 0; k < 3; ++k) {
        const float x = cx - r * 0.45F + static_cast<float>(k) * r * 0.45F;
        if (sky == Sky::Snow) {
          c.circle(x, y0 + r * 0.25F + (k % 2) * r * 0.25F, std::max(1.5F, r * 0.11F), cloud);
        } else if (sky == Sky::Drizzle) {
          c.circle(x, y0 + r * 0.22F + (k % 2) * r * 0.25F, std::max(1.2F, r * 0.08F), drop);
        } else if (sky == Sky::Rain) {
          c.segment(x + r * 0.1F, y0 + r * 0.05F, x - r * 0.1F, y0 + r * 0.55F, std::max(1.3F, r * 0.11F), drop);
        }
      }
      if (sky == Sky::Thunder) {
        c.triangle(cx + r * 0.1F, y0 - r * 0.1F, cx - r * 0.3F, y0 + r * 0.4F, cx + r * 0.02F, y0 + r * 0.4F, bolt);
        c.triangle(cx - r * 0.02F, y0 + r * 0.3F, cx + r * 0.3F, y0 + r * 0.3F, cx - r * 0.2F, y0 + r * 0.85F, bolt);
      }
      break;
    }
  }
}

void WeatherWidget::drawMessage(Canvas& c, float w, float h, const std::string& text) {
  const TextStyle st{.family = m_cfg.font, .size = std::clamp(std::min(w, h) * 0.12F, 11.0F, 22.0F), .weight = 500, .maxWidth = w * 0.86F,
                     .maxLines = 4, .align = 1};
  const auto sz = Canvas::measure(text, st);
  c.text(text, st, (w - st.maxWidth) / 2, (h - sz.h) / 2, withA(m_ink, 0.7F * static_cast<float>(m_cfg.opacity)));
}

void WeatherWidget::drawNow(Canvas& c, float x, float y, float w, float h, bool details) {
  const float op = static_cast<float>(m_cfg.opacity);
  const WeatherReport& r = m_report;
  const float icon = std::min(h * 0.82F, w * 0.36F);
  drawSky(c, skyOf(r.code), r.day, x + icon / 2, y + h / 2, icon * 0.38F, m_cfg.icons != "mono", m_ink, m_accent, op);
  const float tx = x + icon + std::min(w, h) * 0.08F, tw = w - (tx - x);
  const TextStyle big{.family = m_cfg.font, .size = std::min(h * 0.46F, tw * 0.42F), .weight = 600};
  const TextStyle mid{.family = m_cfg.font, .size = std::min(h * 0.15F, tw * 0.12F), .weight = 600, .maxWidth = tw, .maxLines = 1};
  const TextStyle small{.family = m_cfg.font, .size = std::min(h * 0.12F, tw * 0.1F), .weight = 500, .maxWidth = tw, .maxLines = 1};
  const std::string temp = degrees(r.temp);
  const auto ts = Canvas::measure(temp, big);
  const std::string sky = weatherText(r.code, m_es);
  const auto ss = Canvas::measure(sky, mid);
  const bool place = m_cfg.place && !r.place.empty();
  const float block = ts.h * 0.86F + ss.h + (place ? small.size * 1.3F : 0);
  float ty = y + (h - block) / 2;
  c.text(temp, big, tx, ty - ts.h * 0.08F, withA(m_ink, op));
  if (details && !r.days.empty()) {  // today's high and low beside it
    const TextStyle hl{.family = m_cfg.font, .size = big.size * 0.3F, .weight = 600};
    c.text(std::format("↑{}  ↓{}", degrees(r.days[0].hi), degrees(r.days[0].lo)), hl, tx + ts.w + big.size * 0.15F,
           ty + ts.h * 0.42F, withA(m_ink, 0.6F * op));
  }
  ty += ts.h * 0.86F;
  c.text(sky, mid, tx, ty, withA(m_accent, op));
  ty += ss.h;
  if (place) c.text(r.place, small, tx, ty + small.size * 0.15F, withA(m_ink, 0.55F * op));
}

void WeatherWidget::drawHours(Canvas& c, float x, float y, float w, float h, int count) {
  const float op = static_cast<float>(m_cfg.opacity);
  const auto now = static_cast<int64_t>(std::time(nullptr));
  std::vector<WeatherReport::Hour> hs;
  for (const auto& hr : m_report.hours)
    if (hr.at >= now - 1800 && static_cast<int>(hs.size()) < count) hs.push_back(hr);
  if (hs.empty()) return;
  const float cw = w / static_cast<float>(hs.size());
  const TextStyle lab{.family = m_cfg.font, .size = std::min(h * 0.17F, cw * 0.3F), .weight = 500};
  const TextStyle tmp{.family = m_cfg.font, .size = std::min(h * 0.2F, cw * 0.34F), .weight = 600};
  for (size_t i = 0; i < hs.size(); ++i) {
    const float cx = x + cw * (static_cast<float>(i) + 0.5F);
    const int64_t local = hs[i].at + m_report.utcOffset;
    const int hour = static_cast<int>(((local % 86400) + 86400) % 86400 / 3600);
    const std::string when = i == 0 ? (m_es ? "Ahora" : "Now") : std::format("{:02}h", hour);
    centred(c, when, lab, cx, y + h * 0.13F, withA(m_ink, (i == 0 ? 0.9F : 0.55F) * op));
    drawSky(c, skyOf(hs[i].code), hs[i].day, cx, y + h * 0.5F, std::min(cw, h) * 0.25F, m_cfg.icons != "mono", m_ink, m_accent, op);
    centred(c, degrees(hs[i].temp), tmp, cx, y + h * 0.86F, withA(m_ink, op));
  }
}

void WeatherWidget::drawWeek(Canvas& c, float x, float y, float w, float h) {
  const float op = static_cast<float>(m_cfg.opacity);
  const size_t n = std::min<size_t>(7, m_report.days.size());
  if (n == 0) return;
  const float cw = w / static_cast<float>(n);
  const TextStyle lab{.family = m_cfg.font, .size = std::min(h * 0.13F, cw * 0.26F), .weight = 600};
  const TextStyle hi{.family = m_cfg.font, .size = std::min(h * 0.14F, cw * 0.28F), .weight = 600};
  const TextStyle lo{.family = m_cfg.font, .size = std::min(h * 0.12F, cw * 0.24F), .weight = 500};
  for (size_t i = 0; i < n; ++i) {
    const auto& d = m_report.days[i];
    const float cx = x + cw * (static_cast<float>(i) + 0.5F);
    const std::string name = i == 0 ? (m_es ? "Hoy" : "Today") : (m_es ? kShortEs : kShortEn)[weekdayOf(d.y, d.m, d.d)];
    if (i == 0) c.roundRect(cx - cw * 0.45F, y, cw * 0.9F, h, std::min(cw, h) * 0.18F, withA(m_accent, 0.14F * op));
    centred(c, name, lab, cx, y + h * 0.12F, withA(i == 0 ? m_accent : m_ink, (i == 0 ? 1.0F : 0.7F) * op));
    drawSky(c, skyOf(d.code), true, cx, y + h * 0.4F, std::min(cw, h) * 0.2F, m_cfg.icons != "mono", m_ink, m_accent, op);
    centred(c, degrees(d.hi), hi, cx, y + h * 0.68F, withA(m_ink, op));
    centred(c, degrees(d.lo), lo, cx, y + h * 0.84F, withA(m_ink, 0.5F * op));
    if (d.rain >= 20) {  // a chance of rain worth saying
      const TextStyle pr{.family = m_cfg.font, .size = lo.size * 0.8F, .weight = 600};
      centred(c, std::format("{}%", d.rain), pr, cx, y + h * 0.96F, Color{0.42F, 0.70F, 1.0F, 0.9F * op});
    }
  }
}

void WeatherWidget::draw(const DrawContext& ctx) {
  Canvas& c = m_canvas;
  c.begin(ctx.w, ctx.h, ctx.scale, ctx.text);
  const float W = ctx.w, H = ctx.h, op = static_cast<float>(m_cfg.opacity);
  if (m_cfg.plate != "none")
    c.roundRect(0, 0, W, H, std::min(W, H) * 0.1F, withA(m_surface.mix(m_accent, 0.05F), (m_cfg.plate == "solid" ? 0.94F : 0.58F) * op), 1,
                withA(m_ink, 0.1F * op));
  const float pad = std::min(W, H) * 0.09F;
  const float x = pad, y = pad, w = W - 2 * pad, h = H - 2 * pad;
  using S = WeatherReport::State;
  switch (m_report.state) {
    case S::Empty:
      drawMessage(c, W, H, m_es ? "Escribe tu ciudad en el inspector (Ubicación)" : "Type your city in the inspector (Location)");
      return;
    case S::Loading: drawMessage(c, W, H, m_es ? "Buscando el clima…" : "Fetching the weather…"); return;
    case S::NotFound:
      drawMessage(c, W, H, std::format("{} «{}»", m_es ? "No encontré" : "Could not find", m_cfg.location));
      return;
    case S::Failed: drawMessage(c, W, H, m_es ? "Sin conexión con Open-Meteo" : "No answer from Open-Meteo"); return;
    case S::Ready: break;
  }
  if (m_cfg.layout == "week") {
    drawWeek(c, x, y, w, h);
  } else if (m_cfg.layout == "hours") {
    drawHours(c, x, y, w, h, std::clamp(static_cast<int>(w / std::max(40.0F, h * 0.55F)), 3, 12));
  } else if (m_cfg.layout == "card") {
    drawNow(c, x, y, w, h * 0.52F, true);
    // a line of feel, humidity and wind, then the next hours
    const TextStyle st{.family = m_cfg.font, .size = std::min(h * 0.065F, w * 0.045F), .weight = 500};
    const std::string line = std::format("{} {}   ·   {} {}%   ·   {} {} {}", m_es ? "Sensación" : "Feels", degrees(m_report.feels),
                                         m_es ? "Humedad" : "Humidity", m_report.humidity, m_es ? "Viento" : "Wind",
                                         static_cast<int>(std::lround(m_report.wind)), m_report.imperial ? "mph" : "km/h");
    centred(c, line, st, x + w / 2, y + h * 0.6F, withA(m_ink, 0.6F * op));
    c.segment(x + w * 0.04F, y + h * 0.67F, x + w * 0.96F, y + h * 0.67F, 1, withA(m_ink, 0.1F * op), false);
    drawHours(c, x, y + h * 0.7F, w, h * 0.3F, std::clamp(static_cast<int>(w / std::max(40.0F, h * 0.17F)), 3, 8));
  } else {
    drawNow(c, x, y, w, h, false);
  }
  if (ctx.text) ctx.text->collect(120);
}

}  // namespace undershell
