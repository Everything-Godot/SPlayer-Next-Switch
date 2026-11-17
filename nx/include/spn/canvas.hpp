// SPDX-License-Identifier: AGPL-3.0
// Software canvas: draws onto the Switch linear framebuffer.
#pragma once

#include <string>
#include <vector>

#include "spn/types.hpp"

namespace spn {

class Font;
struct Image;

// Thin immediate-mode drawing surface over a u32 RGBA framebuffer.
//
// Coordinates are in pixels, origin top-left. A clip rectangle is honoured by
// every primitive, which is what lets the UI scroll lists cleanly.
class Canvas {
 public:
  Canvas(uint32_t* pixels, int width, int height, int stridePx);

  int width() const { return width_; }
  int height() const { return height_; }

  // --- clip stack -----------------------------------------------------------
  void pushClip(Rect r);
  void popClip();
  Rect clip() const { return clip_; }

  // --- fills ---------------------------------------------------------------
  void clear(Color c);
  void fillRect(Rect r, Color c);
  void fillRectAlpha(Rect r, Color c, uint8_t alpha);
  void fillRoundRect(Rect r, int radius, Color c);
  void fillRoundRectAlpha(Rect r, int radius, Color c, uint8_t alpha);
  void fillCircle(int cx, int cy, int radius, Color c);
  void strokeRoundRect(Rect r, int radius, Color c, int thickness);
  void strokeRect(Rect r, Color c, int thickness = 1);
  void verticalGradient(Rect r, Color top, Color bottom);
  void horizontalGradient(Rect r, Color left, Color right);
  // Soft shadow-ish glow behind a rounded rect.
  void dropShadow(Rect r, int radius, int spread, uint8_t alpha);
  void blendPixel(int x, int y, Color c, uint8_t alpha);

  // --- images --------------------------------------------------------------
  // Draws `img` scaled into `dst`, clipped to `dst` and the clip rect.
  void drawImage(const Image& img, Rect dst, uint8_t alpha = 255);
  // Draws with rounded corners (radius in destination pixels).
  void drawImageRounded(const Image& img, Rect dst, int radius, uint8_t alpha = 255);

  // --- text ----------------------------------------------------------------
  void drawText(const Font& font, int x, int y, const std::string& utf8, Color c);
  void drawTextCentered(const Font& font, Rect box, const std::string& utf8, Color c);
  void drawTextRight(const Font& font, Rect box, const std::string& utf8, Color c);
  void drawTextEllipsis(const Font& font, Rect box, const std::string& utf8, Color c);
  void drawTextEllipsisCentered(const Font& font, Rect box, const std::string& utf8,
                                Color c);
  int measureText(const Font& font, const std::string& utf8) const;
  // Width of the longest line that fits in `maxW`, for word-wrapped text.
  std::vector<std::string> wrapText(const Font& font, const std::string& utf8,
                                    int maxW) const;

 private:
  uint32_t* pixels_;
  int width_;
  int height_;
  int stridePx_;
  Rect clip_;
  std::vector<Rect> clipStack_;

  inline bool visible(int x, int y) const {
    return x >= clip_.x && y >= clip_.y && x < clip_.right() && y < clip_.bottom();
  }
};

// UTF-8 helpers shared by the text renderer.
int utf8Decode(const std::string& s, size_t& i, uint32_t& out);

}  // namespace spn