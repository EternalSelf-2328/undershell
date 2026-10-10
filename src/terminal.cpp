// SPDX-License-Identifier: GPL-3.0-or-later
// A terminal on a CRT, for the computer screens in a wallpaper. Four modes:
//   code     - a shell session typing itself (builds, git, code, the song that
//              is playing), faster with the music, its output pouring on kicks
//   spectrum - the spectrum in block characters, with the song above it
//   matrix   - columns of katakana and digits falling, faster with the music
//   monitor  - this machine's CPU, memory, network and busiest processes
//              (read from /proc once a second), with the music's pulse
// All on a phosphor screen: green, amber, white or the theme's colour.
#include "looks.hpp"

#include "termscreen.hpp"
#include "widget.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <deque>
#include <dirent.h>
#include <format>
#include <fstream>
#include <map>
#include <pwd.h>
#include <sstream>
#include <unistd.h>
#include <vector>

namespace undershell {

namespace {

// ── code: the sessions it types ─────────────────────────────────────────────
// A line typed after a prompt, printed (output), or typed as code. "@SONG@",
// "@TIME@", "@UP@" and "@MS@" are filled in when the line comes up.
enum class Kind { Cmd, Out, Code };
struct Line {
  Kind kind;
  const char* text;
};

const std::vector<std::vector<Line>>& sessions() {
  static const std::vector<std::vector<Line>> s = {
      {{Kind::Cmd, "cd ~/projects/undershell && git pull"},
       {Kind::Out, "remote: Enumerating objects: 42, done."},
       {Kind::Out, "remote: Counting objects: 100% (42/42), done."},
       {Kind::Out, "remote: Compressing objects: 100% (18/18), done."},
       {Kind::Out, "Updating 7bb3c32..92a0624"},
       {Kind::Out, "Fast-forward"},
       {Kind::Out, " src/terminal.cpp   | 412 ++++++++++++++++++++++++++"},
       {Kind::Out, " src/termscreen.cpp | 268 +++++++++++++++++"},
       {Kind::Out, " 2 files changed, 680 insertions(+)"},
       {Kind::Cmd, "meson compile -C build"},
       {Kind::Out, "[1/9] Compiling C++ object src_audio.cpp.o"},
       {Kind::Out, "[2/9] Compiling C++ object src_visualizer.cpp.o"},
       {Kind::Out, "[3/9] Compiling C++ object src_strokes.cpp.o"},
       {Kind::Out, "[4/9] Compiling C++ object src_terminal.cpp.o"},
       {Kind::Out, "[5/9] Compiling C++ object src_termscreen.cpp.o"},
       {Kind::Out, "[6/9] Compiling C++ object src_neon.cpp.o"},
       {Kind::Out, "[7/9] Linking static target libundershell-core.a"},
       {Kind::Out, "[8/9] Compiling C++ object undershell.p/src_main.cpp.o"},
       {Kind::Out, "[9/9] Linking target undershell"},
       {Kind::Cmd, "./build/undershell --version"},
       {Kind::Out, "undershell 1.1.0"}},
      {{Kind::Cmd, "playerctl metadata --format '{{artist}} - {{title}}'"},
       {Kind::Out, "@SONG@"},
       {Kind::Cmd, "pactl list short sinks"},
       {Kind::Out, "57  alsa_output.analog-stereo  PipeWire  float32le 2ch 48000Hz  RUNNING"},
       {Kind::Cmd, "pw-top -b -n 1 | head -4"},
       {Kind::Out, "S   ID  QUANT   RATE    WAIT    BUSY   W/Q   B/Q  ERR  NAME"},
       {Kind::Out, "R   57   1024  48000  85.3us  33.0us  0.00  0.00    0  analog-stereo"},
       {Kind::Out, "R   45   1024  48000  12.1us   8.4us  0.00  0.00    0   + undershell"}},
      {{Kind::Cmd, "cat > kick.cpp"},
       {Kind::Code, "#include <cmath>"},
       {Kind::Code, "// a kick: the low band jumping over the 36 ms before it"},
       {Kind::Code, "float level(const float* low, int n) {"},
       {Kind::Code, "    float e = 0;"},
       {Kind::Code, "    for (int i = 0; i < n; ++i) e += low[i] * low[i];"},
       {Kind::Code, "    return 10.0f * std::log10(1e-10f + e / n);"},
       {Kind::Code, "}"},
       {Kind::Code, "bool kick(float now, float before, float peak) {"},
       {Kind::Code, "    return now - before > 5.0f && now > peak - 6.0f;"},
       {Kind::Code, "}"},
       {Kind::Cmd, "g++ -O2 kick.cpp main.cpp -o kick && ./kick"},
       {Kind::Out, "[@TIME@] kick   strength 0.92   rise 11.4 dB"},
       {Kind::Out, "[@TIME@] kick   strength 0.71   rise  8.2 dB"},
       {Kind::Out, "[@TIME@] kick   strength 0.88   rise 10.7 dB"},
       {Kind::Out, "[@TIME@] kick   strength 0.64   rise  6.9 dB"}},
      {{Kind::Cmd, "uptime"},
       {Kind::Out, " @TIME@ up @UP@,  1 user,  load average: 0.42, 0.37, 0.31"},
       {Kind::Cmd, "ping -c 4 1.1.1.1"},
       {Kind::Out, "PING 1.1.1.1 (1.1.1.1) 56(84) bytes of data."},
       {Kind::Out, "64 bytes from 1.1.1.1: icmp_seq=1 ttl=57 time=@MS@ ms"},
       {Kind::Out, "64 bytes from 1.1.1.1: icmp_seq=2 ttl=57 time=@MS@ ms"},
       {Kind::Out, "64 bytes from 1.1.1.1: icmp_seq=3 ttl=57 time=@MS@ ms"},
       {Kind::Out, "64 bytes from 1.1.1.1: icmp_seq=4 ttl=57 time=@MS@ ms"},
       {Kind::Out, "--- 1.1.1.1 ping statistics ---"},
       {Kind::Out, "4 packets transmitted, 4 received, 0% packet loss, time 3004ms"},
       {Kind::Cmd, "ls ~/Music | wc -l"},
       {Kind::Out, "6837"}},
  };
  return s;
}

std::string clockText() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  return std::format("{:02}:{:02}:{:02}", tm.tm_hour, tm.tm_min, tm.tm_sec);
}

std::string uptimeText() {
  std::ifstream f("/proc/uptime");
  double up = 0;
  f >> up;
  const long m = static_cast<long>(up) / 60;
  return std::format("{}:{:02}", m / 60, m % 60);
}

std::string stamp(double sec) {
  const long s = static_cast<long>(std::max(0.0, sec));
  return std::format("{}:{:02}", s / 60, s % 60);
}

std::string bytes(double b) {
  if (b >= 1e9) return std::format("{:.1f}G", b / 1e9);
  if (b >= 1e6) return std::format("{:.1f}M", b / 1e6);
  if (b >= 1e3) return std::format("{:.0f}K", b / 1e3);
  return std::format("{:.0f}B", b);
}

// ── monitor: what /proc says ───────────────────────────────────────────────
struct Proc {
  int pid;
  std::string user, cmd;
  double cpu, rss;
};

struct System {
  std::vector<double> cores;  // 0..1 each
  double memUsed = 0, memTotal = 1, swapUsed = 0, swapTotal = 0;
  double down = 0, up = 0;    // bytes a second
  std::vector<Proc> top;
  std::string host = "localhost";
};

class Sampler {
public:
  void sample(System& out, double elapsed) {
    readCpu(out);
    readMem(out);
    readNet(out, elapsed);
    readProcs(out, elapsed);
    std::ifstream h("/proc/sys/kernel/hostname");
    std::getline(h, out.host);
  }

private:
  void readCpu(System& out) {
    std::ifstream f("/proc/stat");
    std::string line;
    std::vector<std::pair<double, double>> now;  // busy, total per core
    while (std::getline(f, line)) {
      if (line.rfind("cpu", 0) != 0 || line.size() < 4 || line[3] == ' ') continue;  // per core only
      std::istringstream ss(line.substr(line.find(' ')));
      double v[8] = {0};
      for (double& x : v) ss >> x;
      const double idle = v[3] + v[4], total = v[0] + v[1] + v[2] + v[3] + v[4] + v[5] + v[6] + v[7];
      now.push_back({total - idle, total});
    }
    out.cores.assign(now.size(), 0);
    for (size_t i = 0; i < now.size() && i < m_cpu.size(); ++i) {
      const double dt = now[i].second - m_cpu[i].second;
      out.cores[i] = dt > 0 ? std::clamp((now[i].first - m_cpu[i].first) / dt, 0.0, 1.0) : 0;
    }
    m_cpu = now;
  }
  void readMem(System& out) {
    std::ifstream f("/proc/meminfo");
    std::string key;
    double v;
    std::string unit;
    double total = 0, avail = 0, st = 0, sf = 0;
    while (f >> key >> v) {
      std::getline(f, unit);
      if (key == "MemTotal:") total = v;
      else if (key == "MemAvailable:") avail = v;
      else if (key == "SwapTotal:") st = v;
      else if (key == "SwapFree:") sf = v;
    }
    out.memTotal = std::max(1.0, total * 1024);
    out.memUsed = (total - avail) * 1024;
    out.swapTotal = st * 1024;
    out.swapUsed = (st - sf) * 1024;
  }
  void readNet(System& out, double elapsed) {
    std::ifstream f("/proc/net/dev");
    std::string line;
    double rx = 0, tx = 0;
    while (std::getline(f, line)) {
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
    if (m_rx > 0 && elapsed > 0) {
      out.down = std::max(0.0, (rx - m_rx) / elapsed);
      out.up = std::max(0.0, (tx - m_tx) / elapsed);
    }
    m_rx = rx;
    m_tx = tx;
  }
  void readProcs(System& out, double elapsed) {
    const double hz = static_cast<double>(sysconf(_SC_CLK_TCK)), page = static_cast<double>(sysconf(_SC_PAGESIZE));
    std::map<int, double> ticks;
    std::vector<Proc> all;
    DIR* d = opendir("/proc");
    if (!d) return;
    while (dirent* e = readdir(d)) {
      const int pid = std::atoi(e->d_name);
      if (pid <= 0) continue;
      std::ifstream f(std::format("/proc/{}/stat", pid));
      std::string stat;
      if (!std::getline(f, stat)) continue;
      const auto open = stat.find('('), close = stat.rfind(')');
      if (open == std::string::npos || close == std::string::npos) continue;
      Proc p;
      p.pid = pid;
      p.cmd = stat.substr(open + 1, close - open - 1);
      std::istringstream ss(stat.substr(close + 2));
      std::string field;
      std::vector<std::string> fields;
      while (ss >> field) fields.push_back(field);
      if (fields.size() < 22) continue;
      const double t = std::atof(fields[11].c_str()) + std::atof(fields[12].c_str());  // utime + stime
      p.rss = std::atof(fields[21].c_str()) * page;
      ticks[pid] = t;
      auto prev = m_ticks.find(pid);
      p.cpu = prev != m_ticks.end() && elapsed > 0 ? (t - prev->second) / hz / elapsed * 100 : 0;
      all.push_back(std::move(p));
    }
    closedir(d);
    m_ticks.swap(ticks);
    std::partial_sort(all.begin(), all.begin() + std::min<size_t>(8, all.size()), all.end(),
                      [](const Proc& a, const Proc& b) { return a.cpu != b.cpu ? a.cpu > b.cpu : a.rss > b.rss; });
    all.resize(std::min<size_t>(8, all.size()));
    for (Proc& p : all) p.user = userOf(p.pid);
    out.top = std::move(all);
  }
  std::string userOf(int pid) {
    std::ifstream f(std::format("/proc/{}/status", pid));
    std::string line;
    while (std::getline(f, line))
      if (line.rfind("Uid:", 0) == 0) {
        const unsigned uid = static_cast<unsigned>(std::atoi(line.c_str() + 4));
        auto it = m_users.find(uid);
        if (it != m_users.end()) return it->second;
        const passwd* pw = getpwuid(uid);
        return m_users[uid] = pw ? pw->pw_name : std::to_string(uid);
      }
    return "?";
  }

  std::vector<std::pair<double, double>> m_cpu;
  double m_rx = 0, m_tx = 0;
  std::map<int, double> m_ticks;
  std::map<unsigned, std::string> m_users;
};

// katakana, digits and a few marks, for the rain
const std::u32string& matrixSet() {
  static const std::u32string s = [] {
    std::u32string out;
    for (char32_t c = 0xFF66; c <= 0xFF9D; ++c) out += c;
    out += U"0123456789Z:.=*+-<>|";
    return out;
  }();
  return s;
}

class Terminal final : public StrokeLook {
public:
  void configure(const toml::table& t, const NoctaliaState& noct) override {
    const std::string mode = t["term_mode"].value_or(std::string("code"));
    if (mode != m_mode) {  // a new screen
      m_lines.clear();
      m_cols.clear();
    }
    m_mode = mode;
    const std::string colour = t["term_color"].value_or(std::string("green"));
    const Color c = colour == "amber"   ? Color{1.0F, 0.69F, 0.2F, 1}
                    : colour == "white" ? Color{0.85F, 0.9F, 1.0F, 1}
                    : colour == "theme" ? noct.color("primary")
                                        : Color{0.35F, 1.0F, 0.5F, 1};
    m_crt.phosphor[0] = c.r, m_crt.phosphor[1] = c.g, m_crt.phosphor[2] = c.b;
    m_textScale = static_cast<float>(std::clamp(t["term_size"].value_or(1.0), 0.5, 2.0));
    m_crt.glow = static_cast<float>(std::clamp(t["term_glow"].value_or(0.6), 0.0, 1.0));
    m_crt.scan = static_cast<float>(std::clamp(t["term_scan"].value_or(0.5), 0.0, 1.0));
    m_crt.flicker = static_cast<float>(std::clamp(t["term_flicker"].value_or(0.2), 0.0, 1.0));
    m_crt.curve = static_cast<float>(std::clamp(t["term_curve"].value_or(0.3), 0.0, 1.0));
    m_crt.background = static_cast<float>(std::clamp(t["term_background"].value_or(0.85), 0.0, 1.0));
    m_hits = std::clamp(t["halo_hits"].value_or(0.7), 0.0, 1.0);
    m_preview = t["term_preview"].value_or(-1.0);
    if (m_preview >= 0) {
      m_flash = std::exp(-m_preview / 0.15);
      m_live = true;
      m_energy = 0.6;
      m_warmed = false;
    }
  }

