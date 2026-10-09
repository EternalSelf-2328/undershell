// SPDX-License-Identifier: GPL-3.0-or-later
// Band processing adapted from Noctalia's pipewire_spectrum.cpp (MIT License,
// Copyright (c) 2026 noctalia-dev).
#include "audio.hpp"

#include "common.hpp"

#include <array>
#include <cmath>
#include <numbers>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>

namespace undershell {

namespace {
constexpr float kMaxBandLevel = 0.9F;
constexpr float kMinSensitivity = 0.001F;
constexpr float kMaxSensitivity = 30.0F;

void fft(std::complex<float>* data, int n) {
  for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(data[i], data[j]);
  }
  for (int len = 2; len <= n; len <<= 1) {
    const float angle = -2.0F * std::numbers::pi_v<float> / static_cast<float>(len);
    const std::complex<float> wn(std::cos(angle), std::sin(angle));
    for (int i = 0; i < n; i += len) {
      std::complex<float> w(1.0F, 0.0F);
      const int half = len / 2;
      for (int j = 0; j < half; ++j) {
        const auto u = data[i + j];
        const auto v = data[i + j + half] * w;
        data[i + j] = u + v;
        data[i + j + half] = u - v;
        w *= wn;
      }
    }
  }
}
}  // namespace

struct Audio::Stream {
  Audio* audio = nullptr;
  pw_stream* stream = nullptr;
  spa_hook listener{};
  spa_audio_info_raw format{};
  bool ready = false;
  std::vector<float> mono;

  static void onParam(void* data, uint32_t id, const spa_pod* param) {
    auto* self = static_cast<Stream*>(data);
    if (param == nullptr || id != SPA_PARAM_Format) return;
    spa_audio_info info{};
    if (spa_format_parse(param, &info.media_type, &info.media_subtype) < 0) return;
    if (info.media_type != SPA_MEDIA_TYPE_audio || info.media_subtype != SPA_MEDIA_SUBTYPE_raw) return;
    spa_audio_info_raw raw{};
    if (spa_format_audio_raw_parse(param, &raw) < 0 || raw.format != SPA_AUDIO_FORMAT_F32) {
      self->ready = false;
      return;
    }
    self->format = raw;
    self->ready = raw.channels > 0;
    if (self->ready) self->audio->setSampleRate(static_cast<int>(raw.rate));
  }

  static void onProcess(void* data) {
    auto* self = static_cast<Stream*>(data);
    if (!self->ready || !self->stream) return;
    pw_buffer* b = pw_stream_dequeue_buffer(self->stream);
    if (!b) return;
    spa_buffer* sb = b->buffer;
    if (sb && sb->n_datas > 0 && sb->datas[0].data && sb->datas[0].chunk) {
      const auto* base = static_cast<const uint8_t*>(sb->datas[0].data) + sb->datas[0].chunk->offset;
      const auto* samples = reinterpret_cast<const float*>(base);
      const int ch = static_cast<int>(self->format.channels);
      const int frames = static_cast<int>(sb->datas[0].chunk->size / sizeof(float)) / ch;
      if (frames > 0) {
        self->mono.resize(static_cast<size_t>(frames));
        bool nonZero = false;
        const float inv = 1.0F / static_cast<float>(ch);
        for (int i = 0; i < frames; ++i) {
          float s = 0;
          for (int c = 0; c < ch; ++c) s += samples[i * ch + c];
          s *= inv;
          self->mono[static_cast<size_t>(i)] = s;
          nonZero = nonZero || s != 0.0F;
        }
        self->audio->feed(self->mono.data(), frames, nonZero);
      }
    }
    pw_stream_queue_buffer(self->stream, b);
  }

  static void onState(void*, pw_stream_state, pw_stream_state state, const char* error) {
    if (state == PW_STREAM_STATE_ERROR) US_WARN("audio stream error: {}", error ? error : "unknown");
  }
};

static const pw_stream_events kStreamEvents = [] {
  pw_stream_events e{};
  e.version = PW_VERSION_STREAM_EVENTS;
  e.state_changed = &Audio::Stream::onState;
  e.param_changed = &Audio::Stream::onParam;
  e.process = &Audio::Stream::onProcess;
  return e;
}();

Audio::Audio() {
  pw_init(nullptr, nullptr);
  m_ring.assign(kFftSize, 0.0F);
  m_fft.resize(kFftSize);
  m_window.resize(kFftSize);
  for (int i = 0; i < kFftSize; ++i)
    m_window[i] = 0.5F * (1.0F - std::cos(2.0F * std::numbers::pi_v<float> * i / (kFftSize - 1)));
  setBandCount(64);
  setupKick();
}

