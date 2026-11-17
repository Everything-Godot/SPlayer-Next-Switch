// SPDX-License-Identifier: AGPL-3.0
// Reusable widgets shared by the pages.
#pragma once

#include <string>
#include <vector>

#include "spn/canvas.hpp"
#include "spn/types.hpp"

namespace spn {

struct App;

namespace ui {

// Cover art or a note placeholder, drawn into `r`.
void cover(App& a, Rect r, int coverId, int radius);

// Small square badge (format label, counter, ...).
void badge(App& a, Rect r, const std::string& text, Color bg, Color fg);

// Section header with an optional right-aligned action label.
void sectionHeader(App& a, int x, int y, int w, const std::string& title,
                   const std::string& trailing = std::string());

// One library row: index/playing indicator, cover, title, artist, duration.
void trackRow(App& a, Rect r, int trackIndex, bool selected, bool playing,
              bool showCover, int ordinal);

// Album tile: square cover + name + artist beneath.
void albumTile(App& a, Rect r, const Album& album, bool selected);

// Rounded pill used for tabs and chips.
void pill(App& a, Rect r, const std::string& text, bool active);

// Progress bar with an optional knob.
void progressBar(App& a, Rect r, double fraction, Color fill, Color back);

// Empty state: centred icon + two lines.
void emptyState(App& a, Rect area,
                void (*icon)(Canvas&, Rect, Color),
                const std::string& title, const std::string& hint);

// Heart toggle button; returns the rect it occupies.
Rect heartButton(App& a, Rect r, bool liked);

// "--:--"-safe duration label.
std::string durationLabel(double seconds);

// Publishes the row geometry of the list a page is about to draw, so the touch
// hit-tester can resolve a tap against real pixels instead of mirroring layout
// constants. Call it once per frame, right before the loop that draws the rows.
// `scroll` is the index of the first visible item and `visible` the number of
// rows that fit; pages pass the same numbers they clamp their cursor against.
void publishRows(App& a, Rect topLeft, int rowH, int width, int count, int scroll,
                 int visible);
// Same, for a grid: `cols` columns of pitch `colW`.
void publishGrid(App& a, Rect topLeft, int rowH, int width, int count, int scroll,
                 int visibleRows, int cols, int colW);
// Clears the published geometry (used for pages with nothing tappable).
void clearRows(App& a);

}  // namespace ui
}  // namespace spn