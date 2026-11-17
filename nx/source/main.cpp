// SPDX-License-Identifier: AGPL-3.0
// SPlayer-Next — Nintendo Switch port. Application entry point.
#include <switch.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "spn/audio.hpp"
#include "spn/canvas.hpp"
#include "spn/cover.hpp"
#include "spn/font.hpp"
#include "spn/icons.hpp"
#include "spn/library.hpp"
#include "spn/lyrics.hpp"
#include "spn/netease.hpp"
#include "spn/online_service.hpp"
#include "spn/store.hpp"
#include "spn/types.hpp"
#include "spn/ui.hpp"
#include "spn/widgets.hpp"

using namespace spn;

namespace {

App g;

// The online layer lives in main() rather than inside App: App is a header-only
// struct used by the host preview tool, and pulling sockets into it there would
// break that build. main() hands it over as a borrowed pointer.
online::Service gOnline;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Current selection index for the page in focus, so navigation code stays one
// switch instead of a pile of ifs.
int* selectionFor(App& a) {
  switch (a.page) {
    case Page::Home: return &a.homeSel;
    case Page::Library: return &a.libSel;
    case Page::Artists: return &a.artistSel;
    case Page::Albums: return &a.albumSel;
    case Page::Liked: return &a.likedSel;
    case Page::History: return &a.histSel;
    case Page::Downloads: return &a.libSel;
    case Page::Favorites: return &a.folderSel;
    case Page::Folders: return &a.folderSel;
    case Page::Cloud: return &a.cloudSel;
    case Page::Streaming: return &a.onlineSel;
    case Page::Search: return &a.searchSel;
    case Page::Settings: return &a.settingSel;
    default: return nullptr;
  }
}

// How many selectable entries the current page exposes.
int itemCountFor(App& a) {
  switch (a.page) {
    case Page::Home: {
      const int hist = int(a.store.history().size());
      return std::max(1, hist);
    }
    case Page::Library:
      return int(a.library.tracks().size());
    case Page::Artists:
      return int(a.library.artists().size());
    case Page::Albums:
      return int(a.library.albums().size());
    case Page::Liked: {
      int n = 0;
      for (const std::string& p : a.store.likedPaths()) {
        if (a.library.indexOf(p) >= 0) ++n;
      }
      return n;
    }
    case Page::History: return int(a.store.history().size());
    case Page::Downloads: return int(a.library.tracks().size());
    case Page::Favorites: return int(a.store.playlists().size());
    case Page::Folders: return int(a.library.folders().size());
    case Page::Stats: return 0;
    case Page::Settings: return settingsRowCount();
    case Page::Search: return int(searchResults(a).size());
    default: return 0;
  }
}

// Track that "A" should act on for the current page/selection.
int selectedTrackIndex(App& a) {
  switch (a.page) {
    case Page::Home: {
      const auto& hist = a.store.history();
      if (hist.empty()) return -1;
      const int i = std::clamp(a.homeSel, 0, int(hist.size()) - 1);
      return a.library.indexOf(hist[size_t(i)].path);
    }
    case Page::Library: return a.libSel;
    case Page::Artists: {
      const auto& artists = a.library.artists();
      if (artists.empty()) return -1;
      const int i = std::clamp(a.artistSel, 0, int(artists.size()) - 1);
      const ArtistEntry& e = artists[size_t(i)];
      return e.trackIndices.empty() ? -1 : e.trackIndices.front();
    }
    case Page::Albums: {
      const auto& albums = a.library.albums();
      if (albums.empty()) return -1;
      const int i = std::clamp(a.albumSel, 0, int(albums.size()) - 1);
      const Album& al = albums[size_t(i)];
      return al.trackIndices.empty() ? -1 : al.trackIndices.front();
    }
    case Page::Liked: {
      std::vector<int> idx;
      for (const std::string& p : a.store.likedPaths()) {
        const int k = a.library.indexOf(p);
        if (k >= 0) idx.push_back(k);
      }
      if (idx.empty()) return -1;
      return idx[size_t(std::clamp(a.likedSel, 0, int(idx.size()) - 1))];
    }
    case Page::History: {
      const auto& hist = a.store.history();
      if (hist.empty()) return -1;
      const int i = std::clamp(a.histSel, 0, int(hist.size()) - 1);
      return a.library.indexOf(hist[size_t(i)].path);
    }
    case Page::Downloads: return a.libSel;
    case Page::Search: {
      const std::vector<int> hits = searchResults(a);
      if (hits.empty()) return -1;
      const int i = std::clamp(a.searchSel, 0, int(hits.size()) - 1);
      return hits[size_t(i)];
    }
    case Page::Folders: return -1;
    default: return -1;
  }
}

// Cycles page focus left/right across the sidebar order.
void cyclePage(App& a, int dir) {
  // Same 12 entries as the sidebar, plus Settings at the tail because the port
  // has no nav-user menu to hide it behind.
  static const Page kOrder[] = {
      Page::Home,      Page::Library,   Page::Artists,  Page::Albums,
      Page::Folders,   Page::Stats,     Page::Liked,    Page::Favorites,
      Page::Cloud,     Page::Downloads, Page::Streaming, Page::History,
      Page::Settings};
  constexpr int n = int(sizeof(kOrder) / sizeof(kOrder[0]));
  int cur = 0;
  for (int i = 0; i < n; ++i) {
    if (kOrder[i] == a.page) {
      cur = i;
      break;
    }
  }
  cur = (cur + dir + n) % n;
  a.goTo(kOrder[cur]);
}

// Whether the page consumes vertical navigation as a list.
bool pageIsList(Page p) {
  switch (p) {
    case Page::Stats: return false;
    case Page::Player: return false;
    case Page::Favorites: return false;
    default: return true;
  }
}

// Runs one frame of the online layer: reaps a finished job, publishes it, keeps
// the QR poll ticking at the desktop's 1500 ms cadence, and hands a resolved
// track to the audio engine.
void pumpOnline() {
  gOnline.poll();
  g.on = &gOnline;

  // QR polling, matching QrLoginPanel.vue's 1500 ms interval.
  static double lastPoll = 0.0;
  const online::LoginState ls = gOnline.loginState();
  if (ls == online::LoginState::WaitingQr || ls == online::LoginState::Scanned) {
    if (g.now - lastPoll >= 1.5) {
      gOnline.pollLogin();
      lastPoll = g.now;
    }
  } else {
    lastPoll = g.now;
  }

  // A finished download becomes playback. This is the only place the online
  // path touches the audio engine, so local playback stays untouched.
  std::string ready = gOnline.takeReadyPath();
  if (!ready.empty()) {
    g.playPath(ready);
    const std::string lrc = gOnline.takeLyrics();
    if (!lrc.empty()) {
      g.lyrics.parse(lrc);
      g.toast("正在播放：" + std::string("在线曲目"));
    }
    g.dirty = true;
  } else {
    // Lyrics without a path happen when the track was already cached; drain
    // them so they do not leak into the next track.
    gOnline.takeLyrics();
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void render() {
  if (!g.c) return;
  Canvas& c = *g.c;
  g.th = &themeAt(g.store.settings().themeIndex);

  const int W = c.width();
  const int H = c.height();
  const int sideW = uiSidebarWidth();
  const int topH = uiTopBarHeight();
  const int barH = uiPlayerBarHeight();

  // Backdrop.
  c.verticalGradient(Rect(0, 0, W, H), g.th->bgTop, g.th->bgBottom);

  const Rect side(0, 0, sideW, H);
  drawSidebar(g, side);

  const Rect content(sideW, 0, W - sideW, H);
  const Rect top(content.x, content.y, content.w, topH);
  drawTopBar(g, top);

  const int bodyTop = top.bottom();
  const int bodyBottom = H - barH;
  const Rect body(content.x, bodyTop, content.w, bodyBottom - bodyTop);

  switch (g.page) {
    case Page::Home: drawHome(g, body); break;
    case Page::Library: drawLibrary(g, body); break;
    case Page::Artists: drawArtists(g, body); break;
    case Page::Albums: drawAlbums(g, body); break;
    case Page::Liked: drawLiked(g, body); break;
    case Page::History: drawHistory(g, body); break;
    case Page::Downloads: drawDownloads(g, body); break;
    case Page::Favorites: drawFavorites(g, body); break;
    case Page::Folders: drawFolders(g, body); break;
    case Page::Stats: drawStats(g, body); break;
    case Page::Cloud: drawCloud(g, body); break;
    case Page::Streaming: drawStreaming(g, body); break;
    case Page::Search: drawSearch(g, body); break;
    case Page::Settings: drawSettings(g, body); break;
    case Page::Player: drawPlayer(g, body); break;
    default: break;
  }

  const Rect bar(content.x, bodyBottom, content.w, barH);
  drawPlayerBar(g, bar);

  g.dirty = false;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
void onTrackEnd(void*) {
  // Honour the repeat mode: 2 == repeat one, 1 == repeat all.
  if (g.repeatMode == 2) {
    g.playIndex(g.playingIndex);
    return;
  }
  const int n = int(g.library.tracks().size());
  if (n == 0) return;
  if (g.playingIndex + 1 >= n && g.repeatMode != 1) {
    // End of the queue without repeat: stop cleanly.
    g.audio.stop();
    g.toast("播放列表结束");
    return;
  }
  g.nextTrack(true);
}

void handleInput(u64 kDown, PadState& pad) {
  auto pressed = [&](uint64_t mask) { return (kDown & mask) != 0; };

  // --- global transport ----------------------------------------------------
  if (pressed(HidNpadButton_A)) {
    if (g.page == Page::Settings) {
      activateSetting(g, g.settingSel);
    } else if (g.page == Page::Cloud) {
      // Cloud: logged out -> start the QR flow; logged in -> open the selected
      // playlist (or load the list when it is still empty).
      if (!g.on) {
        // Online layer missing: nothing to do.
      } else if (!g.on->loggedIn()) {
        if (g.on->loginState() == online::LoginState::LoggedOut &&
            g.on->loginUrl().empty()) {
          g.on->beginLogin();
          g.toast("正在获取登录二维码");
        }
      } else if (g.on->playlists().empty()) {
        g.on->loadPlaylists();
        g.toast("正在读取歌单");
      } else {
        g.on->openPlaylist(g.cloudSel);
        g.onlineTitle = g.on->playlists()[size_t(g.cloudSel)].name;
        g.onlineSel = 0;
        g.onlineScroll = 0;
        g.toast("正在打开歌单");
      }
    } else if (g.page == Page::Streaming) {
      // Streaming: A plays the highlighted search result.
      if (g.on && g.on->loggedIn() && g.onlineSel >= 0 &&
          g.onlineSel < int(g.on->songs().size())) {
        if (g.on->playSong(g.onlineSel)) {
          g.toast("正在解析音源");
        } else {
          g.toast(g.on->error().empty() ? "上一项任务还没结束" : g.on->error());
        }
      } else if (g.on && !g.on->loggedIn()) {
        g.on->beginLogin();
        g.toast("正在获取登录二维码");
      }
    } else if (g.page == Page::Stats) {
      g.goTo(Page::Player);
    } else if (g.page == Page::Player) {
      // The player screen has no list; A is the play/pause toggle there.
      g.togglePlayPause();
    } else {
      const int idx = selectedTrackIndex(g);
      if (idx >= 0) {
        if (idx == g.playingIndex && g.audio.isLoaded()) {
          g.togglePlayPause();
        } else {
          g.playIndex(idx);
        }
      }
    }
    g.dirty = true;
  }

  if (pressed(HidNpadButton_B)) {
    if (g.page == Page::Player) {
      g.goTo(g.lastPage);
    } else if (g.page == Page::Search) {
      g.page = Page::Library;
    }
    g.dirty = true;
  }

  if (pressed(HidNpadButton_X)) g.nextTrack(true);
  if (pressed(HidNpadButton_Y)) {
    // Y toggles the like state of the highlighted track.
    const int idx = selectedTrackIndex(g);
    const Track* t = g.trackAt(idx);
    if (t) {
      g.store.toggleLiked(t->path);
      g.toast(g.store.isLiked(t->path) ? "已加入我喜欢的音乐" : "已取消收藏");
      g.dirty = true;
    }
  }

  if (pressed(HidNpadButton_L)) {
    g.audio.seek(std::max(0.0, g.audio.positionSec() - 10.0));
    g.dirty = true;
  }
  if (pressed(HidNpadButton_R)) {
    g.audio.seek(g.audio.positionSec() + 10.0);
    g.dirty = true;
  }

  if (pressed(HidNpadButton_ZL)) {
    g.repeatMode = (g.repeatMode + 1) % 3;
    g.store.settings().repeatMode = g.repeatMode;
    g.audio.setLooping(g.repeatMode == 2);
    g.store.saveSettings();
    static const char* kNames[3] = {"循环关闭", "列表循环", "单曲循环"};
    g.toast(kNames[g.repeatMode]);
    g.dirty = true;
  }

  if (pressed(HidNpadButton_ZR)) {
    g.library.scan();
    g.libSel = 0;
    g.libScroll = 0;
    g.toast("已扫描 " + std::to_string(g.library.tracks().size()) + " 首曲目");
    g.dirty = true;
  }

  // --- page switching ------------------------------------------------------
  if (pressed(HidNpadButton_Up) && g.page == Page::Player) g.goTo(g.lastPage);

  // Left stick flicks switch pages, matching the desktop sidebar order.
  if (pressed(HidNpadButton_StickLLeft)) cyclePage(g, -1);
  if (pressed(HidNpadButton_StickLRight)) cyclePage(g, 1);

  // --- list navigation -----------------------------------------------------
  const bool vertical = pressed(HidNpadButton_Up) ||
                        pressed(HidNpadButton_Down) ||
                        pressed(HidNpadButton_StickLUp) ||
                        pressed(HidNpadButton_StickLDown);

  if (vertical && pageIsList(g.page)) {
    int* sel = selectionFor(g);
    const int count = itemCountFor(g);
    if (sel && count > 0) {
      const bool down = pressed(HidNpadButton_Down) ||
                        pressed(HidNpadButton_StickLDown);
      const int step = (pressed(HidNpadButton_StickLDown) ||
                        pressed(HidNpadButton_StickLUp))
                           ? 5
                           : 1;
      *sel = std::clamp(*sel + (down ? step : -step), 0, count - 1);
      g.dirty = true;
    }
  }

  // Horizontal: library tabs / folders / volume in settings.
  if (pressed(HidNpadButton_Right) || pressed(HidNpadButton_Left)) {
    const bool right = pressed(HidNpadButton_Right);
    if (g.page == Page::Settings) {
      adjustSetting(g, g.settingSel, right ? 1 : -1);
    } else if (g.page == Page::Folders) {
      const int n = int(g.library.folders().size());
      if (n > 0) {
        g.folderSel = std::clamp(g.folderSel + (right ? 1 : -1), 0, n - 1);
        g.dirty = true;
      }
    }
  }

  // --- search text entry ---------------------------------------------------
  if (g.page == Page::Streaming) {
    // Online search: Plus opens the keyboard and fires the request. Minus
    // re-syncs the playlist list when logged in.
    if (pressed(HidNpadButton_Plus)) {
      SwkbdConfig kbd;
      if (R_SUCCEEDED(swkbdCreate(&kbd, 0))) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetGuideText(&kbd, "搜索网易云音乐");
        char buf[128] = {0};
        if (!gOnline.query().empty()) {
          std::snprintf(buf, sizeof(buf), "%s", gOnline.query().c_str());
        }
        const Result rc = swkbdShow(&kbd, buf, sizeof(buf));
        swkbdClose(&kbd);
        if (R_SUCCEEDED(rc) && buf[0] != '\0') {
          gOnline.search(buf);
          g.onlineTitle = std::string("搜索结果：") + buf;
          g.onlineSel = 0;
          g.onlineScroll = 0;
          g.toast(std::string("搜索 ") + buf);
          g.dirty = true;
        }
      }
    }
  }

  if (g.page == Page::Cloud && pressed(HidNpadButton_Minus)) {
    if (g.on && g.on->loggedIn()) {
      g.on->loadPlaylists();
      g.toast("正在刷新歌单");
      g.dirty = true;
    }
  }

  if (g.page == Page::Search) {
    if (pressed(HidNpadButton_Plus)) {
      // Plus opens the system keyboard; keep the query bounded.
      SwkbdConfig kbd;
      if (R_SUCCEEDED(swkbdCreate(&kbd, 0))) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetGuideText(&kbd, "输入搜索关键字");
        char buf[128] = {0};
        if (!g.searchQuery.empty()) {
          std::snprintf(buf, sizeof(buf), "%s", g.searchQuery.c_str());
        }
        const Result rc = swkbdShow(&kbd, buf, sizeof(buf));
        swkbdClose(&kbd);
        if (R_SUCCEEDED(rc)) {
          g.searchQuery = buf;
          g.searchSel = 0;
          g.dirty = true;
        }
      }
    }
    if (pressed(HidNpadButton_Minus)) {
      g.searchQuery.clear();
      g.searchSel = 0;
      g.dirty = true;
    }
  }

  if (g.page == Page::History && pressed(HidNpadButton_Minus)) {
    g.store.clearHistory();
    g.histSel = 0;
    g.toast("播放历史已清空");
    g.dirty = true;
  }

  // --- left stick: volume --------------------------------------------------
  const HidAnalogStickState stick = padGetStickPos(&pad, 0);
  if (std::abs(stick.x) < 12000 && (stick.y < -16000 || stick.y > 16000)) {
    static int accum = 0;
    static u64 lastTick = 0;
    const u64 nowTick = armGetSystemTick();
    accum += stick.y;
    if (std::abs(accum) > 26000 &&
        (lastTick == 0 || nowTick - lastTick > armGetSystemTickFreq() / 12)) {
      const float delta = stick.y < 0 ? 0.05f : -0.05f;
      Settings& s = g.store.settings();
      s.volume = std::clamp(g.audio.volume() + delta, 0.0f, 1.0f);
      g.audio.setVolume(s.volume);
      g.store.saveSettings();
      g.toast("音量 " + std::to_string(int(s.volume * 100)) + "%");
      accum = 0;
      lastTick = nowTick;
      g.dirty = true;
    }
  }
}

// ---------------------------------------------------------------------------
// Screen wrapper
// ---------------------------------------------------------------------------

// NOTE: libnx already declares a `Framebuffer` typedef, so this wrapper must
// not reuse that name.
//
// The Tegra framebuffer is block-linear by default; framebufferMakeLinear()
// installs a shadow buffer in plain row-major order which framebufferBegin()
// then hands back. That is what lets us treat it as a simple u32 canvas.
struct Screen {
  Screen() = default;
  ~Screen() { release(); }

  bool init() {
    if (R_FAILED(framebufferCreate(&fb_, nwindowGetDefault(), 1280, 720,
                                   PIXEL_FORMAT_RGBA_8888, 2))) {
      return false;
    }
    if (R_FAILED(framebufferMakeLinear(&fb_))) {
      framebufferClose(&fb_);
      return false;
    }
    created_ = true;
    return true;
  }

  uint32_t* begin() {
    void* buf = framebufferBegin(&fb_, &stride_);
    return reinterpret_cast<uint32_t*>(buf);
  }

  void end() { framebufferEnd(&fb_); }

  void release() {
    if (created_) {
      framebufferClose(&fb_);
      fb_ = ::Framebuffer{};
      created_ = false;
    }
  }

  ::Framebuffer fb_{};
  u32 stride_ = 0;
  bool created_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  // Console is only used for the pre-init error path; it is shut down before
  // the framebuffer takes over the display.
  consoleInit(nullptr);
  printf("SPlayer-Next (nx)\n");
  if (R_FAILED(romfsInit())) {
    printf("romfsInit failed\n");
    consoleExit(nullptr);
    return 1;
  }

  // Networking for the online (NetEase) side. Failure is non-fatal: the local
  // player must still work on a console with no network configured.
  const bool netOk = R_SUCCEEDED(socketInitializeDefault());
  if (!netOk) {
    printf("socketInitializeDefault failed (online features disabled)\n");
  }


  Font fTiny, fSmall, fBody, fTitle, fHuge;
  const char* fontPath = "romfs:/fonts/wqy-microhei.ttc";
  const bool fontOk = fTiny.loadFromFile(fontPath, 15.f) &&
                      fSmall.loadFromFile(fontPath, 19.f) &&
                      fBody.loadFromFile(fontPath, 23.f) &&
                      fTitle.loadFromFile(fontPath, 31.f) &&
                      fHuge.loadFromFile(fontPath, 74.f);
  if (!fontOk) {
    printf("failed to load font: %s\n", fontPath);
    romfsExit();
    consoleExit(nullptr);
    return 1;
  }

  g.fTiny = &fTiny;
  g.fSmall = &fSmall;
  g.fBody = &fBody;
  g.fTitle = &fTitle;
  g.fHuge = &fHuge;

  // --- persisted state -----------------------------------------------------
  g.store.load();
  g.th = &themeAt(g.store.settings().themeIndex);
  g.shuffle = g.store.settings().shuffle;
  g.repeatMode = g.store.settings().repeatMode;

  // --- audio ---------------------------------------------------------------
  if (!g.audio.init()) {
    printf("audout init failed\n");
  }
  g.audio.setVolume(g.store.settings().volume);
  g.audio.setLooping(g.repeatMode == 2);
  g.audio.setOnTrackEnd(&onTrackEnd, nullptr);

  // --- library -------------------------------------------------------------
  printf("scanning library...\n");
  const size_t found = g.library.scan();
  printf("found %zu tracks under %s\n", found, g.library.root().c_str());
  if (!g.library.lastError().empty()) {
    printf("%s\n", g.library.lastError().c_str());
  }

  // Restore the last track so the player bar is not empty on boot.
  if (!g.store.lastTrack().empty()) {
    const int idx = g.library.indexOf(g.store.lastTrack());
    if (idx >= 0) {
      g.playingIndex = idx;
      const std::string lrc = Library::findLyricFile(g.store.lastTrack());
      if (!lrc.empty()) g.lyrics.loadFile(lrc);
    }
  }

  // --- online -------------------------------------------------------------
  // Only start the layer when the socket service came up; otherwise the worker
  // would fail every request and the QR panel would look broken.
  if (netOk) {
    gOnline.init();
    g.on = &gOnline;
    printf("online: %s\n", gOnline.loggedIn() ? "session restored" : "not logged in");
  }

  printf("starting UI...\n");
  consoleExit(nullptr);

  Screen fbuf;
  if (!fbuf.init()) {
    g.store.saveSettings();
    g.audio.shutdown();
    romfsExit();
    return 1;
  }

  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);

  // Fixed-timestep-ish frame pacing: the UI redraws whenever state is dirty,
  // plus a 4 Hz heartbeat so progress bars and lyric scroll stay smooth.
  u64 lastFrameTick = 0;
  u64 lastHeartbeat = 0;
  double listeningAccum = 0.0;

  while (appletMainLoop()) {
    padUpdate(&pad);
    const u64 kDown = padGetButtonsDown(&pad);
    const u64 nowTick = armGetSystemTick();
    const double tickFreq = double(armGetSystemTickFreq());
    g.now = double(nowTick) / tickFreq;

    if ((kDown & HidNpadButton_Plus) != 0 && g.page != Page::Search) break;

    handleInput(kDown, pad);
    pumpOnline();

    // Accumulate listening time while audio actually advances.
    if (g.audio.isPlaying()) {
      if (lastFrameTick != 0) {
        listeningAccum += double(nowTick - lastFrameTick) / tickFreq;
      }
    }
    lastFrameTick = nowTick;

    if (listeningAccum >= 30.0) {
      g.store.addListeningSeconds(uint64_t(listeningAccum));
      listeningAccum = 0.0;
    }

    // Transport state changes every frame; keep the chrome honest.
    const bool heartbeat =
        lastHeartbeat == 0 || nowTick - lastHeartbeat > u64(tickFreq / 4.0);
    if (heartbeat) lastHeartbeat = nowTick;

    if (!g.dirty && !heartbeat) {
      svcSleepThread(4'000'000LL);  // 4 ms
      continue;
    }

    uint32_t* pixels = fbuf.begin();
    if (!pixels) continue;

    Canvas canvas(pixels, 1280, 720, int(fbuf.stride_ / 4));
    g.c = &canvas;
    render();
    g.c = nullptr;

    fbuf.end();
  }

  // --- shutdown ------------------------------------------------------------
  // Join the online worker before tearing the socket service down, otherwise a
  // download in flight would call into a closed socket.
  gOnline.shutdown();
  g.store.addListeningSeconds(uint64_t(listeningAccum));
  g.store.save();
  g.audio.shutdown();
  fbuf.release();
  if (netOk) socketExit();
  romfsExit();
  return 0;
}