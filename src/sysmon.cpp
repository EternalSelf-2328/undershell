// SPDX-License-Identifier: GPL-3.0-or-later
// The system monitor widget (see sysmon.hpp).
#include "sysmon.hpp"

#include "i18n.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <sys/statvfs.h>

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

std::string firstLine(const std::string& path) {
  std::ifstream f(path);
  std::string s;
  std::getline(f, s);
  while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r')) s.pop_back();
  return s;
}

double number(const std::string& path, double def = -1) {
  const std::string s = firstLine(path);
  if (s.empty()) return def;
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  return end == s.c_str() || !std::isfinite(v) ? def : v;
}

bool exists(const std::string& p) {
  std::error_code ec;
  return fs::exists(p, ec);
}

// "1.2 GB", "340 MB"
std::string size(double b) {
  if (b >= 1e12) return std::format("{:.1f} TB", b / 1e12);
  if (b >= 1e9) return std::format("{:.1f} GB", b / 1e9);
  if (b >= 1e6) return std::format("{:.0f} MB", b / 1e6);
  return std::format("{:.0f} KB", b / 1e3);
}

// a rate, short enough for a ring: "1.2M", "40K"
std::string rate(double b, bool unit) {
  const char* s = unit ? "/s" : "";
  if (b >= 1e9) return std::format("{:.1f} GB{}", b / 1e9, s);
  if (b >= 1e6) return std::format("{:.1f} MB{}", b / 1e6, s);
  if (b >= 1e3) return std::format("{:.0f} KB{}", b / 1e3, s);
  return std::format("{:.0f} B{}", b, s);
}

std::string percent(double f) { return std::format("{}%", static_cast<int>(std::lround(std::clamp(f, 0.0, 1.0) * 100))); }

void place(Canvas& c, const std::string& s, const TextStyle& st, float x, float y, int align, Color col) {
  const auto sz = Canvas::measure(s, st);
  const float at = align == 1 ? x - sz.w / 2 : align == 2 ? x - sz.w : x;
  c.text(s, st, at, y - sz.h / 2, col);
}
}  // namespace

SystemConfig SystemConfig::fromTable(const toml::table& t) {
  SystemConfig c;
  c.layout = get<std::string>(t, "layout", c.layout);
  if (c.layout != "bars" && c.layout != "graph" && c.layout != "text") c.layout = "rings";
  c.cpu = get<bool>(t, "show_cpu", c.cpu);
  c.ram = get<bool>(t, "show_ram", c.ram);
  c.gpu = get<bool>(t, "show_gpu", c.gpu);
  c.temp = get<bool>(t, "show_temp", c.temp);
  c.net = get<bool>(t, "show_net", c.net);
  c.disk = get<bool>(t, "show_disk", c.disk);
  c.battery = get<bool>(t, "show_battery", c.battery);
  c.diskPath = get<std::string>(t, "disk_path", c.diskPath);
  if (c.diskPath.empty() || c.diskPath[0] != '/') c.diskPath = "/";
  if (auto d = t["interval"].value<double>()) c.interval = std::clamp(*d, 0.5, 10.0);
  c.plate = get<std::string>(t, "plate", c.plate);
  c.accent = get<std::string>(t, "accent", c.accent);
  c.ink = get<std::string>(t, "ink", c.ink);
  c.alert = get<std::string>(t, "alert", c.alert);
  c.font = get<std::string>(t, "font", c.font);
  c.language = get<std::string>(t, "language", c.language);
  c.fahrenheit = get<bool>(t, "fahrenheit", c.fahrenheit);
  if (auto d = t["opacity"].value<double>()) c.opacity = std::clamp(*d, 0.0, 1.0);
  return c;
}

// ── reading the machine ─────────────────────────────────────────────────────