Audio::~Audio() {
  stop();
  pw_deinit();
}

bool Audio::start() {
  m_loop = pw_loop_new(nullptr);
  if (!m_loop) return false;
  pw_loop_enter(m_loop);
  m_context = pw_context_new(m_loop, nullptr, 0);
  if (!m_context) return false;
  m_core = pw_context_connect(m_context, nullptr, 0);
  if (!m_core) {
    US_WARN("could not connect to PipeWire");
    return false;
  }
  m_stream = new Stream();
  m_stream->audio = this;
  // Passive capture of the default sink's monitor: it never keeps the sink
  // awake, so a silent/paused system suspends and costs nothing.
  pw_properties* props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_NAME, "Undershell Spectrum",
      PW_KEY_APP_NAME, "undershell", PW_KEY_STREAM_MONITOR, "true", PW_KEY_STREAM_CAPTURE_SINK, "true",
      PW_KEY_NODE_PASSIVE, "true", nullptr);
  m_stream->stream = pw_stream_new(m_core, "undershell-spectrum", props);
  if (!m_stream->stream) return false;
  pw_stream_add_listener(m_stream->stream, &m_stream->listener, &kStreamEvents, m_stream);
  std::array<uint8_t, 512> buf{};
  spa_pod_builder b = SPA_POD_BUILDER_INIT(buf.data(), buf.size());
  spa_audio_info_raw raw{};
  raw.format = SPA_AUDIO_FORMAT_F32;
  const spa_pod* params[1] = {spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &raw)};
  auto flags = static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS);
  if (pw_stream_connect(m_stream->stream, PW_DIRECTION_INPUT, PW_ID_ANY, flags, params, 1) < 0) {
    US_WARN("could not connect the capture stream");
    return false;
  }
  US_INFO("audio capture started (default sink monitor)");
  return true;
}

void Audio::stop() {
  if (m_stream) {
    if (m_stream->stream) {
      spa_hook_remove(&m_stream->listener);
      pw_stream_destroy(m_stream->stream);
    }
    delete m_stream;
    m_stream = nullptr;
  }
  if (m_core) pw_core_disconnect(m_core);
  m_core = nullptr;
  if (m_context) pw_context_destroy(m_context);
  m_context = nullptr;
  if (m_loop) {
    pw_loop_leave(m_loop);
    pw_loop_destroy(m_loop);
  }
  m_loop = nullptr;
}

int Audio::fd() const { return m_loop ? pw_loop_get_fd(m_loop) : -1; }

void Audio::dispatch() {
  if (m_loop) pw_loop_iterate(m_loop, 0);
}

void Audio::setSampleRate(int rate) {
  if (rate > 0 && rate != m_sampleRate) {
    m_sampleRate = rate;
    computeBins();
    setupKick();
  }
}

// Two 2-pole low-passes at 200 Hz (the kick drum's body and attack, 808s),
// read as a level over a sliding 12 ms window every 2 ms: a kick shows within
// a few ms, where the 4096-point spectrum smears it over 85 ms. (A shorter
// window would follow each cycle of a 50 Hz bass up and down.)
void Audio::setupKick() {
  const float w0 = 2.0F * std::numbers::pi_v<float> * 200.0F / static_cast<float>(m_sampleRate);
  const float alpha = std::sin(w0) / std::numbers::sqrt2_v<float>;  // Q = 1/sqrt(2): sin(w0) / 2Q
  const float c = std::cos(w0), a0 = 1.0F + alpha;
  for (auto& f : m_low) {
    f = Biquad{};
    f.b0 = (1.0F - c) / 2.0F / a0;
    f.b1 = (1.0F - c) / a0;
    f.b2 = f.b0;
    f.a1 = -2.0F * c / a0;
    f.a2 = (1.0F - alpha) / a0;
  }
  m_hopLen = std::max(16, m_sampleRate / 500);
  m_hopFill = 0;
  m_hopAcc = 0;
}

