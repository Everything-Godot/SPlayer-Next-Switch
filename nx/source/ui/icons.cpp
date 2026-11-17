// SPDX-License-Identifier: AGPL-3.0
// Hand-drawn vector icons, rasterised from primitives.
#include "spn/icons.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace spn {
namespace icons {
namespace {

// Supersampled polygon coverage keeps edges smooth without a real AA pass.
void blend(Canvas& c, int x, int y, Color col, uint8_t a) {
  if (a) c.blendPixel(x, y, col, a);
}

// Draws a thick line as a series of overlapping quads (thickness in pixels).
void thickLine(Canvas& c, float x0, float y0, float x1, float y1, float thick,
               Color col) {
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.001f) return;
  const float nx = -dy / len * thick * 0.5f;
  const float ny = dx / len * thick * 0.5f;

  const int minX = int(std::floor(std::min(x0, x1) - std::fabs(nx) - 1));
  const int maxX = int(std::ceil(std::max(x0, x1) + std::fabs(nx) + 1));
  const int minY = int(std::floor(std::min(y0, y1) - std::fabs(ny) - 1));
  const int maxY = int(std::ceil(std::max(y0, y1) + std::fabs(ny) + 1));

  // Quad corners: start +/- normal, end +/- normal.
  const float px[4] = {x0 + nx, x1 + nx, x1 - nx, x0 - nx};
  const float py[4] = {y0 + ny, y1 + ny, y1 - ny, y0 - ny};

  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      const float sx = float(x) + 0.5f;
      const float sy = float(y) + 0.5f;
      // Point-in-quad test (all cross products share a sign).
      int pos = 0, neg = 0;
      bool ok = true;
      for (int i = 0; i < 4; ++i) {
        const float ax = px[i], ay = py[i];
        const float bx = px[(i + 1) & 3], by = py[(i + 1) & 3];
        const float cross = (bx - ax) * (sy - ay) - (by - ay) * (sx - ax);
        if (cross > 0.0001f) ++pos;
        else if (cross < -0.0001f) ++neg;
        else ok = false;
      }
      if (ok && (pos == 0 || neg == 0)) blend(c, x, y, col, 255);
    }
  }
}

void arc(Canvas& c, float cx, float cy, float radius, float fromDeg, float toDeg,
         float thick, Color col) {
  const int steps = std::max(8, int(std::fabs(toDeg - fromDeg) / 6.0f));
  float px = 0.f, py = 0.f;
  for (int i = 0; i <= steps; ++i) {
    const float t = float(i) / float(steps);
    const float ang = (fromDeg + (toDeg - fromDeg) * t) * 3.14159265f / 180.f;
    const float x = cx + std::cos(ang) * radius;
    const float y = cy + std::sin(ang) * radius;
    if (i > 0) thickLine(c, px, py, x, y, thick, col);
    px = x;
    py = y;
  }
}

void roundedBox(Canvas& c, Rect r, int radius, Color col) {
  c.fillRoundRect(r, radius, col);
}

void outlineBox(Canvas& c, Rect r, int radius, float thick, Color col) {
  c.strokeRoundRect(r, radius, col, std::max(1, int(thick + 0.5f)));
}

// Common metrics: work in a square inscribed in `r`.
struct Box {
  float x, y, s;
  explicit Box(Rect r) {
    const int side = std::min(r.w, r.h);
    s = float(side);
    x = float(r.x) + (float(r.w) - s) * 0.5f;
    y = float(r.y) + (float(r.h) - s) * 0.5f;
  }
  float px(float t) const { return x + s * t; }
  float py(float t) const { return y + s * t; }
  float len(float t) const { return s * t; }
};

}  // namespace

void fillPolygon(Canvas& c, const int* xs, const int* ys, int n, Color col) {
  if (n < 3) return;
  int minY = ys[0], maxY = ys[0];
  for (int i = 1; i < n; ++i) {
    minY = std::min(minY, ys[i]);
    maxY = std::max(maxY, ys[i]);
  }
  const Rect clip = c.clip();
  minY = std::max(minY, clip.y);
  maxY = std::min(maxY, clip.bottom() - 1);

  std::vector<float> nodes;
  for (int y = minY; y <= maxY; ++y) {
    nodes.clear();
    const float sy = float(y) + 0.5f;
    for (int i = 0, j = n - 1; i < n; j = i++) {
      const float yi = float(ys[i]);
      const float yj = float(ys[j]);
      if ((yi > sy) == (yj > sy)) continue;
      const float t = (sy - yi) / (yj - yi);
      nodes.push_back(float(xs[i]) + t * float(xs[j] - xs[i]));
    }
    std::sort(nodes.begin(), nodes.end());
    for (size_t k = 0; k + 1 < nodes.size(); k += 2) {
      const int x0 = int(std::ceil(nodes[k]));
      const int x1 = int(std::floor(nodes[k + 1]));
      for (int x = x0; x <= x1; ++x) blend(c, x, y, col, 255);
    }
  }
}

