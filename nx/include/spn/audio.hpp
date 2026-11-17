// SPDX-License-Identifier: AGPL-3.0
// Audio decoding (dr_libs) + playback through libnx audout.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace spn {

// Streaming decoder for MP3 / FLAC / WAV(AIFF) — the formats that matter on
// Switch. Everything is converted to interleaved float frames.
class Decoder {
 public:
  enum class Format { None, Mp3, Flac, Wav };

  Decoder() = default;
  ~Decoder();

  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;

  bool open(const std::string& path);
  void close();

  bool isOpen() const { return format_ != Format::None; }
  Format format() const { return format_; }
  int sampleRate() const { return sampleRate_; }
  int channels() const { return channels_; }
  uint64_t totalFrames() const { return totalFrames_; }
  double durationSec() const;

  // Reads up to `frames` interleaved frames. Returns frames actually read.
  uint64_t readFrames(float* out, uint64_t frames);
  bool seekToFrame(uint64_t frame);

 private:
  void* handle_ = nullptr;  // drmp3* / drflac* / drwav*
  Format format_ = Format::None;
  int sampleRate_ = 0;
  int channels_ = 0;
  uint64_t totalFrames_ = 0;
};

// Streams the current track to the console speakers.
class AudioEngine {
 public:
  static constexpr int kOutRate = 48000;
  static constexpr int kOutChannels = 2;

  AudioEngine() = default;
  ~AudioEngine();

  bool init();
  void shutdown();

  bool play(const std::string& path);
  void stop();
  void pause();
  void resume();
  bool togglePause();

  bool isPlaying() const { return playing_.load(); }
  bool isLoaded() const { return sourceRate_ > 0; }
  double positionSec() const;
  double durationSec() const;

  bool seek(double seconds);
  void setVolume(float v);
  float volume() const { return volume_.load(); }
  void setLooping(bool v) { looping_.store(v); }
  bool looping() const { return looping_.load(); }

  // Called from the mixer thread when a track reaches its end.
  void setOnTrackEnd(void (*cb)(void*), void* user);

  std::string currentPath() const;

 private:
  void threadMain();
  void pushSilence();
  bool reopenAt(double seconds);

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> playing_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> looping_{false};
  std::atomic<float> volume_{0.8f};
  std::atomic<uint64_t> playedFrames_{0};

  mutable std::mutex pathMutex_;
  std::string pendingPath_;
  std::string currentPath_;
  std::atomic<bool> pathChanged_{false};

  std::atomic<uint64_t> seekTarget_{0};
  std::atomic<bool> seekRequested_{false};

  std::atomic<int> sourceRate_{0};
  std::atomic<uint64_t> totalSourceFrames_{0};

  void (*endCb_)(void*) = nullptr;
  void* endUser_ = nullptr;

  bool outReady_ = false;
};

}  // namespace spn