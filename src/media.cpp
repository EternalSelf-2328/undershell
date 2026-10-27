// SPDX-License-Identifier: GPL-3.0-or-later
#include "media.hpp"

#include "net.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <systemd/sd-bus.h>

#include <mutex>

namespace undershell {

namespace fs = std::filesystem;

namespace {
constexpr const char* kDest = "dev.noctalia.Mpris";
constexpr const char* kPath = "/dev/noctalia/Mpris";
constexpr const char* kIface = "dev.noctalia.Mpris";
// Ryoku's ArtColor.accentOf: the most vibrant swatch, lifted to a vivid,
// readable version of itself.
bool accentOf(GdkPixbuf* pb, Color& out) {
  GdkPixbuf* small = gdk_pixbuf_scale_simple(pb, 48, 48, GDK_INTERP_BILINEAR);
  if (!small) return false;
  const int n = gdk_pixbuf_get_n_channels(small), stride = gdk_pixbuf_get_rowstride(small);
  const guchar* px = gdk_pixbuf_read_pixels(small);
  struct Bucket {
    double r = 0, g = 0, b = 0;
    int count = 0;
  };
  std::vector<Bucket> buckets(512);
  for (int y = 0; y < 48; ++y)
    for (int x = 0; x < 48; ++x) {
      const guchar* p = px + y * stride + x * n;
      Bucket& bk = buckets[((p[0] >> 5) << 6) | ((p[1] >> 5) << 3) | (p[2] >> 5)];
      bk.r += p[0];
      bk.g += p[1];
      bk.b += p[2];
      ++bk.count;
    }
  g_object_unref(small);
  std::sort(buckets.begin(), buckets.end(), [](const Bucket& a, const Bucket& b) { return a.count > b.count; });
  double bestScore = 0.10;
  Color best;
  bool found = false;
  for (int i = 0; i < 8 && buckets[static_cast<size_t>(i)].count > 0; ++i) {
    const Bucket& bk = buckets[static_cast<size_t>(i)];
    const double r = bk.r / bk.count / 255, g = bk.g / bk.count / 255, b = bk.b / bk.count / 255;
    const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    const double v = (mx > 0 ? (mx - mn) / mx : 0) * mx;
    if (v > bestScore) {
      bestScore = v;
      best = {static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), 1};
      found = true;
    }
  }
  if (!found) return false;
  // HSL lift: saturation >= 0.5, lightness in [0.52, 0.68]
  const float r = best.r, g = best.g, b = best.b;
  const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
  float h = 0, s = 0, l = (mx + mn) / 2;
  if (mx != mn) {
    const float d = mx - mn;
    s = l > 0.5F ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
    else if (mx == g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h /= 6;
  }
  s = std::max(0.5F, s);
  l = std::clamp(l, 0.52F, 0.68F);
  auto hue = [](float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0F / 6) return p + (q - p) * 6 * t;
    if (t < 0.5F) return q;
    if (t < 2.0F / 3) return p + (q - p) * (2.0F / 3 - t) * 6;
    return p;
  };
  const float q = l < 0.5F ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
  out = {hue(p, q, h + 1.0F / 3), hue(p, q, h), hue(p, q, h - 1.0F / 3), 1};
  return true;
}

// "[mm:ss.xx]" stamps (one or more) before a line's words. Read by hand, with
// every number and every line kept short: the text comes from anyone who
// published to LRCLIB.
std::vector<LyricLine> parseLrcImpl(const std::string& text) {
  std::vector<LyricLine> lines;
  std::istringstream in(text);
  auto digits = [](const std::string& s, size_t& i, size_t most, long& out) {
    const size_t from = i;
    out = 0;
    while (i < s.size() && i - from < most && std::isdigit(static_cast<unsigned char>(s[i]))) out = out * 10 + (s[i++] - '0');
    return i > from;
  };
  for (std::string raw; std::getline(in, raw) && lines.size() < 20000;) {
    if (raw.size() > 4000) raw.resize(4000);
    std::vector<int> stamps;
    size_t i = 0;
    while (i < raw.size() && raw[i] == '[') {
      size_t j = i + 1;
      long min = 0, sec = 0, frac = 0;
      if (!digits(raw, j, 4, min) || j >= raw.size() || raw[j] != ':') break;
      ++j;
      if (!digits(raw, j, 2, sec)) break;
      double f = 0;
      if (j < raw.size() && raw[j] == '.') {
        ++j;
        const size_t from = j;
        if (!digits(raw, j, 3, frac)) break;
        f = static_cast<double>(frac) / std::pow(10.0, static_cast<double>(j - from));
      }
      if (j >= raw.size() || raw[j] != ']') break;
      stamps.push_back(static_cast<int>((min * 60 + sec + f) * 1000));
      i = j + 1;
    }
    if (stamps.empty()) continue;
    const std::string rest = raw.substr(i);
    const auto b = rest.find_first_not_of(" \t\r"), e = rest.find_last_not_of(" \t\r");
    const std::string txt = b == std::string::npos ? "" : rest.substr(b, e - b + 1);
    for (int t : stamps) lines.push_back({t, txt});
  }
  std::sort(lines.begin(), lines.end(), [](const LyricLine& a, const LyricLine& b) { return a.ms < b.ms; });
  return lines;
}

struct LyricsResult {
  MediaState::Lyrics kind = MediaState::Lyrics::None;
  std::vector<LyricLine> lines;
  std::string plain;
};

bool fromRecord(const nlohmann::json& r, LyricsResult& out) {
  if (!r.is_object()) return false;
  if (r.value("instrumental", false)) {
    out.kind = MediaState::Lyrics::None;
    return true;
  }
  if (r.contains("syncedLyrics") && r["syncedLyrics"].is_string()) {
    auto lines = parseLrcImpl(r["syncedLyrics"].get<std::string>());
    if (!lines.empty()) {
      out.kind = MediaState::Lyrics::Synced;
      out.lines = std::move(lines);
      return true;
    }
  }
  if (r.contains("plainLyrics") && r["plainLyrics"].is_string() && !r["plainLyrics"].get<std::string>().empty()) {
    out.kind = MediaState::Lyrics::Plain;
    out.plain = r["plainLyrics"].get<std::string>();
    return true;
  }
  return false;
}

int onSignal(sd_bus_message*, void* ud, sd_bus_error*) {
  static_cast<MediaService*>(ud)->requestRefresh();
  return 0;
}

int onReply(sd_bus_message* m, void* ud, sd_bus_error*) {
  static_cast<MediaService*>(ud)->onPlayerReply(m);
  return 0;
}

constexpr const char* kMprisPrefix = "org.mpris.MediaPlayer2.";
constexpr const char* kMprisPath = "/org/mpris/MediaPlayer2";
constexpr const char* kMprisPlayer = "org.mpris.MediaPlayer2.Player";

// NameOwnerChanged (name, old owner, new owner)
int onOwner(sd_bus_message* m, void* ud, sd_bus_error*) {
  const char *name = nullptr, *from = nullptr, *to = nullptr;
  if (sd_bus_message_read(m, "sss", &name, &from, &to) < 0 || !name) return 0;
  auto* self = static_cast<MediaService*>(ud);
  if (std::string_view(name) == kDest) self->onAggregator(to && *to);
  else self->requestRefresh();
  return 0;
}

struct ScanCall {
  MediaService* self;
  uint64_t scan;
  std::string name;
  bool root;
};

int onNames(sd_bus_message* m, void* ud, sd_bus_error*) {
  auto* c = static_cast<ScanCall*>(ud);
  c->self->onPlayerNames(m, c->scan);
  delete c;
  return 0;
}

int onProps(sd_bus_message* m, void* ud, sd_bus_error*) {
  auto* c = static_cast<ScanCall*>(ud);
  c->self->onPlayerProps(m, c->scan, c->name, c->root);
  delete c;
  return 0;
}

// An integer however the player typed it (mpris:length is an x by the spec,
// but t, i, u and even d turn up).
int64_t readNumber(sd_bus_message* m, const char* contents) {
  const char t = contents && contents[0] && !contents[1] ? contents[0] : 0;
  int64_t out = 0;
  if (t == 'x' || t == 't') sd_bus_message_read(m, "v", contents, &out);
  else if (t == 'i' || t == 'u') {
    int32_t v = 0;
    sd_bus_message_read(m, "v", contents, &v);
    out = t == 'u' ? static_cast<uint32_t>(v) : v;
  } else if (t == 'd') {
    double v = 0;
    sd_bus_message_read(m, "v", "d", &v);
    out = static_cast<int64_t>(v);
  } else sd_bus_message_skip(m, "v");
  return out;
}

std::string readString(sd_bus_message* m, const char* contents) {
  const char* v = nullptr;
  if (contents && (std::string_view(contents) == "s" || std::string_view(contents) == "o")) sd_bus_message_read(m, "v", contents, &v);
  else sd_bus_message_skip(m, "v");
  return v ? v : "";
}

bool readBool(sd_bus_message* m, const char* contents) {
  int v = 0;
  if (contents && std::string_view(contents) == "b") sd_bus_message_read(m, "v", "b", &v);
  else sd_bus_message_skip(m, "v");
  return v;
}

// xesam:artist is an `as` by the spec; a few players send one string
std::string readStrings(sd_bus_message* m, const char* contents) {
  if (!contents || std::string_view(contents) != "as") return readString(m, contents);
  std::string out;
  sd_bus_message_enter_container(m, 'v', "as");
  sd_bus_message_enter_container(m, 'a', "s");
  const char* a = nullptr;
  while (sd_bus_message_read(m, "s", &a) > 0) out += (out.empty() ? "" : ", ") + std::string(a ? a : "");
  sd_bus_message_exit_container(m);
  sd_bus_message_exit_container(m);
  return out;
}

// One a{sv} of properties: the player's (Metadata, PlaybackStatus, Position,
// Can*) or the root object's (Identity, DesktopEntry).
void readMprisProps(sd_bus_message* m, MediaState& s, std::string& trackId) {
  if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) return;
  bool canPlay = false, canPause = false;
  while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
    const char* key = nullptr;
    sd_bus_message_read(m, "s", &key);
    char type = 0;
    const char* contents = nullptr;
    sd_bus_message_peek_type(m, &type, &contents);
    const std::string_view k = key ? key : "";
    if (k == "Metadata" && contents && std::string_view(contents) == "a{sv}") {
      sd_bus_message_enter_container(m, 'v', "a{sv}");
      sd_bus_message_enter_container(m, 'a', "{sv}");
      while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        const char* mk = nullptr;
        sd_bus_message_read(m, "s", &mk);
        const char* mc = nullptr;
        sd_bus_message_peek_type(m, &type, &mc);
        const std::string_view n = mk ? mk : "";
        if (n == "xesam:title") s.title = readString(m, mc);
        else if (n == "xesam:album") s.album = readString(m, mc);
        else if (n == "xesam:artist") s.artist = readStrings(m, mc);
        else if (n == "mpris:artUrl") s.artUrl = readString(m, mc);
        else if (n == "mpris:trackid") trackId = readString(m, mc);
        else if (n == "mpris:length") s.lengthUs = readNumber(m, mc);
        else sd_bus_message_skip(m, "v");
        sd_bus_message_exit_container(m);
      }
      sd_bus_message_exit_container(m);
      sd_bus_message_exit_container(m);
    } else if (k == "PlaybackStatus") s.playing = readString(m, contents) == "Playing";
    else if (k == "Position") s.positionUs = readNumber(m, contents);
    else if (k == "CanGoNext") s.canNext = readBool(m, contents);
    else if (k == "CanGoPrevious") s.canPrev = readBool(m, contents);
    else if (k == "CanPlay") canPlay = readBool(m, contents);
    else if (k == "CanPause") canPause = readBool(m, contents);
    else if (k == "CanSeek") s.canSeek = readBool(m, contents);
    else if (k == "Identity") s.identity = readString(m, contents);
    else if (k == "DesktopEntry") s.desktopEntry = readString(m, contents);
    else sd_bus_message_skip(m, "v");
    sd_bus_message_exit_container(m);
  }
  sd_bus_message_exit_container(m);
  s.canToggle = s.canToggle || canPlay || canPause;
}

