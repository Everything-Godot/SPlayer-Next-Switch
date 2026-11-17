// SPDX-License-Identifier: AGPL-3.0
// Full-screen player: large cover, karaoke-style lyric scroll, transport.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "spn/icons.hpp"
#include "spn/ui.hpp"
#include "spn/widgets.hpp"

namespace spn {
namespace {

// Draws the right-hand lyric column, keeping the active line centred.
// Word-timed lyrics (TTML / QRC / KRC / YRC / LYS / enhanced LRC / ASS karaoke)
// get a karaoke sweep: the already-sung prefix is drawn in the accent colour.
// `posMs` is the playback position; line-timed-only lines fall back to a plain
// centred draw in `base`.
void drawLyricLine(Canvas& c, const Font& f, Rect box, const LyricLine& line,
                   Color base, Color sung, uint64_t posMs) {
  if (!line.wordTimed()) {
    c.drawTextEllipsisCentered(f, box, line.text, base);
    return;
  }

  const int fullW = c.measureText(f, line.text);
  if (fullW <= 0) return;
  if (fullW > box.w) {
    // Too long for the column: don't attempt a sweep.
    c.drawTextEllipsisCentered(f, box, line.text, base);
    return;
  }
  const int x = box.x + (box.w - fullW) / 2;
  const int y = box.y + (box.h - f.lineHeight()) / 2;

  // Sum the widths of the syllables that have already been sung.
  float sungW = 0.0f;
  for (const LyricWord& w : line.words) {
    if (w.endMs > w.startMs && posMs >= w.endMs) {
      sungW += float(c.measureText(f, w.text));
      continue;
    }
    if (w.endMs > w.startMs && posMs > w.startMs) {
      const float t = float(posMs - w.startMs) / float(w.endMs - w.startMs);
      sungW += float(c.measureText(f, w.text)) * std::min(1.0f, std::max(0.0f, t));
    }
    break;
  }

  c.drawText(f, x, y, line.text, base);
  const int sweepPx = int(sungW);
  if (sweepPx <= 0) return;
  c.pushClip(Rect(x, box.y, std::min(sweepPx, fullW), box.h));
  c.drawText(f, x, y, line.text, sung);
  c.popClip();
}

void drawLyrics(App& a, Rect area, const Track& t) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;

  if (a.lyrics.empty()) {
    c.drawTextCentered(*a.fSmall, Rect(area.x, area.y + area.h / 2 - 20, area.w, 24),
                       "没有歌词文件", th.textFaint);
    c.drawTextCentered(*a.fTiny, Rect(area.x, area.y + area.h / 2 + 6, area.w, 20),
                       "同目录放同名 .ttml/.lys/.qrc/.krc/.yrc/.lrc/.ass/.srt", th.textFaint);
    return;
  }

  const uint64_t posMs = uint64_t(a.audio.positionSec() * 1000.0);
  const int active = a.lyrics.activeIndex(posMs);
  const auto& lines = a.lyrics.lines();

  const int lineH = std::max(34, a.store.settings().lyricFontSize + 16);
  const int pivot = active < 0 ? 0 : active;

  // Smooth follow; snap when the jump is large (seek).
  const double target = double(pivot) * double(lineH);
  if (std::fabs(target - a.lyricScroll) > double(lineH) * 4.0) {
    a.lyricScroll = target;
  } else {
    a.lyricScroll += (target - a.lyricScroll) * 0.20;
  }

  c.pushClip(area);
  const int centreY = area.y + area.h / 2;
  const int scrollPx = int(a.lyricScroll);
  const int first = std::max(0, (scrollPx - area.h / 2) / lineH - 1);
  const int last = std::min<int>(int(lines.size()), first + area.h / lineH + 3);

  for (int i = first; i < last; ++i) {
    // Active line sits at the vertical centre; everything else is offset by
    // the residual scroll so motion remains continuous between lines.
    const int y = centreY - lineH / 2 + i * lineH - scrollPx;
    if (y < area.y - lineH || y > area.bottom()) continue;

    const bool isActive = i == active;
    const int dist = std::abs(i - pivot);

    Color col = th.textFaint;
    if (isActive) col = th.text;
    else if (dist == 1) col = th.textDim;
    else if (dist <= 3) col = th.textFaint;

    const Font& f = isActive ? *a.fBody : *a.fSmall;
    Rect box(area.x + 12, y, area.w - 24, lineH);

    if (isActive) {
      // Highlight plate behind the active line.
      c.fillRoundRect(Rect(box.x, box.y + 2, box.w, lineH - 4), 10,
                      th.surfaceAlt);
      drawLyricLine(c, f, box, lines[size_t(i)], col, th.accent, posMs);
    } else {
      c.drawTextEllipsisCentered(f, box, lines[size_t(i)].text, col);
    }
  }
  c.popClip();
}

}  // namespace