void SystemReader::find() {
  m_found = true;
  std::error_code ec;
  // the GPU: a busy share (AMD), or the time asleep (Intel i915, Xe)
  if (fs::is_directory(path("/sys/class/drm"), ec))
    for (const auto& e : fs::directory_iterator(path("/sys/class/drm"), ec)) {
      const std::string n = e.path().filename().string();
      if (!n.starts_with("card") || n.find('-') != std::string::npos) continue;  // not a connector
      const std::string d = e.path().string();
      if (m_gpuBusy.empty() && exists(d + "/device/gpu_busy_percent")) m_gpuBusy = d + "/device/gpu_busy_percent";
      if (m_gpuIdle.empty() && exists(d + "/power/rc6_residency_ms")) m_gpuIdle = d + "/power/rc6_residency_ms";
      if (m_gpuIdle.empty() && exists(d + "/device/tile0/gt0/gtidle/idle_residency_ms"))
        m_gpuIdle = d + "/device/tile0/gt0/gtidle/idle_residency_ms";
    }
  if (!m_gpuBusy.empty()) m_gpuIdle.clear();  // a real busy share wins
  // the CPU package's temperature
  if (fs::is_directory(path("/sys/class/hwmon"), ec))
    for (const auto& e : fs::directory_iterator(path("/sys/class/hwmon"), ec)) {
      const std::string d = e.path().string(), name = firstLine(d + "/name");
      if (name != "coretemp" && name != "k10temp" && name != "zenpower") continue;
      std::string pick;
      for (int i = 1; i <= 8 && pick.empty(); ++i) {
        const std::string label = firstLine(std::format("{}/temp{}_label", d, i));
        if (label.starts_with("Package") || label == "Tctl" || label == "Tdie") pick = std::format("{}/temp{}_input", d, i);
      }
      if (pick.empty() && exists(d + "/temp1_input")) pick = d + "/temp1_input";
      if (!pick.empty()) {
        m_temp = pick;
        break;
      }
    }
  if (m_temp.empty() && fs::is_directory(path("/sys/class/thermal"), ec)) {
    std::string acpi;
    for (const auto& e : fs::directory_iterator(path("/sys/class/thermal"), ec)) {
      const std::string d = e.path().string(), type = firstLine(d + "/type");
      if (type == "x86_pkg_temp") m_temp = d + "/temp";
      else if (type == "acpitz" && acpi.empty()) acpi = d + "/temp";
    }
    if (m_temp.empty()) m_temp = acpi;
  }
  // the battery
  if (fs::is_directory(path("/sys/class/power_supply"), ec))
    for (const auto& e : fs::directory_iterator(path("/sys/class/power_supply"), ec)) {
      const std::string d = e.path().string();
      if (firstLine(d + "/type") == "Battery" && exists(d + "/capacity") && firstLine(d + "/scope") != "Device") {
        m_battery = d;  // (scope Device: a mouse's or a headset's)
        break;
      }
    }
}