// a method call that gives up after a second: a hung player must not hold
// the card back
int callAsync(sd_bus* bus, const char* dest, const char* path, const char* iface, const char* method,
              sd_bus_message_handler_t cb, void* ud, const char* arg = nullptr) {
  sd_bus_message* msg = nullptr;
  int r = sd_bus_message_new_method_call(bus, &msg, dest, path, iface, method);
  if (r >= 0 && arg) r = sd_bus_message_append(msg, "s", arg);
  if (r >= 0) r = sd_bus_call_async(bus, nullptr, msg, cb, ud, 1'000'000);
  sd_bus_message_unref(msg);
  return r;
}
}  // namespace

std::vector<LyricLine> parseLrc(const std::string& text) { return parseLrcImpl(text); }

// The file an art url points at, or "" when it is not a local one. Players
// spell it `file:///…`, `file:/…` or a bare path.
std::string artPath(const std::string& url) {
  if (url.empty()) return {};
  if (url[0] == '/') return url;
  if (url.rfind("file:", 0) != 0) return {};
  if (gchar* p = g_filename_from_uri(url.c_str(), nullptr, nullptr); p) {
    std::string out(p);
    g_free(p);
    return out;
  }
  // `file:/path`, which g_filename_from_uri turns down: unescape it by hand
  std::string rest = url.substr(5);
  while (rest.size() > 1 && rest[0] == '/' && rest[1] == '/') rest.erase(0, 1);
  if (rest.empty() || rest[0] != '/') return {};
  if (gchar* u = g_uri_unescape_string(rest.c_str(), nullptr); u) {
    std::string out(u);
    g_free(u);
    return out;
  }
  return rest;
}

