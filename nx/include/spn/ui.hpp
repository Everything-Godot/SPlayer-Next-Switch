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
  // A playlist / album / artist opened from a list. Kept as a page rather than
  // reusing Streaming, so opening a playlist cannot overwrite search results.
  Playlist,
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
  // Playlist detail page: which online playlist is open, its row cursor and
  // scroll offset. Separate from onlineSel so the two lists never share a cursor.
  bool playlistOpen = false;
  int playlistSel = 0;
  int playlistScroll = 0;
  int cloudScroll = 0;
  int streamScroll = 0;
  // Scroll offset of the playlist *list* on 我的收藏 (folderSel is its cursor).
  int playlistListScroll = 0;
  // Geometry of the list the page most recently drew, published by the
  // renderer and consumed by the touch hit-tester. Guessing these numbers in
  // touch.cpp was wrong by the header height on every page, and the values move
  // whenever a page gains a row. Recording them at draw time means a tap can
  // only ever land on a row that is actually on screen.
  struct RowGeometry {
    bool valid = false;
    int top = 0;      // y of the first row slot
    int rowH = 0;     // vertical pitch between rows
    int left = 0;     // x of the first column
    int width = 0;    // total width of the grid/list
    int count = 0;    // number of items in the underlying list
    int scroll = 0;   // first visible item; rows are drawn from here
    int visible = 0;  // rows that fit vertically
    int cols = 1;     // 1 for a list, >1 for the album grid
    int colW = 0;     // horizontal pitch between columns
  } rows;

  // Playback ordering is the library's track list.
  int playingIndex = -1;
  bool shuffle = false;
  int repeatMode = 0;  // 0 off, 1 all, 2 one

  // An online track lives in the cache directory, not in the scanned library, so
  // it cannot be addressed by `playingIndex`. When playingOnline is set this
  // synthetic Track is what the player bar and player page display instead.
  bool playingOnline = false;
  Track onlineTrack;

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
  const Track* currentTrack() const {
    if (playingOnline) return &onlineTrack;
    return trackAt(playingIndex);
  }
  void toast(const std::string& text);
  void goTo(Page p);
  void playIndex(int index);
  void playPath(const std::string& path);
  // Plays a file that may live outside the scanned library (online cache).
  void playFile(const std::string& path, const std::string& title,
                const std::string& artist, const std::string& album,
                const std::string& lyricText = std::string());
  void nextTrack(bool forward);
  void togglePlayPause();
};

// Page layout metrics and the pointer/touch hit-testing rectangle. Exposed so
// main.cpp can convert a touch point without duplicating the geometry.
Rect uiBodyRect(int screenW, int screenH);

// Layout metrics shared by main.cpp and the page renderers.
int uiSidebarWidth();
int uiTopBarHeight();
int uiPlayerBarHeight();
int uiContentPad();

// Row heights, shared by the renderers and the touch hit-tester so a tap always
// lands on the row the player actually sees.
constexpr int kSidebarRowH = 42;   // sidebar nav rows
constexpr int kTrackRowH = 58;     // library / online song rows
constexpr int kPlaylistRowH = 64;  // playlist summary rows
constexpr int kSidebarNavTop = 22 + 34 + 22 + 40 + 14;  // logo + search box

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
// Playlist contents, opened from 我的收藏. `a.playlistSel` is the row cursor.
void drawPlaylistPage(App& a, Rect body);
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