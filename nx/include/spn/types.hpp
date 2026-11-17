// SPDX-License-Identifier: AGPL-3.0
// Shared basic types for the Nintendo Switch port of SPlayer-Next.
#pragma once

#include <cstdint>
#include <string>

namespace spn {

// RGBA8888 colour, matching the framebuffer pixel format.
struct Color {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;

  constexpr Color() = default;
  constexpr Color(uint8_t rr, uint8_t gg, uint8_t bb, uint8_t aa = 255)
      : r(rr), g(gg), b(bb), a(aa) {}

  // Pack into the u32 layout expected by the linear framebuffer.
  constexpr uint32_t packed() const {
    return (uint32_t(r) << 24) | (uint32_t(g) << 16) | (uint32_t(b) << 8) |
           uint32_t(a);
  }

  static constexpr Color rgb(uint32_t v) {
    return Color(uint8_t((v >> 16) & 0xFF), uint8_t((v >> 8) & 0xFF),
                 uint8_t(v & 0xFF));
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
};

// A single library entry.
struct Track {
  std::string path;
  std::string title;
  std::string artist;
  std::string album;
  double durationSec = 0.0;
};

// Milliseconds helper used all over the UI.
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

}  // namespace spn