void home(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float th = b.len(0.09f);
  // Roof: two strokes meeting at the apex.
  thickLine(c, b.px(0.12f), b.py(0.46f), b.px(0.5f), b.py(0.14f), th, col);
  thickLine(c, b.px(0.5f), b.py(0.14f), b.px(0.88f), b.py(0.46f), th, col);
  // Walls.
  thickLine(c, b.px(0.24f), b.py(0.45f), b.px(0.24f), b.py(0.85f), th, col);
  thickLine(c, b.px(0.76f), b.py(0.45f), b.px(0.76f), b.py(0.85f), th, col);
  thickLine(c, b.px(0.24f), b.py(0.85f), b.px(0.76f), b.py(0.85f), th, col);
  roundedBox(c, Rect(int(b.px(0.43f)), int(b.py(0.60f)), int(b.len(0.14f)),
                     int(b.len(0.25f))),
             1, col);
}

void disc(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float cx = b.px(0.5f);
  const float cy = b.py(0.5f);
  arc(c, cx, cy, b.len(0.40f), 0.f, 360.f, b.len(0.10f), col);
  arc(c, cx, cy, b.len(0.22f), 0.f, 360.f, b.len(0.05f), col);
  c.fillCircle(int(cx), int(cy), int(b.len(0.06f)), col);
}

void heart(Canvas& c, Rect r, Color col, bool filled) {
  const Box b(r);
  const float cx = b.px(0.5f);
  // Two lobes + a V body, drawn with circles and lines.
  const float lobeR = b.len(0.19f);
  const int lx = int(b.px(0.31f));
  const int rx = int(b.px(0.69f));
  const int ly = int(b.py(0.35f));
  if (filled) {
    c.fillCircle(lx, ly, int(lobeR), col);
    c.fillCircle(rx, ly, int(lobeR), col);
    const int xs[3] = {int(b.px(0.11f)), int(b.px(0.89f)), int(cx)};
    const int ys[3] = {int(b.py(0.40f)), int(b.py(0.40f)), int(b.py(0.90f))};
    fillPolygon(c, xs, ys, 3, col);
  } else {
    arc(c, float(lx), float(ly), lobeR, 150.f, 400.f, b.len(0.09f), col);
    arc(c, float(rx), float(ly), lobeR, 140.f, 390.f, b.len(0.09f), col);
    thickLine(c, b.px(0.12f), b.py(0.42f), cx, b.py(0.88f), b.len(0.09f), col);
    thickLine(c, b.px(0.88f), b.py(0.42f), cx, b.py(0.88f), b.len(0.09f), col);
  }
}

void clock(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float cx = b.px(0.5f);
  const float cy = b.py(0.5f);
  arc(c, cx, cy, b.len(0.39f), 0.f, 360.f, b.len(0.09f), col);
  thickLine(c, cx, cy, b.px(0.5f), b.py(0.26f), b.len(0.08f), col);
  thickLine(c, cx, cy, b.px(0.70f), b.py(0.56f), b.len(0.08f), col);
}

void download(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float th = b.len(0.10f);
  thickLine(c, b.px(0.5f), b.py(0.16f), b.px(0.5f), b.py(0.62f), th, col);
  thickLine(c, b.px(0.30f), b.py(0.45f), b.px(0.5f), b.py(0.65f), th, col);
  thickLine(c, b.px(0.70f), b.py(0.45f), b.px(0.5f), b.py(0.65f), th, col);
  thickLine(c, b.px(0.20f), b.py(0.84f), b.px(0.80f), b.py(0.84f), th, col);
}

void star(Canvas& c, Rect r, Color col, bool filled) {
  const Box b(r);
  const float cx = b.px(0.5f);
  const float cy = b.py(0.52f);
  const float outer = b.len(0.44f);
  const float inner = outer * 0.42f;
  int xs[10];
  int ys[10];
  for (int i = 0; i < 10; ++i) {
    const float rr = (i % 2 == 0) ? outer : inner;
    const float ang = (-90.f + 36.f * float(i)) * 3.14159265f / 180.f;
    xs[i] = int(cx + std::cos(ang) * rr);
    ys[i] = int(cy + std::sin(ang) * rr);
  }
  if (filled) {
    fillPolygon(c, xs, ys, 10, col);
  } else {
    for (int i = 0; i < 10; ++i) {
      const int j = (i + 1) % 10;
      thickLine(c, float(xs[i]), float(ys[i]), float(xs[j]), float(ys[j]),
                b.len(0.07f), col);
    }
  }
}

