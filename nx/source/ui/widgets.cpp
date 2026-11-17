// SPDX-License-Identifier: AGPL-3.0
// Reusable widgets shared by the pages.
#include "spn/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "spn/icons.hpp"
#include "spn/ui.hpp"

namespace spn {
namespace ui {

std::string durationLabel(double seconds) {
  if (seconds <= 0.0) return "--:--";
  return formatTime(uint64_t(seconds * 1000.0));
}

void cover(App& a, Rect r, int coverId, int radius) {
  Canvas& c = *a.c;
  const Image* img = coverId >= 0 ? a.library.covers().get(coverId) : nullptr;
  if (img) {
    c.drawImageRounded(*img, r, radius);
    // Subtle inner border so covers read as objects, not flat paint.
    c.strokeRoundRect(r, radius, a.th->border, 1);
    return;
  }
  c.fillRoundRect(r, radius, a.th->surfaceAlt);
  c.strokeRoundRect(r, radius, a.th->border, 1);
  const int s = std::max(12, std::min(r.w, r.h) / 2);
  icons::note(*a.c, Rect(r.x + (r.w - s) / 2, r.y + (r.h - s) / 2, s, s),
              a.th->textFaint);
}

void badge(App& a, Rect r, const std::string& text, Color bg, Color fg) {
  Canvas& c = *a.c;
  c.fillRoundRect(r, r.h / 2, bg);
  c.drawTextCentered(*a.fTiny, r, text, fg);
}

void sectionHeader(App& a, int x, int y, int w, const std::string& title,
                   const std::string& trailing) {
  Canvas& c = *a.c;
  c.drawText(*a.fBody, x, y, title, a.th->text);
  if (!trailing.empty()) {
    c.drawTextRight(*a.fTiny, Rect(x, y, w, a.fBody->lineHeight()), trailing,
                    a.th->textFaint);
  }
}

void trackRow(App& a, Rect r, int trackIndex, bool selected, bool playing,
              bool showCover, int ordinal) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const Track* t = a.trackAt(trackIndex);
  if (!t) return;

  if (selected) {
    c.fillRoundRect(r, 10, th.surfaceAlt);
    c.fillRoundRect(Rect(r.x, r.y + 8, 3, r.h - 16), 2, th.accent);
  }

  int x = r.x + 14;
  if (showCover) {
    Rect cov(x, r.y + (r.h - 44) / 2, 44, 44);
    cover(a, cov, t->coverId, 8);
    x = cov.right() + 12;
  } else {
    // Ordinal or "playing" bars.
    if (playing) {
      const int barW = 3;
      for (int i = 0; i < 3; ++i) {
        const int h = 6 + i * 5;
        c.fillRoundRect(Rect(x + i * 5, r.y + (r.h - h) / 2, barW, h), 1,
                        th.accent);
      }
    } else {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%d", ordinal);
      c.drawTextCentered(*a.fTiny, Rect(x, r.y, 22, r.h), buf,
                         selected ? th.textDim : th.textFaint);
    }
    x += 30;
  }

  // Right-hand metadata: format badge, then duration, then the like mark.
  const int right = r.right() - 16;
  int cursor = right;

  if (!t->format.empty()) {
    const int fw = c.measureText(*a.fTiny, t->format) + 16;
    Rect fb(cursor - fw, r.y + (r.h - 20) / 2, fw, 20);
    badge(a, fb, t->format, th.surfaceHi, th.textDim);
    cursor = fb.x - 10;
  }

  const std::string dur = durationLabel(t->durationSec);
  const int durW = c.measureText(*a.fTiny, dur);
  c.drawTextRight(*a.fTiny, Rect(cursor - durW, r.y, durW, r.h), dur,
                  th.textFaint);
  cursor -= durW + 10;

  if (a.store.isLiked(t->path)) {
    Rect hb(cursor - 14, r.y + (r.h - 14) / 2, 14, 14);
    icons::heart(c, hb, th.liked, true);
    cursor = hb.x - 8;
  }

  const int textW = std::max(60, cursor - x);
  c.drawTextEllipsis(*a.fSmall, Rect(x, r.y + 8, textW, 20), t->title,
                     playing ? th.accent : th.text);
  const std::string sub = t->artist + (t->album.empty() ? "" : "  ·  " + t->album);
  c.drawTextEllipsis(*a.fTiny, Rect(x, r.y + r.h - 26, textW, 18), sub,
                     th.textDim);
}

void albumTile(App& a, Rect r, const Album& album, bool selected) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;

  Rect art(r.x, r.y, r.w, r.w);
  if (selected) {
    c.dropShadow(art, 12, 16, 110);
  }
  cover(a, art, album.coverId, 12);
  if (selected) c.strokeRoundRect(art, 12, th.accent, 2);

  const int ty = art.bottom() + 8;
  c.drawTextEllipsis(*a.fSmall, Rect(r.x, ty, r.w, 20), album.name,
                     selected ? th.accent : th.text);
  char meta[96];
  std::snprintf(meta, sizeof(meta), "%s · %zu 首", album.artist.c_str(),
                album.trackIndices.size());
  c.drawTextEllipsis(*a.fTiny, Rect(r.x, ty + 22, r.w, 18), meta, th.textDim);
}

void pill(App& a, Rect r, const std::string& text, bool active) {
  Canvas& c = *a.c;
  if (active) {
    c.fillRoundRect(r, r.h / 2, a.th->accent);
    c.drawTextCentered(*a.fTiny, r, text, a.th->bgTop);
  } else {
    c.fillRoundRect(r, r.h / 2, a.th->surfaceAlt);
    c.drawTextCentered(*a.fTiny, r, text, a.th->textDim);
  }
}

void progressBar(App& a, Rect r, double fraction, Color fill, Color back) {
  Canvas& c = *a.c;
  const double f = std::clamp(fraction, 0.0, 1.0);
  c.fillRoundRect(r, r.h / 2, back);
  const int w = int(double(r.w) * f);
  if (w > 0) c.fillRoundRect(Rect(r.x, r.y, w, r.h), r.h / 2, fill);
}

void emptyState(App& a, Rect area, void (*icon)(Canvas&, Rect, Color),
                const std::string& title, const std::string& hint) {
  Canvas& c = *a.c;
  const int cy = area.y + area.h / 2 - 40;

  if (icon) {
    Rect ic(area.x + (area.w - 56) / 2, cy - 70, 56, 56);
    icon(c, ic, a.th->textFaint);
  }
  c.drawTextCentered(*a.fSmall, Rect(area.x, cy, area.w, 24), title,
                     a.th->textDim);
  if (!hint.empty()) {
    c.drawTextCentered(*a.fTiny, Rect(area.x, cy + 28, area.w, 20), hint,
                       a.th->textFaint);
  }
}

Rect heartButton(App& a, Rect r, bool liked) {
  Canvas& c = *a.c;
  c.fillRoundRect(r, r.h / 2,
                  liked ? a.th->liked.withA(48) : a.th->surfaceAlt);
  icons::heart(c, r.inset(r.h / 4), liked ? a.th->liked : a.th->textDim, liked);
  return r;
}

}  // namespace ui
}  // namespace spn