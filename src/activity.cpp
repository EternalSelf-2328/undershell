// SPDX-License-Identifier: GPL-3.0-or-later
// The activity widget (see activity.hpp).
#include "activity.hpp"

#include "common.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <functional>
#include <glib.h>
#include <sstream>
#include <thread>

namespace undershell {

namespace fs = std::filesystem;

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

std::string shellQuote(const std::string& s) {
  gchar* q = g_shell_quote(s.c_str());
  std::string out(q);
  g_free(q);
  return out;
}

float cellNoise(int64_t d) {  // a steady random per day, for the twinkle
  uint64_t x = static_cast<uint64_t>(d) * 0x9E3779B97F4A7C15ULL;
  x ^= x >> 31;
  x *= 0xBF58476D1CE4E5B9ULL;
  x ^= x >> 29;
  return static_cast<float>(x >> 40) / static_cast<float>(1ULL << 24);
}

void place(Canvas& c, const std::string& s, const TextStyle& st, float x, float y, int align, Color col) {
  const auto sz = Canvas::measure(s, st);
  c.text(s, st, align == 1 ? x - sz.w / 2 : align == 2 ? x - sz.w : x, y - sz.h / 2, col);
}
}  // namespace

ActivityConfig ActivityConfig::fromTable(const toml::table& t) {
  ActivityConfig c;
  c.layout = get<std::string>(t, "layout", c.layout) == "weeks" ? "weeks" : "grid";
  c.weeks = static_cast<int>(std::clamp<int64_t>(get<int64_t>(t, "weeks", c.weeks), 4, 53));
  if (auto* a = t["folders"].as_array()) {
    c.folders.clear();
    for (auto& v : *a)
      if (auto s = v.value<std::string>(); s && !s->empty()) c.folders.push_back(*s);
  } else if (auto s = t["folders"].value<std::string>(); s && !s->empty()) {
    c.folders.clear();  // the inspector's row: one or more, separated by commas
    std::istringstream in(*s);
    for (std::string f; std::getline(in, f, ',');) {
      const auto b = f.find_first_not_of(' '), e = f.find_last_not_of(' ');
      if (b != std::string::npos) c.folders.push_back(f.substr(b, e - b + 1));
    }
  }
  if (c.folders.empty()) c.folders = {"~"};
  c.author = get<std::string>(t, "author", c.author);
  c.plate = get<std::string>(t, "plate", c.plate);
  c.accent = get<std::string>(t, "accent", c.accent);
  c.ink = get<std::string>(t, "ink", c.ink);
  c.font = get<std::string>(t, "font", c.font);
  c.language = get<std::string>(t, "language", c.language);
  c.total = get<bool>(t, "show_total", c.total);
  if (auto d = t["react"].value<double>()) c.react = std::clamp(*d, 0.0, 1.0);
  if (auto d = t["opacity"].value<double>()) c.opacity = std::clamp(*d, 0.0, 1.0);
  return c;
}

std::vector<std::string> findRepositories(const std::vector<std::string>& folders, int depth) {
  static const char* kSkip[] = {"node_modules", "build", "target", "vendor", "venv", "dist", "__pycache__", "Steam", "snap"};
  std::vector<std::string> out;
  std::function<void(const fs::path&, int)> walk = [&](const fs::path& dir, int level) {
    if (out.size() >= 300) return;
    std::error_code ec;
    if (fs::exists(dir / ".git", ec)) {
      out.push_back(dir.string());
      return;  // its submodules and nested checkouts are its own business
    }
    if (level >= depth) return;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end && !ec; it.increment(ec)) {
      const std::string name = it->path().filename().string();
      if (name.empty() || name[0] == '.' || std::ranges::any_of(kSkip, [&](const char* s) { return name == s; })) continue;
      std::error_code e2;
      if (it->is_directory(e2) && !it->is_symlink(e2)) walk(it->path(), level + 1);
    }
  };
  for (const std::string& f : folders) walk(fs::path(expandHome(f)), 0);
  std::ranges::sort(out);
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

int64_t localDay(int64_t unixTime) {
  const std::time_t t = static_cast<std::time_t>(unixTime);
  std::tm tm{};
  localtime_r(&t, &tm);
  return static_cast<int64_t>(std::floor((static_cast<double>(unixTime) + static_cast<double>(tm.tm_gmtoff)) / 86400.0));
}

