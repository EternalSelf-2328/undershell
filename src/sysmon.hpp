// SPDX-License-Identifier: GPL-3.0-or-later
// The system monitor widget: CPU, memory, GPU, temperature, network, disk and
// battery, as rings, bars, short graphs or plain text. Everything comes from
// /proc and /sys (no programs run, no extra permissions): one read of a few
// small files per sample (1 s by default, ~0.2 ms of CPU), and the widget
// redraws only when a sample lands.
//
// GPU: AMD's gpu_busy_percent, or for Intel the share of time the GPU was
// not asleep (i915 rc6_residency_ms, Xe gtidle idle_residency_ms); other
// GPUs show no GPU row. Temperature: the CPU package (coretemp, k10temp,
// zenpower), else the first thermal zone that names the package or ACPI.
#pragma once

#include "canvas.hpp"
#include "widget.hpp"

#include <array>
#include <deque>
#include <string>
#include <vector>

namespace undershell {

struct SystemConfig {
  std::string layout = "rings";  // rings bars graph text
  bool cpu = true, ram = true, gpu = true, temp = true, net = true, disk = false, battery = true;
  std::string diskPath = "/";
  double interval = 1;           // seconds between samples
  std::string plate = "glass";   // glass solid none
  std::string accent = "primary", ink = "on_surface", alert = "error";
  std::string font = "Space Grotesk";
  std::string language = "system";
  bool fahrenheit = false;
  double opacity = 1;

  static SystemConfig fromTable(const toml::table& t);
};

// One reading of the machine. Rates and loads need two samples: the first
// one has them at 0.
struct SystemSample {
  double cpu = 0;                  // 0..1, all cores
  std::vector<double> cores;       // 0..1 each
  double memUsed = 0, memTotal = 0, swapUsed = 0, swapTotal = 0;  // bytes
  double gpu = -1;                 // 0..1; < 0: no GPU reading on this machine
  double tempC = -1;               // < 0: no sensor
  double down = 0, up = 0;         // bytes a second
  double diskUsed = 0, diskTotal = 0;
  double battery = -1;             // 0..1; < 0: no battery
  bool charging = false;
};

// Reads /proc and /sys; where the GPU, the temperature and the battery are is
// found once, on the first sample. `root` ("" = /) is for tests.
class SystemReader {
public:
  explicit SystemReader(std::string root = {}) : m_root(std::move(root)) {}
  SystemSample sample(double now, const std::string& diskPath);

private:
  void find();
  [[nodiscard]] std::string path(const std::string& p) const { return m_root + p; }
  std::string m_root;
  bool m_found = false;
  std::string m_gpuBusy, m_gpuIdle, m_temp, m_battery;  // files ("" = none)
  double m_last = -1;
  double m_busy = 0, m_total = 0, m_rx = -1, m_tx = 0, m_idleMs = -1;
  std::vector<std::pair<double, double>> m_cores;
};

class SystemWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] double nextWakeup(double now) const override { return m_next > 0 ? m_next : now; }
  [[nodiscard]] Color accent() const override { return m_accent; }

  // a reading given from outside (tests, previews): the widget stops sampling
  void setSample(const SystemSample& s);

private:
  struct Metric {
    std::string label, value, detail;
    double frac = 0;       // how full the ring or bar is
    bool alert = false;
    const std::deque<float>* history = nullptr;
    const std::deque<float>* history2 = nullptr;  // the network's upload
  };
  [[nodiscard]] std::vector<Metric> metrics() const;
  void remember(const SystemSample& s);

  SystemConfig m_cfg;
  SystemReader m_reader;
  SystemSample m_sample;
  bool m_fixed = false;
  double m_next = 0;
  double m_netPeak = 1e6;  // the network rows' scale: the busiest second lately (1 MB/s at least)
  static constexpr size_t kHistory = 60;
  std::array<std::deque<float>, 8> m_hist;  // cpu ram gpu temp down up disk battery
  Color m_accent, m_ink, m_alert, m_surface;
  bool m_es = false;
  Canvas m_canvas;
};

}  // namespace undershell
