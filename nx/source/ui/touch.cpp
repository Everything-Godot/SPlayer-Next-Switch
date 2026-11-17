// SPDX-License-Identifier: AGPL-3.0
// Touch hit-testing. See touch.hpp for why this lives apart from the renderer.
#include "spn/touch.hpp"

#include <algorithm>

#include "spn/online_service.hpp"
#include "spn/ui.hpp"

namespace spn {
namespace touch {
namespace {

// --- sidebar geometry (mirrors drawSidebar()) -------------------------------
// drawSidebar lays out: 22 pad -> 34 logo -> 22 gap -> 40 search -> 14 gap ->
// then 12 nav rows of 42 px, with the row body inset by 10 px horizontally.
constexpr int kSidebarPad = 22;
constexpr int kLogoH = 34;
constexpr int kSearchH = 40;
constexpr int kNavRowH = kSidebarRowH;

constexpr int kNavTop = kSidebarPad + kLogoH + kSidebarPad + kSearchH + 14;

// The 12 nav entries, in drawSidebar() order (sidebarNav.ts).
constexpr Page kNavOrder[] = {
    Page::Home,   Page::Library,   Page::Artists,  Page::Albums,
    Page::Folders, Page::Stats,    Page::Liked,    Page::Favorites,
    Page::Cloud,  Page::Downloads, Page::Streaming, Page::History,
};
constexpr int kNavCount = int(sizeof(kNavOrder) / sizeof(kNavOrder[0]));
// --- player bar geometry (mirrors drawPlayerBar()) --------------------------
// The transport cluster is laid out at fixed offsets from the bar's left edge;
// these are the same numbers the icon rects use. The bar starts at the sidebar
// edge (main.cpp passes Rect(content.x, ...)), so every offset below is
// relative to that x, not to the screen.
constexpr int kBarPad = 28;
constexpr int kPrevX = 438;
constexpr int kPlayX = 474;
constexpr int kNextX = 532;
constexpr int kShuffleX = 400;
constexpr int kRepeatX = 570;


// Extra slop so a fingertip near an icon still hits it. Touch targets on a
// 1280x720 screen are small; 8 px of margin makes the cluster usable without
// making neighbouring buttons overlap (they are >= 38 px apart).
constexpr int kTouchSlop = 8;

bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

// The row geometry a page published when it last drew. Every page sets `rows`
// before painting its list, so this is the pixels the player is actually
// looking at -- no mirrored constants, no per-page guesses.
bool rowGeometry(const App& a, App::RowGeometry* out) {
  if (!a.rows.valid || a.rows.rowH <= 0 || a.rows.count <= 0) return false;
  *out = a.rows;
  return true;
}

}  // namespace

int bodyRowAt(const App& a, int x, int y, int w, int h) {
  (void)w;
  (void)h;
  App::RowGeometry rows;
  if (!rowGeometry(a, &rows)) return -1;
  if (x < rows.left || x >= rows.left + rows.width) return -1;

  const int rel = y - rows.top;
  if (rel < 0) return -1;
  const int row = rel / rows.rowH;
  const int visible = rows.visible > 0 ? rows.visible : rows.count;
  if (row < 0 || row >= visible) return -1;

  int col = 0;
  if (rows.cols > 1 && rows.colW > 0) {
    col = (x - rows.left) / rows.colW;
    if (col < 0 || col >= rows.cols) return -1;
  }

  // Rows are drawn starting at rows.scroll, so the item under the finger is
  // offset by it. That is what keeps scrolling from breaking tapping.
  const int idx = rows.scroll + row * rows.cols + col;
  if (idx < 0 || idx >= rows.count) return -1;
  return idx;
}

Hit hitTest(const App& a, int x, int y, int w, int h) {
  Hit out;

  // --- sidebar --------------------------------------------------------------
  const int sideW = uiSidebarWidth();
  if (x < sideW) {
    // Search box.
    const int searchY = kSidebarPad + kLogoH + kSidebarPad;
    if (inRect(x, y, 16, searchY, sideW - 32, kSearchH)) {
      out.action = Action::Search;
      return out;
    }

    // Nav rows. The drawn row is inset 10 px each side of the sidebar.
    const int clickableLeft = 10;
    const int clickableW = sideW - 20;
    if (x >= clickableLeft && x < clickableLeft + clickableW) {
      const int rel = y - kNavTop;
      if (rel >= 0) {
        const int idx = rel / kNavRowH;
        if (idx >= 0 && idx < kNavCount) {
          out.action = Action::Page;
          out.page = kNavOrder[idx];
          return out;
        }
      }
    }
    return out;  // taps on the brand block / stats card do nothing
  }

  // --- player bar -----------------------------------------------------------
  const int barTop = h - uiPlayerBarHeight();
  if (y >= barTop) {
    const int midY = barTop + uiPlayerBarHeight() / 2;
    // The bar is drawn inside the content column, so its origin is the sidebar
    // edge. Every offset below is measured from there, exactly like the draw.
    const int ox = uiSidebarWidth();

    // Physical button order, left to right: shuffle, prev, play, next, repeat.
    if (inRect(x, y, ox + kShuffleX - kTouchSlop, midY - 22,
               22 + kTouchSlop * 2, 44)) {
      return out;  // shuffle toggle is deliberately not touch-driven yet
    }
    if (inRect(x, y, ox + kPrevX - kTouchSlop, midY - 22,
               26 + kTouchSlop * 2, 44)) {
      out.action = Action::Prev;
      return out;
    }
    if (inRect(x, y, ox + kPlayX - kTouchSlop, midY - 30,
               48 + kTouchSlop * 2, 60)) {
      out.action = Action::PlayerBar;
      return out;
    }
    if (inRect(x, y, ox + kNextX - kTouchSlop, midY - 22,
               26 + kTouchSlop * 2, 44)) {
      out.action = Action::Next;
      return out;
    }
    if (inRect(x, y, ox + kRepeatX - kTouchSlop, midY - 22,
               22 + kTouchSlop * 2, 44)) {
      return out;
    }

    // Cover art / titles open the full player.
    if (inRect(x, y, ox + kBarPad, midY - 30, 320, 60)) {
      out.action = Action::PlayerPage;
      return out;
    }
    return out;
  }

  // --- top bar --------------------------------------------------------------
  const int topH = uiTopBarHeight();
  if (y < topH) {
    // The back chip exists only on the player page. drawTopBar places it at
    // area.x + kContentPad, and area.x is the sidebar edge here too.
    if (a.page == Page::Player) {
      const int pad = uiContentPad();
      if (inRect(x, y, uiSidebarWidth() + pad, 22, 32, 32)) {
        out.action = Action::Back;
        return out;
      }
    }
    return out;
  }

  // --- page body ------------------------------------------------------------
  const int row = bodyRowAt(a, x, y, w, h);
  if (row >= 0) {
    out.action = Action::BodyActivate;
    out.index = row;
    return out;
  }

  return out;
}

}  // namespace touch
}  // namespace spn