// One hop of the low band. A kick is:
//  - its level rising over the 36 ms before the window, by more than it
//    usually does (mean + 2 sd over the last couple of seconds, never less
//    than 5 dB). "Before" is their mean power but never under their loudest
//    hop - 1.5 dB: a bass and a kick's tail beating against each other swing
//    in and out, and the climb out of a cancelled trough is not a new hit;
//  - within 6 dB of the song's recent peak (not a bass note under the drums);
//  - 110 ms or more after the previous one.
// Tuned with synthetic kicks over real songs: ~95% caught, 17 ms late at
// 60 fps, where the bands' flux caught ~70%, 100 ms late.
void Audio::onsetHop(float hopSum) {
  const float hopSec = static_cast<float>(m_hopLen) / static_cast<float>(m_sampleRate);
  m_hopSums[m_hop % m_hopSums.size()] = hopSum;
  float sum = 0;
  for (float h : m_hopSums) sum += h;
  const float power = sum / static_cast<float>(m_hopLen * m_hopSums.size());
  float mean = 0, most = 0;
  for (size_t back = m_hopSums.size(); back < m_powers.size(); ++back) {
    const float p = m_powers[(m_hop + m_powers.size() - back) % m_powers.size()];
    mean += p;
    most = std::max(most, p);
  }
  mean /= static_cast<float>(m_powers.size() - m_hopSums.size());
  const float earlier = std::max(mean, 0.7F * most);
  m_powers[m_hop % m_powers.size()] = power;
  ++m_hop;
  const float level = 10.0F * std::log10(1e-10F + power);
  m_levelPeak = std::max(level, m_levelPeak - 3.0F * hopSec);  // the recent loud level, sinking 3 dB a second
  const float floor = m_levelPeak - 30.0F;                     // below this is just quiet
  const float rise = std::max(level, floor) - std::max(10.0F * std::log10(1e-10F + earlier), floor);
  const float k = hopSec / 2.0F;
  m_riseMean += (rise - m_riseMean) * k;
  m_riseVar += ((rise - m_riseMean) * (rise - m_riseMean) - m_riseVar) * k;
  const float threshold = std::max(5.0F, m_riseMean + 2.0F * std::sqrt(m_riseVar));
  const double since = static_cast<double>(m_samplesFed - m_kickSample) / m_sampleRate;
  if (rise > threshold && level > m_levelPeak - 6.0F && since > 0.11) {
    ++m_kicks;
    m_kickSample = m_samplesFed;
    m_kickStrength = std::clamp(0.45F + (rise - threshold) / 20.0F + (level - m_levelPeak + 6.0F) / 15.0F, 0.3F, 1.0F);
  }
}

void Audio::setBandCount(int n) {
  n = std::clamp(n, 1, 256);
  m_bandCount = n;
  auto sz = static_cast<size_t>(n);
  m_bands.assign(sz, 0);
  m_prev.assign(sz, 0);
  m_peak.assign(sz, 0);
  m_fall.assign(sz, 0);
  m_mem.assign(sz, 0);
  m_values.assign(sz, 0);
  computeBins();
}

void Audio::computeBins() {
  const float fLow = 20.0F;
  const float fHigh = std::min(20000.0F, m_sampleRate / 2.0F);
  const float ratio = fHigh / fLow;
  m_bins.resize(static_cast<size_t>(m_bandCount));
  for (int i = 0; i < m_bandCount; ++i) {
    const float t = static_cast<float>(i) / std::max(1, m_bandCount - 1);
    const float freq = fLow * std::pow(ratio, t);
    m_bins[static_cast<size_t>(i)] = std::clamp(freq * kFftSize / static_cast<float>(m_sampleRate), 1.0F,
                                                static_cast<float>(kFftSize / 2));
  }
}

void Audio::feed(const float* mono, int count, bool nonZero) {
  for (int i = 0; i < count; ++i) {
    m_ring[static_cast<size_t>(m_ringPos)] = mono[i];
    m_ringPos = (m_ringPos + 1) % kFftSize;
    if (m_ringPos == 0) m_ringFull = true;
    const float low = m_low[1].run(m_low[0].run(mono[i]));
    m_hopAcc += low * low;
    ++m_samplesFed;
    if (++m_hopFill == m_hopLen) {
      onsetHop(m_hopAcc);
      m_hopFill = 0;
      m_hopAcc = 0;
    }
  }
  if (nonZero) m_samplesReceived = true;
}

int Audio::pollTimeoutMs() const {
  if (!m_stream) return -1;
  if (m_idle && !m_samplesReceived) return -1;
  auto now = std::chrono::steady_clock::now();
  if (m_nextFrameAt <= now) return 0;
  return static_cast<int>(std::chrono::ceil<std::chrono::milliseconds>(m_nextFrameAt - now).count());
}

bool Audio::tick() {
  if (!m_stream || (m_idle && !m_samplesReceived)) return false;
  auto now = std::chrono::steady_clock::now();
  if (m_nextFrameAt.time_since_epoch().count() != 0 && now < m_nextFrameAt) return false;
  const auto interval = std::chrono::nanoseconds(std::chrono::seconds(1)) / kFrameRateHz;
  m_nextFrameAt = (m_nextFrameAt.time_since_epoch().count() == 0 || now - m_nextFrameAt >= interval)
                      ? now + interval
                      : m_nextFrameAt + interval;
  const bool wasIdle = m_idle;
  processFrame();
  return !(wasIdle && m_idle);
}

