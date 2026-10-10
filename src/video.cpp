// SPDX-License-Identifier: GPL-3.0-or-later
#include "video.hpp"

#include "common.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <gdk-pixbuf/gdk-pixbuf.h>

#ifdef US_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

namespace undershell {

namespace {
constexpr size_t kQueue = 4;                     // frames decoded ahead
constexpr size_t kCacheBudget = 32u << 20;       // a clip this small (scaled) is kept whole
constexpr int kMaxFrames = 1200;                 // and never more frames than this

// the size a source is decoded to: covering the box, never above its own size
void fitSize(int srcW, int srcH, int wantW, int wantH, int& w, int& h) {
  const double k = std::min(1.0, std::max(static_cast<double>(wantW) / std::max(1, srcW), static_cast<double>(wantH) / std::max(1, srcH)));
  w = std::max(2, static_cast<int>(std::lround(srcW * k)) & ~1);
  h = std::max(2, static_cast<int>(std::lround(srcH * k)) & ~1);
}
}  // namespace

VideoSource::~VideoSource() { close(); }

bool VideoSource::videoSupport() {
#ifdef US_HAVE_FFMPEG
  return true;
#else
  return false;
#endif
}

bool VideoSource::open(const std::string& path, int w, int h) {
  close();
  m_path = path;
  m_wantW = std::max(16, w);
  m_wantH = std::max(16, h);
  m_stop = false;
  m_error.clear();
  m_open = true;
  m_thread = std::thread([this] { run(); });
  return true;
}

void VideoSource::close() {
  if (m_thread.joinable()) {
    {
      std::lock_guard lock(m_mutex);
      m_stop = true;
    }
    m_cv.notify_all();
    m_thread.join();
  }
  m_open = false;
  m_queue.clear();
  m_cache.clear();
  m_cached = false;
  m_cacheLength = 0;
  m_clock = -1;
  m_cacheIndex = 0;
  m_shown = Frame{};
}

std::string VideoSource::error() const {
  std::lock_guard lock(m_mutex);
  return m_error;
}

bool VideoSource::push(Frame&& f) {
  std::unique_lock lock(m_mutex);
  m_cv.wait(lock, [this] { return m_stop || m_queue.size() < kQueue; });
  if (m_stop) return false;
  m_queue.push_back(std::move(f));
  return true;
}

void VideoSource::run() {
  auto fail = [this](std::string why) {
    std::lock_guard lock(m_mutex);
    m_error = std::move(why);
    US_WARN("tv: {}: {}", m_path, m_error);
  };
#ifdef US_HAVE_FFMPEG
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, m_path.c_str(), nullptr, nullptr) < 0) return fail("cannot open it");
  if (avformat_find_stream_info(fmt, nullptr) < 0) {
    avformat_close_input(&fmt);
    return fail("no stream information");
  }
  const AVCodec* codec = nullptr;
  const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
  if (si < 0 || !codec) {
    avformat_close_input(&fmt);
    return fail("no video in it");
  }
  AVStream* st = fmt->streams[si];
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(ctx, st->codecpar);
  // two threads are plenty for a picture this small; skipping the deblocking
  // filter saves about a third of the work and cannot be seen at this size
  ctx->thread_count = 2;
  ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
  ctx->skip_loop_filter = AVDISCARD_ALL;
  ctx->flags2 |= AV_CODEC_FLAG2_FAST;
  if (avcodec_open2(ctx, codec, nullptr) < 0) {
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    return fail("cannot decode it");
  }
  const double tb = av_q2d(st->time_base);
  const double rate = st->avg_frame_rate.num > 0 ? av_q2d(st->avg_frame_rate) : 25.0;
  const double frameDur = 1.0 / std::clamp(rate, 1.0, 120.0);
  AVPacket* pkt = av_packet_alloc();
  AVFrame* fr = av_frame_alloc();
  SwsContext* sws = nullptr;
  double first = -1, last = 0, loopLen = 0;
  int loops = 0;
  bool caching = true;
  size_t cacheBytes = 0;
  std::vector<Frame> firstPass;
  bool running = true;

  auto emit = [&](AVFrame* f) {
    const int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    double pts = ts != AV_NOPTS_VALUE ? static_cast<double>(ts) * tb : last + frameDur;
    if (first < 0) first = pts;
    pts -= first;
    last = pts;
    int w, h;
    fitSize(f->width, f->height, m_wantW, m_wantH, w, h);
    sws = sws_getCachedContext(sws, f->width, f->height, static_cast<AVPixelFormat>(f->format), w, h, AV_PIX_FMT_YUV420P,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) return;
    {  // out in full range BT.601, whatever came in: the shader knows one form
      const int* srcCs = sws_getCoefficients(f->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_ITU601);
      sws_setColorspaceDetails(sws, srcCs, f->color_range == AVCOL_RANGE_JPEG ? 1 : 0, sws_getCoefficients(SWS_CS_ITU601), 1, 0,
                               1 << 16, 1 << 16);
    }
    Frame out;
    out.pts = pts + loops * loopLen;
    out.w = w;
    out.h = h;
    out.yuv.resize(static_cast<size_t>(w) * h * 3 / 2);
    uint8_t* dst[4] = {out.yuv.data(), out.yuv.data() + static_cast<size_t>(w) * h,
                       out.yuv.data() + static_cast<size_t>(w) * h * 5 / 4, nullptr};
    const int dstStride[4] = {w, w / 2, w / 2, 0};
    sws_scale(sws, f->data, f->linesize, 0, f->height, dst, dstStride);
    if (caching && loops == 0) {
      cacheBytes += out.yuv.size();
      if (cacheBytes > kCacheBudget || firstPass.size() >= kMaxFrames) {
        caching = false;  // too long to keep: it streams
        firstPass.clear();
        firstPass.shrink_to_fit();
      } else {
        firstPass.push_back(out);
      }
    }
    running = push(std::move(out));
  };
  auto drain = [&] {
    while (running && avcodec_receive_frame(ctx, fr) == 0) emit(fr);
  };

  while (running && !m_stop) {
    if (av_read_frame(fmt, pkt) < 0) {  // the end: flush, then loop or keep it whole
      avcodec_send_packet(ctx, nullptr);
      drain();
      loopLen = last + frameDur;
      if (caching && loops == 0 && !firstPass.empty()) {
        std::lock_guard lock(m_mutex);
        m_cache = std::move(firstPass);
        m_cacheLength = loopLen;
        m_cached = true;
        break;  // never decoded again
      }
      ++loops;
      av_seek_frame(fmt, si, st->start_time != AV_NOPTS_VALUE ? st->start_time : 0, AVSEEK_FLAG_BACKWARD);
      avcodec_flush_buffers(ctx);
      first = -1;
      continue;
    }
    if (pkt->stream_index == si && avcodec_send_packet(ctx, pkt) >= 0) drain();
    av_packet_unref(pkt);
  }
  sws_freeContext(sws);
  av_frame_free(&fr);
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  avformat_close_input(&fmt);
#else
  // without FFmpeg: GIFs (and still images) through gdk-pixbuf, kept whole
  // (its animation API is deprecated, and still the one there is)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  GError* err = nullptr;
  GdkPixbufAnimation* anim = gdk_pixbuf_animation_new_from_file(m_path.c_str(), &err);
  if (!anim) {
    if (err) g_error_free(err);
    return fail("cannot open it (videos need undershell built with FFmpeg)");
  }
  GTimeVal t{0, 0};
  GdkPixbufAnimationIter* it = gdk_pixbuf_animation_get_iter(anim, &t);
  std::vector<Frame> frames;
  double pts = 0;
  GdkPixbuf* firstPix = nullptr;
  for (int i = 0; i < kMaxFrames && !m_stop; ++i) {
    GdkPixbuf* pix = gdk_pixbuf_animation_iter_get_pixbuf(it);
    if (i > 0 && pix == firstPix) break;  // round again
    if (i == 0) firstPix = pix;
    int w, h;
    fitSize(gdk_pixbuf_get_width(pix), gdk_pixbuf_get_height(pix), m_wantW, m_wantH, w, h);
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pix, w, h, GDK_INTERP_BILINEAR);
    GdkPixbuf* rgba = gdk_pixbuf_get_has_alpha(scaled) ? g_object_ref(scaled) : gdk_pixbuf_add_alpha(scaled, FALSE, 0, 0, 0);
    Frame f;
    f.pts = pts;
    f.w = w;
    f.h = h;
    f.yuv.resize(static_cast<size_t>(w) * h * 3 / 2);
    const int stride = gdk_pixbuf_get_rowstride(rgba);
    const guchar* px = gdk_pixbuf_read_pixels(rgba);
    uint8_t* Y = f.yuv.data();
    uint8_t* U = Y + static_cast<size_t>(w) * h;
    uint8_t* V = U + static_cast<size_t>(w) * h / 4;
    auto at = [&](int x, int y) { return px + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4; };
    for (int y = 0; y < h; ++y)  // full range BT.601
      for (int x = 0; x < w; ++x) {
        const guchar* p = at(x, y);
        Y[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::clamp(0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2], 0.0, 255.0));
      }
    for (int y = 0; y < h / 2; ++y)
      for (int x = 0; x < w / 2; ++x) {
        double r = 0, g = 0, b = 0;
        for (int k = 0; k < 4; ++k) {
          const guchar* p = at(2 * x + (k & 1), 2 * y + (k >> 1));
          r += p[0] / 4.0, g += p[1] / 4.0, b += p[2] / 4.0;
        }
        U[static_cast<size_t>(y) * (w / 2) + x] = static_cast<uint8_t>(std::clamp(128 - 0.168736 * r - 0.331264 * g + 0.5 * b, 0.0, 255.0));
        V[static_cast<size_t>(y) * (w / 2) + x] = static_cast<uint8_t>(std::clamp(128 + 0.5 * r - 0.418688 * g - 0.081312 * b, 0.0, 255.0));
      }
    g_object_unref(rgba);
    g_object_unref(scaled);
    frames.push_back(std::move(f));
    const int delay = gdk_pixbuf_animation_iter_get_delay_time(it);
    if (delay < 0) break;  // a still image
    pts += std::max(20, delay) / 1000.0;
    g_time_val_add(&t, static_cast<glong>(std::max(20, delay)) * 1000);
    gdk_pixbuf_animation_iter_advance(it, &t);
  }
  g_object_unref(it);
  g_object_unref(anim);
  std::lock_guard lock(m_mutex);
  m_cacheLength = std::max(pts, 0.04);
  m_cache = std::move(frames);
  m_cached = !m_cache.empty();
