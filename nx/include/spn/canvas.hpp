// SPDX-License-Identifier: AGPL-3.0
// Software canvas: draws onto the Switch linear framebuffer.
#pragma once

#include <string>

#include "spn/types.hpp"

namespace spn {

class Font;

// Thin immediate-mode drawing surface over a u32 RGBA framebuffer.
class Canvas {
 public:
  Canvas(uint32_t* pixels, int width, int height, int stridePx);

  int width() const { return width_; }
  int height() const { return height_; }

  void clear(Color c);
  void fillRect(Rect r, Color c);
  void fillRectAlpha(Rect r, Color c, uint8_t alpha);
  void fillRoundRect(Rect r, int radius, Color c);
  void fillRoundRectAlpha(Rect r, int radius, Color c, uint8_t alpha);
  void strokeRoundRect(Rect r, int radius, Color c, int thickness);
  void verticalGradient(Rect r, Color top, Color bottom);
  void blendPixel(int x, int y, Color c, uint8_t alpha);

  // Text drawing. `y` is the top of the line box.
  void drawText(const Font& font, int x, int y, const std::string& utf8, Color c);
  void drawTextEllipsis(const Font& font, Rect box, const std::string& utf8,
                        Color c);
  int measureText(const Font& font, const std::string& utf8) const;

 private:
  uint32_t* pixels_;
  int width_;
  int height_;
  int stridePx_;
};

// UTF-8 helpers shared by the text renderer.
int utf8Decode(const std::string& s, size_t& i, uint32_t& out);

}  // namespace spn