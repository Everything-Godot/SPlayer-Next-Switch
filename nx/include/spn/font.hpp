// SPDX-License-Identifier: AGPL-3.0
// TrueType text rendering backed by stb_truetype.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace spn {

struct Glyph {
  std::vector<uint8_t> bitmap;  // 8-bit coverage, w*h
  int w = 0;
  int h = 0;
  int xoff = 0;
  int yoff = 0;
  float advance = 0.f;
};

// One Font instance == one fixed pixel height (keeps rendering unscaled).
class Font {
 public:
  Font();
  ~Font();

  // Loads a TTF/OTF from memory; the buffer is owned by the font.
  bool loadFromMemory(std::vector<uint8_t> data, float pixelHeight);
  bool loadFromFile(const std::string& path, float pixelHeight);

  bool valid() const { return info_ != nullptr; }
  float pixelHeight() const { return pixelHeight_; }
  int lineHeight() const;
  int ascent() const;
  int descent() const;

  // Returns a cached glyph, rasterising it on first use.
  const Glyph* glyph(uint32_t codepoint) const;

 private:
  struct FontInfo;

  std::unique_ptr<FontInfo> info_;
  std::vector<uint8_t> data_;
  float pixelHeight_ = 20.f;
  float scale_ = 1.f;
  std::unordered_map<uint32_t, Glyph> mutable cache_;
};

}  // namespace spn