#pragma GCC diagnostic pop
#endif
}

bool VideoSource::advance(double dt, double speed) {
  if (!m_open) return false;
  Frame next;
  bool got = false;
  {
    std::lock_guard lock(m_mutex);
    if (m_clock < 0) {
      if (!m_queue.empty()) m_clock = m_queue.front().pts;
      else if (m_cached) m_clock = 0;
      else return false;  // nothing decoded yet
    } else {
      m_clock += dt * speed;
    }
    // first what was decoded, in order; then the kept clip, round and round
    while (!m_queue.empty() && m_queue.front().pts <= m_clock) {
      next = std::move(m_queue.front());
      m_queue.pop_front();
      got = true;
    }
    if (got) m_cv.notify_one();
    if (!got && m_queue.empty() && m_cached && !m_cache.empty()) {
      const double t = std::fmod(m_clock, m_cacheLength);
      size_t i = 0;
      while (i + 1 < m_cache.size() && m_cache[i + 1].pts <= t) ++i;
      if (i != m_cacheIndex || !m_tex[0]) {
        m_cacheIndex = i;
        upload(m_cache[i]);
        return true;
      }
      return false;
    }
  }
  if (!got) return false;
  // upload outside the lock, so the decoder is never kept waiting on GL
  m_shown = std::move(next);
  upload(m_shown);
  return true;
}

// the three planes, as three one-channel textures
void VideoSource::upload(const Frame& f) {
  if (!m_tex[0]) glGenTextures(3, m_tex);
  const bool resize = f.w != m_texW || f.h != m_texH;
  const uint8_t* planes[3] = {f.yuv.data(), f.yuv.data() + static_cast<size_t>(f.w) * f.h,
                              f.yuv.data() + static_cast<size_t>(f.w) * f.h * 5 / 4};
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  for (int k = 0; k < 3; ++k) {
    const int w = k ? f.w / 2 : f.w, h = k ? f.h / 2 : f.h;
    glBindTexture(GL_TEXTURE_2D, m_tex[k]);
    if (resize) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, planes[k]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, planes[k]);
    }
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  m_texW = f.w;
  m_texH = f.h;
}

void VideoSource::releaseGl() {
  if (m_tex[0]) glDeleteTextures(3, m_tex);
  m_tex[0] = m_tex[1] = m_tex[2] = 0;
  m_texW = m_texH = 0;
}

}  // namespace undershell
