// SPDX-License-Identifier: AGPL-3.0
// Hand-drawn vector icons.
//
// The bundled CJK font has no icon glyphs, so every symbol in the UI is
// rasterised here from simple primitives. Each helper draws inside `r` and
// scales itself to the smaller dimension, which keeps the sidebar, list rows
// and player bar consistent at any size.
#pragma once

#include "spn/canvas.hpp"
#include "spn/types.hpp"

namespace spn {
namespace icons {

// Generic convex/concave polygon fill (even-odd scanline), used by the star.
void fillPolygon(Canvas& c, const int* xs, const int* ys, int n, Color col);

void home(Canvas& c, Rect r, Color col);
void disc(Canvas& c, Rect r, Color col);
void heart(Canvas& c, Rect r, Color col, bool filled);
void clock(Canvas& c, Rect r, Color col);
void download(Canvas& c, Rect r, Color col);
void star(Canvas& c, Rect r, Color col, bool filled);
void folder(Canvas& c, Rect r, Color col);
void chart(Canvas& c, Rect r, Color col);
void search(Canvas& c, Rect r, Color col);
void gear(Canvas& c, Rect r, Color col);
void note(Canvas& c, Rect r, Color col);
void play(Canvas& c, Rect r, Color col);
void pause(Canvas& c, Rect r, Color col);
void next(Canvas& c, Rect r, Color col);
void prev(Canvas& c, Rect r, Color col);
void shuffle(Canvas& c, Rect r, Color col);
void repeat(Canvas& c, Rect r, Color col, bool one);
void volume(Canvas& c, Rect r, Color col);
void plus(Canvas& c, Rect r, Color col);
void check(Canvas& c, Rect r, Color col);
void trash(Canvas& c, Rect r, Color col);
void back(Canvas& c, Rect r, Color col);
void queue(Canvas& c, Rect r, Color col);
// Added for the desktop-parity sidebar: /artists uses a person glyph, /cloud a
// cloud outline, /streaming a "live" broadcast glyph.
void person(Canvas& c, Rect r, Color col);
void cloud(Canvas& c, Rect r, Color col);
void broadcast(Canvas& c, Rect r, Color col);

}  // namespace icons
}  // namespace spn