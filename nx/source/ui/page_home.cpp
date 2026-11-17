// SPDX-License-Identifier: AGPL-3.0
// Home page: hero card, quick picks, recently played, album grid.
#include <algorithm>
#include <cstdio>

#include "spn/icons.hpp"
#include "spn/ui.hpp"
#include "spn/widgets.hpp"

namespace spn {
namespace {

// Hero: current/last track with a blurred-ish gradient tinted by its cover.
void drawHero(App& a, Rect r) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;

  const Track* t = a.currentTrack();
  if (!t && !a.library.tracks().empty()) {
    t = &a.library.tracks()[size_t(std::max(0, std::min(a.homeSel,
                                                       int(a.library.tracks().size()) - 1)))];
  }

  // Tint: cover's dominant colour faded into the panel.
  uint32_t accentRaw = a.th->accent.r << 16 | a.th->accent.g << 8 | a.th->accent.b;
  if (t) accentRaw = a.library.covers().accentFor(t->coverId, accentRaw);
  const Color tint(uint8_t((accentRaw >> 16) & 0xFF),
                   uint8_t((accentRaw >> 8) & 0xFF), uint8_t(accentRaw & 0xFF));

  c.dropShadow(r, 18, 14, 100);
  c.fillRoundRect(r, 18, th.surface);
  c.pushClip(r);
  const int gradH = r.h / 2;
  const Color top(tint.r, tint.g, tint.b, 255);
  for (int y = 0; y < gradH; ++y) {
    const float f = 1.f - float(y) / float(gradH);
    c.fillRectAlpha(Rect(r.x, r.y + y, r.w, 1), top,
                    uint8_t(std::clamp(int(150.f * f), 0, 255)));
  }
  c.popClip();

  // Cover.
  Rect art(r.x + 26, r.y + 22, r.h - 44, r.h - 44);
  ui::cover(a, art, t ? t->coverId : -1, 14);

  const int textX = art.right() + 26;
  const int textW = r.right() - textX - 26;

  const char* kicker = t ? "继续聆听" : "开始你的音乐之旅";
  c.drawText(*a.fTiny, textX, r.y + 26, kicker, th.textDim);

  if (t) {
    c.drawTextEllipsis(*a.fTitle, Rect(textX, r.y + 48, textW, 40), t->title,
                       th.text);
    c.drawTextEllipsis(*a.fSmall, Rect(textX, r.y + 92, textW, 24),
                       t->artist + (t->album.empty() ? "" : "  ·  " + t->album),
                       th.textDim);

    // Play button + metadata chips.
    const int btnY = r.y + 132;
    Rect play(textX, btnY, 116, 40);
    c.fillRoundRect(play, 20, th.accent);
    icons::play(c, Rect(play.x + 16, play.y + 10, 20, 20), th.bgTop);
    c.drawText(*a.fSmall, play.x + 44, play.y + 9,
               a.audio.isPlaying() ? "播放中" : "播放", th.bgTop);

    int chipX = play.right() + 12;
    struct Chip {
      std::string text;
    };
    std::vector<Chip> chips;
    if (!t->format.empty()) chips.push_back({t->format});
    if (t->sampleRate > 0) {
      char b[32];
      std::snprintf(b, sizeof(b), "%.1f kHz", double(t->sampleRate) / 1000.0);
      chips.push_back({b});
    }
    if (t->bitDepth > 0) chips.push_back({std::to_string(t->bitDepth) + " bit"});
    if (t->year > 0) chips.push_back({std::to_string(t->year)});
    for (const Chip& ch : chips) {
      const int w = c.measureText(*a.fTiny, ch.text) + 20;
      Rect cb(chipX, btnY + 8, w, 24);
      c.fillRoundRect(cb, 12, th.surfaceHi);
      c.drawTextCentered(*a.fTiny, cb, ch.text, th.textDim);
      chipX = cb.right() + 8;
    }

    // Progress for the active track.
    if (a.audio.isLoaded() && a.playingIndex >= 0) {
      const double pos = a.audio.positionSec();
      const double dur = a.audio.durationSec();
      const int px = textX;
      const int pw = std::max(120, textW);
      ui::progressBar(a, Rect(px, r.bottom() - 30, pw, 5),
                      dur > 0.0 ? pos / dur : 0.0, th.accent, th.surfaceHi);
    }
  } else {
    c.drawTextEllipsis(*a.fTitle, Rect(textX, r.y + 52, textW, 40),
                       "把音乐放进 SD 卡吧", th.text);
    c.drawTextEllipsis(
        *a.fSmall, Rect(textX, r.y + 96, textW, 24),
        "路径：sdmc:/switch/SPlayer-Next/music", th.textDim);
  }
}

// Small stat tile.
void drawStatTile(App& a, Rect r, const char* label, const std::string& value,
                  const char* hint) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  c.fillRoundRect(r, 12, th.surface);
  c.drawText(*a.fTiny, r.x + 14, r.y + 12, label, th.textFaint);
  c.drawTextEllipsis(*a.fTitle, Rect(r.x + 14, r.y + 32, r.w - 28, 34), value,
                     th.text);
  if (hint) c.drawText(*a.fTiny, r.x + 14, r.bottom() - 26, hint, th.textFaint);
}

}  // namespace