  void tick(const Beat& b) override {
    if (m_preview >= 0) return;
    step(b);
  }

  void draw(const DrawContext& ctx, float opacity) override {
    m_screen.layout(ctx.w, ctx.h, ctx.scale, m_textScale);
    if (m_preview >= 0 && !m_warmed) warmUp();
    m_screen.clear();
    const float boost = 1 + 0.35F * static_cast<float>(m_flash);  // a kick lights the screen up
    if (m_mode == "spectrum") drawSpectrum(boost);
    else if (m_mode == "matrix") drawMatrix(boost);
    else if (m_mode == "monitor") drawMonitor(boost);
    else drawCode(boost);
    TermScreen::Crt crt = m_crt;
    crt.opacity = opacity;
    crt.glow = std::min(1.0F, crt.glow * (1 + 0.6F * static_cast<float>(m_flash)));
    crt.noise = m_preview >= 0 ? 0.5F : m_rng.next();
    m_screen.draw(crt);
  }

  [[nodiscard]] bool visible() const override { return true; }
  [[nodiscard]] bool moving() const override { return m_live || m_flash > 0.01; }
  // in silence: the code still types slowly, the rain still falls, the
  // monitor updates its numbers, the spectrum keeps its clock
  [[nodiscard]] double idleFrame() const override {
    return m_mode == "matrix" ? 1.0 / 15 : m_mode == "code" ? 0.1 : m_mode == "monitor" ? 1.0 : 0.5;
  }
  [[nodiscard]] bool wantsMedia() const override { return m_mode == "spectrum" || m_mode == "code"; }

private:
  // ── time ──
  void step(const Beat& b) {
    m_live = b.live;
    m_energy = b.energy;
    m_time += b.dt;
    if (b.kick) m_flash = std::max(m_flash, b.strength * std::clamp(m_hits / 0.7, 0.0, 1.45));
    m_flash *= std::exp(-b.dt / 0.15);
    if (m_flash < 0.003) m_flash = 0;
    m_track = b.track;
    m_playing = b.playing;
    m_title = b.title;
    m_artist = b.artist;
    m_position = b.position;
    m_length = b.length;
    // the spectrum's bars: quick up, slow down
    if (b.bands && !b.bands->empty()) m_bands = *b.bands;
    else std::fill(m_bands.begin(), m_bands.end(), 0.0F);
    if (m_mode == "code") stepCode(b);
    else if (m_mode == "matrix") stepMatrix(b);
    else if (m_mode == "monitor") stepMonitor(b);
    else stepSpectrum(b);
  }