// What tells one cover from another. For a local file that is the file as it
// is right now: players that keep rewriting a single path (mpd-mpris, VLC and
// the browsers all do) change the art without changing the url, and a file
// named before it is written comes back as "missing" until it appears, so the
// next poll picks it up on its own.
std::string artKey(const std::string& url) {
  const std::string path = artPath(url);
  if (path.empty()) return url;
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec) return url + "|missing";
  const auto when = fs::last_write_time(path, ec);
  return std::format("{}|{}|{}", url, ec ? 0 : when.time_since_epoch().count(), size);
}

bool accentOfPixels(const uint8_t* rgba, int w, int h, Color& out) {
  GdkPixbuf* pb = gdk_pixbuf_new_from_data(rgba, GDK_COLORSPACE_RGB, TRUE, 8, w, h, w * 4, nullptr, nullptr);
  if (!pb) return false;
  const bool ok = accentOf(pb, out);
  g_object_unref(pb);
  return ok;
}

// the wallpaper accent: Noctalia's Matugen-faithful C++ generator, or the
// matugen CLI if installed, falling back to Ryoku's ArtColor algorithm over a
// 48 px decode; cached per path, scheme and mode so widgets that reconfigure
// on a wallpaper change share the one decode
bool wallpaperAccent(const std::string& path, Color& out, const std::string& scheme, bool dark) {
  static std::mutex m;
  static std::map<std::string, std::pair<bool, Color>> cache;  // false = its picture has no vivid colour either
  std::lock_guard g(m);
  const std::string cacheKey = path + "|" + scheme + (dark ? "|d" : "|l");
  const auto it = cache.find(cacheKey);
  if (it != cache.end()) {
    if (it->second.first) out = it->second.second;
    return it->second.first;
  }
  Color found{0, 0, 0, 0};
  bool ok = false;
  if (!path.empty() && path.starts_with('/')) {  // anything the shell names, and a scene's still
    // 1. Noctalia theme generator (faithful to Matugen M3)
    if (gchar* p = g_find_program_in_path("noctalia"); p) {
      g_free(p);
      gchar* quoted = g_shell_quote(path.c_str());
      std::string cmd = std::format("noctalia theme {} {}", quoted, dark ? "--dark" : "--light");
      if (!scheme.empty()) {
        gchar* qScheme = g_shell_quote(scheme.c_str());
        cmd += std::format(" --scheme {}", qScheme);
        g_free(qScheme);
      }
      g_free(quoted);
      const std::string json = runCommand(cmd, 4);
      static const std::regex kPrim(R"re("primary"\s*:\s*"(#[0-9a-fA-F]{6})")re");
      std::smatch sm;
      if (std::regex_search(json, sm, kPrim)) {
        found = Color::fromHex(sm[1].str());
        ok = true;
      }
    }
    // 2. matugen CLI fallback if Noctalia is not installed
    if (!ok) {
      if (gchar* p = g_find_program_in_path("matugen"); p) {
        g_free(p);
        gchar* quoted = g_shell_quote(path.c_str());
        const std::string json = runCommand(std::format("matugen image {} --json hex", quoted), 4);
        g_free(quoted);
        static const std::regex kPrim(R"re("primary"\s*:\s*"(#[0-9a-fA-F]{6})")re");
        std::smatch sm;
        if (std::regex_search(json, sm, kPrim)) {
          found = Color::fromHex(sm[1].str());
          ok = true;
        }
      }
    }
    // 3. Fallback: decode 48 px and lift the most vibrant colour
    if (!ok) {
      GError* err = nullptr;
      GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), 48, 48, TRUE, &err);
      if (err) g_error_free(err);
      if (pb) {
        ok = accentOf(pb, found);
        g_object_unref(pb);
      }
    }
  }
  cache[cacheKey] = {ok, found};
  if (ok) out = found;
  return ok;
}

double MediaState::positionSec(double now) const {
  double us = static_cast<double>(positionUs);
  if (playing) us += (now - positionAt) * 1e6;
  if (lengthUs > 0) us = std::min(us, static_cast<double>(lengthUs));
  return std::max(0.0, us / 1e6);
}

MediaService::MediaService(Jobs& jobs) : m_jobs(jobs) {}

MediaService::~MediaService() {
  for (sd_bus_slot* sl : {m_ownerSlot, m_aggSlot, m_propsSlot, m_seekSlot})
    if (sl) sd_bus_slot_unref(sl);
  if (m_matchSlot) sd_bus_slot_unref(m_matchSlot);
  if (m_bus) sd_bus_flush_close_unref(m_bus);
  if (m_state.cover) glDeleteTextures(1, &m_state.cover);
}

bool MediaService::start() {
  if (sd_bus_open_user(&m_bus) < 0) {
    US_WARN("no session bus: music widgets stay empty");
    m_bus = nullptr;
    return false;
  }
  // any change on the aggregator (track, player, playback) → refresh
  const std::string match = std::format("type='signal',sender='{}',path='{}',interface='{}'", kDest, kPath, kIface);
  sd_bus_add_match(m_bus, &m_matchSlot, match.c_str(), onSignal, this);
  // Noctalia starting or quitting switches between the aggregator and the
  // players themselves
  sd_bus_add_match(m_bus, &m_aggSlot,
                   std::format("type='signal',sender='org.freedesktop.DBus',member='NameOwnerChanged',arg0='{}'", kDest).c_str(),
                   onOwner, this);
  return true;
}

int MediaService::fd() const { return m_bus ? sd_bus_get_fd(m_bus) : -1; }

void MediaService::dispatch() {
  if (!m_bus) return;
  while (sd_bus_process(m_bus, nullptr) > 0) {
  }
}

void MediaService::setWanted(bool wanted, bool lyrics) {
  if (wanted && !m_wanted) m_nextPoll = 0;
  m_wanted = wanted;
  m_wantLyrics = lyrics;
}

void MediaService::requestRefresh() { m_nextPoll = 0; }

int MediaService::pollTimeoutMs(double now) const {
  if (!m_bus || !m_wanted) return -1;
  return std::max(0, static_cast<int>((m_nextPoll - now) * 1000));
}

void MediaService::poll(double now) {
  if (!m_bus || !m_wanted || m_inFlight || now < m_nextPoll) return;
  // playing: 1 s keeps the position honest; paused/idle: slow heartbeat,
  // signals bring changes in immediately
  m_nextPoll = now + (m_state.playing ? 1.0 : 5.0);
  if (m_direct) {
    scanPlayers();
    return;
  }
  if (sd_bus_call_method_async(m_bus, nullptr, kDest, kPath, kIface, "GetActivePlayer", onReply, this, "") >= 0)
    m_inFlight = true;
}

void MediaService::clearPlayer() {
  m_activeBus.clear();
  m_activeTrack.clear();
  if (!m_state.present) return;
  const GLuint keepTex = m_state.cover;
  m_state = MediaState{};
  m_state.cover = keepTex;
  m_state.coverW = m_state.coverH = 0;
  m_coverFor.clear();
  ++m_generation;
}

void MediaService::onPlayerReply(sd_bus_message* m) {
  m_inFlight = false;
  if (sd_bus_message_is_method_error(m, nullptr)) {
    // no aggregator on this bus (no Noctalia): ask the players themselves
    if (sd_bus_message_is_method_error(m, "org.freedesktop.DBus.Error.ServiceUnknown") ||
        sd_bus_message_is_method_error(m, "org.freedesktop.DBus.Error.NameHasNoOwner")) {
      enterDirect();
      scanPlayers();
      return;
    }
    clearPlayer();
    return;
  }
  int found = 0;
  if (sd_bus_message_read(m, "b", &found) < 0) return;
  if (!found) {
    clearPlayer();
    return;
  }
  MediaState s = m_state;
  s.present = true;
  std::string artists, busName, trackId;
  if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) return;
  while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
    const char* key = nullptr;
    sd_bus_message_read(m, "s", &key);
    char type = 0;
    const char* contents = nullptr;
    sd_bus_message_peek_type(m, &type, &contents);
    const std::string k = key ? key : "";
    if (contents && std::string_view(contents) == "s") {
      const char* v = nullptr;
      sd_bus_message_read(m, "v", "s", &v);
      const std::string sv = v ? v : "";
      if (k == "title") s.title = sv;
      else if (k == "album") s.album = sv;
      else if (k == "art_url") s.artUrl = sv;
      else if (k == "identity") s.identity = sv;
      else if (k == "desktop_entry") s.desktopEntry = sv;
      else if (k == "playback_status") s.playing = sv == "Playing";
      else if (k == "bus_name") busName = sv;
      else if (k == "track_id") trackId = sv;
    } else if (contents && std::string_view(contents) == "b") {
      int v = 0;
      sd_bus_message_read(m, "v", "b", &v);
      if (k == "can_go_next") s.canNext = v;
      else if (k == "can_go_previous") s.canPrev = v;
      else if (k == "can_pause" || k == "can_play") s.canToggle = s.canToggle || v;
      else if (k == "can_seek") s.canSeek = v;
    } else if (contents && std::string_view(contents) == "x") {
      int64_t v = 0;
      sd_bus_message_read(m, "v", "x", &v);
      if (k == "length_us") s.lengthUs = v;
      else if (k == "position_us") s.positionUs = v;
    } else if (contents && std::string_view(contents) == "as") {
      sd_bus_message_enter_container(m, 'v', "as");
      sd_bus_message_enter_container(m, 'a', "s");
      const char* a = nullptr;
      artists.clear();
      while (sd_bus_message_read(m, "s", &a) > 0) artists += (artists.empty() ? "" : ", ") + std::string(a ? a : "");
      sd_bus_message_exit_container(m);
      sd_bus_message_exit_container(m);
      if (k == "artists") s.artist = artists;
    } else {
      sd_bus_message_skip(m, "v");
    }
    sd_bus_message_exit_container(m);
  }
  sd_bus_message_exit_container(m);
  apply(std::move(s), busName, trackId);
}

