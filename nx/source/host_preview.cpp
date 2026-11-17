// SPDX-License-Identifier: AGPL-3.0
// Host-side smoke test: renders the UI to a PPM so the layout can be checked
// without a Switch. Built only when SPN_HOST_CHECK is defined.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <switch.h>

#include "spn/audio.hpp"
#include "spn/canvas.hpp"
#include "spn/font.hpp"
#include "spn/library.hpp"
#include "spn/lyrics.hpp"
#include "spn/types.hpp"

using namespace spn;

namespace {

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

}  // namespace

int hostRenderPreview(const char* fontPath, const char* outPpm) {
  constexpr int W = 1280;
  constexpr int H = 720;

  std::vector<uint32_t> pixels(size_t(W) * H, 0);
  Canvas c(pixels.data(), W, H, W);

  Font fontUi, fontSmall, fontTitle, fontBig;
  if (!fontUi.loadFromFile(fontPath, 22.f)) {
    std::fprintf(stderr, "font load failed: %s\n", fontPath);
    return 1;
  }
  fontSmall.loadFromFile(fontPath, 17.f);
  fontTitle.loadFromFile(fontPath, 30.f);
  fontBig.loadFromFile(fontPath, 96.f);

  // Fake library content so the list/lyrics layout can be inspected.
  std::vector<Track> tracks;
  const char* names[][3] = {
      {"晴天", "周杰伦", "叶惠美"},
      {"夜曲", "周杰伦", "十一月的萧邦"},
      {"富士山下", "陈奕迅", "What's Going On...?"},
      {"Lemon", "米津玄師", "STRAY SHEEP"},
      {"夜に駆ける", "YOASOBI", "THE BOOK"},
      {"Someone Like You", "Adele", "21"},
      {"Bohemian Rhapsody", "Queen", "A Night At The Opera"},
      {"残酷な天使のテーゼ", "高橋洋子", "Neon Genesis Evangelion"},
      {"海阔天空", "Beyond", "乐与怒"},
      {"平凡之路", "朴树", "猎户星座"},
  };
  for (auto& n : names) {
    Track t;
    t.title = n[0];
    t.artist = n[1];
    t.album = n[2];
    t.path = std::string("sdmc:/music/") + n[1] + " - " + n[0] + ".flac";
    t.durationSec = 245.0;
    tracks.push_back(t);
  }

  // Background
  c.verticalGradient(Rect(0, 0, W, H), kBgTop, kBgBottom);

  // Header
  c.fillRect(Rect(0, 0, W, kHeaderH), kSurface);
  c.fillRect(Rect(0, kHeaderH - 2, W, 2), kAccentDim);
  c.drawText(fontTitle, 24, 18, "SPlayer-Next", kText);
  const int logoW = c.measureText(fontTitle, "SPlayer-Next");
  c.drawText(fontSmall, 24 + logoW + 12, 26, "Nintendo Switch", kTextFaint);
  const std::string countStr = std::to_string(tracks.size()) + " tracks";
  const int countW = c.measureText(fontSmall, countStr);
  c.drawText(fontSmall, W - 24 - countW, 26, countStr, kTextDim);

  const int bodyY = kHeaderH;
  const int bodyH = H - kHeaderH - kFooterH;

  // Sidebar
  c.fillRect(Rect(0, bodyY, kSidebarW, bodyH), kSurface);
  c.fillRect(Rect(kSidebarW - 1, bodyY, 1, bodyH), kSurfaceAlt);
  int y = bodyY + 20;
  c.drawText(fontSmall, 20, y, "NOW PLAYING", kAccent);
  y += 30;

  Rect cover(20, y, kSidebarW - 40, kSidebarW - 40);
  c.fillRoundRect(cover, 16, kSurfaceAlt);
  c.strokeRoundRect(cover, 16, kAccentDim, 2);
  const std::string note = "♪";
  const int nw = c.measureText(fontBig, note);
  c.drawText(fontBig, cover.x + (cover.w - nw) / 2,
             cover.y + (cover.h - fontBig.lineHeight()) / 2, note, kAccent);
  y = cover.y + cover.h + 20;

  c.drawTextEllipsis(fontUi, Rect(20, y, kSidebarW - 40, 26), tracks[0].title, kText);
  y += 28;
  c.drawTextEllipsis(fontSmall, Rect(20, y, kSidebarW - 40, 22), tracks[0].artist,
                     kTextDim);
  y += 34;

  Rect bar(20, y, kSidebarW - 40, 6);
  c.fillRoundRect(bar, 3, kSurfaceAlt);
  c.fillRoundRect(Rect(bar.x, bar.y, int(bar.w * 0.42), bar.h), 3, kAccent);
  y += 16;
  c.drawText(fontSmall, 20, y, "01:43 / 04:05", kTextDim);
  const std::string volStr = "Vol 80%";
  const int volW = c.measureText(fontSmall, volStr);
  c.drawText(fontSmall, kSidebarW - 20 - volW, y, volStr, kTextDim);
  y += 34;
  c.drawText(fontSmall, 20, y, "Playing", kAccent);

  // Library list
  Rect libArea(kSidebarW, bodyY, (W - kSidebarW) / 2, bodyH);
  c.fillRect(libArea, kBgBottom);
  constexpr int rowH = 56;
  const int visibleRows = bodyH / rowH;
  for (int i = 0; i < visibleRows && i < int(tracks.size()); ++i) {
    Rect row(libArea.x, libArea.y + i * rowH, libArea.w, rowH - 4);
    const bool sel = i == 1;
    if (sel) {
      c.fillRoundRectAlpha(row, 12, kAccentDim, 200);
      c.strokeRoundRect(row, 12, kAccent, 2);
    } else if (i % 2 == 1) {
      c.fillRoundRectAlpha(row, 12, kSurfaceAlt, 90);
    }
    c.drawText(fontSmall, row.x + 16, row.y + 16, std::to_string(i + 1),
               sel ? kText : kTextFaint);
    const int textX = row.x + 60;
    const int textW = row.w - 60 - 100;
    c.drawTextEllipsis(fontUi, Rect(textX, row.y + 8, textW, 24), tracks[size_t(i)].title,
                       i == 0 ? kAccent : kText);
    c.drawTextEllipsis(fontSmall, Rect(textX, row.y + 30, textW, 20),
                       tracks[size_t(i)].artist, kTextDim);
    const std::string d = "04:05";
    const int dw = c.measureText(fontSmall, d);
    c.drawText(fontSmall, row.x + row.w - 20 - dw, row.y + 18, d, kTextFaint);
  }

  // Now playing / lyrics
  Rect nowArea(kSidebarW + libArea.w, bodyY, W - kSidebarW - libArea.w, bodyH);
  c.fillRect(nowArea, kBgTop);
  c.drawTextEllipsis(fontTitle, Rect(nowArea.x + 24, nowArea.y + 16, nowArea.w - 48, 34),
                     tracks[0].title, kText);
  c.drawTextEllipsis(fontSmall, Rect(nowArea.x + 24, nowArea.y + 52, nowArea.w - 48, 22),
                     tracks[0].artist + "  ·  " + tracks[0].album, kTextDim);

  Rect lyr(nowArea.x + 24, nowArea.y + 92, nowArea.w - 48, nowArea.h - 92 - 16);
  c.fillRectAlpha(lyr, kSurface, 60);

  const char* lines[] = {
      "故事的小黄花",
      "从出生那年就飘着",
      "童年的荡秋千",
      "随记忆一直晃到现在",
      "吹着前奏望着天空",
      "我想起花瓣试着掉落",
      "为你翘课的那一天",
      "花落的那一天",
      "教室的那一间",
      "我怎么看不见",
  };
  constexpr int lineH = 44;
  const int mid = lyr.y + lyr.h / 2;
  const int active = 4;
  for (int i = 0; i < 10; ++i) {
    const int ly = mid - lineH / 2 + (i - active) * lineH;
    if (ly < lyr.y - lineH || ly > lyr.y + lyr.h) continue;
    const int dist = std::abs(i - active);
    Color col = kTextFaint;
    if (dist == 0) col = kAccent;
    else if (dist == 1) col = kTextDim;
    else if (dist == 2) col = Color::rgb(0x707D96);
    const Font& f = dist == 0 ? fontUi : fontSmall;
    const int tw = c.measureText(f, lines[i]);
    const int tx = lyr.x + (lyr.w - tw) / 2;
    c.drawText(f, tx, ly, lines[i], col);
  }

  // Footer
  const int fy = H - kFooterH;
  c.fillRect(Rect(0, fy, W, kFooterH), kSurface);
  c.fillRect(Rect(0, fy, W, 1), kSurfaceAlt);
  c.drawTextEllipsis(fontSmall, Rect(16, fy + 14, W - 32, 22),
                     "A: Play/Pause   B: Pause   X: Next   Y: Prev   L/R: Seek 10s   "
                     "Stick L/R: Shuffle   ZL: Loop   ZR: Rescan   Stick Y: Volume   +: Exit",
                     kTextFaint);

  // Write a PPM (P6) so we can eyeball the result.
  FILE* fp = std::fopen(outPpm, "wb");
  if (!fp) {
    std::fprintf(stderr, "cannot write %s\n", outPpm);
    return 1;
  }
  std::fprintf(fp, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; ++i) {
    const uint32_t p = pixels[size_t(i)];
    const unsigned char rgb[3] = {static_cast<unsigned char>(p & 0xFF),
                                  static_cast<unsigned char>((p >> 8) & 0xFF),
                                  static_cast<unsigned char>((p >> 16) & 0xFF)};
    std::fwrite(rgb, 1, 3, fp);
  }
  std::fclose(fp);
  std::printf("wrote %s\n", outPpm);
  return 0;
}
