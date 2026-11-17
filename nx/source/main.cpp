// SPDX-License-Identifier: AGPL-3.0
// SPlayer-Next — Nintendo Switch port. Application entry point.
#include <switch.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "spn/audio.hpp"
#include "spn/canvas.hpp"
#include "spn/font.hpp"
#include "spn/library.hpp"
#include "spn/lyrics.hpp"
#include "spn/types.hpp"

using namespace spn;

namespace {

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
constexpr Color kBgTop = Color::rgb(0x0B0E14);
constexpr Color kBgBottom = Color::rgb(0x161B26);
constexpr Color kSurface = Color::rgb(0x1C2230);
constexpr Color kSurfaceAlt = Color::rgb(0x232B3C);
constexpr Color kAccent = Color::rgb(0x4C8DFF);
constexpr Color kAccentDim = Color::rgb(0x2C4B8A);
constexpr Color kText = Color::rgb(0xF2F5FA);
constexpr Color kTextDim = Color::rgb(0x94A0B8);
constexpr Color kTextFaint = Color::rgb(0x5C6884);

constexpr int kHeaderH = 64;
constexpr int kFooterH = 48;
constexpr int kSidebarW = 300;

// ---------------------------------------------------------------------------
// View state
// ---------------------------------------------------------------------------
enum class Focus { Library, NowPlaying };

struct App {
  Canvas* canvas = nullptr;
  Font* fontUi = nullptr;
  Font* fontSmall = nullptr;
  Font* fontTitle = nullptr;
  Font* fontBig = nullptr;

  AudioEngine audio;
  Library library;
  Lyrics lyrics;

  int selected = 0;
  int scroll = 0;
  Focus focus = Focus::Library;
  bool shuffle = false;
  bool dirty = true;
  double lyricScrollAnchor = 0.0;
  std::string status;

  int viewW = 1280;
  int viewH = 720;

