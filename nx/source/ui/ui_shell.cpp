// SPDX-License-Identifier: AGPL-3.0
// UI shell implementation: theme table, page metadata, shared chrome.
#include "spn/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "spn/icons.hpp"

namespace spn {

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------
namespace {
constexpr int kSidebarW = 232;
constexpr int kTopBarH = 76;
constexpr int kPlayerBarH = 92;
constexpr int kContentPad = 28;

int gcdInt(int a, int b) {
  while (b != 0) {
    const int t = a % b;
    a = b;
    b = t;
  }
  return a < 0 ? -a : a;
}
}  // namespace

int uiSidebarWidth() { return kSidebarW; }
int uiTopBarHeight() { return kTopBarH; }
int uiPlayerBarHeight() { return kPlayerBarH; }
int uiContentPad() { return kContentPad; }

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------
const char* pageTitle(Page p) {
  switch (p) {
    case Page::Home: return "首页";
    case Page::Library: return "音乐库";
    case Page::Artists: return "艺术家";
    case Page::Albums: return "专辑";
    case Page::Folders: return "文件夹";
    case Page::Stats: return "统计";
    case Page::Liked: return "我喜欢的音乐";
    case Page::Favorites: return "我的收藏";
    case Page::Cloud: return "云盘";
    case Page::Downloads: return "下载";
    case Page::Streaming: return "流媒体";
    case Page::History: return "播放历史";
    case Page::Search: return "搜索";
    case Page::Settings: return "全局设置";
    case Page::Player: return "正在播放";
    default: return "";
  }
}

const char* pageSubtitle(Page p) {
  switch (p) {
    case Page::Home: return "为你精选，最近播放与收藏";
    case Page::Library: return "SD 卡上的全部曲目";
    case Page::Artists: return "按艺术家浏览曲库";
    case Page::Albums: return "按专辑浏览曲库";
    case Page::Folders: return "按目录浏览音乐";
    case Page::Stats: return "曲库与收听数据";
    case Page::Liked: return "用心标记过的旋律";
    case Page::Favorites: return "自建歌单与收藏夹";
    case Page::Cloud: return "网易云歌单与账号";
    case Page::Downloads: return "本地已缓存的音源";
    case Page::Streaming: return "在线搜索与播放";
    case Page::History: return "按时间倒序的播放记录";
    case Page::Search: return "输入关键字过滤曲目";
    case Page::Settings: return "外观、播放与按键";
    case Page::Player: return "歌词与封面";
    default: return "";
  }
}

// ---------------------------------------------------------------------------
// Themes — three palettes matching the desktop app's dark identities.
// ---------------------------------------------------------------------------
namespace {
const Theme kThemes[] = {
    {"暮色",
     Color::rgb(0x0A0D14), Color::rgb(0x141A26),
     Color::rgb(0x0D1119), Color::rgb(0x1A2333),
     Color::rgb(0x161D2A), Color::rgb(0x1E2739), Color::rgb(0x27324A),
     Color::rgb(0x243049),
     Color::rgb(0x5B9DFF), Color::rgb(0x2B4A82),
     Color::rgb(0xF1F4FA), Color::rgb(0x93A0BA), Color::rgb(0x59647D),
     Color::rgb(0xE0574F), Color::rgb(0xFF6B81)},
    {"午夜",
     Color::rgb(0x07080C), Color::rgb(0x0E1018),
     Color::rgb(0x090B11), Color::rgb(0x161A25),
     Color::rgb(0x101420), Color::rgb(0x171C2A), Color::rgb(0x212839),
     Color::rgb(0x1D2434),
     Color::rgb(0xA47BFF), Color::rgb(0x3D2E66),
     Color::rgb(0xEFEFF7), Color::rgb(0x8B93AA), Color::rgb(0x505870),
     Color::rgb(0xD65A6E), Color::rgb(0xFF7C9A)},
    {"极光",
     Color::rgb(0x061014), Color::rgb(0x0B1A20),
     Color::rgb(0x081317), Color::rgb(0x12262C),
     Color::rgb(0x0E1E24), Color::rgb(0x14282F), Color::rgb(0x1B3540),
     Color::rgb(0x183039),
     Color::rgb(0x3FE0C0), Color::rgb(0x1C5C57),
     Color::rgb(0xEAF7F5), Color::rgb(0x8AAFAE), Color::rgb(0x4E7473),
     Color::rgb(0xE06A4F), Color::rgb(0xFF8E6B)},
};
}  // namespace

const Theme& themeAt(int index) {
  const int n = int(sizeof(kThemes) / sizeof(kThemes[0]));
  if (index < 0 || index >= n) index = 0;
  return kThemes[index];
}

int themeCount() { return int(sizeof(kThemes) / sizeof(kThemes[0])); }

// ---------------------------------------------------------------------------
// App helpers
// ---------------------------------------------------------------------------
void App::toast(const std::string& text) {
  status = text;
  statusUntil = now + 2.6;
  dirty = true;
}

void App::goTo(Page p) {
  if (p == page) return;
  lastPage = page;
  page = p;
  dirty = true;
}

void App::playIndex(int index) {
  const Track* t = trackAt(index);
  if (!t) return;
  playingIndex = index;
  audio.play(t->path);
  lyrics = Lyrics();
  const std::string lrc = Library::findLyricFile(t->path);
  if (!lrc.empty()) lyrics.loadFile(lrc);
  lyricScroll = 0.0;
  store.setLastTrack(t->path);
  store.recordPlay(t->path, uint64_t(now));
  dirty = true;
}

void App::playPath(const std::string& path) {
  const int idx = library.indexOf(path);
  if (idx < 0) {
    toast("曲库里没有这首曲子");
    return;
  }
  playIndex(idx);
}

void App::togglePlayPause() {
  if (!audio.isLoaded()) {
    if (playingIndex < 0 && !library.tracks().empty()) {
      playIndex(0);
      return;
    }
    if (playingIndex >= 0) playIndex(playingIndex);
    return;
  }
  audio.togglePause();
  dirty = true;
}

void App::nextTrack(bool forward) {
  const int n = int(library.tracks().size());
  if (n == 0) return;

  if (repeatMode == 2 && !forward) {
    playIndex(playingIndex);  // restart current
    return;
  }

  int idx = playingIndex < 0 ? 0 : playingIndex;
  if (shuffle && n > 1) {
    // Deterministic-ish shuffle: step by a coprime stride so repeats are rare.
    int stride = 7;
    while (stride < n && gcdInt(stride, n) != 1) ++stride;
    if (stride >= n) stride = 1;
    idx = (idx + (forward ? stride : n - stride)) % n;
  } else {
    idx += forward ? 1 : -1;
    if (idx >= n) {
      if (repeatMode == 1) idx = 0;
      else idx = n - 1;
    }
    if (idx < 0) idx = 0;
  }
  playIndex(idx);
}

// ---------------------------------------------------------------------------
// Cover helper shared by many pages
// ---------------------------------------------------------------------------
namespace {

// Draws a cover or a placeholder tile with a music note.
void drawCover(App& a, Rect r, int coverId, int radius) {
  Canvas& c = *a.c;
  const Image* img = coverId >= 0 ? a.library.covers().get(coverId) : nullptr;
  if (img) {
    c.drawImageRounded(*img, r, radius);
    return;
  }
  c.fillRoundRect(r, radius, a.th->surfaceAlt);
  const int s = std::min(r.w, r.h) / 2;
  Rect icon(r.x + (r.w - s) / 2, r.y + (r.h - s) / 2, s, s);
  icons::note(c, icon, a.th->textFaint);
}

}  // namespace

// ---------------------------------------------------------------------------
// Sidebar
// ---------------------------------------------------------------------------
void drawSidebar(App& a, Rect area) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;

