// SPDX-License-Identifier: AGPL-3.0
// Audio decoding (dr_libs) + playback through libnx audout.
#include "spn/audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <switch.h>

#include "dr_libs/dr_flac.h"
#include "dr_libs/dr_mp3.h"
#include "dr_libs/dr_wav.h"

namespace spn {

namespace {

std::string lowerExt(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return {};
  std::string ext = path.substr(dot + 1);
  for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
  return ext;
}

}  // namespace

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

Decoder::~Decoder() { close(); }

void Decoder::close() {
  if (handle_) {
    switch (format_) {
      case Format::Mp3: {
        auto* p = static_cast<drmp3*>(handle_);
        drmp3_uninit(p);
        delete p;
        break;
      }
      case Format::Flac:
        drflac_close(static_cast<drflac*>(handle_));
        break;
      case Format::Wav: {
        auto* p = static_cast<drwav*>(handle_);
        drwav_uninit(p);
        delete p;
        break;
      }
      default:
        break;
    }
  }
  handle_ = nullptr;
  format_ = Format::None;
  sampleRate_ = 0;
  channels_ = 0;
  totalFrames_ = 0;
}

bool Decoder::open(const std::string& path) {
  close();
  const std::string ext = lowerExt(path);

  if (ext == "mp3") {
    auto* p = new drmp3();
    if (!drmp3_init_file(p, path.c_str(), nullptr)) {
      delete p;
      return false;
    }
    format_ = Format::Mp3;
    handle_ = p;
    sampleRate_ = int(p->sampleRate);
    channels_ = int(p->channels);
    totalFrames_ = drmp3_get_pcm_frame_count(p);
    return channels_ > 0;
  }
  if (ext == "flac") {
    drflac* p = drflac_open_file(path.c_str(), nullptr);
    if (!p) return false;
    format_ = Format::Flac;
    handle_ = p;
    sampleRate_ = int(p->sampleRate);
    channels_ = int(p->channels);
    totalFrames_ = p->totalPCMFrameCount;
    return channels_ > 0;
  }
  if (ext == "wav" || ext == "wave") {
    auto* p = new drwav();
    if (!drwav_init_file(p, path.c_str(), nullptr)) {
      delete p;
      return false;
    }
    format_ = Format::Wav;
    handle_ = p;
    sampleRate_ = int(p->sampleRate);
    channels_ = int(p->channels);
    totalFrames_ = p->totalPCMFrameCount;
    return channels_ > 0;
  }
  return false;
}

double Decoder::durationSec() const {
  if (sampleRate_ <= 0) return 0.0;
  return double(totalFrames_) / double(sampleRate_);
}

uint64_t Decoder::readFrames(float* out, uint64_t frames) {
  if (!handle_ || frames == 0) return 0;
  switch (format_) {
    case Format::Mp3:
      return drmp3_read_pcm_frames_f32(static_cast<drmp3*>(handle_), frames, out);
    case Format::Flac:
      return drflac_read_pcm_frames_f32(static_cast<drflac*>(handle_), frames,
                                        out);
    case Format::Wav:
      return drwav_read_pcm_frames_f32(static_cast<drwav*>(handle_), frames, out);
    default:
      return 0;
  }
}

bool Decoder::seekToFrame(uint64_t frame) {
  if (!handle_) return false;
  switch (format_) {
    case Format::Mp3:
      return drmp3_seek_to_pcm_frame(static_cast<drmp3*>(handle_), frame);
    case Format::Flac:
      return drflac_seek_to_pcm_frame(static_cast<drflac*>(handle_), frame);
    case Format::Wav:
      return drwav_seek_to_pcm_frame(static_cast<drwav*>(handle_), frame);
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// AudioEngine
// ---------------------------------------------------------------------------

namespace {

constexpr int kBlockFrames = 1024;  // frames per audout buffer
constexpr int kBufferCount = 4;
constexpr uint64_t kWaitNs = 50000000ULL;  // 50 ms

// Resampling source: linear interpolation between adjacent decoder frames.
class Source {
 public:
  explicit Source(Decoder* d) : dec_(d) {}

  void reset() {
    cache_.clear();
    cachePos_ = 0;
    eof_ = false;
    valid_ = false;
  }

  bool begin() {
    reset();
    if (!nextRaw(aL_, aR_)) return false;
    if (!nextRaw(bL_, bR_)) {
      bL_ = aL_;
      bR_ = aR_;
    }
    valid_ = true;
    return true;
  }

  // Advances by `step` source frames and returns the interpolated frame.
  bool advance(double step, float& l, float& r) {
    if (!valid_) return false;
    pos_ += step;
    while (pos_ >= 1.0) {
      aL_ = bL_;
      aR_ = bR_;
      if (!nextRaw(bL_, bR_)) {
        bL_ = aL_;
        bR_ = aR_;
        valid_ = false;
        return false;
      }
      pos_ -= 1.0;
    }
    const float t = float(pos_);
    l = aL_ + (bL_ - aL_) * t;
    r = aR_ + (bR_ - aR_) * t;
    return true;
  }

  bool atEnd() const { return eof_ && cachePos_ >= cacheFrames_; }

 private:
  bool refill() {
    if (eof_) return false;
    constexpr uint64_t kChunk = 4096;
    cache_.resize(size_t(kChunk) * 2);
    const uint64_t got = dec_->readFrames(cache_.data(), kChunk);
    if (got == 0) {
      eof_ = true;
      cache_.clear();
      cacheFrames_ = 0;
      cachePos_ = 0;
      return false;
    }
    cacheFrames_ = got;
    cachePos_ = 0;
    return true;
  }

  bool nextRaw(float& l, float& r) {
    if (cachePos_ >= cacheFrames_) {
      if (!refill()) return false;
    }
    const int ch = dec_->channels();
    const float* frame = cache_.data() + cachePos_ * size_t(ch);
    l = frame[0];
    r = ch > 1 ? frame[1] : frame[0];
    ++cachePos_;
    return true;
  }

  Decoder* dec_;
  std::vector<float> cache_;
  uint64_t cacheFrames_ = 0;
  uint64_t cachePos_ = 0;
  bool eof_ = false;
  bool valid_ = false;
  double pos_ = 0.0;
  float aL_ = 0.f, aR_ = 0.f;
  float bL_ = 0.f, bR_ = 0.f;
};

inline int16_t toS16(float v) {
  if (v > 1.f) v = 1.f;
  if (v < -1.f) v = -1.f;
  return int16_t(v * 32767.f);
}

}  // namespace

AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init() {
  if (outReady_) return true;
  if (R_FAILED(audoutInitialize())) return false;
  if (R_FAILED(audoutStartAudioOut())) {
    audoutExit();
    return false;
  }
  outReady_ = true;
  running_.store(true);
  thread_ = std::thread(&AudioEngine::threadMain, this);
  return true;
}

void AudioEngine::shutdown() {
  if (!outReady_) return;
  running_.store(false);
  playing_.store(false);
  if (thread_.joinable()) thread_.join();
  audoutStopAudioOut();
  audoutExit();
  outReady_ = false;
}

bool AudioEngine::play(const std::string& path) {
  {
    std::lock_guard<std::mutex> lk(pathMutex_);
    pendingPath_ = path;
  }
  seekRequested_.store(false);
  paused_.store(false);
  pathChanged_.store(true);
  playing_.store(true);
  return true;
}

void AudioEngine::stop() {
  playing_.store(false);
  paused_.store(false);
  playedFrames_.store(0);
  sourceRate_.store(0);
  totalSourceFrames_.store(0);
  std::lock_guard<std::mutex> lk(pathMutex_);
  pendingPath_.clear();
  currentPath_.clear();
  pathChanged_.store(true);
}

void AudioEngine::pause() { paused_.store(true); }

void AudioEngine::resume() {
  if (playing_.load()) paused_.store(false);
}

bool AudioEngine::togglePause() {
  if (!playing_.load()) return false;
  const bool nowPaused = !paused_.load();
  paused_.store(nowPaused);
  return nowPaused;
}

double AudioEngine::positionSec() const {
  const int rate = sourceRate_.load();
  if (rate <= 0) return 0.0;
  return double(playedFrames_.load()) / double(rate);
}

double AudioEngine::durationSec() const {
  const int rate = sourceRate_.load();
  if (rate <= 0) return 0.0;
  return double(totalSourceFrames_.load()) / double(rate);
}

bool AudioEngine::seek(double seconds) {
  const int rate = sourceRate_.load();
  if (rate <= 0) return false;
  const uint64_t frame =
      uint64_t(std::max(0.0, seconds) * double(rate));
  seekTarget_.store(frame);
  seekRequested_.store(true);
  return true;
}

void AudioEngine::setVolume(float v) {
  volume_.store(std::clamp(v, 0.f, 1.f));
}

void AudioEngine::setOnTrackEnd(void (*cb)(void*), void* user) {
  endCb_ = cb;
  endUser_ = user;
}

std::string AudioEngine::currentPath() const {
  std::lock_guard<std::mutex> lk(pathMutex_);
  return currentPath_;
}

void AudioEngine::threadMain() {
  std::vector<std::vector<int16_t>> buffers(kBufferCount);
  std::vector<AudioOutBuffer> outBufs(kBufferCount);
  for (int i = 0; i < kBufferCount; ++i) {
    buffers[i].assign(size_t(kBlockFrames) * kOutChannels, 0);
    outBufs[i].buffer = buffers[i].data();
    outBufs[i].buffer_size = buffers[i].size() * sizeof(int16_t);
    outBufs[i].data_size = outBufs[i].buffer_size;
    audoutAppendAudioOutBuffer(&outBufs[i]);
  }

  Decoder decoder;
  Source source(&decoder);
  int cursor = 0;                 // next buffer expected back from the DSP
  uint64_t consumedFrames = 0;    // absolute source frames consumed
  bool trackEnded = false;

  while (running_.load()) {
    // 1) Apply a pending track change.
    if (pathChanged_.exchange(false)) {
      std::string path;
      {
        std::lock_guard<std::mutex> lk(pathMutex_);
        path = pendingPath_;
        currentPath_ = pendingPath_;
      }
      decoder.close();
      source.reset();
      consumedFrames = 0;
      playedFrames_.store(0);
      trackEnded = false;
      if (!path.empty() && decoder.open(path)) {
        sourceRate_.store(decoder.sampleRate());
        totalSourceFrames_.store(decoder.totalFrames());
        if (source.begin()) {
          playing_.store(true);
        } else {
          playing_.store(false);
          sourceRate_.store(0);
        }
      } else {
        sourceRate_.store(0);
        totalSourceFrames_.store(0);
        playing_.store(false);
      }
    }

    // 2) Apply a pending seek.
    if (seekRequested_.exchange(false)) {
      const uint64_t target = seekTarget_.load();
      if (decoder.isOpen() && decoder.seekToFrame(target)) {
        source.reset();
        consumedFrames = target;
        playedFrames_.store(target);
        trackEnded = false;
        if (!source.begin()) {
          playing_.store(false);
          sourceRate_.store(0);
        }
      }
    }

    // 3) Wait for a buffer to come back.
    AudioOutBuffer* released = nullptr;
    u32 releasedCount = 0;
    const Result rc = audoutWaitPlayFinish(&released, &releasedCount, kWaitNs);
    if (R_FAILED(rc)) {
      svcSleepThread(2000000ULL);
      continue;
    }
    if (releasedCount == 0) continue;

    for (u32 n = 0; n < releasedCount; ++n) {
      int16_t* dst = buffers[cursor].data();
      AudioOutBuffer* submit = &outBufs[cursor];
      cursor = (cursor + 1) % kBufferCount;

      const bool active =
          playing_.load() && !paused_.load() && decoder.isOpen() && !trackEnded;

      if (!active) {
        std::memset(dst, 0, size_t(kBlockFrames) * kOutChannels * sizeof(int16_t));
      } else {
        const int srcRate = decoder.sampleRate();
        const double step = srcRate > 0 ? double(srcRate) / double(kOutRate) : 1.0;
        const float vol = volume_.load();
        int produced = 0;
        for (; produced < kBlockFrames; ++produced) {
          float l = 0.f, r = 0.f;
          if (!source.advance(step, l, r)) break;
          dst[produced * 2 + 0] = toS16(l * vol);
          dst[produced * 2 + 1] = toS16(r * vol);
        }
        if (produced < kBlockFrames) {
          std::memset(dst + produced * 2, 0,
                      size_t(kBlockFrames - produced) * kOutChannels *
                          sizeof(int16_t));
          consumedFrames += uint64_t(double(produced) * step);

          if (looping_.load()) {
            decoder.seekToFrame(0);
            source.reset();
            consumedFrames = 0;
            if (!source.begin()) {
              trackEnded = true;
              playing_.store(false);
            }
          } else {
            trackEnded = true;
            playing_.store(false);
            if (endCb_) endCb_(endUser_);
          }
        } else {
          consumedFrames += uint64_t(double(produced) * step);
        }
        playedFrames_.store(consumedFrames);
      }

      submit->data_size = size_t(kBlockFrames) * kOutChannels * sizeof(int16_t);
      audoutAppendAudioOutBuffer(submit);
    }
  }

  decoder.close();
}

}  // namespace spn