// SPDX-License-Identifier: AGPL-3.0
// Software canvas: draws onto the Switch linear framebuffer.
#include "spn/canvas.hpp"

#include <algorithm>
#include <cmath>

#include "spn/font.hpp"

namespace spn {

namespace {

// The Switch framebuffer is PIXEL_FORMAT_RGBA_8888, i.e. bytes in memory are
// R,G,B,A. Read as a little-endian u32 that means 0xAABBGGRR.
inline uint32_t packRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) |
         (uint32_t(a) << 24);
}

inline uint8_t mix(uint8_t dst, uint8_t src, int a) {
  return uint8_t((src * a + dst * (255 - a)) / 255);
}

}  // namespace

int utf8Decode(const std::string& s, size_t& i, uint32_t& out) {
  if (i >= s.size()) {
    out = 0;
    return 0;
  }
  const unsigned char c = static_cast<unsigned char>(s[i]);
  if (c < 0x80) {
    out = c;
    i += 1;
    return 1;
  }
  int extra = 0;
  uint32_t cp = 0;
  if ((c & 0xE0) == 0xC0) {
    cp = c & 0x1Fu;
    extra = 1;
  } else if ((c & 0xF0) == 0xE0) {
    cp = c & 0x0Fu;
    extra = 2;
  } else if ((c & 0xF8) == 0xF0) {
    cp = c & 0x07u;
    extra = 3;
  } else {
    out = 0xFFFD;
    i += 1;
    return 1;
  }
  for (int k = 1; k <= extra; ++k) {
    if (i + static_cast<size_t>(k) >= s.size()) {
      out = 0xFFFD;
      i = s.size();
      return 1;
    }
    const unsigned char cc = static_cast<unsigned char>(s[i + k]);
    if ((cc & 0xC0) != 0x80) {
      out = 0xFFFD;
      i += 1;
      return 1;
    }
    cp = (cp << 6) | (cc & 0x3Fu);
  }
  i += static_cast<size_t>(extra) + 1;
  out = cp;
  return extra + 1;
}

Canvas::Canvas(uint32_t* pixels, int width, int height, int stridePx)
    : pixels_(pixels), width_(width), height_(height), stridePx_(stridePx) {}

void Canvas::clear(Color c) { fillRect(Rect(0, 0, width_, height_), c); }

void Canvas::blendPixel(int x, int y, Color c, uint8_t alpha) {
  if (alpha == 0 || x < 0 || y < 0 || x >= width_ || y >= height_) return;
  uint32_t& dst = pixels_[static_cast<size_t>(y) * stridePx_ + x];
  if (alpha == 255) {
    dst = packRgba(c.r, c.g, c.b, 255);
    return;
  }
  const uint8_t dr = uint8_t(dst & 0xFF);
  const uint8_t dg = uint8_t((dst >> 8) & 0xFF);
  const uint8_t db = uint8_t((dst >> 16) & 0xFF);
  dst = packRgba(mix(dr, c.r, alpha), mix(dg, c.g, alpha), mix(db, c.b, alpha),
                 255);
}

void Canvas::fillRect(Rect r, Color c) { fillRectAlpha(r, c, 255); }

void Canvas::fillRectAlpha(Rect r, Color c, uint8_t alpha) {
  if (alpha == 0) return;
  const int x0 = std::max(0, r.x);
  const int y0 = std::max(0, r.y);
  const int x1 = std::min(width_, r.x + r.w);
  const int y1 = std::min(height_, r.y + r.h);
  if (x0 >= x1 || y0 >= y1) return;

  const uint32_t solid = packRgba(c.r, c.g, c.b, 255);
  for (int y = y0; y < y1; ++y) {
    uint32_t* row = pixels_ + static_cast<size_t>(y) * stridePx_;
    if (alpha == 255) {
      for (int x = x0; x < x1; ++x) row[x] = solid;
    } else {
      for (int x = x0; x < x1; ++x) {
        const uint32_t dst = row[x];
        row[x] = packRgba(mix(uint8_t(dst & 0xFF), c.r, alpha),
                          mix(uint8_t((dst >> 8) & 0xFF), c.g, alpha),
                          mix(uint8_t((dst >> 16) & 0xFF), c.b, alpha), 255);
      }
    }
  }
}

void Canvas::fillRoundRect(Rect r, int radius, Color c) {
  fillRoundRectAlpha(r, radius, c, 255);
}

