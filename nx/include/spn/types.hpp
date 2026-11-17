// SPDX-License-Identifier: AGPL-3.0
// Shared basic types for the Nintendo Switch port of SPlayer-Next.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace spn {

// RGBA8888 colour.
struct Color {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;

  constexpr Color() = default;
  constexpr Color(uint8_t rr, uint8_t gg, uint8_t bb, uint8_t aa = 255)
      : r(rr), g(gg), b(bb), a(aa) {}

  static constexpr Color rgb(uint32_t v) {
    return Color(uint8_t((v >> 16) & 0xFF), uint8_t((v >> 8) & 0xFF),
                 uint8_t(v & 0xFF));
  }
  static constexpr Color rgba(uint32_t v) {
    return Color(uint8_t((v >> 24) & 0xFF), uint8_t((v >> 16) & 0xFF),
                 uint8_t((v >> 8) & 0xFF), uint8_t(v & 0xFF));
  }

  // Clears the alpha channel: the UI composites itself, the compositor needs
  // fully opaque pixels.
  constexpr Color opaque() const { return Color(r, g, b, 255); }

  constexpr Color withA(uint8_t aa) const { return Color(r, g, b, aa); }

  // Linear interpolation, used for hover/transition effects.
  static Color lerp(Color a, Color b, float t) {
    if (t <= 0.f) return a;
    if (t >= 1.f) return b;
    return Color(uint8_t(a.r + (b.r - a.r) * t), uint8_t(a.g + (b.g - a.g) * t),
                 uint8_t(a.b + (b.b - a.b) * t), 255);
  }
};

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  constexpr Rect() = default;
  constexpr Rect(int xx, int yy, int ww, int hh) : x(xx), y(yy), w(ww), h(hh) {}

  constexpr bool contains(int px, int py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
  constexpr int right() const { return x + w; }
  constexpr int bottom() const { return y + h; }
  constexpr Rect inset(int d) const { return Rect(x + d, y + d, w - d * 2, h - d * 2); }
  constexpr bool empty() const { return w <= 0 || h <= 0; }
};

// ---------------------------------------------------------------------------
// Library model (mirrors the desktop app's Track)
// ---------------------------------------------------------------------------

struct Track {
  std::string path;          // absolute sdmc: path
  std::string title;
  std::string artist;
  std::string album;
  std::string albumArtist;
  std::string genre;
  int year = 0;
  int trackNo = 0;
  int discNo = 0;
  double durationSec = 0.0;
  uint64_t fileSize = 0;
  std::string format;        // "MP3" / "FLAC" / "WAV"
  int sampleRate = 0;
  int channels = 0;
  int bitDepth = 0;
  int bitrateKbps = 0;

  // Cover art thumbnail, decoded from the embedded tag at scan time.
  // Index into the cover cache; -1 == none.
  int coverId = -1;

  bool liked = false;
};

struct Album {
  std::string name;
  std::string artist;
  int year = 0;
  std::vector<int> trackIndices;
  int coverId = -1;
};

struct ArtistEntry {
  std::string name;
  std::vector<int> trackIndices;
  std::vector<std::string> albums;
};

// A saved playlist / favourite list.
struct Playlist {
  std::string name;
  std::vector<int> trackIndices;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

inline std::string formatTime(uint64_t ms) {
  uint64_t totalSec = ms / 1000ULL;
  uint64_t m = totalSec / 60ULL;
  uint64_t s = totalSec % 60ULL;
  std::string out;
  if (m < 10) out.push_back('0');
  out += std::to_string(m);
  out.push_back(':');
  if (s < 10) out.push_back('0');
  out += std::to_string(s);
  return out;
}

// Long form used by the stats page: "3 小时 12 分".
inline std::string formatLongDuration(uint64_t seconds) {
  const uint64_t h = seconds / 3600ULL;
  const uint64_t m = (seconds % 3600ULL) / 60ULL;
  if (h > 0) return std::to_string(h) + " 小时 " + std::to_string(m) + " 分";
  return std::to_string(m) + " 分";
}

inline std::string humanSize(uint64_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double v = double(bytes);
  int u = 0;
  while (v >= 1024.0 && u < 4) {
    v /= 1024.0;
    ++u;
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
  return std::string(buf);
}

}  // namespace spn