  // ── code ──
  std::string fill(std::string s) {
    auto replace = [&s](const std::string& key, const std::string& by) {
      for (size_t at; (at = s.find(key)) != std::string::npos;) s.replace(at, key.size(), by);
    };
    replace("@SONG@", m_track && !m_title.empty() ? (m_artist.empty() ? m_title : m_artist + " - " + m_title)
                                                  : std::string("No players found"));
    replace("@TIME@", m_preview >= 0 ? std::string("21:42:07") : clockText());
    replace("@UP@", m_preview >= 0 ? std::string("3:12") : uptimeText());
    while (s.find("@MS@") != std::string::npos) s.replace(s.find("@MS@"), 4, std::format("{:.1f}", 11 + 4 * m_rng.next()));
    return s;
  }
  void push(std::string text, float bright) {
    m_lines.push_back({std::move(text), bright});
    while (m_lines.size() > 200) m_lines.pop_front();
  }
  void stepCode(const Beat& b) {
    const auto& all = sessions();
    const std::vector<Line>& session = all[m_session % all.size()];
    if (m_wait > 0) {  // a breath between commands
      m_wait -= b.dt;
      return;
    }
    if (m_entry >= session.size()) {  // the next session
      m_session = (m_session + 1) % all.size();
      m_entry = 0;
      m_wait = 0.8;
      return;
    }
    const Line& line = session[m_entry];
    if (line.kind == Kind::Out) {
      // output pours: lines a second, more with the music, a burst on a kick
      m_budget += b.dt * (b.live ? 8 + 25 * b.energy : 3) + (b.kick ? 4 * b.strength : 0);
      while (m_budget >= 1 && m_entry < session.size() && session[m_entry].kind == Kind::Out) {
        m_budget -= 1;
        push(fill(session[m_entry].text), 0.62F);
        ++m_entry;
      }
      if (m_entry < session.size() && session[m_entry].kind != Kind::Out) m_wait = 0.25;
      return;
    }
    // typed: a command after a prompt, or code, characters a second
    if (m_typed == 0) {
      m_current = fill(line.text);
      push(line.kind == Kind::Cmd ? kPrompt : "", line.kind == Kind::Cmd ? 1.0F : 0.85F);
    }
    m_budget += b.dt * (b.live ? 18 + 70 * b.energy : 7) * (line.kind == Kind::Code ? 2.2 : 1) + (b.kick ? 5 : 0);
    const std::u32string target = TermScreen::decode(m_current);
    while (m_budget >= 1 && m_typed < target.size()) {
      m_budget -= 1;
      ++m_typed;
    }
    std::u32string shown = target.substr(0, m_typed);
    std::string utf8;
    for (char32_t c : shown) utf8 += toUtf8(c);
    m_lines.back().text = (line.kind == Kind::Cmd ? kPrompt : "") + utf8;
    if (m_typed >= target.size()) {
      m_typed = 0;
      ++m_entry;
      m_budget = 0;
      m_wait = line.kind == Kind::Cmd ? 0.35 : 0.05;
    }
  }
  static std::string toUtf8(char32_t c) {
    std::string s;
    if (c < 0x80) s += static_cast<char>(c);
    else if (c < 0x800) s += static_cast<char>(0xC0 | (c >> 6)), s += static_cast<char>(0x80 | (c & 0x3F));
    else s += static_cast<char>(0xE0 | (c >> 12)), s += static_cast<char>(0x80 | ((c >> 6) & 0x3F)), s += static_cast<char>(0x80 | (c & 0x3F));
    return s;
  }
  void drawCode(float boost) {
    const int cols = m_screen.cols(), rows = m_screen.rows();
    // the lines, wrapped at the screen's width, newest at the foot
    std::vector<std::pair<std::u32string, float>> wrapped;
    for (const auto& l : m_lines) {
      std::u32string u = TermScreen::decode(l.text);
      if (u.empty()) wrapped.push_back({u, l.bright});
      for (size_t i = 0; i < u.size(); i += static_cast<size_t>(cols)) wrapped.push_back({u.substr(i, static_cast<size_t>(cols)), l.bright});
    }
    const int first = std::max(0, static_cast<int>(wrapped.size()) - rows);
    for (int r = 0; r < rows && first + r < static_cast<int>(wrapped.size()); ++r) {
      const auto& [u, bright] = wrapped[static_cast<size_t>(first + r)];
      for (size_t c = 0; c < u.size(); ++c) m_screen.put(static_cast<int>(c), r, u[c], bright * boost);
    }
    // the cursor: solid while typing, blinking while it waits
    const int row = std::min(rows, static_cast<int>(wrapped.size())) - 1;
    const int col = wrapped.empty() ? 0 : static_cast<int>(wrapped.back().first.size());
    const bool on = m_wait <= 0 || std::fmod(m_time, 1.0) < 0.55;
    if (on && row >= 0) m_screen.put(std::min(col, cols - 1), row, U'█', 0.9F * boost);
  }