void MediaService::apply(MediaState s, const std::string& busName, const std::string& trackId) {
  s.present = true;
  s.positionAt = nowSeconds();
  s.trackKey = busName + "|" + trackId + "|" + s.title;
  const bool trackChanged = s.trackKey != m_state.trackKey;
  const bool changed = trackChanged || s.playing != m_state.playing || s.present != m_state.present ||
                       s.canNext != m_state.canNext || s.canPrev != m_state.canPrev || s.lengthUs != m_state.lengthUs;
  m_state = std::move(s);
  if (trackChanged) onTrackChanged();
  syncCover();
  if (changed) ++m_generation;
  if (m_state.playing) m_nextPoll = std::min(m_nextPoll, nowSeconds() + 1.0);
}

// ── without Noctalia: the MPRIS players themselves ──────────────────────────

void MediaService::enterDirect() {
  if (m_direct) return;
  m_direct = true;
  US_INFO("media: no {} on the bus, reading the MPRIS players directly", kDest);
  // players appearing and leaving, and anything they change, refresh at once
  sd_bus_add_match(m_bus, &m_ownerSlot,
                   "type='signal',sender='org.freedesktop.DBus',member='NameOwnerChanged',arg0namespace='org.mpris.MediaPlayer2'",
                   onOwner, this);
  sd_bus_add_match(m_bus, &m_propsSlot,
                   "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',path='/org/mpris/MediaPlayer2'",
                   onSignal, this);
  sd_bus_add_match(m_bus, &m_seekSlot,
                   "type='signal',interface='org.mpris.MediaPlayer2.Player',member='Seeked',path='/org/mpris/MediaPlayer2'", onSignal,
                   this);
}

