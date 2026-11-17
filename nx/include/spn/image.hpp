// SPDX-License-Identifier: AGPL-3.0
// Decoded RGBA image, used for cover art.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spn {

struct Image {
  int w = 0;
  int h = 0;
  std::vector<uint32_t> px;  // 0xAABBGGRR (same layout as the framebuffer)

  bool valid() const { return w > 0 && h > 0 && px.size() == size_t(w) * size_t(h); }
  void reset() {
    w = h = 0;
    px.clear();
  }

  // Estimates the dominant colour (used for the adaptive accent tint).
  uint32_t dominantColor() const;

  // Decodes any stb_image-supported format (JPEG/PNG/BMP/...) from memory.
  static bool decode(const uint8_t* data, size_t size, Image& out);
  static bool decodeFile(const std::string& path, Image& out);
};

}  // namespace spn