  // ── spectrum ──
  void stepSpectrum(const Beat& b) {
    const size_t n = 48;
    if (m_levels.size() != n) m_levels.assign(n, 0), m_peaks.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
      float v = 0;
      if (!m_bands.empty()) {
        const float pos = static_cast<float>(i) / static_cast<float>(n - 1) * static_cast<float>(m_bands.size() - 1);
        const size_t a = static_cast<size_t>(pos);
        const float f = pos - static_cast<float>(a);
        v = m_bands[a] * (1 - f) + m_bands[std::min(a + 1, m_bands.size() - 1)] * f;
      }
      const float k = static_cast<float>(1 - std::exp(-b.dt / (v > m_levels[i] ? 0.04 : 0.22)));
      m_levels[i] += (v - m_levels[i]) * k;
      m_peaks[i] = std::max(m_levels[i], m_peaks[i] - static_cast<float>(b.dt) * 0.35F);
    }
  }
  void drawSpectrum(float boost) {
    const int cols = m_screen.cols(), rows = m_screen.rows();
    // the song across the top
    const std::string head = m_track && !m_title.empty() ? "♪ " + (m_artist.empty() ? m_title : m_artist + " — " + m_title)
                                                         : std::string("♪ nothing playing");
    m_screen.text(1, 0, head, 1.0F * boost);
    if (m_track && m_length > 0) {
      const std::string t = stamp(m_position) + "/" + stamp(m_length);
      m_screen.text(cols - static_cast<int>(t.size()) - 1, 0, t, 0.7F * boost);
    }
    // the bars, two columns each where there is room
    const int top = 2, bottom = rows - 3, h = std::max(1, bottom - top + 1);
    const int pitch = cols >= 40 ? 2 : 1;  // a gap between bars, where there is room
    const int bars = std::max(1, (cols - 2) / pitch);
    for (int i = 0; i < bars; ++i) {
      const size_t src = m_levels.empty() ? 0 : static_cast<size_t>(i) * m_levels.size() / static_cast<size_t>(bars);
      const float v = m_levels.empty() ? 0 : std::clamp(m_levels[src], 0.0F, 1.0F);
      const float pk = m_peaks.empty() ? 0 : std::clamp(m_peaks[src], 0.0F, 1.0F);
      const float cells = v * static_cast<float>(h);
      const int col = 1 + i * pitch;
      for (int k = 0; k < h; ++k) {
        const float fill = std::clamp(cells - static_cast<float>(k), 0.0F, 1.0F);
        if (fill > 0) m_screen.bar(col, bottom - k, fill, 0.9F * boost);
      }
      const int peakRow = bottom - std::min(h - 1, static_cast<int>(pk * static_cast<float>(h)));
      if (pk > 0.02F && peakRow < bottom - static_cast<int>(cells)) m_screen.put(col, peakRow, U'▔', 1.0F * boost);
    }
    for (int c = 0; c < cols; ++c) m_screen.put(c, rows - 2, U'━', 0.5F * boost);
    // where the song is: a bar along the foot
    if (m_track && m_length > 0) {
      const float frac = static_cast<float>(std::clamp(m_position / m_length, 0.0, 1.0));
      const float span = frac * static_cast<float>(cols - 2);
      for (int c = 0; c < cols - 2; ++c) m_screen.hbar(1 + c, rows - 1, std::clamp(span - static_cast<float>(c), 0.0F, 1.0F) * 0.999F, 0.55F * boost);
    }
  }

