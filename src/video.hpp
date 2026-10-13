// SPDX-License-Identifier: GPL-3.0-or-later
// A looping video or GIF for the TV look and the image widget, decoded on its
// own thread (FFmpeg, when built with it; GIFs, still pictures and anything
// FFmpeg cannot open through gdk-pixbuf), scaled down to the size it is shown
// at so no frame is bigger than it needs to be. For the TV frames stay in YUV
// 4:2:0 (a video's own form, 1.5 bytes a pixel) as three textures the shader
// turns into colour; with `rgba` (stickers: transparent GIFs, WebP, PNG) they
// are RGBA, one texture with mipmaps. A short clip is kept, scaled, after its
// first loop and never decoded again (up to 32 MB). The main thread plays it
// on its own clock at any speed and uploads only the frames that change.
#pragma once

#include "gl.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace undershell {

class VideoSource {
public:
  VideoSource() = default;
  ~VideoSource();
  VideoSource(const VideoSource&) = delete;
  VideoSource& operator=(const VideoSource&) = delete;

  // Starts decoding `path`, its frames scaled to cover `w` x `h` device px
  // (never above its own size). False if it cannot even be opened.
  bool open(const std::string& path, int w, int h, bool rgba = false);
  void close();
  [[nodiscard]] bool isOpen() const { return m_open; }
  [[nodiscard]] const std::string& path() const { return m_path; }
  // a decoding error, or "" (a missing file, an unknown format, no FFmpeg)
  [[nodiscard]] std::string error() const;

  // Moves its clock on by `dt` seconds at `speed`, and uploads the frame due
  // (needs GL). True when the picture changed.
  bool advance(double dt, double speed);
  // seconds until the picture changes at `speed` (a large value: never, as a
  // still picture or a file that failed), so a caller can sleep till then
  [[nodiscard]] double nextFrameIn(double speed) const;
  [[nodiscard]] bool still() const;  // decoded, one frame
  [[nodiscard]] int wantWidth() const { return m_wantW; }
  [[nodiscard]] int wantHeight() const { return m_wantH; }
  // the picture's planes: luma, and the two chromas at half size (full range,
  // or with rgba the picture itself, straight alpha, in texture()
  // BT.601: r = y + 1.402 (v - .5), g = y - .344 (u - .5) - .714 (v - .5),
  // b = y + 1.772 (u - .5))
  [[nodiscard]] GLuint texture() const { return m_tex[0]; }
  [[nodiscard]] GLuint textureU() const { return m_tex[1]; }
  [[nodiscard]] GLuint textureV() const { return m_tex[2]; }
  [[nodiscard]] int width() const { return m_texW; }
  [[nodiscard]] int height() const { return m_texH; }
  void releaseGl();

  static bool videoSupport();  // built with FFmpeg

  struct Frame {
    double pts = 0;  // seconds, growing across loops
    int w = 0, h = 0;  // even
    std::vector<std::uint8_t> yuv;  // Y (w x h), then U and V (w/2 x h/2 each); or RGBA
  };

private:
  void run();
  bool runFfmpeg();  // false when FFmpeg cannot open it (gdk-pixbuf may)
  void runPixbuf();
  bool push(Frame&& f);  // from the decoder; false when asked to stop
  void upload(const Frame& f);

  std::string m_path;
  int m_wantW = 0, m_wantH = 0;
  bool m_open = false;
  bool m_rgba = false;
  std::thread m_thread;
  std::atomic<bool> m_stop{false};
  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  std::deque<Frame> m_queue;           // decoded, not yet shown
  std::vector<Frame> m_cache;          // the whole clip, once it fits
  double m_cacheLength = 0;            // its length when cached (one loop)
  bool m_cached = false;
  std::string m_error;
  // playing
  double m_clock = -1;
  Frame m_shown;
  bool m_dirty = false;
  size_t m_cacheIndex = 0;
  GLuint m_tex[3] = {0, 0, 0};
  int m_texW = 0, m_texH = 0;
};

}  // namespace undershell