SystemSample SystemReader::sample(double now, const std::string& diskPath) {
  if (!m_found) find();
  SystemSample s;
  const double dt = m_last < 0 ? 0 : now - m_last;
  m_last = now;
  // CPU: the share of time not idle since the last sample
  {
    std::ifstream f(path("/proc/stat"));
    std::vector<std::pair<double, double>> cores;
    for (std::string line; std::getline(f, line) && line.starts_with("cpu");) {
      std::istringstream ss(line.substr(line.find(' ')));
      double v[8] = {0};
      for (double& x : v) ss >> x;
      const double idle = v[3] + v[4], total = v[0] + v[1] + v[2] + v[3] + v[4] + v[5] + v[6] + v[7];
      if (line[3] == ' ') {
        if (m_total > 0 && total > m_total) s.cpu = std::clamp((total - idle - m_busy) / (total - m_total), 0.0, 1.0);
        m_busy = total - idle;
        m_total = total;
      } else {
        cores.push_back({total - idle, total});
      }
    }
    s.cores.assign(cores.size(), 0);
    for (size_t i = 0; i < cores.size() && i < m_cores.size(); ++i) {
      const double d = cores[i].second - m_cores[i].second;
      s.cores[i] = d > 0 ? std::clamp((cores[i].first - m_cores[i].first) / d, 0.0, 1.0) : 0;
    }
    m_cores = std::move(cores);
  }
  // memory
  {
    std::ifstream f(path("/proc/meminfo"));
    std::string key, unit;
    double v = 0, total = 0, avail = 0, st = 0, sf = 0;
    while (f >> key >> v) {
      std::getline(f, unit);
      if (key == "MemTotal:") total = v;
      else if (key == "MemAvailable:") avail = v;
      else if (key == "SwapTotal:") st = v;
      else if (key == "SwapFree:") sf = v;
    }
    s.memTotal = total * 1024;
    s.memUsed = (total - avail) * 1024;
    s.swapTotal = st * 1024;
    s.swapUsed = (st - sf) * 1024;
  }
  // GPU
  if (!m_gpuBusy.empty()) {
    const double b = number(m_gpuBusy);
    if (b >= 0) s.gpu = std::clamp(b / 100, 0.0, 1.0);
  } else if (!m_gpuIdle.empty()) {
    const double ms = number(m_gpuIdle);
    s.gpu = 0;
    if (ms >= 0 && m_idleMs >= 0 && dt > 0) s.gpu = std::clamp(1 - (ms - m_idleMs) / 1000 / dt, 0.0, 1.0);
    if (ms >= 0) m_idleMs = ms;
  }
  // temperature (millidegrees)
  if (!m_temp.empty())
    if (const double t = number(m_temp); t > -1000) s.tempC = t / 1000;
  // network: every interface but the loopback
  {
    std::ifstream f(path("/proc/net/dev"));
    double rx = 0, tx = 0;
    for (std::string line; std::getline(f, line);) {
      const auto colon = line.find(':');
      if (colon == std::string::npos) continue;
      std::string name = line.substr(0, colon);
      name.erase(0, name.find_first_not_of(' '));
      if (name == "lo") continue;
      std::istringstream ss(line.substr(colon + 1));
      double v[9] = {0};
      for (double& x : v) ss >> x;
      rx += v[0];
      tx += v[8];
    }
    if (m_rx >= 0 && dt > 0) {
      s.down = std::max(0.0, (rx - m_rx) / dt);
      s.up = std::max(0.0, (tx - m_tx) / dt);
    }
    m_rx = rx;
    m_tx = tx;
  }
  // the disk holding diskPath
  if (struct statvfs v{}; statvfs(diskPath.c_str(), &v) == 0) {
    s.diskTotal = static_cast<double>(v.f_blocks) * v.f_frsize;
    s.diskUsed = static_cast<double>(v.f_blocks - v.f_bfree) * v.f_frsize;
  }
  // battery
  if (!m_battery.empty()) {
    const double cap = number(m_battery + "/capacity");
    if (cap >= 0) s.battery = std::clamp(cap / 100, 0.0, 1.0);
    const std::string st = firstLine(m_battery + "/status");
    s.charging = st == "Charging" || st == "Full";
  }
  return s;
}

// ── the widget ──────────────────────────────────────────────────────────────

void SystemWidget::configure(const WidgetConfig& cfg, const NoctaliaState& noct) {
  const double was = m_cfg.interval;
  m_cfg = SystemConfig::fromTable(cfg.options);
  m_accent = noct.color(m_cfg.accent, Color::fromHex("#e2342a"));
  m_ink = noct.color(m_cfg.ink, Color::fromHex("#f4f1ea"));
  m_alert = noct.color(m_cfg.alert, Color::fromHex("#ff6b6b"));
  m_surface = noct.color("surface", Color::fromHex("#16161a"));
  m_es = m_cfg.language == "es" || (m_cfg.language != "en" && spanishUi());
  if (m_cfg.interval != was) m_next = 0;  // the new pace from now
}

void SystemWidget::setSample(const SystemSample& s) {
  m_fixed = true;
  m_sample = s;
  remember(s);
}

