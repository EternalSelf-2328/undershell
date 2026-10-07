// SPDX-License-Identifier: GPL-3.0-or-later
#include "media.hpp"

#include <algorithm>
#include <cmath>
#include <curl/curl.h>
#include <filesystem>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <systemd/sd-bus.h>

namespace undershell {

namespace fs = std::filesystem;

namespace {
constexpr const char* kDest = "dev.noctalia.Mpris";
constexpr const char* kPath = "/dev/noctalia/Mpris";
constexpr const char* kIface = "dev.noctalia.Mpris";
constexpr const char* kUserAgent = "undershell/0.3 (https://github.com/ryoku-dev/ryoku port)";

std::string cacheDir(const char* sub) {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  std::string d = std::string(xdg && *xdg ? xdg : expandHome("~/.cache")) + "/undershell/" + sub;
  std::error_code ec;
  fs::create_directories(d, ec);
  return d;
}

std::string hashOf(const std::string& s) {
  gchar* h = g_compute_checksum_for_string(G_CHECKSUM_SHA1, s.c_str(), -1);
  std::string out(h);
  g_free(h);
  return out.substr(0, 20);
}

size_t curlWrite(char* p, size_t size, size_t n, void* ud) {
  static_cast<std::string*>(ud)->append(p, size * n);
  return size * n;
}

// blocking GET (worker threads only)
bool httpGet(const std::string& url, std::string& body, long timeoutSec = 10) {
  CURL* c = curl_easy_init();
  if (!c) return false;
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWrite);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, timeoutSec);
  curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  const CURLcode rc = curl_easy_perform(c);
  long status = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(c);
  return rc == CURLE_OK && status >= 200 && status < 300;
}

std::string urlEncode(const std::string& s) {
  gchar* e = g_uri_escape_string(s.c_str(), nullptr, FALSE);
  std::string out(e);
  g_free(e);
  return out;
}

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

std::vector<LyricLine> parseLrcImpl(const std::string& text) {
  std::vector<LyricLine> lines;
  static const std::regex kStamp(R"(^\[(\d+):(\d+(?:\.\d+)?)\])");
  std::istringstream in(text);
  for (std::string raw; std::getline(in, raw);) {
    std::vector<int> stamps;
    std::smatch m;
    std::string rest = raw;
    while (std::regex_search(rest, m, kStamp)) {
      stamps.push_back(static_cast<int>((std::stoi(m[1]) * 60 + std::stod(m[2])) * 1000));
      rest = m.suffix();
    }
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

double MediaState::positionSec(double now) const {
  double us = static_cast<double>(positionUs);
  if (playing) us += (now - positionAt) * 1e6;
  if (lengthUs > 0) us = std::min(us, static_cast<double>(lengthUs));
  return std::max(0.0, us / 1e6);
}

MediaService::MediaService(Jobs& jobs) : m_jobs(jobs) { curl_global_init(CURL_GLOBAL_DEFAULT); }

MediaService::~MediaService() {
  if (m_matchSlot) sd_bus_slot_unref(m_matchSlot);
  if (m_bus) sd_bus_flush_close_unref(m_bus);
  if (m_state.cover) glDeleteTextures(1, &m_state.cover);
  curl_global_cleanup();
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
  if (sd_bus_call_method_async(m_bus, nullptr, kDest, kPath, kIface, "GetActivePlayer", onReply, this, "") >= 0)
    m_inFlight = true;
}

void MediaService::onPlayerReply(sd_bus_message* m) {
  m_inFlight = false;
  if (sd_bus_message_is_method_error(m, nullptr)) {
    if (m_state.present) {
      m_state = MediaState{};
      ++m_generation;
    }
    return;
  }
  int found = 0;
  if (sd_bus_message_read(m, "b", &found) < 0) return;
  if (!found) {
    if (m_state.present) {
      const GLuint keepTex = m_state.cover;
      m_state = MediaState{};
      m_state.cover = keepTex;
      m_state.coverW = m_state.coverH = 0;
      m_coverFor.clear();
      ++m_generation;
    }
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
    std::string bytes;
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
      const std::string cached = cacheDir("covers") + "/" + hashOf(url);
      bytes = readFile(cached);
      if (bytes.empty() && httpGet(url, bytes)) writeFileAtomic(cached, bytes);
    } else if (const std::string path = artPath(url); !path.empty()) {
      bytes = readFile(path);
    }
    if (bytes.empty()) return giveUp();
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    gdk_pixbuf_loader_write(loader, reinterpret_cast<const guchar*>(bytes.data()), bytes.size(), nullptr);
    gdk_pixbuf_loader_close(loader, nullptr);
    GdkPixbuf* pb = gdk_pixbuf_loader_get_pixbuf(loader);
    if (!pb) {
      g_object_unref(loader);
      return giveUp();
    }
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

void MediaService::toggle() { call("PlayPauseActive"); }
void MediaService::next() { call("NextActive"); }
void MediaService::previous() { call("PreviousActive"); }
void MediaService::seek(double seconds) {
  call("SetPositionActive", "x", static_cast<int64_t>(std::max(0.0, seconds) * 1e6));
  m_state.positionUs = static_cast<int64_t>(std::max(0.0, seconds) * 1e6);
  m_state.positionAt = nowSeconds();
}

}  // namespace undershell
