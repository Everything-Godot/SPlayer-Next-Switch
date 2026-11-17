// SPDX-License-Identifier: AGPL-3.0
// Liked / History / Downloads / Favorites pages.
#include <algorithm>
#include <cstdio>

#include "spn/icons.hpp"
#include "spn/ui.hpp"
#include "spn/widgets.hpp"

namespace spn {
namespace {

constexpr int kRowH = 58;

// `heart` / `star` take an extra `filled` flag, so they cannot be passed
// straight through as plain `void(Canvas&, Rect, Color)` callbacks.
void heartGlyph(Canvas& c, Rect r, Color col) { icons::heart(c, r, col, true); }
void starGlyph(Canvas& c, Rect r, Color col) { icons::star(c, r, col, true); }

// Shared scroll list used by the three track-based pages.
void trackList(App& a, Rect content, const std::vector<int>& indices, int& sel,
               int& scroll, const char* emptyTitle, const char* emptyHint,
               void (*icon)(Canvas&, Rect, Color)) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;

  if (indices.empty()) {
    ui::emptyState(a, content, icon, emptyTitle, emptyHint);
    return;
  }

  const int visible = std::max(1, content.h / kRowH);
  sel = std::clamp(sel, 0, int(indices.size()) - 1);

  if (sel < scroll) scroll = sel;
  if (sel >= scroll + visible) scroll = sel - visible + 1;
  scroll = std::clamp(scroll, 0, std::max(0, int(indices.size()) - visible));

  c.fillRoundRect(content, 14, th.surface);
  c.pushClip(Rect(content.x + 4, content.y + 6, content.w - 8, content.h - 12));
  for (int i = 0; i < visible; ++i) {
    const int slot = scroll + i;
    if (slot >= int(indices.size())) break;
    const int trackIdx = indices[size_t(slot)];
    Rect row(content.x + 6, content.y + 6 + i * kRowH, content.w - 12, kRowH - 4);
    ui::trackRow(a, row, trackIdx, slot == sel, trackIdx == a.playingIndex, true,
                 slot + 1);
  }
  c.popClip();

  // Scrollbar.
  if (int(indices.size()) > visible) {
    Rect rail(content.right() - 8, content.y + 6, 4, content.h - 12);
    c.fillRoundRect(rail, 2, th.surfaceHi);
    const int h = std::max(24, int(double(rail.h) * visible / indices.size()));
    const int maxScroll = std::max(1, int(indices.size()) - visible);
    const int y =
        rail.y + int(double(rail.h - h) * double(scroll) / double(maxScroll));
    c.fillRoundRect(Rect(rail.x, y, rail.w, h), 2, th.textFaint);
  }
}

// A one-line summary header chip.
void summaryRow(App& a, Rect body, int& y, const std::string& left,
                const std::string& right) {
  Canvas& c = *a.c;
  const int pad = uiContentPad();
  c.drawText(*a.fTiny, body.x + pad, y, left, a.th->textDim);
  c.drawTextRight(*a.fTiny, Rect(body.x + pad, y, body.w - pad * 2, 18), right,
                  a.th->textFaint);
  y += 26;
}

}  // namespace