  // ── matrix ──
  struct Stream {
    float head, speed, len;
  };
  void stepMatrix(const Beat& b) {
    const int cols = m_screen.cols(), rows = m_screen.rows();
    if (cols <= 0 || rows <= 0) return;
    const auto& set = matrixSet();
    if (static_cast<int>(m_cols.size()) != cols || static_cast<int>(m_glyphs.size()) != cols * rows) {
      m_cols.clear();
      for (int c = 0; c < cols; ++c) m_cols.push_back(newStream(rows, true));
      m_glyphs.resize(static_cast<size_t>(cols) * rows);
      for (char32_t& g : m_glyphs) g = set[static_cast<size_t>(m_rng.next() * set.size()) % set.size()];
    }
    const float pace = static_cast<float>((b.live ? 0.6 + 1.2 * b.energy : 0.35) * (1 + 2 * m_flash));
    for (int c = 0; c < cols; ++c) {
      Stream& s = m_cols[static_cast<size_t>(c)];
      s.head += s.speed * pace * static_cast<float>(b.dt);
      if (s.head - s.len > static_cast<float>(rows)) s = newStream(rows, false);
      if (b.kick && m_rng.next() < 0.15F * static_cast<float>(b.strength)) s = newStream(rows, false), s.head = 0;  // a glitch
    }
    // glyphs change now and then
    const int changes = static_cast<int>(static_cast<float>(cols * rows) * 0.02F);
    for (int k = 0; k < changes; ++k)
      m_glyphs[static_cast<size_t>(m_rng.next() * static_cast<float>(m_glyphs.size())) % m_glyphs.size()] =
          set[static_cast<size_t>(m_rng.next() * set.size()) % set.size()];
  }
  Stream newStream(int rows, bool anywhere) {
    Stream s;
    s.len = 4 + m_rng.next() * static_cast<float>(rows) * 0.7F;
    s.head = anywhere ? m_rng.next() * static_cast<float>(rows + s.len) : -m_rng.next() * static_cast<float>(rows) * 0.8F;
    s.speed = 6 + m_rng.next() * 14;
    return s;
  }
  void drawMatrix(float boost) {
    const int cols = m_screen.cols(), rows = m_screen.rows();
    if (static_cast<int>(m_cols.size()) != cols) return;
    for (int c = 0; c < cols; ++c) {
      const Stream& s = m_cols[static_cast<size_t>(c)];
      const int head = static_cast<int>(s.head);
      for (int r = std::max(0, static_cast<int>(s.head - s.len)); r <= std::min(rows - 1, head); ++r) {
        const float k = (s.head - static_cast<float>(r)) / s.len;
        const float bright = r == head ? 1.35F : 0.85F * std::pow(1 - k, 1.4F);
        if (bright > 0.04F) m_screen.put(c, r, m_glyphs[static_cast<size_t>(r) * cols + c], bright * boost);
      }
    }
  }