float Audio::energy() const {
  if (m_values.empty()) return 0;
  float s = 0;
  for (float v : m_values) s += v;
  return s / static_cast<float>(m_values.size()) / kMaxBandLevel;
}

void Audio::clear() {
  std::fill(m_values.begin(), m_values.end(), 0.0F);
  std::fill(m_prev.begin(), m_prev.end(), 0.0F);
  std::fill(m_peak.begin(), m_peak.end(), 0.0F);
  std::fill(m_fall.begin(), m_fall.end(), 0.0F);
  std::fill(m_mem.begin(), m_mem.end(), 0.0F);
  m_idle = true;
  m_samplesReceived = false;
  m_idleFrames = 0;
}

void Audio::processFrame() {
  if (!m_ringFull) {
    m_samplesReceived = false;
    return;
  }
  if (!m_samplesReceived) {
    for (auto& s : m_ring) s *= 0.85F;
  }
  m_samplesReceived = false;

  for (int i = 0; i < kFftSize; ++i) {
    const int idx = (m_ringPos + i) % kFftSize;
    m_fft[static_cast<size_t>(i)] = {m_ring[static_cast<size_t>(idx)] * m_window[static_cast<size_t>(i)], 0.0F};
  }
  fft(m_fft.data(), kFftSize);

  for (int i = 0; i < m_bandCount; ++i) {
    const float bin = m_bins[static_cast<size_t>(i)];
    const int lo = std::clamp(static_cast<int>(std::floor(bin)), 1, kFftSize / 2);
    const int hi = std::clamp(lo + 1, lo, kFftSize / 2);
    const float t = std::clamp(bin - lo, 0.0F, 1.0F);
    const float a = std::abs(m_fft[static_cast<size_t>(lo)]);
    const float b = std::abs(m_fft[static_cast<size_t>(hi)]);
    m_bands[static_cast<size_t>(i)] = a + (b - a) * t;
  }

  const float nr = m_noiseReduction;
  const float gate = nr * kFftSize * 0.00005F;
  constexpr float kCompress = 0.15F;
  bool overshoot = false, silence = true;
  for (auto& band : m_bands) {
    band = std::max(0.0F, band - gate);
    band = std::log1p(band * kCompress) / kCompress;
    band *= m_sensitivity;
    if (band > kMaxBandLevel) {
      overshoot = true;
      band = kMaxBandLevel;
    }
    if (band > 0.01F) silence = false;
  }
  if (overshoot) {
    m_sensitivity *= 0.98F;
    m_sensInit = false;
  } else if (!silence) {
    m_sensitivity *= 1.001F;
    if (m_sensInit) m_sensitivity *= 1.1F;
  }
  m_sensitivity = std::clamp(m_sensitivity, kMinSensitivity, kMaxSensitivity);

  if (silence) {
    if (++m_idleFrames >= kFrameRateHz) {
      if (!m_idle) {
        clear();
        US_DEBUG("audio idle");
      }
      return;
    }
  } else {
    m_idleFrames = 0;
    m_idle = false;
  }

  const double gravityMod = std::max(1.0, 1.54 / std::max(static_cast<double>(nr), 0.01));
  const auto n = static_cast<size_t>(m_bandCount);
  for (size_t i = 0; i < n; ++i) {
    float v = m_bands[i];
    if (v < m_prev[i] && nr > 0.1F) {
      v = static_cast<float>(m_peak[i] * (1.0 - m_fall[i] * m_fall[i] * gravityMod));
      v = std::max(v, 0.0F);
      m_fall[i] += 0.04F;
    } else {
      m_peak[i] = v;
      m_fall[i] = 0;
    }
    m_prev[i] = v;
    v = std::clamp(m_mem[i] * nr + v * (1.0F - nr), 0.0F, kMaxBandLevel);
    m_mem[i] = v;
    m_bands[i] = v;
  }
  if (m_monstercat) {
    for (size_t z = 0; z < n; ++z) {
      float spread = m_bands[z] / 1.5F;
      for (size_t m = z; m > 0 && spread > 0.01F;) {
        --m;
        m_bands[m] = std::max(m_bands[m], spread);
        spread /= 1.5F;
      }
      spread = m_bands[z] / 1.5F;
      for (size_t m = z + 1; m < n && spread > 0.01F; ++m) {
        m_bands[m] = std::max(m_bands[m], spread);
        spread /= 1.5F;
      }
    }
  }
  for (size_t i = 0; i < n; ++i) m_values[i] = std::clamp(m_bands[i], 0.0F, kMaxBandLevel);
}

}  // namespace undershell