// ---------------------------------------------------------------------------
// Liked
// ---------------------------------------------------------------------------
void drawLiked(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  std::vector<int> indices;
  for (const std::string& p : a.store.likedPaths()) {
    const int idx = a.library.indexOf(p);
    if (idx >= 0) indices.push_back(idx);
  }

  int y = body.y + pad;

  // Header card with the heart motif.
  Rect head(body.x + pad, y, body.w - pad * 2, 108);
  c.fillRoundRect(head, 16, th.surface);
  c.pushClip(head);
  const Color pink = th.liked;
  for (int i = 0; i < 108; ++i) {
    const float f = 1.f - float(i) / 108.f;
    c.fillRectAlpha(Rect(head.x, head.y + i, head.w, 1), pink,
                    uint8_t(std::clamp(int(70.f * f), 0, 255)));
  }
  c.popClip();

  c.fillCircle(head.x + 62, head.y + 54, 34, th.liked.withA(70));
  icons::heart(c, Rect(head.x + 62 - 16, head.y + 54 - 16, 32, 32), th.liked,
               true);

  c.drawText(*a.fTitle, head.x + 116, head.y + 24, "我喜欢的音乐", th.text);
  char meta[128];
  std::snprintf(meta, sizeof(meta), "%zu 首 · 累计时长 %s", indices.size(),
                formatLongDuration(uint64_t(a.library.totalDurationSec())).c_str());
  c.drawTextEllipsis(*a.fSmall, Rect(head.x + 118, head.y + 66, head.w - 150, 24),
                     meta, th.textDim);

  y = head.bottom() + 18;
  summaryRow(a, body, y, "已收藏曲目", "Y 取消收藏");

  Rect content(body.x + pad, y, body.w - pad * 2, body.bottom() - y - pad);
  trackList(a, content, indices, a.likedSel, a.likedScroll, "还没有喜欢的曲子",
            "在音乐库里按 Y 把喜欢的曲子收进来", heartGlyph);
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------
void drawHistory(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  const auto& hist = a.store.history();
  std::vector<int> indices;
  indices.reserve(hist.size());
  for (const HistoryEntry& h : hist) indices.push_back(a.library.indexOf(h.path));

  // Only tracks still present are selectable.
  std::vector<int> playable;
  for (int idx : indices) {
    if (idx >= 0) playable.push_back(idx);
  }

  int y = body.y + pad;
  char line[128];
  std::snprintf(line, sizeof(line), "%zu 条记录 · 总播放 %zu 次", hist.size(),
                a.store.totalPlayCount());
  summaryRow(a, body, y, line, "Minus 清空历史");

  Rect content(body.x + pad, y, body.w - pad * 2, body.bottom() - y - pad);

  if (hist.empty()) {
    ui::emptyState(a, content, icons::clock, "还没有播放记录",
                   "播放过的曲子会按时间倒序出现在这里");
    return;
  }

  // Render the raw history (including unavailable files) with relative time.
  const int visible = std::max(1, content.h / (kRowH + 6));
  a.histSel = std::clamp(a.histSel, 0, int(hist.size()) - 1);
  if (a.histSel < a.histScroll) a.histScroll = a.histSel;
  if (a.histSel >= a.histScroll + visible) a.histScroll = a.histSel - visible + 1;
  a.histScroll =
      std::clamp(a.histScroll, 0, std::max(0, int(hist.size()) - visible));

  c.fillRoundRect(content, 14, th.surface);
  c.pushClip(Rect(content.x + 4, content.y + 6, content.w - 8, content.h - 12));

  for (int i = 0; i < visible; ++i) {
    const int slot = a.histScroll + i;
    if (slot >= int(hist.size())) break;
    const HistoryEntry& h = hist[size_t(slot)];
    const int idx = a.library.indexOf(h.path);
    Rect row(content.x + 6, content.y + 6 + i * (kRowH + 6), content.w - 12,
             kRowH - 4);

    if (idx >= 0) {
      ui::trackRow(a, row, idx, slot == a.histSel, idx == a.playingIndex, true,
                   slot + 1);

      // Play count + timestamp on the right.
      char when[64];
      const uint64_t delta = a.now > double(h.playedAt)
                                 ? uint64_t(a.now) - h.playedAt
                                 : 0;
      if (delta < 60) std::snprintf(when, sizeof(when), "刚刚");
      else if (delta < 3600)
        std::snprintf(when, sizeof(when), "%llu 分钟前",
                      static_cast<unsigned long long>(delta / 60));
      else if (delta < 86400)
        std::snprintf(when, sizeof(when), "%llu 小时前",
                      static_cast<unsigned long long>(delta / 3600));
      else
        std::snprintf(when, sizeof(when), "%llu 天前",
                      static_cast<unsigned long long>(delta / 86400));

      char meta[96];
      std::snprintf(meta, sizeof(meta), "%s · %u 次", when, h.playCount);
      c.drawTextRight(*a.fTiny, Rect(row.x, row.y, row.w - 190, row.h), meta,
                      th.textFaint);
    } else {
      if (slot == a.histSel) c.fillRoundRect(row, 10, th.surfaceAlt);
      const size_t slash = h.path.find_last_of('/');
      const std::string name =
          slash == std::string::npos ? h.path : h.path.substr(slash + 1);
      ui::cover(a, Rect(row.x + 14, row.y + 6, 44, 44), -1, 8);
      c.drawTextEllipsis(*a.fSmall, Rect(row.x + 70, row.y + 8, row.w - 220, 20),
                         name, th.textDim);
      c.drawText(*a.fTiny, row.x + 70, row.y + 30, "文件已不存在", th.danger);
    }
  }
  c.popClip();
  (void)playable;
}

// ---------------------------------------------------------------------------
// Downloads — the Switch app is fully offline, so this mirrors "local cache".
// ---------------------------------------------------------------------------
void drawDownloads(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  int y = body.y + pad;

  // Storage summary card.
  Rect card(body.x + pad, y, body.w - pad * 2, 116);
  c.fillRoundRect(card, 16, th.surface);

  Rect ic(card.x + 24, card.y + 26, 44, 44);
  c.fillCircle(ic.x + 22, ic.y + 22, 22, th.accent.withA(48));
  icons::download(c, ic.inset(11), th.accent);

  c.drawText(*a.fTitle, ic.right() + 20, card.y + 20, "本地音源", th.text);
  char line[160];
  std::snprintf(line, sizeof(line), "%zu 首曲目 · 共 %s",
                a.library.tracks().size(),
                humanSize(a.library.totalBytes()).c_str());
  c.drawText(*a.fSmall, ic.right() + 22, card.y + 58, line, th.textDim);

  const std::string root = a.library.root().empty() ? "（未找到音乐目录）"
                                                    : a.library.root();
  c.drawTextEllipsis(*a.fTiny, Rect(ic.right() + 22, card.y + 84,
                                    card.w - 220, 18),
                     "扫描路径：" + root, th.textFaint);

  // Format breakdown chips on the right.
  {
    int mp3 = 0, flac = 0, wav = 0, other = 0;
    for (const Track& t : a.library.tracks()) {
      if (t.format == "MP3") ++mp3;
      else if (t.format == "FLAC") ++flac;
      else if (t.format == "WAV") ++wav;
      else ++other;
    }
    struct Chip {
      const char* name;
      int count;
    };
    const Chip chips[4] = {{"MP3", mp3}, {"FLAC", flac}, {"WAV", wav},
                           {"其他", other}};
    int x = card.right() - 24;
    for (int i = 3; i >= 0; --i) {
      char b[48];
      std::snprintf(b, sizeof(b), "%s %d", chips[i].name, chips[i].count);
      const int w = c.measureText(*a.fTiny, b) + 22;
      Rect cb(x - w, card.y + 44, w, 28);
      ui::badge(a, cb, b, th.surfaceHi, th.textDim);
      x = cb.x - 8;
    }
  }

  y = card.bottom() + 20;
  summaryRow(a, body, y, "已缓存曲目", "离线播放，无需联网");

  // Reuse the library list; downloads == everything on the SD card.
  std::vector<int> all;
  for (int i = 0; i < int(a.library.tracks().size()); ++i) all.push_back(i);
  Rect content(body.x + pad, y, body.w - pad * 2, body.bottom() - y - pad);

  if (all.empty()) {
    ui::emptyState(a, content, icons::download, "没有可离线播放的曲目",
                   "把音乐文件放进 sdmc:/switch/SPlayer-Next/music");
    return;
  }
  trackList(a, content, all, a.libSel, a.libScroll, "", "", icons::download);
}

// ---------------------------------------------------------------------------
// Favorites — saved playlists.
// ---------------------------------------------------------------------------
void drawFavorites(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  const auto& lists = a.store.playlists();
  int y = body.y + pad;

  summaryRow(a, body, y, "自建歌单", "游戏内暂不支持新建");

  if (lists.empty()) {
    Rect content(body.x + pad, y, body.w - pad * 2, body.bottom() - y - pad);
    ui::emptyState(a, content, starGlyph, "还没有歌单",
                   "桌面版创建的收藏夹会显示在这里");
    return;
  }

  constexpr int tileH = 116;
  for (int i = 0; i < int(lists.size()); ++i) {
    if (y + tileH > body.bottom() - pad) break;
    const SavedPlaylist& p = lists[size_t(i)];
    Rect card(body.x + pad, y, body.w - pad * 2, tileH - 12);
    const bool sel = i == a.folderSel;
    c.fillRoundRect(card, 14, sel ? th.surfaceAlt : th.surface);
    if (sel) c.strokeRoundRect(card, 14, th.accent, 2);

    Rect ic(card.x + 18, card.y + 20, 60, 60);
    c.fillRoundRect(ic, 12, th.accentSoft);
    icons::queue(c, ic.inset(16), th.accent);

    c.drawTextEllipsis(*a.fSmall, Rect(ic.right() + 16, card.y + 22,
                                       card.w - 220, 22),
                       p.name, th.text);
    char meta[96];
    std::snprintf(meta, sizeof(meta), "%zu 首", p.paths.size());
    c.drawText(*a.fTiny, ic.right() + 18, card.y + 50, meta, th.textDim);

    // Album covers preview strip.
    int previewX = card.right() - 250;
    int shown = 0;
    for (const std::string& path : p.paths) {
      if (shown >= 3) break;
      const int idx = a.library.indexOf(path);
      if (idx < 0) continue;
      const Track* t = a.trackAt(idx);
      ui::cover(a, Rect(previewX, card.y + 22, 56, 56), t ? t->coverId : -1, 8);
      previewX += 62;
      ++shown;
    }
    y += tileH;
  }
}

}  // namespace spn