  // ── monitor ──
  void stepMonitor(const Beat& b) {
    m_sinceSample += b.dt;
    if (m_sinceSample >= 1.0 || m_sys.cores.empty()) {
      m_sampler.sample(m_sys, m_sinceSample);
      m_sinceSample = 0;
      m_netHistory.push_back(m_sys.down);
      while (m_netHistory.size() > 256) m_netHistory.pop_front();
    }
  }
  // an htop meter: [|||||     ]
  void meter(int col, int row, int width, double frac, float bright) {
    m_screen.put(col, row, U'[', 0.6F * bright);
    m_screen.put(col + width + 1, row, U']', 0.6F * bright);
    const int n = static_cast<int>(std::lround(std::clamp(frac, 0.0, 1.0) * width));
    for (int c = 0; c < n; ++c) m_screen.put(col + 1 + c, row, U'|', 0.85F * bright);
  }
  void drawMonitor(float boost) {
    const int cols = m_screen.cols(), rows = m_screen.rows();
    int r = 0;
    m_screen.text(1, r, std::format("{} ── up {} ── {}", m_sys.host, m_preview >= 0 ? std::string("3:12") : uptimeText(),
                                    m_preview >= 0 ? std::string("21:42:07") : clockText()),
                  1.0F * boost);
    r += 2;
    const int mw = std::max(6, cols - 16);
    // the cores: one a row, or two a row on a short screen
    const size_t n = m_sys.cores.size();
    const bool twoUp = static_cast<int>(n) > rows / 3 && cols >= 50;
    const int perRow = twoUp ? 2 : 1, cw = twoUp ? (cols - 2) / 2 : cols - 2;
    for (size_t i = 0; i < n && r < rows; ++i) {
      const int c = 1 + static_cast<int>(i % static_cast<size_t>(perRow)) * cw;
      const std::string label = std::format("{:<3}", i);
      m_screen.text(c, r, label, 0.7F * boost);
      const int w = std::max(4, cw - 12);
      meter(c + 4, r, w, m_sys.cores[i], boost);
      m_screen.text(c + 6 + w, r, std::format("{:3.0f}%", m_sys.cores[i] * 100), 0.8F * boost);
      if (i % static_cast<size_t>(perRow) == static_cast<size_t>(perRow - 1) || i + 1 == n) ++r;
    }
    if (r < rows) {
      m_screen.text(1, r, "Mem", 0.7F * boost);
      meter(5, r, mw, m_sys.memUsed / m_sys.memTotal, boost);
      m_screen.text(7 + mw, r++, bytes(m_sys.memUsed), 0.8F * boost);
    }
    if (r < rows && m_sys.swapTotal > 0) {
      m_screen.text(1, r, "Swp", 0.7F * boost);
      meter(5, r, mw, m_sys.swapUsed / m_sys.swapTotal, boost);
      m_screen.text(7 + mw, r++, bytes(m_sys.swapUsed), 0.8F * boost);
    }
    if (r < rows) {
      m_screen.text(1, r++, std::format("Net ↓ {}/s  ↑ {}/s", bytes(m_sys.down), bytes(m_sys.up)), 0.8F * boost);
    }
    // the download, a sparkline
    if (r < rows && !m_netHistory.empty()) {
      double most = 1;
      for (double v : m_netHistory) most = std::max(most, v);
      const int w = cols - 2;
      for (int c = 0; c < w; ++c) {
        const int idx = static_cast<int>(m_netHistory.size()) - w + c;
        if (idx < 0) continue;
        m_screen.bar(1 + c, r, static_cast<float>(m_netHistory[static_cast<size_t>(idx)] / most), 0.6F * boost);
      }
      ++r;
    }
    // the music's pulse
    if (r < rows) {
      m_screen.text(1, r, "♪", 1.0F * boost);
      meter(5, r, mw, m_live ? m_energy : 0, boost);
      if (m_flash > 0.2) m_screen.put(7 + mw, r, U'●', 1.2F * boost);
      ++r;
    }
    ++r;
    if (r < rows) m_screen.text(1, r++, "  PID USER       CPU%    MEM  COMMAND", 1.0F * boost);
    for (const Proc& p : m_sys.top) {
      if (r >= rows) break;
      m_screen.text(1, r++, std::format("{:5} {:<9} {:5.1f} {:>6}  {}", p.pid, p.user.substr(0, 9), p.cpu, bytes(p.rss), p.cmd), 0.65F * boost);
    }
  }

