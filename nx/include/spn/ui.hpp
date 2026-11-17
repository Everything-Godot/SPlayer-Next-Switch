// SPDX-License-Identifier: AGPL-3.0
// UI shell: page set, theme, and the shared application state.
//
// Page names and grouping mirror the desktop app's sidebar
// (src/components/SideBar.vue + src/router/index.ts).
#pragma once

#include <string>
#include <vector>

#include "spn/audio.hpp"
#include "spn/canvas.hpp"
#include "spn/cover.hpp"
#include "spn/font.hpp"
#include "spn/image.hpp"
#include "spn/library.hpp"
#include "spn/lyrics.hpp"
#include "spn/store.hpp"
#include "spn/types.hpp"

namespace spn {

namespace online {
class Service;
}

namespace qr {
struct Code;
}

// Page order follows the desktop sidebar exactly (src/utils/sidebarNav.ts), so
// left/right cycling and the sidebar list share one source of truth.
enum class Page {
  Home = 0,
  Library,
  Artists,
  Albums,
  Folders,
  Stats,
  Liked,
  Favorites,
  Cloud,
  Downloads,
  Streaming,
  History,
  Search,
  Settings,
  Player,
  Count
};

const char* pageTitle(Page p);
const char* pageSubtitle(Page p);

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
struct Theme {
  const char* name;
  Color bgTop;
  Color bgBottom;
  Color sidebar;
  Color sidebarActive;
  Color surface;
  Color surfaceAlt;
  Color surfaceHi;
  Color border;
  Color accent;
  Color accentSoft;
  Color text;
  Color textDim;
  Color textFaint;
  Color danger;
  Color liked;
};

const Theme& themeAt(int index);
int themeCount();

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
struct App {
  Canvas* c = nullptr;
  Font* fTiny = nullptr;
  Font* fSmall = nullptr;
  Font* fBody = nullptr;
  Font* fTitle = nullptr;
  Font* fHuge = nullptr;
  const Theme* th = nullptr;

  AudioEngine audio;
  Library library;
  Lyrics lyrics;
  Store store;

  Page page = Page::Home;
  Page lastPage = Page::Home;

  // The online layer is owned by main() and only borrowed here: keeping it a
  // pointer means this header does not pull in sockets/threads, so the host
  // layout preview still builds without linking the network stack.
  online::Service* on = nullptr;

  // Selection state per page (index into that page's visible list).
  int homeSel = 0;
  int libSel = 0;
  int libScroll = 0;
  int likedSel = 0;
  int likedScroll = 0;
  int histSel = 0;
  int histScroll = 0;
  int albumSel = 0;
  int artistSel = 0;
  int folderSel = 0;
  int searchSel = 0;
  int settingSel = 0;
  int cloudSel = 0;
  int streamSel = 0;
  int onlineSel = 0;

  // Playback ordering is the library's track list.
  int playingIndex = -1;
  bool shuffle = false;
  int repeatMode = 0;  // 0 off, 1 all, 2 one

  std::string status;      // transient toast
  double statusUntil = 0;  // seconds since boot

  std::string searchQuery;
  bool searchActive = false;

  // Header shown above the online song list: "搜索结果" or the playlist name,
  // depending on where the list came from.
  std::string onlineTitle;
  int onlineScroll = 0;

  double lyricScroll = 0.0;
  bool dirty = true;
  double now = 0.0;

  // --- helpers -------------------------------------------------------------
  const Track* trackAt(int index) const {
    if (index < 0 || index >= int(library.tracks().size())) return nullptr;
    return &library.tracks()[size_t(index)];
  }
  const Track* currentTrack() const { return trackAt(playingIndex); }
  void toast(const std::string& text);
  void goTo(Page p);
  void playIndex(int index);
  void playPath(const std::string& path);
  void nextTrack(bool forward);
  void togglePlayPause();
};

// Layout metrics shared by main.cpp and the page renderers.
int uiSidebarWidth();
int uiTopBarHeight();
int uiPlayerBarHeight();
int uiContentPad();

// Page renderers. Each returns nothing; `body` is the content area.
void drawHome(App& a, Rect body);
void drawLibrary(App& a, Rect body);
void drawArtists(App& a, Rect body);
void drawAlbums(App& a, Rect body);
void drawLiked(App& a, Rect body);
void drawHistory(App& a, Rect body);
void drawDownloads(App& a, Rect body);
void drawFavorites(App& a, Rect body);
void drawFolders(App& a, Rect body);
void drawStats(App& a, Rect body);
void drawCloud(App& a, Rect body);
void drawStreaming(App& a, Rect body);
void drawSearch(App& a, Rect body);
void drawSettings(App& a, Rect body);
void drawPlayer(App& a, Rect body);

// QR login card, drawn the way the desktop dialog draws it (white rounded
// panel, 8 px modules, status line underneath). Shared by the cloud page and
// the settings row so both entry points look identical.
void drawLoginCard(App& a, Rect area);

// Renders a QR matrix into `box` with integer module scaling and a quiet zone.
// Exposed (rather than file-static) so nxtest can rasterise the exact pixels the
// app draws and hand them to zbar: a matrix that decodes but a picture that does
// not would still be a broken login screen.
void drawQrCode(Canvas& c, const qr::Code& code, Rect box);

// Settings page rows, shared with the input layer.
int settingsRowCount();
void activateSetting(App& a, int index);
void adjustSetting(App& a, int index, int delta);

// Tracks matching the current search query (library indices). Used by both the
// search page and the input layer so a keystroke and a button press agree.
std::vector<int> searchResults(App& a);

// Chrome shared by every page.
void drawSidebar(App& a, Rect area);
void drawPlayerBar(App& a, Rect area);
void drawTopBar(App& a, Rect area);

}  // namespace spn