void SystemWidget::remember(const SystemSample& s) {
  const float temp = static_cast<float>(s.tempC < 0 ? 0 : s.tempC / 100);
  const float vals[8] = {static_cast<float>(s.cpu),
                         static_cast<float>(s.memTotal > 0 ? s.memUsed / s.memTotal : 0),
                         static_cast<float>(std::max(0.0, s.gpu)),
                         temp,
                         static_cast<float>(s.down),
                         static_cast<float>(s.up),
                         static_cast<float>(s.diskTotal > 0 ? s.diskUsed / s.diskTotal : 0),
                         static_cast<float>(std::max(0.0, s.battery))};
  for (size_t i = 0; i < m_hist.size(); ++i) {
    m_hist[i].push_back(vals[i]);
    while (m_hist[i].size() > kHistory) m_hist[i].pop_front();
  }
  // the network's scale: the busiest second in the window
  double peak = 1e6;
  for (float v : m_hist[4]) peak = std::max(peak, static_cast<double>(v));
  for (float v : m_hist[5]) peak = std::max(peak, static_cast<double>(v));
  m_netPeak = peak;
}

void SystemWidget::tick(const TickContext& ctx) {
  if (m_fixed || ctx.now < m_next) return;
  m_sample = m_reader.sample(ctx.now, m_cfg.diskPath);
  remember(m_sample);
  // the first sample has no rates yet: the second comes soon after
  m_next = ctx.now + (m_hist[0].size() < 2 ? std::min(0.5, m_cfg.interval) : m_cfg.interval);
}

std::vector<SystemWidget::Metric> SystemWidget::metrics() const {
  const SystemSample& s = m_sample;
  std::vector<Metric> out;
  if (m_cfg.cpu) out.push_back({"CPU", percent(s.cpu), std::format("{} {}", s.cores.size(), m_es ? "núcleos" : "cores"), s.cpu, s.cpu >= 0.9, &m_hist[0]});
  if (m_cfg.ram && s.memTotal > 0) {
    const double f = s.memUsed / s.memTotal;
    out.push_back({"RAM", percent(f), std::format("{} / {}", size(s.memUsed), size(s.memTotal)), f, f >= 0.9, &m_hist[1]});
  }
  if (m_cfg.gpu && s.gpu >= 0) out.push_back({"GPU", percent(s.gpu), "", s.gpu, s.gpu >= 0.95, &m_hist[2]});
  if (m_cfg.temp && s.tempC >= 0) {
    const std::string t = m_cfg.fahrenheit ? std::format("{}°F", static_cast<int>(std::lround(s.tempC * 9 / 5 + 32)))
                                           : std::format("{}°C", static_cast<int>(std::lround(s.tempC)));
    out.push_back({m_es ? "Temp." : "Temp", t, "", std::clamp(s.tempC / 100, 0.0, 1.0), s.tempC >= 85, &m_hist[3]});
  }
  if (m_cfg.net)
    out.push_back({m_es ? "Red" : "Net", "↓ " + rate(s.down, true), "↑ " + rate(s.up, true), std::clamp(s.down / m_netPeak, 0.0, 1.0), false,
                   &m_hist[4], &m_hist[5]});
  if (m_cfg.disk && s.diskTotal > 0) {
    const double f = s.diskUsed / s.diskTotal;
    out.push_back({m_es ? "Disco" : "Disk", percent(f), std::format("{} {}", size(s.diskTotal - s.diskUsed), m_es ? "libres" : "free"), f,
                   f >= 0.9, &m_hist[6]});
  }
  if (m_cfg.battery && s.battery >= 0)
    out.push_back({m_es ? "Batería" : "Battery", percent(s.battery), s.charging ? (m_es ? "cargando" : "charging") : "", s.battery,
                   s.battery < 0.15 && !s.charging, &m_hist[7]});
  return out;
}

