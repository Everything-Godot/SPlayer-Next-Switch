// SPDX-License-Identifier: AGPL-3.0
// Software canvas: draws onto the Switch linear framebuffer.
#include "spn/canvas.hpp"

#include <algorithm>
#include <cmath>

#include "spn/font.hpp"
#include "spn/image.hpp"

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

inline Rect intersectRect(Rect a, Rect b) {
  const int x0 = std::max(a.x, b.x);
  const int y0 = std::max(a.y, b.y);
  const int x1 = std::min(a.right(), b.right());
  const int y1 = std::min(a.bottom(), b.bottom());
  if (x0 >= x1 || y0 >= y1) return Rect(0, 0, 0, 0);
  return Rect(x0, y0, x1 - x0, y1 - y0);
}

// Signed distance to a rounded box laid out inside (0..w, 0..h); <0 == inside.
inline float roundBoxSdf(float px, float py, float halfW, float halfH, float rr) {
  const float qx = std::fabs(px - halfW) - (halfW - rr);
  const float qy = std::fabs(py - halfH) - (halfH - rr);
  const float mx = std::max(qx, 0.f);
  const float my = std::max(qy, 0.f);
  return std::sqrt(mx * mx + my * my) + std::min(std::max(qx, qy), 0.f) - rr;
}