void countDays(const std::string& log, ActivityDays& days) {
  std::istringstream in(log);
  for (std::string line; std::getline(in, line);) {
    char* end = nullptr;
    const long long t = std::strtoll(line.c_str(), &end, 10);
    if (end != line.c_str() && t > 0) ++days[localDay(t)];
  }
}

void ActivityWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  m_cfg = ActivityConfig::fromTable(cfg.options);
  m_accent = noct.color(m_cfg.accent, Color::fromHex("#39d353"));
  m_ink = noct.color(m_cfg.ink, Color::fromHex("#f4f1ea"));
  m_surface = noct.color("surface", Color::fromHex("#16161a"));
  m_es = m_cfg.language == "es" || (m_cfg.language != "en" && spanishUi());
  std::string key = m_cfg.author + "|" + std::to_string(m_cfg.weeks);
  for (const auto& f : m_cfg.folders) key += "|" + f;
  if (key != m_key) {  // other folders, author or span: read again now
    m_key = key;
    m_nextScan = 0;
  }
}

void ActivityWidget::setDays(ActivityDays days, int64_t today) {
  m_fixed = true;
  m_days = std::move(days);
  m_repos = 1;
  m_today = today;
}

void ActivityWidget::startScan(double now) {
  auto scan = std::make_shared<Scan>();
  m_scan = scan;
  m_nextScan = now + 30 * 60;
  const ActivityConfig cfg = m_cfg;
  std::thread([scan, cfg] {
    std::string author;
    if (cfg.author.empty()) {
      author = runCommand("git config --global user.email", 3);
      while (!author.empty() && (author.back() == '\n' || author.back() == ' ')) author.pop_back();
    } else if (cfg.author != "all") {
      author = cfg.author;
    }
    const std::vector<std::string> repos = findRepositories(cfg.folders);
    ActivityDays days;
    const std::string since = std::to_string(cfg.weeks * 7 + 7) + ".days";
    for (const std::string& r : repos) {
      std::string cmd = "git -C " + shellQuote(r) + " log --all --no-merges --since=" + since + " --format=%at";
      if (!author.empty()) cmd += " --author=" + shellQuote(author);
      countDays(runCommand(cmd, 10), days);
    }
    std::lock_guard g(scan->mutex);
    scan->days = std::move(days);
    scan->repos = static_cast<int>(repos.size());
    scan->done = true;
  }).detach();
}

void ActivityWidget::tick(const TickContext& ctx) {
  if (!m_fixed) {
    if (m_scan) {
      std::lock_guard g(m_scan->mutex);
      if (m_scan->done) {
        m_days = std::move(m_scan->days);
        m_repos = m_scan->repos;
        m_scan.reset();
      }
    }
    if (!m_scan && ctx.now >= m_nextScan) startScan(ctx.now);
    m_today = localDay(static_cast<int64_t>(std::time(nullptr)));
  }
  // the music: a kick lights a scattering of cells
  if (m_cfg.react > 0) {
    if (ctx.audio.onsets && ctx.audio.kicks != m_kicks) {
      m_kicks = ctx.audio.kicks;
      m_beat = std::max(m_beat, static_cast<float>(std::clamp(ctx.audio.kickStrength, 0.4, 1.0)));
    }
    m_beat *= static_cast<float>(std::exp(-ctx.dt * 5));
  }
}

double ActivityWidget::nextWakeup(double now) const {
  if (m_scan) return now + 0.5;  // a scan running: look for its result soon
  // the next scan, or just after the next local midnight (a new day starts a cell)
  const auto t = static_cast<int64_t>(std::time(nullptr));
  const std::time_t tt = static_cast<std::time_t>(t);
  std::tm tm{};
  localtime_r(&tt, &tm);
  const int64_t midnight = (localDay(t) + 1) * 86400 - tm.tm_gmtoff;
  return std::min(m_nextScan, now + static_cast<double>(midnight - t) + 1);
}