void folder(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float th = b.len(0.09f);
  // Tab.
  thickLine(c, b.px(0.14f), b.py(0.30f), b.px(0.44f), b.py(0.30f), th, col);
  thickLine(c, b.px(0.14f), b.py(0.30f), b.px(0.14f), b.py(0.44f), th, col);
  // Body.
  outlineBox(c, Rect(int(b.px(0.12f)), int(b.py(0.40f)), int(b.len(0.76f)),
                     int(b.len(0.44f))),
             3, th, col);
}

void chart(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const int w = int(b.len(0.13f));
  const int base = int(b.py(0.84f));
  const int heights[4] = {22, 40, 30, 52};
  for (int i = 0; i < 4; ++i) {
    const int x = int(b.px(0.14f)) + i * int(b.len(0.19f));
    const int h = int(b.len(float(heights[i]) / 100.f));
    c.fillRoundRect(Rect(x, base - h, w, h), 2, col);
  }
}

void search(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float th = b.len(0.10f);
  arc(c, b.px(0.44f), b.py(0.42f), b.len(0.28f), 0.f, 360.f, th, col);
  thickLine(c, b.px(0.64f), b.py(0.64f), b.px(0.86f), b.py(0.86f), th, col);
}

void gear(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float cx = b.px(0.5f);
  const float cy = b.py(0.5f);
  const int teeth = 8;
  for (int i = 0; i < teeth; ++i) {
    const float ang = 3.14159265f * 2.f * float(i) / float(teeth);
    const float x0 = cx + std::cos(ang) * b.len(0.18f);
    const float y0 = cy + std::sin(ang) * b.len(0.18f);
    const float x1 = cx + std::cos(ang) * b.len(0.40f);
    const float y1 = cy + std::sin(ang) * b.len(0.40f);
    thickLine(c, x0, y0, x1, y1, b.len(0.15f), col);
  }
  arc(c, cx, cy, b.len(0.30f), 0.f, 360.f, b.len(0.12f), col);
  c.fillCircle(int(cx), int(cy), int(b.len(0.10f)), col);
}

void note(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float th = b.len(0.10f);
  // Stem + flag.
  thickLine(c, b.px(0.40f), b.py(0.72f), b.px(0.40f), b.py(0.20f), th, col);
  thickLine(c, b.px(0.40f), b.py(0.20f), b.px(0.78f), b.py(0.32f), th, col);
  c.fillCircle(int(b.px(0.30f)), int(b.py(0.74f)), int(b.len(0.14f)), col);
  c.fillCircle(int(b.px(0.68f)), int(b.py(0.82f)), int(b.len(0.14f)), col);
  thickLine(c, b.px(0.78f), b.py(0.32f), b.px(0.78f), b.py(0.80f), th, col);
}

void play(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const int xs[3] = {int(b.px(0.32f)), int(b.px(0.32f)), int(b.px(0.82f))};
  const int ys[3] = {int(b.py(0.18f)), int(b.py(0.82f)), int(b.py(0.50f))};
  fillPolygon(c, xs, ys, 3, col);
}

void pause(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const int w = int(b.len(0.18f));
  c.fillRoundRect(Rect(int(b.px(0.26f)), int(b.py(0.16f)), w, int(b.len(0.68f))),
                  2, col);
  c.fillRoundRect(Rect(int(b.px(0.56f)), int(b.py(0.16f)), w, int(b.len(0.68f))),
                  2, col);
}

void next(Canvas& c, Rect r, Color col) {
  const Box b(r);
  int xs[3] = {int(b.px(0.20f)), int(b.px(0.20f)), int(b.px(0.62f))};
  int ys[3] = {int(b.py(0.20f)), int(b.py(0.80f)), int(b.py(0.50f))};
  fillPolygon(c, xs, ys, 3, col);
  c.fillRoundRect(Rect(int(b.px(0.64f)), int(b.py(0.20f)), int(b.len(0.12f)),
                       int(b.len(0.60f))),
                  2, col);
}

void prev(Canvas& c, Rect r, Color col) {
  const Box b(r);
  int xs[3] = {int(b.px(0.80f)), int(b.px(0.80f)), int(b.px(0.38f))};
  int ys[3] = {int(b.py(0.20f)), int(b.py(0.80f)), int(b.py(0.50f))};
  fillPolygon(c, xs, ys, 3, col);
  c.fillRoundRect(Rect(int(b.px(0.24f)), int(b.py(0.20f)), int(b.len(0.12f)),
                       int(b.len(0.60f))),
                  2, col);
}

