// SPDX-License-Identifier: AGPL-3.0
// Touch input: hit-testing that turns a raw (x, y) into an action.
//
// The renderer is immediate-mode, so there is no retained widget tree to ask
// "what is under this point?". Instead this file mirrors the layout constants
// the pages draw with and answers that question. Keeping it separate from
// main.cpp means the hit-testing can be exercised on the host with synthetic
// coordinates, which is the only way to check a 1280x720 touch target without a
// console in hand.
#pragma once

#include "spn/types.hpp"
#include "spn/ui.hpp"

namespace spn {

struct App;

namespace touch {

// What a tap resolved to. `None` means the point hit empty space.
enum class Action {
  None,
  Page,        // switch to `page`
  BodyActivate,// select row `index` and run the page's A action
  Search,      // open the search page
  PlayerBar,   // play/pause from the bottom bar
  Next,        // next track
  Prev,        // previous track
  PlayerPage,  // open the full player page
  Back,        // leave the current sub-page
};

struct Hit {
  Action action = Action::None;
  Page page = Page::Home;
  int index = -1;
};

// Sidebar entries, in the same order drawSidebar() lays them out. `Page::Count`
// stands for the search entry, which is not a page of its own.
struct SidebarEntry {
  Page page;
  const char* label;
};

// Resolves a tap at (x, y) on a `w`x`h` screen for the current app state.
// Pure: it reads the app but never mutates it, so tests can call it freely.
Hit hitTest(const App& a, int x, int y, int w, int h);

// Number of rows the page body currently shows, using the same geometry the
// renderer uses. Returns -1 when the page has no tappable rows.
int bodyRowAt(const App& a, int x, int y, int w, int h);

}  // namespace touch
}  // namespace spn