  c.fillRect(area, th.sidebar);
  c.fillRect(Rect(area.right() - 1, area.y, 1, area.h), th.border);

  struct Item {
    Page page;
    const char* label;
    void (*icon)(Canvas&, Rect, Color);
    // Mirrors SidebarNavEntry.feature: when the corresponding feature toggle is
    // off the desktop hides the entry, so the port dims and greys it instead of
    // reflowing the list (a fixed 12-slot sidebar is easier to navigate with a
    // stick than a list that shifts under the cursor).
    int feature = 0;  // 0 none, 1 download, 2 streaming
  };
  // Order is copied verbatim from src/utils/sidebarNav.ts so muscle memory and
  // screenshots line up with the desktop app.
  static const Item kItems[] = {
      {Page::Home, "首页", icons::home, 0},
      {Page::Library, "音乐库", icons::disc, 0},
      {Page::Artists, "艺术家", icons::person, 0},
      {Page::Albums, "专辑", icons::note, 0},
      {Page::Folders, "文件夹", icons::folder, 0},
      {Page::Stats, "统计", icons::chart, 0},
      {Page::Liked, "我喜欢的音乐", nullptr, 0},
      {Page::Favorites, "我的收藏", nullptr, 0},
      {Page::Cloud, "云盘", icons::cloud, 0},
      {Page::Downloads, "下载", icons::download, 1},
      {Page::Streaming, "流媒体", icons::broadcast, 2},
      {Page::History, "播放历史", icons::clock, 0},
  };
  constexpr int kCount = int(sizeof(kItems) / sizeof(kItems[0]));