inline uint8_t coverage(float sdf) {
  const float c = 0.5f - sdf;  // ~1px analytic antialias band
  if (c <= 0.f) return 0;
  if (c >= 1.f) return 255;
  return uint8_t(c * 255.f);
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
    : pixels_(pixels),
      width_(width),
      height_(height),
      stridePx_(stridePx),
      clip_(0, 0, width, height) {}

// --- clip ------------------------------------------------------------------

void Canvas::pushClip(Rect r) {
  clipStack_.push_back(clip_);
  clip_ = intersectRect(clip_, r);
}

void Canvas::popClip() {
  if (clipStack_.empty()) {
    clip_ = Rect(0, 0, width_, height_);
    return;
  }
  clip_ = clipStack_.back();
  clipStack_.pop_back();
}

// --- pixel -----------------------------------------------------------------

void Canvas::blendPixel(int x, int y, Color c, uint8_t alpha) {
  if (alpha == 0 || !visible(x, y)) return;
  uint32_t& dst = pixels_[static_cast<size_t>(y) * stridePx_ + x];
  if (alpha == 255) {
    dst = packRgba(c.r, c.g, c.b, 255);
    return;
  }
  dst = packRgba(mix(uint8_t(dst & 0xFF), c.r, alpha),
                 mix(uint8_t((dst >> 8) & 0xFF), c.g, alpha),
                 mix(uint8_t((dst >> 16) & 0xFF), c.b, alpha), 255);
}

// --- fills -----------------------------------------------------------------

void Canvas::clear(Color c) { fillRect(Rect(0, 0, width_, height_), c); }

void Canvas::fillRect(Rect r, Color c) { fillRectAlpha(r, c, 255); }

void Canvas::fillRectAlpha(Rect r, Color c, uint8_t alpha) {
  if (alpha == 0) return;
  const Rect a = intersectRect(r, clip_);
  if (a.empty()) return;
  const uint32_t solid = packRgba(c.r, c.g, c.b, 255);
  for (int y = a.y; y < a.bottom(); ++y) {
    uint32_t* row = pixels_ + static_cast<size_t>(y) * stridePx_;
    if (alpha == 255) {
      for (int x = a.x; x < a.right(); ++x) row[x] = solid;
    } else {
      for (int x = a.x; x < a.right(); ++x) {
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
  if (r.w <= 0 || r.h <= 0 || alpha == 0) return;
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

void Canvas::fillCircle(int cx, int cy, int radius, Color c) {
  if (radius <= 0) return;
  const Rect box = intersectRect(
      Rect(cx - radius, cy - radius, radius * 2, radius * 2), clip_);
  if (box.empty()) return;
  const float rr = float(radius);
  for (int y = box.y; y < box.bottom(); ++y) {
    const float dy = float(y - cy) + 0.5f;
    for (int x = box.x; x < box.right(); ++x) {
      const float dx = float(x - cx) + 0.5f;
      const float sdf = std::sqrt(dx * dx + dy * dy) - rr;
      if (sdf > 0.5f) continue;
      const uint8_t cov = coverage(sdf);
      if (cov) blendPixel(x, y, c, cov);
    }
  }
}

void Canvas::strokeRoundRect(Rect r, int radius, Color c, int thickness) {
  if (r.w <= 0 || r.h <= 0 || thickness <= 0) return;
  radius = std::min(radius, std::min(r.w, r.h) / 2);
  thickness = std::max(1, std::min(thickness, std::min(r.w, r.h) / 2));
  const float rr = float(radius);
  const float halfW = float(r.w) * 0.5f;
  const float halfH = float(r.h) * 0.5f;
  const Rect box = intersectRect(r, clip_);
  if (box.empty()) return;
  for (int y = box.y; y < box.bottom(); ++y) {
    for (int x = box.x; x < box.right(); ++x) {
      const float px = float(x - r.x) + 0.5f;
      const float py = float(y - r.y) + 0.5f;
      const float sdf = roundBoxSdf(px, py, halfW, halfH, rr);
      if (sdf <= 0.f && sdf > -float(thickness)) blendPixel(x, y, c, 255);
    }
  }
}

void Canvas::strokeRect(Rect r, Color c, int thickness) {
  if (r.w <= 0 || r.h <= 0 || thickness <= 0) return;
  thickness = std::max(1, std::min(thickness, std::min(r.w, r.h) / 2));
  fillRect(Rect(r.x, r.y, r.w, thickness), c);
  fillRect(Rect(r.x, r.bottom() - thickness, r.w, thickness), c);
  fillRect(Rect(r.x, r.y + thickness, thickness, r.h - thickness * 2), c);
  fillRect(Rect(r.right() - thickness, r.y + thickness, thickness,
                r.h - thickness * 2),
           c);
}

void Canvas::verticalGradient(Rect r, Color top, Color bottom) {
  if (r.h <= 0 || r.w <= 0) return;
  const Rect box = intersectRect(r, clip_);
  if (box.empty()) return;
  for (int y = box.y; y < box.bottom(); ++y) {
    const float t = r.h > 1 ? float(y - r.y) / float(r.h - 1) : 0.f;
    const Color c(uint8_t(top.r + (bottom.r - top.r) * t),
                  uint8_t(top.g + (bottom.g - top.g) * t),
                  uint8_t(top.b + (bottom.b - top.b) * t), 255);
    fillRectAlpha(Rect(box.x, y, box.w, 1), c, 255);
  }
}

void Canvas::horizontalGradient(Rect r, Color left, Color right) {
  if (r.h <= 0 || r.w <= 0) return;
  const Rect box = intersectRect(r, clip_);
  if (box.empty()) return;
  for (int y = box.y; y < box.bottom(); ++y) {
    uint32_t* row = pixels_ + static_cast<size_t>(y) * stridePx_;
    for (int x = box.x; x < box.right(); ++x) {
      const float t = r.w > 1 ? float(x - r.x) / float(r.w - 1) : 0.f;
      row[x] = packRgba(uint8_t(left.r + (right.r - left.r) * t),
                        uint8_t(left.g + (right.g - left.g) * t),
                        uint8_t(left.b + (right.b - left.b) * t), 255);
    }
  }
}

void Canvas::dropShadow(Rect r, int radius, int spread, uint8_t alpha) {
  if (r.w <= 0 || r.h <= 0 || spread <= 0 || alpha == 0) return;
  radius = std::min(radius, std::min(r.w, r.h) / 2);
  const float rr = float(radius);
  const float halfW = float(r.w) * 0.5f;
  const float halfH = float(r.h) * 0.5f;
  const Rect outer = intersectRect(
      Rect(r.x - spread, r.y - spread, r.w + spread * 2, r.h + spread * 2),
      clip_);
  if (outer.empty()) return;
  const Color shadow(0, 0, 0, 255);
  for (int y = outer.y; y < outer.bottom(); ++y) {
    for (int x = outer.x; x < outer.right(); ++x) {
      const float px = float(x - r.x) + 0.5f;
      const float py = float(y - r.y) + 0.5f;
      const float sdf = roundBoxSdf(px, py, halfW, halfH, rr);
      if (sdf <= 0.f) continue;
      const float k = 1.f - sdf / float(spread);
      if (k <= 0.f) continue;
      const int a = int(float(alpha) * k * k);
      if (a > 0) blendPixel(x, y, shadow, uint8_t(std::min(255, a)));
    }
  }
}

// --- images ----------------------------------------------------------------

void Canvas::drawImage(const Image& img, Rect dst, uint8_t alpha) {
  drawImageRounded(img, dst, 0, alpha);
}

void Canvas::drawImageRounded(const Image& img, Rect dst, int radius,
                              uint8_t alpha) {
  if (!img.valid() || dst.w <= 0 || dst.h <= 0 || alpha == 0) return;
  const Rect box = intersectRect(dst, clip_);
  if (box.empty()) return;

  const float sx = float(img.w) / float(dst.w);
  const float sy = float(img.h) / float(dst.h);
  const float rr = float(std::max(0, std::min(radius, std::min(dst.w, dst.h) / 2)));
  const float halfW = float(dst.w) * 0.5f;
  const float halfH = float(dst.h) * 0.5f;
  const int maxX = img.w - 1;
  const int maxY = img.h - 1;

  for (int y = box.y; y < box.bottom(); ++y) {
    for (int x = box.x; x < box.right(); ++x) {
      uint8_t a = alpha;
      if (rr > 0.f) {
        const float px = float(x - dst.x) + 0.5f;
        const float py = float(y - dst.y) + 0.5f;
        const float sdf = roundBoxSdf(px, py, halfW, halfH, rr);
        if (sdf > 0.5f) continue;
        a = uint8_t(int(a) * coverage(sdf) / 255);
        if (a == 0) continue;
      }

      // Bilinear sample in source coordinates.
      float fx = (float(x - dst.x) + 0.5f) * sx - 0.5f;
      float fy = (float(y - dst.y) + 0.5f) * sy - 0.5f;
      fx = std::max(0.f, std::min(float(maxX), fx));
      fy = std::max(0.f, std::min(float(maxY), fy));
      const int x0 = int(fx);
      const int y0 = int(fy);
      const int x1 = std::min(maxX, x0 + 1);
      const int y1 = std::min(maxY, y0 + 1);
      const float tx = fx - float(x0);
      const float ty = fy - float(y0);

      const uint32_t p00 = img.px[size_t(y0) * size_t(img.w) + size_t(x0)];
      const uint32_t p10 = img.px[size_t(y0) * size_t(img.w) + size_t(x1)];
      const uint32_t p01 = img.px[size_t(y1) * size_t(img.w) + size_t(x0)];
      const uint32_t p11 = img.px[size_t(y1) * size_t(img.w) + size_t(x1)];

      auto lerp2 = [&](int shift) {
        const float v00 = float((p00 >> shift) & 0xFFu);
        const float v10 = float((p10 >> shift) & 0xFFu);
        const float v01 = float((p01 >> shift) & 0xFFu);
        const float v11 = float((p11 >> shift) & 0xFFu);
        const float t0 = v00 + (v10 - v00) * tx;
        const float t1 = v01 + (v11 - v01) * tx;
        return uint8_t(t0 + (t1 - t0) * ty);
      };

      const Color c(lerp2(0), lerp2(8), lerp2(16), 255);
      blendPixel(x, y, c, a);
    }
  }
}

// --- text ------------------------------------------------------------------

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
    if (cp == '\n' || cp == '\r') continue;  // caller splits lines
    const Glyph* g = font.glyph(cp);
    if (!g) continue;
    const int gx = penX + g->xoff;
    const int gy = baseline + g->yoff;
    if (g->w > 0 && g->h > 0) {
      for (int row = 0; row < g->h; ++row) {
        const int py = gy + row;
        if (py < clip_.y || py >= clip_.bottom()) continue;
        const int left = std::max(gx, clip_.x);
        const int right = std::min(gx + g->w, clip_.right());
        if (left >= right) continue;
        const uint8_t* src = g->bitmap.data() + size_t(row) * g->w;
        for (int px = left; px < right; ++px) {
          const uint8_t cov = src[px - gx];
          if (cov) blendPixel(px, py, c, cov);
        }
      }
    }
    penX += int(g->advance + 0.5f);
  }
}

void Canvas::drawTextCentered(const Font& font, Rect box,
                              const std::string& utf8, Color c) {
  const int w = measureText(font, utf8);
  const int x = box.x + (box.w - w) / 2;
  const int y = box.y + (box.h - font.lineHeight()) / 2;
  drawText(font, x, y, utf8, c);
}

void Canvas::drawTextRight(const Font& font, Rect box, const std::string& utf8,
                           Color c) {
  const int w = measureText(font, utf8);
  const int y = box.y + (box.h - font.lineHeight()) / 2;
  drawText(font, box.right() - w, y, utf8, c);
}

void Canvas::drawTextEllipsis(const Font& font, Rect box,
                              const std::string& utf8, Color c) {
  if (measureText(font, utf8) <= box.w) {
    drawText(font, box.x, box.y, utf8, c);
    return;
  }
  const std::string dots = "...";
  const int limit = std::max(0, box.w - measureText(font, dots));
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

void Canvas::drawTextEllipsisCentered(const Font& font, Rect box,
                                      const std::string& utf8, Color c) {
  std::string text = utf8;
  if (measureText(font, utf8) > box.w) {
    const std::string dots = "...";
    const int limit = std::max(0, box.w - measureText(font, dots));
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
    text = out + dots;
  }
  drawTextCentered(font, box, text, c);
}

std::vector<std::string> Canvas::wrapText(const Font& font,
                                          const std::string& utf8,
                                          int maxW) const {
  std::vector<std::string> lines;
  if (utf8.empty()) return lines;
  if (maxW <= 0) {
    lines.push_back(utf8);
    return lines;
  }

  std::string cur;
  int curW = 0;
  size_t breakPos = std::string::npos;  // byte offset in `cur` after a space
  size_t i = 0;

  while (i < utf8.size()) {
    const size_t start = i;
    uint32_t cp = 0;
    if (utf8Decode(utf8, i, cp) == 0) break;
    if (cp == '\r') continue;
    if (cp == '\n') {
      lines.push_back(cur);
      cur.clear();
      curW = 0;
      breakPos = std::string::npos;
      continue;
    }

    const Glyph* g = font.glyph(cp);
    const int adv = g ? int(g->advance + 0.5f) : 0;

    if (curW + adv > maxW && !cur.empty()) {
      if (cp != ' ' && breakPos != std::string::npos && breakPos > 0 &&
          breakPos < cur.size()) {
        // Prefer breaking at the last space so Latin words stay intact.
        std::string rest = cur.substr(breakPos);
        std::string head = cur.substr(0, breakPos);
        lines.push_back(head);
        cur = rest;
        curW = measureText(font, cur);
        breakPos = std::string::npos;
      } else {
        lines.push_back(cur);
        cur.clear();
        curW = 0;
        breakPos = std::string::npos;
      }
      if (cp == ' ') continue;
    }

    cur.append(utf8, start, i - start);
    curW += adv;
    if (cp == ' ') breakPos = cur.size();
  }

  if (!cur.empty()) lines.push_back(cur);
  if (lines.empty()) lines.push_back(std::string());
  return lines;
}

}  // namespace spn