void drawHome(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  // Vertical rhythm: hero → stats → recently played → albums.
  const int heroH = 208;
  Rect hero(body.x + pad, body.y + pad, body.w - pad * 2, heroH);
  drawHero(a, hero);

  int y = hero.bottom() + 22;

  // --- stats row ------------------------------------------------------------
  {
    const int tileH = 96;
    const int gap = 14;
    const int n = 4;
    const int tileW = (body.w - pad * 2 - gap * (n - 1)) / n;
    char buf[64];

    std::snprintf(buf, sizeof(buf), "%zu", a.library.tracks().size());
    drawStatTile(a, Rect(body.x + pad, y, tileW, tileH), "曲目", buf, "本地文件");
    std::snprintf(buf, sizeof(buf), "%zu", a.library.albums().size());
    drawStatTile(a, Rect(body.x + pad + (tileW + gap), y, tileW, tileH), "专辑",
                 buf, "按专辑分组");
    drawStatTile(a, Rect(body.x + pad + (tileW + gap) * 2, y, tileW, tileH),
                 "收藏", std::to_string(a.store.likeCount()), "我喜欢的音乐");
    drawStatTile(a, Rect(body.x + pad + (tileW + gap) * 3, y, tileW, tileH),
                 "总时长", formatLongDuration(uint64_t(a.library.totalDurationSec())),
                 "曲库合计");
    y += tileH + 26;
  }

  // --- recently played ------------------------------------------------------
  const int bottomReserve = 8;
  const int listRows = 4;
  const int rowH = 56;
  const int listH = rowH * listRows + 40;

  if (y + listH < body.bottom() - bottomReserve) {
    ui::sectionHeader(a, body.x + pad, y, body.w - pad * 2, "最近播放",
                      "摇杆 ↑↓ 选择 · A 播放");
    y += 34;

    Rect panel(body.x + pad, y, body.w - pad * 2, listH);
    c.fillRoundRect(panel, 14, th.surface);

    const auto& hist = a.store.history();
    if (hist.empty()) {
      c.drawTextCentered(*a.fSmall, Rect(panel.x, panel.y, panel.w, panel.h),
                         "还没有播放记录，选一首试试", th.textFaint);
    } else {
      const int shown = std::min<int>(listRows, int(hist.size()));
      for (int i = 0; i < shown; ++i) {
        const HistoryEntry& h = hist[size_t(i)];
        const int idx = a.library.indexOf(h.path);
        Rect row(panel.x + 6, panel.y + 8 + i * rowH, panel.w - 12, rowH - 4);

        if (idx >= 0) {
          const bool playing = idx == a.playingIndex;
          ui::trackRow(a, row, idx, i == a.homeSel, playing, true, i + 1);
          continue;
        }

        // File vanished from the SD card — show what used to be there.
        if (i == a.homeSel) {
          c.fillRoundRect(row, 10, th.surfaceAlt);
          c.fillRoundRect(Rect(row.x, row.y + 8, 3, row.h - 16), 2, th.danger);
        }
        const size_t slash = h.path.find_last_of('/');
        const std::string name =
            slash == std::string::npos ? h.path : h.path.substr(slash + 1);
        ui::cover(a, Rect(row.x + 14, row.y + 6, 44, 44), -1, 8);
        c.drawTextEllipsis(*a.fSmall, Rect(row.x + 70, row.y + 8, row.w - 200, 20),
                           name, th.textDim);
        c.drawText(*a.fTiny, row.x + 70, row.y + 30, "文件已不存在", th.danger);
      }
    }
    y += listH + 20;
  }

  // --- album grid -----------------------------------------------------------
  const auto& albums = a.library.albums();
  if (!albums.empty()) {
    const int remaining = body.bottom() - y - bottomReserve;
    const int tileMin = 132;
    const int cols = std::max(1, (body.w - pad * 2) / (tileMin + 16));
    const int tileW = (body.w - pad * 2 - 16 * (cols - 1)) / cols;
    const int rows = std::max(0, remaining / (tileW + 56));
    if (rows > 0) {
      ui::sectionHeader(a, body.x + pad, y, body.w - pad * 2, "专辑",
                        std::to_string(albums.size()) + " 张");
      y += 34;
      const int total = std::min<int>(int(albums.size()), cols * rows);
      for (int i = 0; i < total; ++i) {
        const int cx = i % cols;
        const int cy = i / cols;
        Rect tile(body.x + pad + cx * (tileW + 16), y + cy * (tileW + 56), tileW,
                  tileW + 48);
        ui::albumTile(a, tile, albums[size_t(i)], i == a.albumSel);
      }
    }
  }
}

}  // namespace spn