  // Brand block.
  int y = area.y + 22;
  {
    Rect logo(area.x + 22, y, 34, 34);
    c.fillRoundRect(logo, 10, th.accent);
    icons::note(c, logo.inset(7), th.bgTop);
    c.drawText(*a.fBody, logo.right() + 12, y + 4, "SPlayer-Next", th.text);
    c.drawText(*a.fTiny, logo.right() + 12, y + 26, "for Nintendo Switch",
               th.textFaint);
    y += 34 + 22;
  }

  // Search entry.
  {
    Rect box(area.x + 16, y, area.w - 32, 40);
    const bool active = a.page == Page::Search;
    c.fillRoundRect(box, 10, active ? th.sidebarActive : th.surface);
    icons::search(c, Rect(box.x + 10, box.y + 10, 20, 20),
                  active ? th.accent : th.textDim);
    c.drawText(*a.fSmall, box.x + 38, box.y + 9, "搜索", th.textDim);
    y += 40 + 14;
  }

  // Nav list.
  constexpr int rowH = 42;
  for (int i = 0; i < kCount; ++i) {
    Rect row(area.x + 10, y, area.w - 20, rowH - 4);
    const bool sel = a.page == kItems[i].page;

    if (sel) {
      c.fillRoundRect(row, 9, th.sidebarActive);
      c.fillRoundRect(Rect(row.x, row.y + 8, 3, row.h - 16), 2, th.accent);
    }

    const Color fg = sel ? th.text : th.textDim;
    Rect icon(row.x + 12, row.y + (row.h - 18) / 2, 18, 18);
    switch (kItems[i].page) {
      case Page::Liked:
        icons::heart(c, icon, sel ? th.liked : fg, sel);
        break;
      case Page::Favorites:
        icons::star(c, icon, sel ? th.accent : fg, sel);
        break;
      default:
        if (kItems[i].icon) kItems[i].icon(c, icon, fg);
        break;
    }
    c.drawText(*a.fSmall, row.x + 40, row.y + (row.h - a.fSmall->lineHeight()) / 2,
               kItems[i].label, fg);

    // Counters on the right edge.
    std::string badge;
    if (kItems[i].page == Page::Liked) {
      badge = std::to_string(a.store.likeCount());
    } else if (kItems[i].page == Page::Library) {
      badge = std::to_string(a.library.tracks().size());
    } else if (kItems[i].page == Page::History) {
      badge = std::to_string(a.store.history().size());
    } else if (kItems[i].page == Page::Favorites) {
      badge = std::to_string(a.store.playlists().size());
    }
    if (!badge.empty()) {
      c.drawTextRight(*a.fTiny, Rect(row.x, row.y, row.w - 14, row.h), badge,
                      sel ? th.textDim : th.textFaint);
    }
    y += rowH;
  }

  // Library summary at the bottom.
  {
    const int boxH = 74;
    Rect box(area.x + 14, area.bottom() - boxH - 14, area.w - 28, boxH);
    c.fillRoundRect(box, 12, th.surface);

    char line[96];
    std::snprintf(line, sizeof(line), "%zu 首曲目 · %zu 张专辑",
                  a.library.tracks().size(), a.library.albums().size());
    c.drawTextEllipsis(*a.fTiny, Rect(box.x + 12, box.y + 10, box.w - 24, 18),
                       line, th.textDim);

    const std::string dur = formatLongDuration(
        uint64_t(a.library.totalDurationSec()));
    c.drawTextEllipsis(*a.fTiny, Rect(box.x + 12, box.y + 30, box.w - 24, 18),
                       "总时长 " + dur, th.textFaint);

    const std::string size = humanSize(a.library.totalBytes());
    c.drawTextEllipsis(*a.fTiny, Rect(box.x + 12, box.y + 48, box.w - 24, 18),
                       "占用 " + size, th.textFaint);
  }
}

// ---------------------------------------------------------------------------
// Top bar
// ---------------------------------------------------------------------------
void drawTopBar(App& a, Rect area) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;

  c.fillRect(area, th.bgTop);
  c.fillRect(Rect(area.x, area.bottom() - 1, area.w, 1), th.border);

  // Back chip when the player is open.
  int titleX = area.x + kContentPad;
  if (a.page == Page::Player) {
    Rect chip(area.x + kContentPad, area.y + 22, 32, 32);
    c.fillRoundRect(chip, 9, th.surface);
    icons::back(c, chip.inset(7), th.text);
    titleX = chip.right() + 14;
  }

  c.drawText(*a.fTitle, titleX, area.y + 14, pageTitle(a.page), th.text);
  c.drawText(*a.fTiny, titleX + 2, area.y + 50, pageSubtitle(a.page),
             th.textFaint);

  // Right side: transport status pill.
  {
    const bool playing = a.audio.isPlaying();
    const std::string label =
        a.audio.isLoaded() ? (playing ? "正在播放" : "已暂停") : "空闲";
    const Color dot = playing ? th.accent : th.textFaint;
    const int textW = c.measureText(*a.fTiny, label);
    Rect pill(area.right() - kContentPad - (textW + 44), area.y + 24, textW + 44, 30);
    c.fillRoundRect(pill, 15, th.surface);
    c.fillCircle(pill.x + 16, pill.y + 15, 4, dot);
    c.drawText(*a.fSmall, pill.x + 28, pill.y + 6, label, th.textDim);
  }
}

