// SPDX-License-Identifier: AGPL-3.0
// Decoded RGBA image helpers: cover art decoding + dominant colour sampling.
#include "spn/image.hpp"

#include <algorithm>
#include <cstdio>

#include "stb/stb_image.h"

namespace spn {

namespace {

// Reorders the 4-byte RGBA stream stb gives us into the framebuffer's
// little-endian 0xAABBGGRR layout.
void repack(uint8_t* rgba, size_t count, std::vector<uint32_t>& out) {
  out.resize(count);
  for (size_t i = 0; i < count; ++i) {
    const uint8_t r = rgba[i * 4 + 0];
    const uint8_t g = rgba[i * 4 + 1];
    const uint8_t b = rgba[i * 4 + 2];
    out[i] = uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) |
             (uint32_t(0xFF) << 24);
  }
}

}  // namespace

uint32_t Image::dominantColor() const {
  if (!valid()) return 0xFFFFCC44u;  // default amber accent

  // Sample on a coarse grid: cheap and stable enough for a tint.
  const int step = std::max(1, std::min(w, h) / 48);
  uint64_t sr = 0, sg = 0, sb = 0, n = 0;

  for (int y = 0; y < h; y += step) {
    for (int x = 0; x < w; x += step) {
      const uint32_t p = px[size_t(y) * size_t(w) + size_t(x)];
      const uint8_t r = uint8_t(p & 0xFF);
      const uint8_t g = uint8_t((p >> 8) & 0xFF);
      const uint8_t b = uint8_t((p >> 16) & 0xFF);

      // Skip near-black / near-white pixels: they make dull accents.
      const int mx = std::max({int(r), int(g), int(b)});
      const int mn = std::min({int(r), int(g), int(b)});
      if (mx < 32 || mn > 232) continue;

      sr += r;
      sg += g;
      sb += b;
      ++n;
    }
  }

  if (n == 0) {
    // Everything was monochrome; fall back to the plain average.
    n = 0;
    for (int y = 0; y < h; y += step) {
      for (int x = 0; x < w; x += step) {
        const uint32_t p = px[size_t(y) * size_t(w) + size_t(x)];
        sr += uint8_t(p & 0xFF);
        sg += uint8_t((p >> 8) & 0xFF);
        sb += uint8_t((p >> 16) & 0xFF);
        ++n;
      }
    }
    if (n == 0) return 0xFFFFCC44u;
  }

  uint8_t r = uint8_t(sr / n);
  uint8_t g = uint8_t(sg / n);
  uint8_t b = uint8_t(sb / n);

  // Push saturation up a bit so the accent reads as a colour, not grey.
  const int lum = (r * 30 + g * 59 + b * 11) / 100;
  auto boost = [&](uint8_t v) {
    const int d = int(v) - lum;
    return uint8_t(std::clamp(lum + d * 2, 0, 255));
  };
  r = boost(r);
  g = boost(g);
  b = boost(b);

  return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) |
         (uint32_t(0xFF) << 24);
}

bool Image::decode(const uint8_t* data, size_t size, Image& out) {
  out.reset();
  if (!data || size == 0) return false;

  int w = 0, h = 0, comp = 0;
  stbi_uc* rgba = stbi_load_from_memory(data, int(size), &w, &h, &comp, 4);
  if (!rgba) return false;
  if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
    stbi_image_free(rgba);
    return false;
  }

  out.w = w;
  out.h = h;
  repack(rgba, size_t(w) * size_t(h), out.px);
  stbi_image_free(rgba);
  return out.valid();
}

bool Image::decodeFile(const std::string& path, Image& out) {
  out.reset();
  FILE* fp = std::fopen(path.c_str(), "rb");
  if (!fp) return false;
  if (std::fseek(fp, 0, SEEK_END) != 0) {
    std::fclose(fp);
    return false;
  }
  const long size = std::ftell(fp);
  if (size <= 0 || size > 64 * 1024 * 1024) {
    std::fclose(fp);
    return false;
  }
  std::fseek(fp, 0, SEEK_SET);
  std::vector<uint8_t> buf(static_cast<size_t>(size));
  const size_t got = std::fread(buf.data(), 1, buf.size(), fp);
  std::fclose(fp);
  if (got != buf.size()) return false;
  return decode(buf.data(), buf.size(), out);
}

}  // namespace spn