void Canvas::fillRoundRectAlpha(Rect r, int radius, Color c, uint8_t alpha) {
  if (r.w <= 0 || r.h <= 0) return;
  radius = std::min(radius, std::min(r.w, r.h) / 2);
  if (radius <= 0) {
    fillRectAlpha(r, c, alpha);
    return;
  }
  const float rr = float(radius);
  for (int dy = 0; dy < r.h; ++dy) {
    int inset = 0;
    if (dy < radius) {
      const float d = rr - float(dy) - 0.5f;
      inset = radius - int(std::sqrt(std::max(0.f, rr * rr - d * d)) + 0.5f);
    } else if (dy >= r.h - radius) {
      const float d = float(dy) - float(r.h - radius) + 0.5f;
      inset = radius - int(std::sqrt(std::max(0.f, rr * rr - d * d)) + 0.5f);
    }
    fillRectAlpha(Rect(r.x + inset, r.y + dy, r.w - inset * 2, 1), c, alpha);
  }
}

void Canvas::strokeRoundRect(Rect r, int radius, Color c, int thickness) {
  if (r.w <= 0 || r.h <= 0 || thickness <= 0) return;
  radius = std::min(radius, std::min(r.w, r.h) / 2);
  thickness = std::max(1, std::min(thickness, std::min(r.w, r.h) / 2));
  const float rr = float(radius);
  const float halfW = float(r.w) * 0.5f;
  const float halfH = float(r.h) * 0.5f;
  const int x0 = std::max(0, r.x);
  const int y0 = std::max(0, r.y);
  const int x1 = std::min(width_, r.x + r.w);
  const int y1 = std::min(height_, r.y + r.h);

  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      // Signed distance to a rounded box (negative == inside).
      const float px = float(x - r.x) + 0.5f;
      const float py = float(y - r.y) + 0.5f;
      const float qx = std::fabs(px - halfW) - (halfW - rr);
      const float qy = std::fabs(py - halfH) - (halfH - rr);
      const float mx = std::max(qx, 0.f);
      const float my = std::max(qy, 0.f);
      const float sdf = std::sqrt(mx * mx + my * my) +
                        std::min(std::max(qx, qy), 0.f) - rr;
      if (sdf <= 0.f && sdf > -float(thickness)) blendPixel(x, y, c, 255);
    }
  }
}

void Canvas::verticalGradient(Rect r, Color top, Color bottom) {
  if (r.h <= 0 || r.w <= 0) return;
  for (int dy = 0; dy < r.h; ++dy) {
    const float t = r.h > 1 ? float(dy) / float(r.h - 1) : 0.f;
    Color c(uint8_t(top.r + (bottom.r - top.r) * t),
            uint8_t(top.g + (bottom.g - top.g) * t),
            uint8_t(top.b + (bottom.b - top.b) * t), 255);
    fillRectAlpha(Rect(r.x, r.y + dy, r.w, 1), c, 255);
  }
}

int Canvas::measureText(const Font& font, const std::string& utf8) const {
  int x = 0;
  size_t i = 0;
  while (i < utf8.size()) {
    uint32_t cp = 0;
    if (utf8Decode(utf8, i, cp) == 0) break;
    const Glyph* g = font.glyph(cp);
    if (g) x += int(g->advance + 0.5f);
  }
  return x;
}

void Canvas::drawText(const Font& font, int x, int y, const std::string& utf8,
                      Color c) {
  const int baseline = y + font.ascent();
  int penX = x;
  size_t i = 0;
  while (i < utf8.size()) {
    uint32_t cp = 0;
    if (utf8Decode(utf8, i, cp) == 0) break;
    const Glyph* g = font.glyph(cp);
    if (!g) continue;
    if (g->w > 0 && g->h > 0) {
      const int gx = penX + g->xoff;
      const int gy = baseline + g->yoff;
      for (int row = 0; row < g->h; ++row) {
        const int py = gy + row;
        if (py < 0 || py >= height_) continue;
        const uint8_t* src = g->bitmap.data() + size_t(row) * g->w;
        for (int col = 0; col < g->w; ++col) {
          const uint8_t cov = src[col];
          if (cov) blendPixel(gx + col, py, c, cov);
        }
      }
    }
    penX += int(g->advance + 0.5f);
  }
}

void Canvas::drawTextEllipsis(const Font& font, Rect box,
                              const std::string& utf8, Color c) {
  if (measureText(font, utf8) <= box.w) {
    drawText(font, box.x, box.y, utf8, c);
    return;
  }
  const std::string dots = "...";
  const int dotsW = measureText(font, dots);
  const int limit = box.w - dotsW;
  std::string out;
  size_t i = 0;
  int w = 0;
  while (i < utf8.size()) {
    const size_t start = i;
    uint32_t cp = 0;
    if (utf8Decode(utf8, i, cp) == 0) break;
    const Glyph* g = font.glyph(cp);
    const int adv = g ? int(g->advance + 0.5f) : 0;
    if (w + adv > limit) break;
    w += adv;
    out.append(utf8, start, i - start);
  }
  out += dots;
  drawText(font, box.x, box.y, out, c);
}

}  // namespace spn