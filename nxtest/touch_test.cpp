// SPDX-License-Identifier: AGPL-3.0
// Host test for the touch hit-tester.
//
// Touch is the one feature that cannot be judged from screenshots: a target
// that is 20 px off still *looks* right. These checks pin the geometry down by
// driving the same functions the app drives, so a coordinate regression fails
// here instead of on the console.
#include <cstdio>
#include <string>

#include "spn/online_service.hpp"
#include "spn/touch.hpp"
#include "spn/ui.hpp"
#include "spn/widgets.hpp"

using namespace spn;

static int g_fail = 0;
static int g_pass = 0;

static void check(const char* what, bool ok, const std::string& detail = {}) {
  if (ok) {
    ++g_pass;
  } else {
    ++g_fail;
    std::printf("FAIL %s  [%s]\n", what, detail.c_str());
  }
}

static constexpr int kW = 1280;
static constexpr int kH = 720;

int main() {
  App a;

  // --- sidebar --------------------------------------------------------------
  // The 12 nav rows start after: 22 pad + 34 logo + 22 gap + 40 search + 14 gap.
  const int navTop = 22 + 34 + 22 + 40 + 14;
  {
    const int sideW = uiSidebarWidth();
    const int x = sideW / 2;
    // First row centre.
    const touch::Hit home =
        touch::hitTest(a, x, navTop + kSidebarRowH / 2, kW, kH);
    check("sidebar row 0 is Home",
          home.action == touch::Action::Page && home.page == Page::Home);

    // Row 8 is 云盘, which is the one users reach for most.
    const touch::Hit cloud =
        touch::hitTest(a, x, navTop + 8 * kSidebarRowH + kSidebarRowH / 2, kW, kH);
    check("sidebar row 8 is Cloud",
          cloud.action == touch::Action::Page && cloud.page == Page::Cloud);

    // Last row must still resolve; an off-by-one here drops 播放历史.
    const touch::Hit hist =
        touch::hitTest(a, x, navTop + 11 * kSidebarRowH + kSidebarRowH / 2, kW, kH);
    check("sidebar row 11 is History",
          hist.action == touch::Action::Page && hist.page == Page::History);

    // Below the last row is dead space, not a phantom 13th entry.
    const touch::Hit below =
        touch::hitTest(a, x, navTop + 12 * kSidebarRowH + 4, kW, kH);
    check("below the nav list is not a page", below.action == touch::Action::None);

    // Search box sits above the nav list.
    const touch::Hit search = touch::hitTest(a, x, 22 + 34 + 22 + 20, kW, kH);
    check("search box opens search", search.action == touch::Action::Search);
  }

  // --- player bar -----------------------------------------------------------
  // The bar is drawn from the sidebar edge, so its buttons are NOT at the raw
  // screen offsets 438/474/532. This is the bug that made taps land 232 px off.
  {
    const int sideW = uiSidebarWidth();
    const int barTop = kH - uiPlayerBarHeight();
    const int midY = barTop + uiPlayerBarHeight() / 2;

    const touch::Hit play =
        touch::hitTest(a, sideW + 474 + 24, midY, kW, kH);
    check("play button toggles playback", play.action == touch::Action::PlayerBar);

    const touch::Hit next = touch::hitTest(a, sideW + 532 + 13, midY, kW, kH);
    check("next button advances", next.action == touch::Action::Next);

    const touch::Hit prev = touch::hitTest(a, sideW + 438 + 13, midY, kW, kH);
    check("prev button rewinds", prev.action == touch::Action::Prev);

    // The old, un-offset coordinate must NOT hit the play button any more: it
    // now falls in the cover/title area, which opens the player page.
    const touch::Hit stale = touch::hitTest(a, 474 + 24, midY, kW, kH);
    check("un-offset coordinate is not the play button",
          stale.action != touch::Action::PlayerBar);

    // Cover art opens the full player.
    const touch::Hit cover = touch::hitTest(a, sideW + 28 + 20, midY, kW, kH);
    check("cover opens the player", cover.action == touch::Action::PlayerPage);
  }

  // --- row geometry ---------------------------------------------------------
  // A page publishes what it drew; a tap must map to the item under the finger,
  // including while the list is scrolled.
  {
    const Rect body = uiBodyRect(kW, kH);
    const int rowH = 58;
    const int top = body.y + 100;
    const int left = body.x + 34;

    ui::publishRows(a, Rect(left, top, 600, rowH), rowH, 600, 40, 0, 8);
    check("row 0 at the top", touch::bodyRowAt(a, left + 10, top + 5, kW, kH) == 0);
    check("row 3 by pitch",
          touch::bodyRowAt(a, left + 10, top + 3 * rowH + 10, kW, kH) == 3);
    check("left of the rows is a miss",
          touch::bodyRowAt(a, left - 5, top + 10, kW, kH) == -1);
    check("below the last visible row is a miss",
          touch::bodyRowAt(a, left + 10, top + 8 * rowH + 10, kW, kH) == -1);

    // Scrolled by 4: the same pixel now means item 4, not item 0.
    ui::publishRows(a, Rect(left, top, 600, rowH), rowH, 600, 40, 4, 8);
    check("scroll offsets the row index",
          touch::bodyRowAt(a, left + 10, top + 5, kW, kH) == 4);

    // Past the end of the list: no phantom rows.
    ui::publishRows(a, Rect(left, top, 600, rowH), rowH, 600, 5, 0, 8);
    check("no row past the list length",
          touch::bodyRowAt(a, left + 10, top + 6 * rowH + 5, kW, kH) == -1);

    // An empty list publishes nothing, so nothing is tappable.
    ui::clearRows(a);
    check("cleared geometry has no rows",
          touch::bodyRowAt(a, left + 10, top + 5, kW, kH) == -1);
  }

  // --- grid (albums) --------------------------------------------------------
  {
    const int rowH = 200;
    const int colW = 166;
    const int left = 260;
    const int top = 160;
    ui::publishGrid(a, Rect(left, top, 600, rowH), rowH, 600, 30, 0, 3, 3, colW);

    check("grid cell 0,0", touch::bodyRowAt(a, left + 5, top + 5, kW, kH) == 0);
    check("grid cell 0,2",
          touch::bodyRowAt(a, left + 2 * colW + 5, top + 5, kW, kH) == 2);
    check("grid cell 1,0",
          touch::bodyRowAt(a, left + 5, top + rowH + 5, kW, kH) == 3);
    check("grid cell 2,1",
          touch::bodyRowAt(a, left + colW + 5, top + 2 * rowH + 5, kW, kH) == 7);
    check("grid beyond the columns is a miss",
          touch::bodyRowAt(a, left + 3 * colW + 5, top + 5, kW, kH) == -1);
  }

  // --- top bar back chip ----------------------------------------------------
  {
    a.page = Page::Player;
    const touch::Hit back =
        touch::hitTest(a, uiSidebarWidth() + uiContentPad() + 16, 38, kW, kH);
    check("back chip leaves the player", back.action == touch::Action::Back);

    // It must not respond on other pages.
    a.page = Page::Library;
    const touch::Hit none =
        touch::hitTest(a, uiSidebarWidth() + uiContentPad() + 16, 38, kW, kH);
    check("no back chip outside the player", none.action == touch::Action::None);
  }

  std::printf("touch: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