// Noctalia came (back): its aggregator knows which player the bar shows
void MediaService::onAggregator(bool present) {
  if (!present || !m_direct) return;
  m_direct = false;
  for (sd_bus_slot** sl : {&m_ownerSlot, &m_propsSlot, &m_seekSlot}) *sl = sd_bus_slot_unref(*sl);
  US_INFO("media: {} is back", kDest);
  requestRefresh();
}

void MediaService::scanPlayers() {
  if (m_scanPending > 0) return;
  ++m_scan;
  m_players.clear();
  if (callAsync(m_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", onNames,
                new ScanCall{this, m_scan, {}, false}) >= 0) {
    m_scanPending = 1;
    m_inFlight = true;
  }
}

void MediaService::onPlayerNames(sd_bus_message* m, uint64_t scan) {
  if (scan != m_scan) return;
  m_scanPending = 0;
  std::vector<std::string> names;
  if (!sd_bus_message_is_method_error(m, nullptr) && sd_bus_message_enter_container(m, 'a', "s") >= 0) {
    const char* n = nullptr;
    while (sd_bus_message_read(m, "s", &n) > 0)
      // playerctld only mirrors another player
      if (n && std::string_view(n).starts_with(kMprisPrefix) && std::string_view(n) != "org.mpris.MediaPlayer2.playerctld")
        names.emplace_back(n);
    sd_bus_message_exit_container(m);
  }
  for (auto it = m_idents.begin(); it != m_idents.end();)
    it = std::ranges::find(names, it->first) == names.end() ? m_idents.erase(it) : std::next(it);
  for (auto& n : names) {
    m_players[n];
    if (callAsync(m_bus, n.c_str(), kMprisPath, "org.freedesktop.DBus.Properties", "GetAll", onProps,
                  new ScanCall{this, scan, n, false}, kMprisPlayer) >= 0)
      ++m_scanPending;
    // the name and the .desktop entry never change: once per player
    if (!m_idents.contains(n) && callAsync(m_bus, n.c_str(), kMprisPath, "org.freedesktop.DBus.Properties", "GetAll", onProps,
                                           new ScanCall{this, scan, n, true}, "org.mpris.MediaPlayer2") >= 0)
      ++m_scanPending;
  }
  if (m_scanPending == 0) finishScan();
}

