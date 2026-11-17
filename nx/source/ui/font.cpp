// SPDX-License-Identifier: AGPL-3.0
// TrueType text rendering backed by stb_truetype.
#include "spn/font.hpp"

#include <cmath>
#include <cstdio>

#include "stb/stb_truetype.h"

namespace spn {

struct Font::FontInfo {
  stbtt_fontinfo tt{};
};

Font::Font() = default;
Font::~Font() = default;

bool Font::loadFromMemory(std::vector<uint8_t> data, float pixelHeight) {
  if (data.empty()) return false;
  data_ = std::move(data);
  info_ = std::make_unique<FontInfo>();
  const int offset = stbtt_GetFontOffsetForIndex(data_.data(), 0);
  if (offset < 0) {
    info_.reset();
    return false;
  }
  if (!stbtt_InitFont(&info_->tt, data_.data(), offset)) {
    info_.reset();
    return false;
  }
  pixelHeight_ = pixelHeight;
  scale_ = stbtt_ScaleForPixelHeight(&info_->tt, pixelHeight);
  return true;
}

bool Font::loadFromFile(const std::string& path, float pixelHeight) {
  FILE* fp = std::fopen(path.c_str(), "rb");
  if (!fp) return false;
  std::fseek(fp, 0, SEEK_END);
  const long size = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(fp);
    return false;
  }
  std::vector<uint8_t> data(static_cast<size_t>(size));
  const size_t got = std::fread(data.data(), 1, data.size(), fp);
  std::fclose(fp);
  if (got != data.size()) return false;
  return loadFromMemory(std::move(data), pixelHeight);
}

int Font::ascent() const {
  if (!info_) return int(pixelHeight_);
  int a = 0, d = 0, g = 0;
  stbtt_GetFontVMetrics(&info_->tt, &a, &d, &g);
  return int(std::ceil(float(a) * scale_));
}

int Font::descent() const {
  if (!info_) return 0;
  int a = 0, d = 0, g = 0;
  stbtt_GetFontVMetrics(&info_->tt, &a, &d, &g);
  return int(std::ceil(float(-d) * scale_));
}

int Font::lineHeight() const { return ascent() + descent(); }

const Glyph* Font::glyph(uint32_t codepoint) const {
  if (!info_) return nullptr;
  auto it = cache_.find(codepoint);
  if (it != cache_.end()) return &it->second;

  Glyph g;
  int advance = 0;
  int leftBearing = 0;
  stbtt_GetCodepointHMetrics(&info_->tt, int(codepoint), &advance, &leftBearing);
  g.advance = float(advance) * scale_;

  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  stbtt_GetCodepointBitmapBox(&info_->tt, int(codepoint), scale_, scale_, &x0, &y0,
                              &x1, &y1);
  g.w = x1 - x0;
  g.h = y1 - y0;
  g.xoff = x0;
  g.yoff = y0;

  if (g.w > 0 && g.h > 0) {
    g.bitmap.assign(static_cast<size_t>(g.w) * size_t(g.h), 0);
    stbtt_MakeCodepointBitmap(&info_->tt, g.bitmap.data(), g.w, g.h, g.w, scale_,
                              scale_, int(codepoint));
  }

  auto res = cache_.emplace(codepoint, std::move(g));
  return &res.first->second;
}

}  // namespace spn