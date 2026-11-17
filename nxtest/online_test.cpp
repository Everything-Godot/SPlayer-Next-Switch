// SPDX-License-Identifier: AGPL-3.0
// Host test for the online track cache: resolve a real stream, download it,
// then hand the cached file to the same decoder the player uses.
//
// The final assertion is the one that matters: the decoder reports a healthy
// duration, which cannot happen unless the bytes on disk are a valid MP3.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "spn/audio.hpp"
#include "spn/netease.hpp"
#include "spn/online_player.hpp"

using namespace spn;

static int g_fail = 0;
static int g_pass = 0;

static void check(const char* what, bool ok, const std::string& detail = {}) {
  if (ok) {
    ++g_pass;
  } else {
    ++g_fail;
    std::printf("FAIL %s  [%s]\n", what, detail.c_str());
  }
}

int main() {
  online::TrackCache::setCacheDir("/tmp/nxcache");
  netease::Client::setStateRoot("/tmp/nxstate");
  (void)!::system("mkdir -p /tmp/nxcache /tmp/nxstate");

  // --- path layout (pure) -------------------------------------------------
  {
    const std::string p = online::TrackCache::pathFor(123, "http://h/a/b.mp3?x=1");
    check("path ext from url", p.find("123.mp3") != std::string::npos, p);
    const std::string q = online::TrackCache::pathFor(7, "http://h/a/b.flac");
    check("path flac", q.find("7.flac") != std::string::npos, q);
    const std::string r = online::TrackCache::pathFor(9, "http://h/a");
    check("path default mp3", r.find("9.mp3") != std::string::npos, r);
    std::string dummy;
    check("lookup miss", !online::TrackCache::lookup(999999, dummy), dummy);
  }

  // --- resolve then download ---------------------------------------------
  netease::Client client;
  std::vector<netease::Song> songs;
  std::string err;
  if (!client.search("纯音乐", 20, 0, songs, err) || songs.empty()) {
    std::printf("FAIL search for candidates: %s\n", err.c_str());
    return 1;
  }

  int64_t chosenId = 0;
  std::string streamUrl;
  for (const auto& s : songs) {
    std::string url;
    int br = 0;
    std::string uerr;
    if (client.songUrl(s.id, "exhigh", url, br, uerr)) {
      chosenId = s.id;
      streamUrl = url;
      std::printf("  chosen: %s (id=%lld)\n", s.name.c_str(), (long long)s.id);
      break;
    }
  }
  check("found a playable track", chosenId != 0, "no free track in results");
  if (!chosenId) return 1;

  online::TrackCache cache;
  cache.begin(chosenId, streamUrl);

  // Poll like the UI would, with a generous ceiling for a slow link.
  int spins = 0;
  while (cache.state() == online::CacheState::Downloading && spins < 600) {
    (void)!::system("sleep 0.1");
    ++spins;
  }
  check("cache reached a terminal state",
        cache.state() != online::CacheState::Downloading,
        online::cacheStateName(cache.state()));

  const std::string path = cache.resultPath();
  check("cache ready", cache.state() == online::CacheState::Ready,
        cache.error() + " state=" + online::cacheStateName(cache.state()));
  check("cache path set", !path.empty(), path);
  std::printf("  cached: %s  progress=%.2f\n", path.c_str(), cache.progress());

  if (cache.state() == online::CacheState::Ready) {
    // --- the real proof: can the player decode what we fetched? -----------
    Decoder dec;
    const bool opened = dec.open(path);
    check("decoder opens cached file", opened, path);
    if (opened) {
      check("decoder duration sane", dec.durationSec() > 5.0 && dec.durationSec() < 900.0,
            std::to_string(dec.durationSec()));
      check("decoder sample rate", dec.sampleRate() >= 8000,
            std::to_string(dec.sampleRate()));
      check("decoder channels", dec.channels() == 1 || dec.channels() == 2,
            std::to_string(dec.channels()));
      std::printf("  decoded: %.1fs, %dHz, %dch, fmt=%d\n", dec.durationSec(),
                  dec.sampleRate(), dec.channels(), int(dec.format()));

      // Seek into the body of the track before sampling: the first frames of a
// real MP3 are routinely still silent, so testing them alone proves nothing.
      check("decoder seek", dec.seekToFrame(dec.totalFrames() / 2),
            std::to_string(dec.totalFrames() / 2));

      // Read a few frames and make sure they are not all silence.
      std::vector<float> pcm(2048 * 2, 0.f);
      const uint64_t got = dec.readFrames(pcm.data(), 2048);
      check("decoder produced frames", got > 0, std::to_string(got));
      float peak = 0.f;
      for (size_t i = 0; i < got * size_t(dec.channels()); ++i) {
        const float a = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (a > peak) peak = a;
      }
      check("audio is not silent", peak > 0.001f, "peak=" + std::to_string(peak));
      std::printf("  peak amplitude mid-track: %.3f\n", peak);
    }

    // Second call must hit the cache without a thread.
    online::TrackCache again;
    again.begin(chosenId, streamUrl);
    check("cache hit is instant", again.state() == online::CacheState::Ready,
          online::cacheStateName(again.state()));
    check("cache hit same path", again.resultPath() == path, again.resultPath());

    check("cache bytes counted", online::TrackCache::cacheBytes() > 100000,
          std::to_string(online::TrackCache::cacheBytes()));
  }

  online::TrackCache::clearCache();
  check("cache cleared", online::TrackCache::cacheBytes() == 0,
        std::to_string(online::TrackCache::cacheBytes()));

  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}