void MediaService::onPlayerProps(sd_bus_message* m, uint64_t scan, const std::string& name, bool root) {
  if (scan != m_scan) return;
  if (!sd_bus_message_is_method_error(m, nullptr)) {
    if (root) {
      MediaState s;
      std::string unused;
      readMprisProps(m, s, unused);
      m_idents[name] = {s.identity, s.desktopEntry};
    } else {
      Player& p = m_players[name];
      readMprisProps(m, p.s, p.trackId);
      p.props = true;
    }
  }
  if (--m_scanPending == 0) finishScan();
}

// The player the card follows: the one playing (staying with the current one
// while it plays), else the current one, else the one heard last.
void MediaService::finishScan() {
  m_inFlight = false;
  const double now = nowSeconds();
  std::erase_if(m_players, [](const auto& kv) { return !kv.second.props; });
  for (auto it = m_lastPlaying.begin(); it != m_lastPlaying.end();)
    it = m_players.contains(it->first) ? std::next(it) : m_lastPlaying.erase(it);
  for (auto& [n, p] : m_players)
    if (p.s.playing) m_lastPlaying[n] = now;
  std::string pick;
  auto playing = [&](const std::string& n) { return m_players.contains(n) && m_players[n].s.playing; };
  if (playing(m_activeBus)) pick = m_activeBus;
  for (auto& [n, p] : m_players)
    if (pick.empty() && p.s.playing) pick = n;
  if (pick.empty() && m_players.contains(m_activeBus)) pick = m_activeBus;
  double last = -1;
  if (pick.empty())
    for (auto& [n, t] : m_lastPlaying)
      if (t > last) {
        last = t;
        pick = n;
      }
  if (pick.empty() && !m_players.empty()) pick = m_players.begin()->first;
  if (pick.empty()) {
    clearPlayer();
    return;
  }
  const Player& p = m_players[pick];
  MediaState s = m_state;
  s.title = p.s.title;
  s.artist = p.s.artist;
  s.album = p.s.album;
  s.artUrl = p.s.artUrl;
  s.playing = p.s.playing;
  s.lengthUs = p.s.lengthUs;
  s.positionUs = p.s.positionUs;
  s.canNext = p.s.canNext;
  s.canPrev = p.s.canPrev;
  s.canToggle = p.s.canToggle;
  s.canSeek = p.s.canSeek;
  const auto& id = m_idents[pick];
  s.identity = id.first.empty() ? pick.substr(std::string_view(kMprisPrefix).size()) : id.first;
  s.desktopEntry = id.second;
  m_activeBus = pick;
  m_activeTrack = p.trackId;
  apply(std::move(s), pick, p.trackId);
}

void MediaService::callPlayer(const char* method, const char* sig, const void* a, const void* b) {
  if (m_activeBus.empty()) return;
  const char* bus = m_activeBus.c_str();
  if (!sig) sd_bus_call_method_async(m_bus, nullptr, bus, kMprisPath, kMprisPlayer, method, nullptr, nullptr, "");
  else if (std::string_view(sig) == "x")
    sd_bus_call_method_async(m_bus, nullptr, bus, kMprisPath, kMprisPlayer, method, nullptr, nullptr, sig,
                             *static_cast<const int64_t*>(a));
  else  // "ox"
    sd_bus_call_method_async(m_bus, nullptr, bus, kMprisPath, kMprisPlayer, method, nullptr, nullptr, sig,
                             static_cast<const char*>(a), *static_cast<const int64_t*>(b));
  requestRefresh();
}

