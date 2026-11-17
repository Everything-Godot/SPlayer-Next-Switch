// SPDX-License-Identifier: AGPL-3.0
// Library page: tabs (songs / albums / artists) + scrollable lists.
#include <algorithm>
#include <cstdio>

#include "spn/icons.hpp"
#include "spn/ui.hpp"
#include "spn/widgets.hpp"

namespace spn {
namespace {

// Which tab the library page is showing. Kept as a file-static because it is
// pure view state and never persisted.
int gLibTab = 0;  // 0 songs, 1 albums, 2 artists

const char* kTabNames[] = {"歌曲", "专辑", "艺术家"};

// Draws a vertical scrollbar gutter on the right of `area`.
void scrollbar(App& a, Rect area, int first, int total, int visible) {
  if (total <= visible) return;
  Canvas& c = *a.c;
  Rect rail(area.right() - 6, area.y + 4, 4, area.h - 8);
  c.fillRoundRect(rail, 2, a.th->surfaceHi);
  const int h = std::max(24, int(double(rail.h) * double(visible) / double(total)));
  const int maxFirst = std::max(1, total - visible);
  const int y = rail.y + int(double(rail.h - h) * double(first) / double(maxFirst));
  c.fillRoundRect(Rect(rail.x, y, rail.w, h), 2, a.th->textFaint);
}

}  // namespace

int libraryTab() { return gLibTab; }
void setLibraryTab(int t) { gLibTab = std::clamp(t, 0, 2); }

void drawLibrary(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  // --- tab bar --------------------------------------------------------------
  int y = body.y + pad;
  {
    int x = body.x + pad;
    for (int i = 0; i < 3; ++i) {
      const int w = c.measureText(*a.fSmall, kTabNames[i]) + 36;
      Rect r(x, y, w, 36);
      ui::pill(a, r, kTabNames[i], i == gLibTab);
      x = r.right() + 10;
    }

    // Library root + rescan hint on the right.
    const std::string hint = "ZR 重新扫描";
    c.drawTextRight(*a.fTiny, Rect(body.x + pad, y, body.w - pad * 2, 36), hint,
                    th.textFaint);
    y += 48;
  }

  Rect content(body.x + pad, y, body.w - pad * 2, body.bottom() - y - pad);

  // --- songs ---------------------------------------------------------------
  if (gLibTab == 0) {
    const auto& tracks = a.library.tracks();
    if (tracks.empty()) {
      ui::emptyState(a, content, icons::disc, "曲库还是空的",
                     "把 MP3 / FLAC / WAV 放进 sdmc:/switch/SPlayer-Next/music");
      return;
    }

    constexpr int rowH = 58;
    const int visible = std::max(1, content.h / rowH);
    a.libSel = std::clamp(a.libSel, 0, int(tracks.size()) - 1);

    if (a.libSel < a.libScroll) a.libScroll = a.libSel;
    if (a.libSel >= a.libScroll + visible) a.libScroll = a.libSel - visible + 1;
    a.libScroll = std::clamp(a.libScroll, 0,
                             std::max(0, int(tracks.size()) - visible));

    Rect panel = content;
    c.fillRoundRect(panel, 14, th.surface);
    c.pushClip(Rect(panel.x + 4, panel.y + 6, panel.w - 8, panel.h - 12));

    for (int i = 0; i < visible; ++i) {
      const int idx = a.libScroll + i;
      if (idx >= int(tracks.size())) break;
      Rect row(panel.x + 6, panel.y + 6 + i * rowH, panel.w - 12, rowH - 4);
      ui::trackRow(a, row, idx, idx == a.libSel, idx == a.playingIndex, true,
                   idx + 1);
    }
    c.popClip();
    scrollbar(a, panel, a.libScroll, int(tracks.size()), visible);
    return;
  }

  // --- albums --------------------------------------------------------------
  if (gLibTab == 1) {
    const auto& albums = a.library.albums();
    if (albums.empty()) {
      ui::emptyState(a, content, icons::note, "还没有专辑",
                     "带有专辑标签的曲目会自动分组");
      return;
    }
    const int tileMin = 150;
    const int gap = 16;
    const int cols = std::max(1, (content.w) / (tileMin + gap));
    const int tileW = (content.w - gap * (cols - 1)) / cols;
    const int rows = std::max(1, content.h / (tileW + 56));
    const int perPage = cols * rows;

    a.albumSel = std::clamp(a.albumSel, 0, int(albums.size()) - 1);
    const int page = a.albumSel / perPage;
    const int first = page * perPage;
    const int total = std::min<int>(perPage, int(albums.size()) - first);

    for (int i = 0; i < total; ++i) {
      const int cx = i % cols;
      const int cy = i / cols;
      Rect tile(content.x + cx * (tileW + gap), content.y + cy * (tileW + 56),
                tileW, tileW + 48);
      ui::albumTile(a, tile, albums[size_t(first + i)], first + i == a.albumSel);
    }
    return;
  }

  // --- artists -------------------------------------------------------------
  {
    const auto& artists = a.library.artists();
    if (artists.empty()) {
      ui::emptyState(a, content, icons::disc, "还没有艺术家", "曲目艺术家标签为空");
      return;
    }
    constexpr int rowH = 64;
    const int visible = std::max(1, content.h / rowH);
    a.albumSel = std::clamp(a.albumSel, 0, int(artists.size()) - 1);
    const int first = std::max(0, std::min(a.albumSel, int(artists.size()) - visible));

    for (int i = 0; i < visible; ++i) {
      const int idx = first + i;
      if (idx >= int(artists.size())) break;
      const ArtistEntry& e = artists[size_t(idx)];
      Rect row(content.x, content.y + i * rowH, content.w, rowH - 6);

      if (idx == a.albumSel) {
        c.fillRoundRect(row, 10, th.surfaceAlt);
        c.fillRoundRect(Rect(row.x, row.y + 8, 3, row.h - 16), 2, th.accent);
      }

      // Circular avatar with the first character.
      const int cy = row.y + row.h / 2;
      c.fillCircle(row.x + 34, cy, 22, th.surfaceHi);
      std::string initial;
      size_t bi = 0;
      uint32_t cp = 0;
      if (utf8Decode(e.name, bi, cp) > 0 && cp) {
        // Re-encode the single codepoint.
        if (cp < 0x80) initial.push_back(char(cp));
        else if (cp < 0x800) {
          initial.push_back(char(0xC0 | (cp >> 6)));
          initial.push_back(char(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
          initial.push_back(char(0xE0 | (cp >> 12)));
          initial.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
          initial.push_back(char(0x80 | (cp & 0x3F)));
        } else {
          initial.push_back(char(0xF0 | (cp >> 18)));
          initial.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
          initial.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
          initial.push_back(char(0x80 | (cp & 0x3F)));
        }
      }
      c.drawTextCentered(*a.fBody, Rect(row.x + 12, cy - 16, 44, 32), initial,
                         th.accent);

      c.drawTextEllipsis(*a.fSmall, Rect(row.x + 70, row.y + 10, row.w - 260, 22),
                         e.name, th.text);
      char meta[96];
      std::snprintf(meta, sizeof(meta), "%zu 首 · %zu 张专辑",
                    e.trackIndices.size(), e.albums.size());
      c.drawTextEllipsis(*a.fTiny, Rect(row.x + 70, row.y + 32, row.w - 260, 18),
                         meta, th.textDim);
    }
    scrollbar(a, content, first, int(artists.size()), visible);
  }
}

}  // namespace spn