void drawPlayer(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  const Track* t = a.currentTrack();
  if (!t) {
    ui::emptyState(a, body, icons::note, "没有正在播放的曲目",
                   "从音乐库挑一首，按 A 开始播放");
    return;
  }

  // Background wash derived from the cover's dominant colour.
  uint32_t raw = th.accent.r << 16 | th.accent.g << 8 | th.accent.b;
  raw = a.library.covers().accentFor(t->coverId, raw);
  const Color tint(uint8_t((raw >> 16) & 0xFF), uint8_t((raw >> 8) & 0xFF),
                   uint8_t(raw & 0xFF));
  for (int i = 0; i < body.h / 3; ++i) {
    const float f = 1.f - float(i) / float(std::max(1, body.h / 3));
    c.fillRectAlpha(Rect(body.x, body.y + i, body.w, 1), tint,
                    uint8_t(std::clamp(int(90.f * f), 0, 255)));
  }

  // --- left: cover + transport ---------------------------------------------
  const int leftW = std::min(440, body.w * 2 / 5);
  Rect left(body.x + pad, body.y + pad, leftW, body.h - pad * 2);

  const int artSide = std::min(leftW - 8, left.h - 210);
  Rect art(left.x, left.y, artSide, artSide);
  c.dropShadow(art, 18, 20, 120);
  ui::cover(a, art, t->coverId, 18);

  int ty = art.bottom() + 22;
  c.drawTextEllipsis(*a.fTitle, Rect(left.x, ty, leftW, 36), t->title, th.text);
  ty += 40;
  c.drawTextEllipsis(*a.fSmall, Rect(left.x, ty, leftW, 24),
                     t->artist + (t->album.empty() ? "" : "  ·  " + t->album),
                     th.textDim);
  ty += 34;

  // Progress + times.
  const double pos = a.audio.positionSec();
  const double dur = a.audio.durationSec();
  ui::progressBar(a, Rect(left.x, ty, leftW, 6),
                  dur > 0.0 ? pos / dur : 0.0, th.accent, th.surfaceHi);
  ty += 14;
  c.drawText(*a.fTiny, left.x, ty, formatTime(uint64_t(pos * 1000.0)),
             th.textDim);
  c.drawTextRight(*a.fTiny, Rect(left.x, ty, leftW, 18),
                  formatTime(uint64_t(dur * 1000.0)), th.textDim);
  ty += 28;

  // Buttons row.
  {
    const int midX = left.x + leftW / 2;
    icons::shuffle(c, Rect(left.x + 4, ty + 8, 22, 22),
                   a.shuffle ? th.accent : th.textFaint);
    icons::prev(c, Rect(midX - 74, ty + 2, 28, 28), th.textDim);
    Rect btn(midX - 26, ty - 6, 52, 52);
    c.fillCircle(btn.x + 26, btn.y + 26, 26, th.accent);
    if (a.audio.isPlaying()) icons::pause(c, btn.inset(14), th.bgTop);
    else icons::play(c, btn.inset(14), th.bgTop);
    icons::next(c, Rect(midX + 46, ty + 2, 28, 28), th.textDim);
    icons::repeat(c, Rect(left.right() - 26, ty + 8, 22, 22),
                  a.repeatMode ? th.accent : th.textFaint, a.repeatMode == 2);
    ty += 58;
  }

  // Tech badges.
  {
    int x = left.x;
    const int y = ty;
    auto chip = [&](const std::string& text) {
      if (text.empty()) return;
      const int w = c.measureText(*a.fTiny, text) + 20;
      if (x + w > left.right()) return;
      Rect cb(x, y, w, 24);
      ui::badge(a, cb, text, th.surfaceHi, th.textDim);
      x = cb.right() + 8;
    };
    chip(t->format);
    if (t->sampleRate > 0) {
      char b[32];
      std::snprintf(b, sizeof(b), "%.1f kHz", double(t->sampleRate) / 1000.0);
      chip(b);
    }
    if (t->bitDepth > 0) chip(std::to_string(t->bitDepth) + " bit");
    if (t->bitrateKbps > 0) chip(std::to_string(t->bitrateKbps) + " kbps");
    if (a.store.isLiked(t->path)) chip("已收藏");
  }

  // --- right: lyrics -------------------------------------------------------
  if (a.store.settings().showLyrics) {
    Rect right(art.right() + 26, body.y + pad, body.right() - pad - art.right() - 26,
               body.h - pad * 2);
    if (right.w > 160) {
      c.fillRoundRect(right, 18, th.surface.withA(150));
      drawLyrics(a, right.inset(12), *t);
    }
  }
}

}  // namespace spn