void MediaService::onTrackChanged() {
  m_state.hasAccent = false;
  m_state.lyrics = MediaState::Lyrics::None;
  m_state.lines.clear();
  m_state.plain.clear();
  m_lyricsFor.clear();
  if (m_wantLyrics) fetchLyrics();
}

// Follows the art url wherever it goes. Only Spotify hands over a whole track
// at once; mpd-mpris, VLC, the browsers and most of the rest publish the title
// first and the art once they have written it, so the sleeve has to follow the
// url, not the track.
void MediaService::syncCover() {
  const std::string key = artKey(m_state.artUrl);
  if (key == m_coverFor) return;
  fetchCover(m_state.artUrl, key);
}

namespace {
// A cover's bytes decoded, from the formats a cover comes in and up to a
// sane size: the bytes are fed in pieces and the decoding stops as soon as
// the header says the picture is something else or absurdly large (a few
// bytes of PNG can promise gigabytes of pixels). Returns the loader that
// owns `out` (or nullptr).
GdkPixbufLoader* decodeCover(const std::string& bytes, GdkPixbuf*& out) {
  out = nullptr;
  GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
  bool tooBig = false;
  g_signal_connect(loader, "size-prepared", G_CALLBACK(+[](GdkPixbufLoader*, int w, int h, gpointer big) {
                     if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || static_cast<int64_t>(w) * h > 40'000'000) *static_cast<bool*>(big) = true;
                   }),
                   &tooBig);
  bool ok = true, checked = false;
  for (size_t at = 0; ok && at < bytes.size() && !tooBig; at += 16384) {
    const size_t n = std::min<size_t>(16384, bytes.size() - at);
    ok = gdk_pixbuf_loader_write(loader, reinterpret_cast<const guchar*>(bytes.data() + at), n, nullptr);
    if (ok && !checked) {
      if (GdkPixbufFormat* f = gdk_pixbuf_loader_get_format(loader)) {
        checked = true;
        const std::string name = gdk_pixbuf_format_get_name(f);
        ok = name == "png" || name == "jpeg" || name == "webp" || name == "gif" || name == "bmp";
      }
    }
  }
  gdk_pixbuf_loader_close(loader, nullptr);
  if (ok && !tooBig && checked) out = gdk_pixbuf_loader_get_pixbuf(loader);
  if (!out) {
    g_object_unref(loader);
    return nullptr;
  }
  return loader;
}
}  // namespace

void MediaService::fetchCover(const std::string& url, const std::string& key) {
  m_coverFor = key;
  if (url.empty()) {
    m_state.coverW = m_state.coverH = 0;
    m_state.hasAccent = false;
    return;
  }
  m_jobs.run([this, url, key]() -> Jobs::Done {
    // nothing came of it: the sleeve goes back to the placeholder instead of
    // keeping the last song's art
    auto giveUp = [this, key]() -> Jobs::Done {
      return [this, key]() {
        if (m_coverFor != key) return;
        m_state.coverW = m_state.coverH = 0;
        m_state.hasAccent = false;
        ++m_generation;
      };
    };
    std::string bytes, cachedFile;
    if (url.rfind("data:", 0) == 0) {
      // some players hand the art over inline (`data:image/jpeg;base64,…`)
      const size_t comma = url.find(',');
      if (comma != std::string::npos) {
        const std::string head = url.substr(5, comma - 5), body = url.substr(comma + 1);
        if (head.find("base64") != std::string::npos) {
          gsize n = 0;
          if (guchar* d = g_base64_decode(body.c_str(), &n); d) {
            bytes.assign(reinterpret_cast<char*>(d), n);
            g_free(d);
          }
        } else if (gchar* u = g_uri_unescape_string(body.c_str(), nullptr); u) {
          bytes = u;
          g_free(u);
        }
      }
    } else if (url.rfind("http", 0) == 0) {
      cachedFile = cacheDir("covers") + "/" + hashOf(url);
      bytes = readRegularFile(cachedFile);
      if (!bytes.empty()) cachedFile.clear();  // already kept
      // the address came from another program: the internet only, not this machine or its network
      else if (!httpGet(url, bytes, 10, 16u << 20, true)) bytes.clear();
    } else if (const std::string path = artPath(url); !path.empty()) {
      bytes = readRegularFile(path);  // a file, not a device or a pipe; 16 MB at most
    }
    if (bytes.empty()) return giveUp();
    GdkPixbuf* pb = nullptr;
    GdkPixbufLoader* loader = decodeCover(bytes, pb);
    if (!pb) {
      if (loader) g_object_unref(loader);
      return giveUp();
    }
    if (!cachedFile.empty()) writeFileAtomic(cachedFile, bytes);  // kept only once it proved to be a picture
    // at most 512 px for the texture; square-ish covers stay sharp at card size
    const int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb);
    const double k = std::min(1.0, 512.0 / std::max(w, h));
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pb, std::max(1, static_cast<int>(w * k)), std::max(1, static_cast<int>(h * k)),
                                                GDK_INTERP_BILINEAR);
    GdkPixbuf* rgba = gdk_pixbuf_add_alpha(scaled, FALSE, 0, 0, 0);
    Color accent;
    const bool hasAccent = accentOf(pb, accent);
    const int tw = gdk_pixbuf_get_width(rgba), th = gdk_pixbuf_get_height(rgba), stride = gdk_pixbuf_get_rowstride(rgba);
    std::vector<uint8_t> pixels(static_cast<size_t>(tw) * th * 4);
    const guchar* src = gdk_pixbuf_read_pixels(rgba);
    for (int y = 0; y < th; ++y) std::copy_n(src + y * stride, tw * 4, pixels.data() + static_cast<size_t>(y) * tw * 4);
    g_object_unref(rgba);
    g_object_unref(scaled);
    g_object_unref(loader);
    return [this, key, pixels = std::move(pixels), tw, th, hasAccent, accent]() mutable {
      if (m_coverFor != key) return;  // the song moved on
      m_pendingRgba = std::move(pixels);
      m_pendingW = tw;
      m_pendingH = th;
      m_pendingKey = key;
      if (hasAccent) {
        m_state.accent = accent;
        m_state.hasAccent = true;
      }
      ++m_generation;
    };
  });
}