  // previews and tests: a screen that does not depend on the machine
  void warmUp() {
    m_warmed = true;
    m_rng = BoltRandom(11);
    m_lines.clear();
    m_session = 0;
    m_entry = 0;
    m_typed = 0;
    m_budget = 0;
    m_wait = 0;
    m_cols.clear();
    m_track = true;
    m_title = "Web";
    m_artist = "070 Shake";
    m_position = 84;
    m_length = 182;
    if (m_mode == "monitor") {
      m_sys.host = "desk";
      m_sys.cores = {0.42, 0.18, 0.66, 0.3};
      m_sys.memUsed = 3.1e9;
      m_sys.memTotal = 7.6e9;
      m_sys.swapTotal = 4e9;
      m_sys.swapUsed = 0.2e9;
      m_sys.down = 1.2e6;
      m_sys.up = 84e3;
      m_sys.top = {{2101, "you", "undershell", 12.4, 175e6}, {1, "root", "systemd", 0.3, 12e6}, {812, "you", "mpd", 2.1, 48e6}};
      for (int i = 0; i < 120; ++i) m_netHistory.push_back(1e6 * (1 + std::sin(i * 0.3)));
      return;
    }
    std::vector<float> bands(64);
    for (int f = 0; f < 240; ++f) {
      for (int i = 0; i < 64; ++i) bands[static_cast<size_t>(i)] = static_cast<float>(0.2 + 0.6 * std::exp(-i / 12.0) * (0.6 + 0.4 * std::sin(f * 0.2 + i * 0.5)));
      Beat b;
      b.dt = 1.0 / 60;
      b.live = true;
      b.energy = 0.6;
      b.kick = f % 30 == 0;
      b.strength = 0.8;
      b.bands = &bands;
      b.track = true;
      b.title = m_title;
      b.artist = m_artist;
      b.position = m_position;
      b.length = m_length;
      const double keep = m_flash;
      step(b);
      m_flash = keep;
    }
  }

  static constexpr const char* kPrompt = "you@desk:~$ ";
  std::string m_mode = "code";
  float m_textScale = 1;
  TermScreen::Crt m_crt;
  TermScreen m_screen;
  double m_hits = 0.7, m_preview = -1, m_time = 0, m_flash = 0, m_energy = 0;
  bool m_live = false, m_warmed = false;
  BoltRandom m_rng{2028};
  // the song
  bool m_track = false, m_playing = false;
  std::string m_title, m_artist;
  double m_position = 0, m_length = 0;
  std::vector<float> m_bands;
  // code
  struct Shown {
    std::string text;
    float bright;
  };
  std::deque<Shown> m_lines;
  size_t m_session = 0, m_entry = 0, m_typed = 0;
  double m_budget = 0, m_wait = 0;
  std::string m_current;
  // spectrum
  std::vector<float> m_levels, m_peaks;
  // matrix
  std::vector<Stream> m_cols;
  std::vector<char32_t> m_glyphs;
  // monitor
  Sampler m_sampler;
  System m_sys;
  double m_sinceSample = 1;
  std::deque<double> m_netHistory;
};

}  // namespace

std::unique_ptr<StrokeLook> makeTerminal() { return std::make_unique<Terminal>(); }

}  // namespace undershell