void shuffle(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float th = b.len(0.09f);
  thickLine(c, b.px(0.14f), b.py(0.28f), b.px(0.52f), b.py(0.28f), th, col);
  thickLine(c, b.px(0.52f), b.py(0.28f), b.px(0.84f), b.py(0.72f), th, col);
  thickLine(c, b.px(0.14f), b.py(0.72f), b.px(0.52f), b.py(0.72f), th, col);
  thickLine(c, b.px(0.52f), b.py(0.72f), b.px(0.84f), b.py(0.28f), th, col);
  // Arrow heads.
  thickLine(c, b.px(0.72f), b.py(0.74f), b.px(0.86f), b.py(0.74f), th, col);
  thickLine(c, b.px(0.72f), b.py(0.26f), b.px(0.86f), b.py(0.26f), th, col);
}

void repeat(Canvas& c, Rect r, Color col, bool one) {
  const Box b(r);
  const float th = b.len(0.09f);
  arc(c, b.px(0.5f), b.py(0.5f), b.len(0.30f), 200.f, 560.f, th, col);
  // Arrow head at the top right.
  thickLine(c, b.px(0.66f), b.py(0.16f), b.px(0.84f), b.py(0.22f), th, col);
  thickLine(c, b.px(0.84f), b.py(0.22f), b.px(0.68f), b.py(0.30f), th, col);
  if (one) {
    c.fillRoundRect(Rect(int(b.px(0.47f)), int(b.py(0.40f)), int(b.len(0.06f)),
                         int(b.len(0.24f))),
                    1, col);
  }
}

void volume(Canvas& c, Rect r, Color col) {
  const Box b(r);
  int xs[3] = {int(b.px(0.14f)), int(b.px(0.34f)), int(b.px(0.58f))};
  int ys[3] = {int(b.py(0.38f)), int(b.py(0.38f)), int(b.py(0.14f))};
  fillPolygon(c, xs, ys, 3, col);
  int xs2[3] = {int(b.px(0.14f)), int(b.px(0.34f)), int(b.px(0.58f))};
  int ys2[3] = {int(b.py(0.62f)), int(b.py(0.62f)), int(b.py(0.86f))};
  fillPolygon(c, xs2, ys2, 3, col);
  c.fillRect(Rect(int(b.px(0.14f)), int(b.py(0.38f)), int(b.len(0.22f)),
                  int(b.len(0.24f))),
             col);
  arc(c, b.px(0.52f), b.py(0.50f), b.len(0.22f), -55.f, 55.f, b.len(0.07f), col);
  arc(c, b.px(0.52f), b.py(0.50f), b.len(0.36f), -55.f, 55.f, b.len(0.07f), col);
}

void plus(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float th = b.len(0.11f);
  thickLine(c, b.px(0.50f), b.py(0.20f), b.px(0.50f), b.py(0.80f), th, col);
  thickLine(c, b.px(0.20f), b.py(0.50f), b.px(0.80f), b.py(0.50f), th, col);
}

void check(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float th = b.len(0.12f);
  thickLine(c, b.px(0.18f), b.py(0.52f), b.px(0.42f), b.py(0.76f), th, col);
  thickLine(c, b.px(0.42f), b.py(0.76f), b.px(0.84f), b.py(0.26f), th, col);
}

void trash(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float th = b.len(0.08f);
  thickLine(c, b.px(0.18f), b.py(0.28f), b.px(0.82f), b.py(0.28f), th, col);
  thickLine(c, b.px(0.40f), b.py(0.18f), b.px(0.60f), b.py(0.18f), th, col);
  thickLine(c, b.px(0.28f), b.py(0.30f), b.px(0.34f), b.py(0.84f), th, col);
  thickLine(c, b.px(0.72f), b.py(0.30f), b.px(0.66f), b.py(0.84f), th, col);
  thickLine(c, b.px(0.34f), b.py(0.84f), b.px(0.66f), b.py(0.84f), th, col);
}

void back(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float th = b.len(0.11f);
  thickLine(c, b.px(0.60f), b.py(0.20f), b.px(0.32f), b.py(0.50f), th, col);
  thickLine(c, b.px(0.32f), b.py(0.50f), b.px(0.60f), b.py(0.80f), th, col);
}

void queue(Canvas& c, Rect r, Color col) {
  const Box b(r);
  const float th = b.len(0.09f);
  for (int i = 0; i < 3; ++i) {
    const float y = b.py(0.28f + 0.22f * float(i));
    thickLine(c, b.px(0.16f), y, b.px(0.68f), y, th, col);
  }
  c.fillCircle(int(b.px(0.80f)), int(b.py(0.72f)), int(b.len(0.10f)), col);
  thickLine(c, b.px(0.86f), b.py(0.30f), b.px(0.86f), b.py(0.70f), th, col);
}

}  // namespace icons
}  // namespace spn