void MediaService::uploadPending() {
  if (m_pendingRgba.empty()) return;
  if (!m_state.cover) glGenTextures(1, &m_state.cover);
  glBindTexture(GL_TEXTURE_2D, m_state.cover);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_pendingW, m_pendingH, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_pendingRgba.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  m_state.coverW = m_pendingW;
  m_state.coverH = m_pendingH;
  m_pendingRgba.clear();
  m_pendingRgba.shrink_to_fit();
}

void MediaService::fetchLyrics() {
  const std::string key = m_state.artist + "\n" + m_state.title;
  if (key == m_lyricsFor || m_state.title.empty()) return;
  m_lyricsFor = key;
  m_state.lyrics = MediaState::Lyrics::Searching;
  const std::string title = m_state.title, artist = m_state.artist, album = m_state.album;
  const int64_t lengthUs = m_state.lengthUs;
  m_jobs.run([this, key, title, artist, album, lengthUs]() -> Jobs::Done {
    const std::string file = cacheDir("lyrics") + "/" + hashOf(key) + ".json";
    LyricsResult res;
    bool ok = false;
    std::string cached = readFile(file);
    if (!cached.empty()) {
      try {
        ok = fromRecord(nlohmann::json::parse(cached), res) || true;
      } catch (...) {
        ok = false;
      }
    }
    if (!ok) {
      const std::string q = "track_name=" + urlEncode(title) + "&artist_name=" + urlEncode(artist);
      std::string url = "https://lrclib.net/api/get?" + q;
      if (!album.empty()) url += "&album_name=" + urlEncode(album);
      if (lengthUs > 0) url += "&duration=" + std::to_string(static_cast<long>(std::lround(lengthUs / 1e6)));
      std::string body;
      nlohmann::json record;
      if (httpGet(url, body)) {
        try {
          record = nlohmann::json::parse(body);
          ok = fromRecord(record, res);
        } catch (...) {
        }
      }
      if (!ok) {
        body.clear();
        if (httpGet("https://lrclib.net/api/search?" + q, body)) {
          try {
            for (auto& r : nlohmann::json::parse(body)) {
              LyricsResult cand;
              if (fromRecord(r, cand) && (!ok || (res.kind != MediaState::Lyrics::Synced && cand.kind == MediaState::Lyrics::Synced))) {
                res = cand;
                record = r;
                ok = true;
              }
            }
          } catch (...) {
          }
        }
      }
      // cache the answer (an empty object records "nothing found")
      writeFileAtomic(file, ok ? record.dump() : std::string("{}"));
    }
    return [this, key, res = std::move(res)]() mutable {
      if (m_lyricsFor != key) return;
      m_state.lyrics = res.kind;
      m_state.lines = std::move(res.lines);
      m_state.plain = std::move(res.plain);
      ++m_generation;
    };
  });
}

void MediaService::call(const char* method, const char* sig, int64_t arg) {
  if (!m_bus) return;
  if (sig) sd_bus_call_method_async(m_bus, nullptr, kDest, kPath, kIface, method, nullptr, nullptr, sig, arg);
  else sd_bus_call_method_async(m_bus, nullptr, kDest, kPath, kIface, method, nullptr, nullptr, "");
  requestRefresh();
}

void MediaService::toggle() { m_direct ? callPlayer("PlayPause") : call("PlayPauseActive"); }
void MediaService::next() { m_direct ? callPlayer("Next") : call("NextActive"); }
void MediaService::previous() { m_direct ? callPlayer("Previous") : call("PreviousActive"); }
void MediaService::seek(double seconds) {
  const int64_t to = static_cast<int64_t>(std::max(0.0, seconds) * 1e6);
  if (!m_direct) call("SetPositionActive", "x", to);
  else if (sd_bus_object_path_is_valid(m_activeTrack.c_str())) callPlayer("SetPosition", "ox", m_activeTrack.c_str(), &to);
  else {
    // no track id to aim at: a relative seek from where it is now
    const int64_t by = to - static_cast<int64_t>(m_state.positionSec(nowSeconds()) * 1e6);
    callPlayer("Seek", "x", &by);
  }
  m_state.positionUs = to;
  m_state.positionAt = nowSeconds();
}

}  // namespace undershell
