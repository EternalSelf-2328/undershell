// SPDX-License-Identifier: GPL-3.0-or-later
// The media feed every music widget reads: the active player from Noctalia's
// MPRIS aggregator (dev.noctalia.Mpris, over sd-bus), the cover (downloaded
// and decoded off-thread), the album accent (Ryoku's ArtColor.accentOf) and
// synced lyrics from LRCLIB, cached on disk.
#pragma once

#include "common.hpp"
#include "jobs.hpp"

#include <GLES3/gl3.h>

#include <cstdint>
#include <string>
#include <vector>

struct sd_bus;
struct sd_bus_slot;
struct sd_bus_message;

namespace undershell {

struct LyricLine {
  int ms = 0;
  std::string text;
};

struct MediaState {
  bool present = false;
  std::string title, artist, album, identity, desktopEntry, artUrl, trackKey;
  bool playing = false;
  int64_t lengthUs = 0;
  int64_t positionUs = 0;
  double positionAt = 0;  // steady seconds when positionUs was sampled
  bool canNext = false, canPrev = false, canToggle = false, canSeek = false;

  // cover (GL texture uploaded lazily on the main thread)
  GLuint cover = 0;
  int coverW = 0, coverH = 0;
  bool hasAccent = false;
  Color accent;

  enum class Lyrics { None, Searching, Synced, Plain } lyrics = Lyrics::None;
  std::vector<LyricLine> lines;
  std::string plain;

  [[nodiscard]] bool radio() const { return lengthUs <= 0; }
  // position now, extrapolated from the last sample while playing
  [[nodiscard]] double positionSec(double now) const;
};

// exposed for tests
std::vector<LyricLine> parseLrc(const std::string& text);
// The file an MPRIS art url points at, or "" when it is not a local one.
std::string artPath(const std::string& url);
// What tells one cover from another: the url and, for a local file, the file
// as it is right now.
std::string artKey(const std::string& url);
// Ryoku's ArtColor.accentOf over RGBA pixels (w x h); false = no vivid colour
bool accentOfPixels(const uint8_t* rgba, int w, int h, Color& out);

class MediaService {
public:
  explicit MediaService(Jobs& jobs);
  ~MediaService();

  // Connects to the session bus; call once. Returns false without a bus.
  bool start();
  [[nodiscard]] int fd() const;
  void dispatch();             // when fd() is readable
  void poll(double now);       // periodic refresh (position, missed signals)
  [[nodiscard]] int pollTimeoutMs(double now) const;

  void setWanted(bool wanted, bool lyrics);  // no widget → no polling
  [[nodiscard]] const MediaState& state() const { return m_state; }
  [[nodiscard]] uint64_t generation() const { return m_generation; }  // bumps on any change
  void uploadPending();  // GL: turns a decoded cover into a texture

  void setStateForTest(MediaState s) {
    m_state = std::move(s);
    ++m_generation;
  }

  void toggle();
  void next();
  void previous();
  void seek(double seconds);

  // internals used by the sd-bus callbacks
  void onPlayerReply(sd_bus_message* m);
  void requestRefresh();

private:
  void call(const char* method, const char* sig = nullptr, int64_t arg = 0);
  void onTrackChanged();
  void syncCover();
  void fetchCover(const std::string& url, const std::string& key);
  void fetchLyrics();

  Jobs& m_jobs;
  sd_bus* m_bus = nullptr;
  sd_bus_slot* m_matchSlot = nullptr;
  MediaState m_state;
  uint64_t m_generation = 0;
  bool m_wanted = false, m_wantLyrics = false;
  bool m_inFlight = false;
  double m_nextPoll = 0;
  std::string m_coverFor, m_lyricsFor;  // m_coverFor: the cover key, not the url
  // decoded cover waiting for upload
  std::vector<uint8_t> m_pendingRgba;
  int m_pendingW = 0, m_pendingH = 0;
  std::string m_pendingKey;
};

}  // namespace undershell
