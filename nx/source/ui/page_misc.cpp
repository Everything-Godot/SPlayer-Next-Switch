// SPDX-License-Identifier: AGPL-3.0
// Folders / Stats / Search pages.
#include <algorithm>
#include <cctype>
#include <cstdio>

#include "spn/icons.hpp"
#include "spn/ui.hpp"
#include "spn/widgets.hpp"

namespace spn {

namespace {

std::string baseName(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Case-insensitive substring test. strcasestr is declared but not implemented
// in the bundled compatibility layer, so we roll our own.
bool containsCI(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  if (needle.size() > haystack.size()) return false;
  auto lower = [](char c) {
    return char(std::tolower(static_cast<unsigned char>(c)));
  };
  for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    size_t j = 0;
    for (; j < needle.size(); ++j) {
      if (lower(haystack[i + j]) != lower(needle[j])) break;
    }
    if (j == needle.size()) return true;
  }
  return false;
}

// Horizontal bar used by the stats page.
void barRow(App& a, Rect r, const std::string& label, int value, int maxValue,
            const std::string& trailing) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  c.drawTextEllipsis(*a.fTiny, Rect(r.x, r.y, 170, r.h), label, th.textDim);
  c.drawTextRight(*a.fTiny, Rect(r.x, r.y, r.w, r.h), trailing, th.textFaint);

