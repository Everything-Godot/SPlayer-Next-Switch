// SPDX-License-Identifier: AGPL-3.0
// Online playback: turns a NetEase stream URL into something the local decoder
// can open.
//
// Why cache-then-play instead of true streaming: dr_libs opens a file and reads
// it sequentially, and the CDN URL is an ordinary http object. Feeding a decoder
// from a socket needs a seekable ring buffer plus EOF-vs-not-yet-arrived
// disambiguation, which is a lot of machinery for a few seconds of latency on
// the first play. Downloading to the SD cache costs one wait, then every replay
// is instant and seeking works normally. That trade is documented in the README
// rather than hidden.
//
// Everything runs on a worker thread with a small state machine the UI polls,
// so the render loop never blocks on a socket.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace spn::online {

enum class CacheState {
  Idle,
  Downloading,
  Ready,
  Failed,
};

const char* cacheStateName(CacheState s);

// Downloads a track to the SD cache and reports progress.
class TrackCache {
 public:
  // Cache directory on the Switch, or an overridable root for host tests.
  static const std::string& cacheDir();
  static void setCacheDir(const std::string& dir);

  // The path a given song id will be cached at. Extension is guessed from the
  // URL, since the CDN serves both .mp3 and .flac under the same host.
  static std::string pathFor(int64_t songId, const std::string& url);

  // Returns true and fills `path` when the track is already cached and complete.
  static bool lookup(int64_t songId, std::string& path);

  TrackCache() = default;
  ~TrackCache();

  TrackCache(const TrackCache&) = delete;
  TrackCache& operator=(const TrackCache&) = delete;

  // Starts (or restarts) a download. Cancels any download in flight.
  void begin(int64_t songId, const std::string& url);
  void cancel();

  CacheState state() const { return state_.load(); }
  // 0..1, monotonic while Downloading.
  float progress() const { return progress_.load(); }
  // Set on Failed.
  std::string error() const;
  // Set on Ready: the local file the decoder should open.
  std::string resultPath() const;

  // Total bytes already cached across the whole cache directory, for the
  // settings page. Cheap enough to call from the UI occasionally.
  static uint64_t cacheBytes();
  static void clearCache();

 private:
  void threadMain(int64_t songId, std::string url);

  std::thread thread_;
  std::atomic<bool> cancel_{false};
  std::atomic<CacheState> state_{CacheState::Idle};
  std::atomic<float> progress_{0.f};

  mutable std::mutex errMutex_;
  std::string error_;
  std::string resultPath_;
};

// Writes `bytes` to `path` atomically-ish: streams into path.part then renames,
// so a killed download never looks like a complete cache entry.
bool downloadToFile(const std::string& url, const std::string& path, int64_t songId,
                    std::atomic<float>& progress, std::atomic<bool>& cancel,
                    std::string& err);

}  // namespace spn::online