void ActivityWidget::draw(const DrawContext& ctx) {
  Canvas& c = m_canvas;
  c.begin(ctx.w, ctx.h, ctx.scale, ctx.text);
  const float W = ctx.w, H = ctx.h, op = static_cast<float>(m_cfg.opacity);
  if (m_cfg.plate != "none")
    c.roundRect(0, 0, W, H, std::min(W, H) * 0.08F, withA(m_surface.mix(m_accent, 0.05F), (m_cfg.plate == "solid" ? 0.94F : 0.58F) * op), 1,
                withA(m_ink, 0.1F * op));
  const float pad = std::min(W, H) * 0.08F;
  float x0 = pad, y0 = pad, w = W - 2 * pad, h = H - 2 * pad;
  const Color dim = withA(m_ink, 0.62F * op);
  const TextStyle note{.family = m_cfg.font, .size = std::clamp(h * 0.11F, 9.0F, 18.0F), .weight = 500};
  if (m_repos <= 0) {
    std::string s = m_repos < 0 ? (m_es ? "Leyendo los repositorios…" : "Reading the repositories…")
                                : (m_es ? "No hay repositorios de git en esas carpetas" : "No git repositories in those folders");
    place(c, s, note, W / 2, H / 2, 1, dim);
    return;
  }
  // the span: whole weeks, Monday first, ending with this one
  const int weeks = m_cfg.weeks;
  const int64_t todayDow = ((m_today + 3) % 7 + 7) % 7;  // 1970-01-01 was a Thursday: Monday = 0
  const int64_t first = m_today - todayDow - 7 * (weeks - 1);
  int total = 0, peak = 0;
  for (int64_t d = first; d <= m_today; ++d)
    if (auto it = m_days.find(d); it != m_days.end() && it->second > 0) {
      total += it->second;
      peak = std::max(peak, it->second);
    }
  if (m_cfg.total && h > note.size * 3) {
    const std::string span = weeks >= 52 ? (m_es ? "en el último año" : "in the last year")
                                         : std::format("{} {} {}", m_es ? "en las últimas" : "in the last", weeks, m_es ? "semanas" : "weeks");
    place(c, std::format("{} {} {}", total, total == 1 ? "commit" : "commits", span), note, x0, y0 + note.size * 0.6F, 0, dim);
    y0 += note.size * 1.6F;
    h -= note.size * 1.6F;
  }
  // four levels, as a share of the busiest day (quartiles of the busy days
  // put a steady streak on the lowest level)
  auto level = [&](int n) {
    if (n <= 0) return 0;
    const double f = static_cast<double>(n) / std::max(1, peak);
    return f <= 0.25 ? 1 : f <= 0.5 ? 2 : f <= 0.75 ? 3 : 4;
  };
  static constexpr float kAlpha[5] = {0.09F, 0.32F, 0.55F, 0.78F, 1.0F};
  if (m_cfg.layout == "grid") {
    const float s = std::min(w / static_cast<float>(weeks), h / 7.0F);
    const float gx = x0 + (w - s * static_cast<float>(weeks)) / 2, gy = y0 + (h - s * 7) / 2;
    const float cell = s * 0.8F, r = s * 0.18F;
    for (int64_t d = first; d <= m_today; ++d) {
      const int col = static_cast<int>((d - first) / 7), row = static_cast<int>((d - first) % 7);
      const auto it = m_days.find(d);
      const int n = it == m_days.end() ? 0 : it->second;
      const int lv = level(n);
      float a = kAlpha[lv];
      if (lv > 0 && m_cfg.react > 0) a = std::min(1.0F, a + m_beat * static_cast<float>(m_cfg.react) * cellNoise(d) * 0.7F);
      c.roundRect(gx + static_cast<float>(col) * s, gy + static_cast<float>(row) * s, cell, cell, r, withA(lv == 0 ? m_ink : m_accent, a * op));
    }
  } else {  // a bar per week
    const float bw = w / static_cast<float>(weeks);
    std::vector<int> sums(static_cast<size_t>(weeks), 0);
    for (const auto& [d, n] : m_days)
      if (d >= first && d <= m_today) sums[static_cast<size_t>((d - first) / 7)] += n;
    const int most = std::max(1, *std::ranges::max_element(sums));
    for (int i = 0; i < weeks; ++i) {
      const float f = static_cast<float>(sums[static_cast<size_t>(i)]) / static_cast<float>(most);
      const float bh = std::max(bw * 0.25F, f * h);
      float a = sums[static_cast<size_t>(i)] > 0 ? 0.35F + 0.65F * f : kAlpha[0];
      if (sums[static_cast<size_t>(i)] > 0 && m_cfg.react > 0)
        a = std::min(1.0F, a + m_beat * static_cast<float>(m_cfg.react) * cellNoise(i) * 0.6F);
      c.roundRect(x0 + static_cast<float>(i) * bw + bw * 0.12F, y0 + h - bh, bw * 0.76F, bh, bw * 0.25F,
                  withA(sums[static_cast<size_t>(i)] > 0 ? m_accent : m_ink, a * op));
    }
  }
  if (ctx.text) ctx.text->collect(120);
}

}  // namespace undershell