  Rect track(r.x + 180, r.y + r.h / 2 - 4, std::max(40, r.w - 180 - 70), 8);
  c.fillRoundRect(track, 4, th.surfaceHi);
  if (maxValue > 0 && value > 0) {
    const int w =
        std::max(4, int(double(track.w) * double(value) / double(maxValue)));
    c.fillRoundRect(Rect(track.x, track.y, w, track.h), 4, th.accent);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Folders
// ---------------------------------------------------------------------------
void drawFolders(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  const std::vector<std::string> dirs = a.library.folders();
  int y = body.y + pad;

  char line[96];
  std::snprintf(line, sizeof(line), "%zu 个目录 · %zu 首曲目", dirs.size(),
                a.library.tracks().size());
  c.drawText(*a.fTiny, body.x + pad, y, line, th.textDim);
  c.drawTextRight(*a.fTiny, Rect(body.x + pad, y, body.w - pad * 2, 18),
                  "左摇杆 ↑↓ 选择目录", th.textFaint);
  y += 28;

  if (dirs.empty()) {
    ui::emptyState(
        a, Rect(body.x + pad, y, body.w - pad * 2, body.bottom() - y - pad),
        icons::folder, "还没有音乐目录",
        "把音乐放到 sdmc:/switch/SPlayer-Next/music 下的子文件夹");
    return;
  }

  a.folderSel = std::clamp(a.folderSel, 0, int(dirs.size()) - 1);

  // Left column: folder list. Right column: the selected folder's tracks.
  const int listW = std::min(320, body.w / 3);
  Rect listRect(body.x + pad, y, listW, body.bottom() - y - pad);
  c.fillRoundRect(listRect, 14, th.surface);

  constexpr int rowH = 48;
  const int visible = std::max(1, (listRect.h - 12) / rowH);
  const int first = std::max(0, std::min(a.folderSel, int(dirs.size()) - visible));
  ui::publishRows(a, Rect(listRect.x + 6, listRect.y + 6, listRect.w - 12, rowH),
                  rowH, listRect.w - 12, int(dirs.size()), first, visible);


  for (int i = 0; i < visible; ++i) {
    const int idx = first + i;
    if (idx >= int(dirs.size())) break;
    Rect row(listRect.x + 6, listRect.y + 6 + i * rowH, listRect.w - 12,
             rowH - 4);
    const bool sel = idx == a.folderSel;
    if (sel) {
      c.fillRoundRect(row, 9, th.surfaceAlt);
      c.fillRoundRect(Rect(row.x, row.y + 8, 3, row.h - 16), 2, th.accent);
    }
    icons::folder(c, Rect(row.x + 12, row.y + (row.h - 18) / 2, 18, 18),
                  sel ? th.accent : th.textDim);
    c.drawTextEllipsis(*a.fSmall, Rect(row.x + 40, row.y + (row.h - 20) / 2,
                                       row.w - 52, 20),
                       baseName(dirs[size_t(idx)]), sel ? th.text : th.textDim);
  }

  // Right: tracks inside the selected folder.
  const std::string& dir = dirs[size_t(a.folderSel)];
  std::vector<int> inside;
  for (int i = 0; i < int(a.library.tracks().size()); ++i) {
    const std::string& p = a.library.tracks()[size_t(i)].path;
    if (p.size() > dir.size() && p.compare(0, dir.size(), dir) == 0) {
      inside.push_back(i);
    }
  }

  const int rightX = listRect.right() + 16;
  Rect rightRect(rightX, y, body.right() - pad - rightX, body.bottom() - y - pad);
  c.fillRoundRect(rightRect, 14, th.surface);
  c.drawTextEllipsis(*a.fSmall,
                     Rect(rightRect.x + 16, rightRect.y + 10, rightRect.w - 32, 22),
                     dir, th.textDim);

  if (inside.empty()) {
    c.drawTextCentered(*a.fSmall,
                       Rect(rightRect.x, rightRect.y + 60, rightRect.w, 24),
                       "这个目录里没有曲目", th.textFaint);
    return;
  }

  const int trackTop = rightRect.y + 40;
  const int rows = std::max(1, (rightRect.h - 48) / rowH);
  c.pushClip(Rect(rightRect.x + 4, trackTop, rightRect.w - 8,
                  rightRect.h - (trackTop - rightRect.y) - 6));
  for (int i = 0; i < rows && i < int(inside.size()); ++i) {
    Rect row(rightRect.x + 8, trackTop + i * rowH, rightRect.w - 16, rowH - 4);
    const int idx = inside[size_t(i)];
    ui::trackRow(a, row, idx, idx == a.playingIndex, idx == a.playingIndex, false,
                 i + 1);
  }
  c.popClip();

  if (int(inside.size()) > rows) {
    c.drawTextRight(
        *a.fTiny,
        Rect(rightRect.x, rightRect.bottom() - 24, rightRect.w - 16, 18),
        "+" + std::to_string(inside.size() - rows) + " 首", th.textFaint);
  }
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------
void drawStats(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  int y = body.y + pad;

  // Headline tiles.
  {
    const int gap = 14;
    const int n = 3;
    const int w = (body.w - pad * 2 - gap * (n - 1)) / n;
    const int h = 104;

    struct Tile {
      const char* label;
      std::string value;
      const char* hint;
    };
    const Tile tiles[3] = {
        {"曲库总时长",
         formatLongDuration(uint64_t(a.library.totalDurationSec())), "全部曲目"},
        {"累计收听", formatLongDuration(a.store.listeningSeconds()), "本次及历史"},
        {"播放次数", std::to_string(a.store.totalPlayCount()), "重复播放累计"},
    };

    for (int i = 0; i < n; ++i) {
      Rect r(body.x + pad + i * (w + gap), y, w, h);
      c.fillRoundRect(r, 14, th.surface);
      c.drawText(*a.fTiny, r.x + 16, r.y + 14, tiles[i].label, th.textFaint);
      c.drawTextEllipsis(*a.fTitle, Rect(r.x + 16, r.y + 38, r.w - 32, 36),
                         tiles[i].value, th.text);
      c.drawText(*a.fTiny, r.x + 16, r.bottom() - 28, tiles[i].hint,
                 th.textFaint);
    }
    y += h + 22;
  }

  // Format distribution.
  {
    int counts[4] = {0, 0, 0, 0};
    const char* names[4] = {"MP3", "FLAC", "WAV", "其他"};
    int maxCount = 1;
    for (const Track& t : a.library.tracks()) {
      int slot = 3;
      if (t.format == "MP3") slot = 0;
      else if (t.format == "FLAC") slot = 1;
      else if (t.format == "WAV") slot = 2;
      ++counts[slot];
      maxCount = std::max(maxCount, counts[slot]);
    }

    ui::sectionHeader(a, body.x + pad, y, body.w - pad * 2, "格式分布", "");
    y += 34;

    Rect panel(body.x + pad, y, body.w - pad * 2, 4 * 34 + 20);
    if (panel.bottom() < body.bottom()) {
      c.fillRoundRect(panel, 14, th.surface);
      for (int i = 0; i < 4; ++i) {
        Rect row(panel.x + 16, panel.y + 10 + i * 34, panel.w - 32, 24);
        char trailing[32];
        std::snprintf(trailing, sizeof(trailing), "%d", counts[i]);
        barRow(a, row, names[i], counts[i], maxCount, trailing);
      }
      y = panel.bottom() + 20;
    }
  }

  // Top artists by track count.
  const auto& artists = a.library.artists();
  if (!artists.empty() && y + 80 < body.bottom()) {
    ui::sectionHeader(a, body.x + pad, y, body.w - pad * 2, "曲目最多的艺术家",
                      "Top 5");
    y += 34;

    std::vector<const ArtistEntry*> top;
    top.reserve(artists.size());
    for (const ArtistEntry& e : artists) top.push_back(&e);
    std::sort(top.begin(), top.end(),
              [](const ArtistEntry* l, const ArtistEntry* r) {
                return l->trackIndices.size() > r->trackIndices.size();
              });

    const int n = std::min<int>(5, int(top.size()));
    const int maxCount =
        std::max<int>(1, int(top[0]->trackIndices.size()));

    Rect panel(body.x + pad, y, body.w - pad * 2, n * 34 + 20);
    if (panel.bottom() < body.bottom()) {
      c.fillRoundRect(panel, 14, th.surface);
      for (int i = 0; i < n; ++i) {
        const ArtistEntry* e = top[size_t(i)];
        Rect row(panel.x + 16, panel.y + 10 + i * 34, panel.w - 32, 24);
        barRow(a, row, e->name, int(e->trackIndices.size()), maxCount,
               std::to_string(e->trackIndices.size()));
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------
std::vector<int> searchResults(App& a) {
  const std::string& q = a.searchQuery;
  std::vector<int> hits;
  for (int i = 0; i < int(a.library.tracks().size()); ++i) {
    const Track& t = a.library.tracks()[size_t(i)];
    if (q.empty() || containsCI(t.title, q) || containsCI(t.artist, q) ||
        containsCI(t.album, q) || containsCI(t.genre, q)) {
      hits.push_back(i);
    }
  }
  return hits;
}

void drawSearch(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  int y = body.y + pad;

  // Query field.
  Rect field(body.x + pad, y, body.w - pad * 2, 52);
  c.fillRoundRect(field, 14, th.surface);
  icons::search(c, Rect(field.x + 16, field.y + 14, 24, 24), th.textDim);

  if (a.searchQuery.empty()) {
    c.drawText(*a.fBody, field.x + 52, field.y + 14, "按 + 输入关键字…",
               th.textFaint);
  } else {
    c.drawTextEllipsis(*a.fBody,
                       Rect(field.x + 52, field.y + 14, field.w - 160, 26),
                       a.searchQuery, th.text);
  }
  {
    const int cw = c.measureText(*a.fTiny, "Minus 清除") + 24;
    Rect chip(field.right() - cw - 14, field.y + 12, cw, 28);
    ui::badge(a, chip, "Minus 清除", th.surfaceHi, th.textDim);
  }

  y = field.bottom() + 16;

  const std::vector<int> hits = searchResults(a);
  char line[96];
  std::snprintf(line, sizeof(line), "%zu 个结果", hits.size());
  c.drawText(*a.fTiny, body.x + pad, y, line, th.textDim);
  c.drawTextRight(*a.fTiny, Rect(body.x + pad, y, body.w - pad * 2, 18),
                  "A 播放 · Y 收藏", th.textFaint);
  y += 26;

  Rect content(body.x + pad, y, body.w - pad * 2, body.bottom() - y - pad);
  if (hits.empty()) {
    ui::emptyState(a, content, icons::search, "没有匹配的曲目",
                   "试试艺术家、专辑或流派关键字");
    return;
  }

  constexpr int rowH = 58;
  const int visible = std::max(1, content.h / rowH);
  a.searchSel = std::clamp(a.searchSel, 0, int(hits.size()) - 1);
  const int first = std::max(0, std::min(a.searchSel, int(hits.size()) - visible));

  c.fillRoundRect(content, 14, th.surface);
  c.pushClip(Rect(content.x + 4, content.y + 6, content.w - 8, content.h - 12));
  ui::publishRows(a, Rect(content.x + 6, content.y + 6, content.w - 12, rowH),
                  rowH, content.w - 12, int(hits.size()), first, visible);

  for (int i = 0; i < visible; ++i) {
    const int slot = first + i;
    if (slot >= int(hits.size())) break;
    Rect row(content.x + 6, content.y + 6 + i * rowH, content.w - 12, rowH - 4);
    ui::trackRow(a, row, hits[size_t(slot)], slot == a.searchSel,
                 hits[size_t(slot)] == a.playingIndex, true, slot + 1);
  }
  c.popClip();
}

}  // namespace spn