// ---------------------------------------------------------------------------
// Player bar
// ---------------------------------------------------------------------------
void drawPlayerBar(App& a, Rect area) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const Track* t = a.currentTrack();

  c.fillRect(area, th.surface);
  c.fillRect(Rect(area.x, area.y, area.w, 1), th.border);

  const int pad = kContentPad;
  const int midY = area.y + area.h / 2;

  // --- cover + titles -------------------------------------------------------
  Rect cover(area.x + pad, midY - 30, 60, 60);
  drawCover(a, cover, t ? t->coverId : -1, 10);

  const int textX = cover.right() + 14;
  const int textW = 260;
  if (t) {
    c.drawTextEllipsis(*a.fSmall, Rect(textX, midY - 20, textW, 20), t->title,
                       th.text);
    c.drawTextEllipsis(*a.fTiny, Rect(textX, midY + 2, textW, 18), t->artist,
                       th.textDim);
  } else {
    c.drawText(*a.fSmall, textX, midY - 20, "未在播放", th.textFaint);
    c.drawText(*a.fTiny, textX, midY + 2, "从音乐库选一首吧", th.textFaint);
  }

  // --- transport ------------------------------------------------------------
  const double pos = a.audio.positionSec();
  const double dur = a.audio.durationSec();

  // Shuffle.
  icons::shuffle(c, Rect(area.x + 400, midY - 11, 22, 22),
                 a.shuffle ? th.accent : th.textFaint);
  // Prev / play / next centred.
  icons::prev(c, Rect(area.x + 438, midY - 13, 26, 26), th.textDim);
  {
    Rect btn(area.x + 474, midY - 24, 48, 48);
    c.fillCircle(btn.x + 24, btn.y + 24, 24, th.accent);
    if (a.audio.isPlaying()) icons::pause(c, btn.inset(13), th.bgTop);
    else icons::play(c, btn.inset(13), th.bgTop);
  }
  icons::next(c, Rect(area.x + 532, midY - 13, 26, 26), th.textDim);
  // Repeat.
  icons::repeat(c, Rect(area.x + 570, midY - 11, 22, 22),
                a.repeatMode ? th.accent : th.textFaint, a.repeatMode == 2);

  // --- progress -------------------------------------------------------------
  const int barX = area.x + 620;
  const int barW = std::max(120, area.w - (barX - area.x) - 250);
  Rect track(barX, midY - 3, barW, 6);
  c.fillRoundRect(track, 3, th.surfaceHi);
  const double frac = dur > 0.0 ? std::clamp(pos / dur, 0.0, 1.0) : 0.0;
  if (frac > 0.0) {
    c.fillRoundRect(Rect(track.x, track.y, std::max(3, int(barW * frac)), 6), 3,
                    th.accent);
    c.fillCircle(track.x + int(barW * frac), track.y + 3, 5, th.text);
  }
  c.drawText(*a.fTiny, barX, midY + 10, formatTime(uint64_t(pos * 1000.0)),
             th.textFaint);
  c.drawTextRight(*a.fTiny, Rect(barX, midY + 10, barW, 16),
                  formatTime(uint64_t(dur * 1000.0)), th.textFaint);

  // --- volume ---------------------------------------------------------------
  const int volX = area.right() - pad - 118;
  icons::volume(c, Rect(volX, midY - 10, 20, 20), th.textDim);
  Rect volTrack(volX + 28, midY - 3, 78, 6);
  c.fillRoundRect(volTrack, 3, th.surfaceHi);
  const int vw = int(volTrack.w * std::clamp(a.audio.volume(), 0.0f, 1.0f));
  if (vw > 0) c.fillRoundRect(Rect(volTrack.x, volTrack.y, vw, 6), 3, th.accent);

  // --- toast ----------------------------------------------------------------
  if (!a.status.empty() && a.now < a.statusUntil) {
    const int w = c.measureText(*a.fSmall, a.status) + 32;
    Rect box(area.x + (area.w - w) / 2, area.y - 46, w, 34);
    c.dropShadow(box, 14, 10, 90);
    c.fillRoundRect(box, 14, th.surfaceHi);
    c.drawTextCentered(*a.fSmall, box, a.status, th.text);
  }
}

}  // namespace spn