void SystemWidget::draw(const DrawContext& ctx) {
  Canvas& c = m_canvas;
  c.begin(ctx.w, ctx.h, ctx.scale, ctx.text);
  const float W = ctx.w, H = ctx.h, op = static_cast<float>(m_cfg.opacity);
  if (m_cfg.plate != "none")
    c.roundRect(0, 0, W, H, std::min(W, H) * 0.1F, withA(m_surface.mix(m_accent, 0.05F), (m_cfg.plate == "solid" ? 0.94F : 0.58F) * op), 1,
                withA(m_ink, 0.1F * op));
  const float pad = std::min(W, H) * 0.08F;
  const float x0 = pad, y0 = pad, w = W - 2 * pad, h = H - 2 * pad;
  const std::vector<Metric> ms = metrics();
  const int n = static_cast<int>(ms.size());
  const Color ink = withA(m_ink, op), dim = withA(m_ink, 0.62F * op), track = withA(m_ink, 0.13F * op);
  auto tone = [&](const Metric& m) { return withA(m.alert ? m_alert : m_accent, op); };
  if (n == 0 || w <= 0 || h <= 0) {
    place(c, m_es ? "Nada que mostrar" : "Nothing to show", {.family = m_cfg.font, .size = std::max(10.0F, H * 0.12F), .weight = 500}, W / 2,
          H / 2, 1, dim);
    return;
  }
  // a grid of cells: as many columns as the box's shape asks for
  auto grid = [&](float aspect, int& cols, int& rows) {
    cols = std::clamp(static_cast<int>(std::lround(std::sqrt(n * (w / h) / aspect))), 1, n);
    rows = (n + cols - 1) / cols;
  };

  if (m_cfg.layout == "rings") {
    // the grid that gives the biggest rings
    int cols = 1, rows = n;
    float best = 0;
    for (int k = 1; k <= n; ++k) {
      const int rk = (n + k - 1) / k;
      const float rad = std::min(w / static_cast<float>(k) * 0.4F, h / static_cast<float>(rk) * 0.33F);
      if (rad > best + 0.01F) best = rad, cols = k, rows = rk;
    }
    const float cw = w / static_cast<float>(cols), ch = h / static_cast<float>(rows);
    const float r = std::min(cw * 0.4F, ch * 0.33F), lw = std::max(2.0F, r * 0.17F);
    for (int i = 0; i < n; ++i) {
      const Metric& m = ms[static_cast<size_t>(i)];
      const float cx = x0 + (static_cast<float>(i % cols) + 0.5F) * cw, cy = y0 + static_cast<float>(i / cols) * ch + ch * 0.42F;
      c.arc(cx, cy, r, lw, 0, 6.2831853F, track, false);  // (angles from 12 o'clock, clockwise)
      if (m.frac > 0.002) c.arc(cx, cy, r, lw, 0, static_cast<float>(m.frac) * 6.2831853F, tone(m), true);
      const std::string v = m.history2 ? rate(m_sample.down, false) : m.value;  // the network's ring: the download, short
      place(c, v, {.family = m_cfg.font, .size = r * (v.size() > 5 ? 0.36F : 0.46F), .weight = 650}, cx, cy, 1, ink);
      place(c, m.label, {.family = m_cfg.font, .size = std::max(9.0F, r * 0.32F), .weight = 500}, cx, cy + r + lw + r * 0.28F, 1, dim);
    }
  } else if (m_cfg.layout == "bars" || m_cfg.layout == "text") {
    int cols = 1, rows = 1;
    grid(m_cfg.layout == "bars" ? 5.0F : 4.0F, cols, rows);
    const float cw = w / cols, rh = h / rows;
    const float fs = std::min(rh * 0.42F, cw * 0.075F);
    const TextStyle label{.family = m_cfg.font, .size = fs, .weight = 500}, value{.family = m_cfg.font, .size = fs, .weight = 650};
    float labelW = 0;  // the labels' column: as wide as the widest of them
    for (const Metric& m : ms) labelW = std::max(labelW, Canvas::measure(m.label, label).w);
    for (int i = 0; i < n; ++i) {
      const Metric& m = ms[static_cast<size_t>(i)];
      const float x = x0 + static_cast<float>(i / rows) * cw, cy = y0 + (static_cast<float>(i % rows) + 0.5F) * rh;
      const float gap = std::max(fs * 0.6F, cw * 0.04F), right = x + cw - (cols > 1 ? gap : 0);
      place(c, m.label, label, x, cy, 0, dim);
      // the detail too when it fits beside the label
      std::string v = m.value;
      if (m_cfg.layout == "text" && !m.detail.empty() && Canvas::measure(m.value + "  ·  " + m.detail, value).w <= right - x - labelW - gap)
        v = m.value + "  ·  " + m.detail;
      place(c, v, value, right, cy, 2, m.alert ? tone(m) : ink);
      if (m_cfg.layout == "bars") {
        const float bx = x + labelW + gap, bw = std::max(4.0F, right - Canvas::measure(m.value, value).w - gap - bx);
        const float bh = std::max(3.0F, rh * 0.22F);
        c.roundRect(bx, cy - bh / 2, bw, bh, bh / 2, track);
        if (m.frac > 0.002) c.roundRect(bx, cy - bh / 2, std::max(bh, bw * static_cast<float>(m.frac)), bh, bh / 2, tone(m));
      }
    }
  } else {  // graph: the last minute of each, as an area under a line
    int cols = 1, rows = 1;
    grid(3.2F, cols, rows);
    const float cw = w / cols, rh = h / rows;
    const float fs = std::min(rh * 0.24F, cw * 0.06F);
    const TextStyle label{.family = m_cfg.font, .size = fs, .weight = 500}, value{.family = m_cfg.font, .size = fs, .weight = 650};
    for (int i = 0; i < n; ++i) {
      const Metric& m = ms[static_cast<size_t>(i)];
      const float gap = cw * 0.04F;
      const float x = x0 + static_cast<float>(i / rows) * cw, y = y0 + static_cast<float>(i % rows) * rh, gw = cw - (cols > 1 ? gap : 0);
      place(c, m.label, label, x, y + fs * 0.6F, 0, dim);
      place(c, m.history2 ? m.value + "  " + m.detail : m.value, value, x + gw, y + fs * 0.6F, 2, m.alert ? tone(m) : ink);
      const float gy = y + fs * 1.35F, gh = rh - fs * 1.35F - rh * 0.1F;
      if (gh < 4) continue;
      c.segment(x, gy + gh, x + gw, gy + gh, 1, track, false);
      auto plot = [&](const std::deque<float>& hist, float scale, Color line, bool area) {
        const size_t k = hist.size();
        if (k < 2) return;
        const float step = gw / static_cast<float>(kHistory - 1);
        float px = x + gw - static_cast<float>(k - 1) * step, py = 0;
        for (size_t j = 0; j < k; ++j) {
          const float vx = x + gw - static_cast<float>(k - 1 - j) * step;
          const float vy = gy + gh - std::clamp(hist[j] / scale, 0.0F, 1.0F) * gh;
          if (j > 0) {
            if (area) {
              c.triangle(px, py, vx, vy, vx, gy + gh, withA(line, 0.22F));
              c.triangle(px, py, vx, gy + gh, px, gy + gh, withA(line, 0.22F));
            }
            c.segment(px, py, vx, vy, std::max(1.2F, rh * 0.03F), line);
          }
          px = vx, py = vy;
        }
      };
      if (m.history2) {
        plot(*m.history, static_cast<float>(m_netPeak), withA(m_accent, op), true);
        plot(*m.history2, static_cast<float>(m_netPeak), withA(m_ink, 0.7F * op), false);
      } else if (m.history) {
        plot(*m.history, 1.0F, tone(m), true);
      }
    }
  }
  if (ctx.text) ctx.text->collect(120);
}

}  // namespace undershell