  const Track* currentTrack() const {
    if (selected < 0 || selected >= int(library.tracks().size())) return nullptr;
    return &library.tracks()[size_t(selected)];
  }
};

App g;

void loadLyricsFor(const Track& t) {
  g.lyrics = Lyrics();
  const std::string lrc = Library::findLyricFile(t.path);
  if (!lrc.empty()) g.lyrics.loadFile(lrc);
}

void playIndex(int index) {
  if (index < 0 || index >= int(g.library.tracks().size())) return;
  g.selected = index;
  const Track& t = g.library.tracks()[size_t(index)];
  g.audio.play(t.path);
  loadLyricsFor(t);
  g.dirty = true;
}

void playNext(bool forward) {
  const int n = int(g.library.tracks().size());
  if (n == 0) return;
  int idx = g.selected;
  if (g.shuffle && n > 1) {
    int next = idx;
    while (next == idx) next = rand() % n;
    idx = next;
  } else {
    idx += forward ? 1 : -1;
    if (idx >= n) idx = 0;
    if (idx < 0) idx = n - 1;
  }
  playIndex(idx);
}

void onTrackEnd(void*) {
  g.dirty = true;
}

// ---------------------------------------------------------------------------
// Panels
// ---------------------------------------------------------------------------

void drawHeader(Canvas& c) {
  c.fillRect(Rect(0, 0, c.width(), kHeaderH), kSurface);
  c.fillRect(Rect(0, kHeaderH - 2, c.width(), 2), kAccentDim);

  c.drawText(*g.fontTitle, 24, 18, "SPlayer-Next", kText);
  const int logoW = c.measureText(*g.fontTitle, "SPlayer-Next");
  c.drawText(*g.fontSmall, 24 + logoW + 12, 26, "Nintendo Switch", kTextFaint);

  char buf[128];
  std::snprintf(buf, sizeof(buf), "%zu tracks", g.library.tracks().size());
  const int w = c.measureText(*g.fontSmall, buf);
  c.drawText(*g.fontSmall, c.width() - 24 - w, 26, buf, kTextDim);
}

void drawFooter(Canvas& c) {
  const int y = c.height() - kFooterH;
  c.fillRect(Rect(0, y, c.width(), kFooterH), kSurface);
  c.fillRect(Rect(0, y, c.width(), 1), kSurfaceAlt);
  const std::string hint =
      "A: Play/Pause   B: Pause   X: Next   Y: Prev   L/R: Seek 10s   "
      "Stick L/R: Shuffle   ZL: Loop   ZR: Rescan   Stick Y: Volume   +: Exit";
  c.drawTextEllipsis(*g.fontSmall, Rect(16, y + 14, c.width() - 32, 22), hint,
                     kTextFaint);
}

void drawSidebar(Canvas& c, int y0, int h) {
  c.fillRect(Rect(0, y0, kSidebarW, h), kSurface);
  c.fillRect(Rect(kSidebarW - 1, y0, 1, h), kSurfaceAlt);

  const Track* cur = g.currentTrack();
  int y = y0 + 20;

  c.drawText(*g.fontSmall, 20, y, "NOW PLAYING", kAccent);
  y += 30;

  // Cover placeholder: a rounded square with a note-ish glyph.
  Rect cover(20, y, kSidebarW - 40, kSidebarW - 40);
  c.fillRoundRect(cover, 16, kSurfaceAlt);
  c.strokeRoundRect(cover, 16, kAccentDim, 2);

  const std::string coverText = cur ? "♪" : "—";
  const int cw = c.measureText(*g.fontBig, coverText);
  const int ch = g.fontBig->lineHeight();
  c.drawText(*g.fontBig, cover.x + (cover.w - cw) / 2,
             cover.y + (cover.h - ch) / 2, coverText, kAccent);
  y = cover.y + cover.h + 20;

  if (cur) {
    c.drawTextEllipsis(*g.fontUi, Rect(20, y, kSidebarW - 40, 26), cur->title,
                       kText);
    y += 28;
    c.drawTextEllipsis(*g.fontSmall, Rect(20, y, kSidebarW - 40, 22),
                       cur->artist, kTextDim);
    y += 34;
  } else {
    c.drawText(*g.fontUi, 20, y, "Nothing playing", kTextFaint);
    y += 34;
  }

  // Progress bar.
  const double pos = g.audio.positionSec();
  const double dur = g.audio.durationSec();
  const double frac = dur > 0.0 ? std::min(1.0, pos / dur) : 0.0;
  Rect bar(20, y, kSidebarW - 40, 6);
  c.fillRoundRect(bar, 3, kSurfaceAlt);
  Rect filled(bar.x, bar.y, int(bar.w * frac), bar.h);
  if (filled.w > 0) c.fillRoundRect(filled, 3, kAccent);
  y += 16;

  const std::string timeStr =
      formatTime(uint64_t(pos * 1000.0)) + " / " + formatTime(uint64_t(dur * 1000.0));
  c.drawText(*g.fontSmall, 20, y, timeStr, kTextDim);

  const std::string volStr =
      "Vol " + std::to_string(int(g.audio.volume() * 100.0)) + "%";
  const int volW = c.measureText(*g.fontSmall, volStr);
  c.drawText(*g.fontSmall, kSidebarW - 20 - volW, y, volStr, kTextDim);

  y += 34;
  std::string state = "Stopped";
  if (g.audio.isPlaying()) state = "Playing";
  if (!g.audio.isPlaying() && g.audio.isLoaded()) state = "Paused";
  c.drawText(*g.fontSmall, 20, y, state, g.audio.isPlaying() ? kAccent : kTextFaint);
}

void drawLibraryList(Canvas& c, Rect area) {
  const auto& tracks = g.library.tracks();
  c.fillRect(area, kBgBottom);

  if (tracks.empty()) {
    const std::string msg = g.library.lastError().empty()
                                ? "No music found on the SD card."
                                : g.library.lastError();
    c.drawText(*g.fontUi, area.x + 24, area.y + 32, msg, kTextDim);
    c.drawText(*g.fontSmall, area.x + 24, area.y + 64,
               "Supported: MP3 / FLAC / WAV", kTextFaint);
    return;
  }

  constexpr int rowH = 56;
  const int visibleRows = std::max(1, area.h / rowH);

  // Keep the selection inside the viewport.
  if (g.selected < g.scroll) g.scroll = g.selected;
  if (g.selected >= g.scroll + visibleRows) g.scroll = g.selected - visibleRows + 1;
  g.scroll = std::clamp(g.scroll, 0, std::max(0, int(tracks.size()) - visibleRows));

  for (int i = 0; i < visibleRows; ++i) {
    const int idx = g.scroll + i;
    if (idx >= int(tracks.size())) break;
    const Track& t = tracks[size_t(idx)];
    Rect row(area.x, area.y + i * rowH, area.w, rowH - 4);

    const bool isSelected = idx == g.selected;
    const bool isPlaying =
        g.audio.isLoaded() && g.audio.currentPath() == t.path && g.audio.isPlaying();

    if (isSelected) {
      c.fillRoundRectAlpha(row, 12, kAccentDim, 200);
      c.strokeRoundRect(row, 12, kAccent, 2);
    } else if (idx % 2 == 1) {
      c.fillRoundRectAlpha(row, 12, kSurfaceAlt, 90);
    }

    const int numX = row.x + 16;
    c.drawText(*g.fontSmall, numX, row.y + 16, std::to_string(idx + 1),
               isSelected ? kText : kTextFaint);

    const int textX = row.x + 60;
    const int textW = std::max(80, row.w - 60 - 100);
    c.drawTextEllipsis(*g.fontUi, Rect(textX, row.y + 8, textW, 24), t.title,
                       isPlaying ? kAccent : kText);
    c.drawTextEllipsis(*g.fontSmall, Rect(textX, row.y + 30, textW, 20), t.artist,
                       kTextDim);

    if (t.durationSec > 0.0) {
      const std::string d = formatTime(uint64_t(t.durationSec * 1000.0));
      const int dw = c.measureText(*g.fontSmall, d);
      c.drawText(*g.fontSmall, row.x + row.w - 20 - dw, row.y + 18, d, kTextFaint);
    }
  }
}

void drawNowPlaying(Canvas& c, Rect area) {
  c.fillRect(area, kBgTop);

  const Track* cur = g.currentTrack();
  const int centerX = area.x + area.w / 2;

  if (!cur) {
    const std::string msg = "Select a track and press A";
    const int w = c.measureText(*g.fontUi, msg);
    c.drawText(*g.fontUi, centerX - w / 2, area.y + area.h / 2 - 12, msg,
               kTextFaint);
    return;
  }

  // Title block.
  const int titleW = c.measureText(*g.fontTitle, cur->title);
  c.drawTextEllipsis(*g.fontTitle, Rect(area.x + 24, area.y + 16, area.w - 48, 34),
                     cur->title, kText);
  (void)titleW;
  c.drawTextEllipsis(*g.fontSmall, Rect(area.x + 24, area.y + 52, area.w - 48, 22),
                     cur->artist + "  ·  " + cur->album, kTextDim);

  // Lyrics viewport.
  Rect lyr(area.x + 24, area.y + 92, area.w - 48, area.h - 92 - 16);
  c.fillRectAlpha(lyr, kSurface, 60);

  if (g.lyrics.empty()) {
    c.drawText(*g.fontSmall, lyr.x + 16, lyr.y + 16, "No lyrics (.lrc) found",
               kTextFaint);
    return;
  }

  const uint64_t posMs = uint64_t(g.audio.positionSec() * 1000.0);
  const int active = g.lyrics.activeIndex(posMs);
  const auto& lines = g.lyrics.lines();

  constexpr int lineH = 44;
  const int mid = lyr.y + lyr.h / 2;
  const int centerLine = active < 0 ? 0 : active;

  // Smoothly scroll so the active line sits at the vertical centre.
  const double target = double(centerLine) * lineH;
  g.lyricScrollAnchor += (target - g.lyricScrollAnchor) * 0.18;
  const int scrollPx = int(g.lyricScrollAnchor);

  for (int i = 0; i < int(lines.size()); ++i) {
    const int ly = mid - lineH / 2 + i * lineH - scrollPx;
    if (ly < lyr.y - lineH || ly > lyr.y + lyr.h) continue;

    const bool isActive = i == active;
    const int dist = std::abs(i - centerLine);
    Color col = kTextFaint;
    if (isActive) {
      col = kAccent;
    } else if (dist <= 1) {
      col = kTextDim;
    } else if (dist <= 3) {
      col = Color::rgb(0x707D96);
    }

    const Font& f = isActive ? *g.fontUi : *g.fontSmall;
    const int tw = c.measureText(f, lines[size_t(i)].text);
    c.drawTextEllipsis(f, Rect(lyr.x + 16, ly, lyr.w - 32, 30),
                       lines[size_t(i)].text, col);
    (void)tw;
    (void)centerX;
  }
}

void render() {
  if (!g.canvas) return;
  Canvas& c = *g.canvas;

  c.verticalGradient(Rect(0, 0, c.width(), c.height()), kBgTop, kBgBottom);
  drawHeader(c);

  const int bodyY = kHeaderH;
  const int bodyH = c.height() - kHeaderH - kFooterH;

  drawSidebar(c, bodyY, bodyH);

  Rect libArea(kSidebarW, bodyY, (c.width() - kSidebarW) / 2, bodyH);
  Rect nowArea(kSidebarW + libArea.w, bodyY, c.width() - kSidebarW - libArea.w,
               bodyH);

  drawLibraryList(c, libArea);
  drawNowPlaying(c, nowArea);

  drawFooter(c);
  g.dirty = false;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void handleInput(u64 kDown, PadState& pad) {
  auto pressed = [&](uint64_t mask) { return (kDown & mask) != 0; };

  if (pressed(HidNpadButton_Up) || pressed(HidNpadButton_StickLUp)) {
    if (g.selected > 0) {
      --g.selected;
      g.dirty = true;
    }
  }
  if (pressed(HidNpadButton_Down) || pressed(HidNpadButton_StickLDown)) {
    if (g.selected + 1 < int(g.library.tracks().size())) {
      ++g.selected;
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
  if (pressed(HidNpadButton_A)) {
    const Track* cur = g.currentTrack();
    const bool sameTrack =
        cur && g.audio.isLoaded() && g.audio.currentPath() == cur->path;
    if (sameTrack) {
      g.audio.togglePause();
    } else {
      playIndex(g.selected);
    }
    g.dirty = true;
  }
  if (pressed(HidNpadButton_B)) {
    g.audio.pause();
    g.dirty = true;
  }
  if (pressed(HidNpadButton_X)) {
    playNext(true);
  }
  if (pressed(HidNpadButton_Y)) {
    playNext(false);
  }
  if (pressed(HidNpadButton_StickLLeft) || pressed(HidNpadButton_StickLRight)) {
    g.shuffle = !g.shuffle;
    g.dirty = true;
  }
  if (pressed(HidNpadButton_ZR)) {
    g.library.scan();
    g.selected = 0;
    g.scroll = 0;
    g.dirty = true;
  }
  if (pressed(HidNpadButton_ZL)) {
    g.audio.setLooping(!g.audio.looping());
    g.dirty = true;
  }

  // Volume with the left stick's vertical axis.
  const HidAnalogStickState stick = padGetStickPos(&pad, 0);
  if (stick.y < -12000 || stick.y > 12000) {
    static int accum = 0;
    accum += stick.y;
    if (std::abs(accum) > 20000) {
      const float delta = accum < 0 ? 0.02f : -0.02f;
      g.audio.setVolume(g.audio.volume() + delta);
      accum = 0;
      g.dirty = true;
    }
  }
}

// ---------------------------------------------------------------------------
// Framebuffer helpers
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

  Font fontUi, fontSmall, fontTitle, fontBig;
  const char* fontPath = "romfs:/fonts/wqy-microhei.ttc";
  const bool okUi = fontUi.loadFromFile(fontPath, 22.f);
  const bool okSmall = fontSmall.loadFromFile(fontPath, 17.f);
  const bool okTitle = fontTitle.loadFromFile(fontPath, 30.f);
  const bool okBig = fontBig.loadFromFile(fontPath, 96.f);

  if (!okUi || !okSmall || !okTitle || !okBig) {
    printf("failed to load font: %s\n", fontPath);
    romfsExit();
    consoleExit(nullptr);
    return 1;
  }

  g.fontUi = &fontUi;
  g.fontSmall = &fontSmall;
  g.fontTitle = &fontTitle;
  g.fontBig = &fontBig;

  g.audio.init();
  g.audio.setOnTrackEnd(&onTrackEnd, nullptr);

  printf("scanning library...\n");
  const size_t found = g.library.scan();
  printf("found %zu tracks under %s\n", found, g.library.root().c_str());
  if (!g.library.lastError().empty()) {
    printf("%s\n", g.library.lastError().c_str());
  }
  printf("starting UI...\n");

  consoleExit(nullptr);

  Screen fbuf;
  if (!fbuf.init()) {
    g.audio.shutdown();
    romfsExit();
    return 1;
  }

  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);

  while (appletMainLoop()) {
    padUpdate(&pad);
    const u64 kDown = padGetButtonsDown(&pad);

    if ((kDown & HidNpadButton_Plus) != 0) break;

    handleInput(kDown, pad);

    uint32_t* pixels = fbuf.begin();
    if (!pixels) continue;

    Canvas canvas(pixels, 1280, 720, int(fbuf.stride_ / 4));
    g.canvas = &canvas;
    render();

    fbuf.end();
  }

  g.audio.shutdown();
  fbuf.release();
  romfsExit();
  return 0;
}