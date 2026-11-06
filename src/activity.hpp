// SPDX-License-Identifier: GPL-3.0-or-later
// The activity widget: how much you committed each day, as the contribution
// grid code hosts draw (weeks across, days down) or as bars per week. It
// reads the git repositories under your folders (no network, no account):
// `git log` in each, on a thread of its own, every half hour. Cells can
// twinkle with the music.
#pragma once

#include "canvas.hpp"
#include "widget.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace undershell {

struct ActivityConfig {
  std::string layout = "grid";              // grid weeks
  int weeks = 53;                           // how far back (4..53)
  std::vector<std::string> folders{"~"};    // where repositories are looked for (three levels down)
  std::string author;                       // "" your git email, "all" anyone, or a name or email
  std::string plate = "glass";              // glass solid none
  std::string accent = "primary", ink = "on_surface";
  std::string font = "Space Grotesk";
  std::string language = "system";
  bool total = true;                        // the count above the grid
  double react = 0;                         // cells twinkle with the music (0 off)
  double opacity = 1;

  static ActivityConfig fromTable(const toml::table& t);
};

// commits per local day (days since 1970-01-01)
using ActivityDays = std::map<int64_t, int>;

// git repositories under `folders` (a .git folder or file), at most `depth`
// levels down, skipping hidden folders and build/dependency trees; at most 300
[[nodiscard]] std::vector<std::string> findRepositories(const std::vector<std::string>& folders, int depth = 3);
// `git log --format=%at` output (one unix time a line) into commits per local day
void countDays(const std::string& log, ActivityDays& days);
// the local day a unix time falls on
[[nodiscard]] int64_t localDay(int64_t unixTime);

class ActivityWidget final : public WidgetImpl {
public:
  void configure(const WidgetConfig& cfg, const NoctaliaState& noct) override;
  void tick(const TickContext& ctx) override;
  void draw(const DrawContext& ctx) override;
  [[nodiscard]] double nextWakeup(double now) const override;
  [[nodiscard]] bool animating(const TickContext& ctx) const override { return m_cfg.react > 0 && !ctx.audio.silent; }
  [[nodiscard]] bool usesAudio() const override { return m_cfg.react > 0; }
  [[nodiscard]] Color accent() const override { return m_accent; }

  // days given from outside (tests, previews): the widget stops reading git
  void setDays(ActivityDays days, int64_t today);

private:
  struct Scan {  // shared with the worker, which may outlive the widget
    std::mutex mutex;
    bool done = false;
    ActivityDays days;
    int repos = 0;
  };
  void startScan(double now);

  ActivityConfig m_cfg;
  std::string m_key;  // what the last scan was for
  std::shared_ptr<Scan> m_scan;
  ActivityDays m_days;
  int m_repos = -1;   // -1: not read yet
  int64_t m_today = 0;
  bool m_fixed = false;
  double m_nextScan = 0;
  float m_beat = 0;
  uint64_t m_kicks = 0;
  Color m_accent, m_ink, m_surface;
  bool m_es = false;
  Canvas m_canvas;
};

}  // namespace undershell
