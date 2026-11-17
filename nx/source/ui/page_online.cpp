// SPDX-License-Identifier: AGPL-3.0
// Online pages: the QR login card, the NetEase result list, and the two
// sidebar entries the desktop app calls "云盘" and "流媒体".
//
// The layout is a deliberate 1:1 port of the desktop components, because the
// user asked for the same arrangement rather than "something that works":
//   * modals/QrLoginPanel.vue  -> drawLoginCard()
//   * pages/Search.vue         -> drawStreaming() result list
// The metrics that matter are carried over verbatim and commented where the
// web units (size-40, p-3, rounded-2xl) do not translate to pixels directly.
#include <algorithm>
#include <cstdio>

#include "spn/icons.hpp"
#include "spn/online_service.hpp"
#include "spn/qrcode.hpp"
#include "spn/ui.hpp"
#include "spn/widgets.hpp"

namespace spn {
namespace {

// --- desktop -> pixel metric conversion -------------------------------------
// Tailwind size-40 is 10rem == 160 px at the default root size, and the desktop
// window is 1280x720 logical too, so the numbers carry across unchanged.
constexpr int kQrPanel = 160;   // size-40
constexpr int kQrPad = 12;      // p-3
constexpr int kQrModule = 8;    // pixelSize: 8 in the uqr call
constexpr int kQrRadius = 16;   // rounded-2xl
constexpr int kQrQuiet = 2;     // border: 0 in uqr, so we supply our own
// Draws the QR matrix into `box`, module-scaled and centred.
void drawQrMatrix(Canvas& c, const qr::Code& code, Rect box, Color dark,
                  Color light) {
  c.fillRect(box, light);
  if (code.size <= 0) return;

  const int avail = std::min(box.w, box.h) - kQrQuiet * 2 * kQrModule;
  if (avail <= 0) return;

  // Integer scale keeps modules crisp; fractional scaling would blur the
  // finder patterns and make the code hard to scan on a TV-mounted console.
  int scale = avail / code.size;
  if (scale < 1) scale = 1;

  const int total = code.size * scale;
  const int ox = box.x + (box.w - total) / 2;
  const int oy = box.y + (box.h - total) / 2;

  for (int y = 0; y < code.size; ++y) {
    for (int x = 0; x < code.size; ++x) {
      if (!code.at(x, y)) continue;
      c.fillRect(Rect(ox + x * scale, oy + y * scale, scale, scale), dark);
    }
  }
}

// Status line under the card, matching the four desktop states.
std::string loginTip(online::LoginState st, bool busy) {
  switch (st) {
    case online::LoginState::LoggedOut:
      return busy ? "正在获取二维码…" : "按 A 获取二维码";
    case online::LoginState::WaitingQr:
      return "用网易云音乐 App 扫码登录";
    case online::LoginState::Scanned:
      return "已扫码，请在手机上确认";
    case online::LoginState::LoggedIn:
      return "已登录";
  }
  return "";
}

}  // namespace

// Public wrapper: the same pixels the login card draws, but callable from the
// rasteriser test so zbar can confirm the on-screen image actually scans.
void drawQrCode(Canvas& c, const qr::Code& code, Rect box) {
  drawQrMatrix(c, code, box, Color::rgb(0x000000), Color::rgb(0xFFFFFF));
}

// ---------------------------------------------------------------------------
// QR login card (desktop: modals/QrLoginPanel.vue)
// ---------------------------------------------------------------------------
void drawLoginCard(App& a, Rect area) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  if (!a.on) return;

  const online::LoginState st = a.on->loginState();

  // Logged in: the desktop hides the QR entirely and shows the account row.
  if (st == online::LoginState::LoggedIn) {
    const int cy = area.y + area.h / 2;
    Rect card(area.x + (area.w - 360) / 2, cy - 66, 360, 132);
    c.fillRoundRect(card, 16, th.surface);
    c.strokeRoundRect(card, 16, th.border, 1);

    // Avatar stand-in: a circle with the first glyph of the nickname, which is
    // what the desktop shows when the remote image has not been fetched yet.
    const int av = 48;
    c.fillCircle(card.x + 36 + av / 2, card.y + 40 + av / 2, av / 2, th.accentSoft);
    std::string initial = "?";
    {
      const std::string& nick = a.on->nickname();
      if (!nick.empty()) {
        size_t i = 0;
        uint32_t cp = 0;
        if (utf8Decode(nick, i, cp) > 0 && cp) {
          initial.clear();
          if (cp < 0x80) {
            initial.push_back(char(cp));
          } else if (cp < 0x800) {
            initial.push_back(char(0xC0 | (cp >> 6)));
            initial.push_back(char(0x80 | (cp & 0x3F)));
          } else {
            initial.push_back(char(0xE0 | (cp >> 12)));
            initial.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            initial.push_back(char(0x80 | (cp & 0x3F)));
          }
        }
      }
    }
    c.drawTextCentered(*a.fTitle, Rect(card.x + 36, card.y + 40, av, av), initial,
                       th.bgTop);
    c.drawTextEllipsis(*a.fBody, Rect(card.x + 100, card.y + 34, 220, 26),
                       a.on->nickname().empty() ? "网易云用户" : a.on->nickname(),
                       th.text);
    c.drawText(*a.fTiny, card.x + 100, card.y + 62, "已连接网易云音乐", th.textDim);
    c.drawText(*a.fTiny, card.x + 100, card.y + 88, "按 A 读取云盘曲目", th.textFaint);
    return;
  }

  // The card itself.
  const int cardW = kQrPanel + kQrPad * 2;
  Rect card(area.x + (area.w - cardW) / 2, area.y + 46, cardW, cardW);
  c.fillRoundRect(card, kQrRadius, Color::rgb(0xFFFFFF));
  c.strokeRoundRect(card, kQrRadius, th.border, 1);

  const Rect inner = card.inset(kQrPad);

  if (a.on->loginUrl().empty()) {
    // Nothing to scan yet: same footprint, so the panel does not jump when the
    // code arrives.
    c.fillRect(inner, Color::rgb(0xF4F4F5));
    if (a.on->busy()) {
      c.drawTextCentered(*a.fSmall, inner, "获取中…", Color::rgb(0x9CA3AF));
    } else {
      icons::search(c, Rect(inner.x + inner.w / 2 - 16,
                            inner.y + inner.h / 2 - 30, 32, 32),
                    Color::rgb(0xC4C7CE));
      c.drawTextCentered(*a.fSmall, Rect(inner.x, inner.y + inner.h / 2 + 10,
                                        inner.w, 24),
                         "按 A 获取", Color::rgb(0x9CA3AF));
    }
  } else {
    qr::Code code;
    if (qr::encode(a.on->loginUrl(), code)) {
      drawQrCode(c, code, inner);
    } else {
      c.drawTextCentered(*a.fSmall, inner, "二维码生成失败",
                         Color::rgb(0x9CA3AF));
    }

    // state === "scanned": the desktop dims and blurs the QR and puts the
    // avatar + nickname in the middle. Blur is not available here, so the QR is
    // covered by a translucent white sheet -- same read, one less shader.
    if (st == online::LoginState::Scanned) {
      c.fillRectAlpha(inner, Color::rgb(0xFFFFFF), 205);
      const int av = 48;  // size-12
      const int cx = inner.x + inner.w / 2;
      const int cy = inner.y + inner.h / 2;
      c.fillCircle(cx, cy - 8, av / 2, th.accentSoft);
      c.drawTextCentered(*a.fTitle, Rect(cx - av / 2, cy - 8 - av / 2, av, av),
                         "✓", th.bgTop);
      c.drawTextCentered(*a.fSmall, Rect(inner.x, cy + 24, inner.w, 22),
                         a.on->nickname().empty() ? "已扫码" : a.on->nickname(),
                         Color::rgb(0x111827));
    }
  }

  // Status line, text-xs below the card.
  const std::string tip = loginTip(st, a.on->busy());
  c.drawTextCentered(*a.fSmall,
                     Rect(area.x, card.bottom() + 14, area.w, 22), tip,
                     st == online::LoginState::Scanned ? th.accent : th.textDim);

  // Error goes below the tip; the desktop panel has no room, the console does.
  if (!a.on->error().empty()) {
    c.drawTextEllipsisCentered(*a.fTiny,
                               Rect(area.x + 40, card.bottom() + 40,
                                    area.w - 80, 20),
                               a.on->error(), th.danger);
  }
}

// ---------------------------------------------------------------------------
// Cloud drive (desktop: /cloud) — the user's *uploaded* songs.
//
// This page used to draw the playlist list, which is what made the sidebar read
// as if playlists lived in the cloud drive. Playlists belong to 我的收藏.
// ---------------------------------------------------------------------------
void drawCloud(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  if (!a.on) {
    ui::emptyState(a, body, icons::cloud, "在线功能未初始化", "");
    return;
  }

  if (!a.on->loggedIn()) {
    drawLoginCard(a, Rect(body.x, body.y, body.w, body.h));
    return;
  }

  int y = body.y + pad;
  const Rect head(body.x + pad, y, body.w - pad * 2, 34);
  ui::sectionHeader(a, head.x, head.y, head.w, "云盘",
                    a.on->busy() ? online::jobName(a.on->currentJob()) : "Y 刷新");
  y = head.bottom() + 10;

  const auto& songs = a.on->cloudSongs();
  Rect content(body.x + pad, y, body.w - pad * 2, body.bottom() - y - pad);

  if (songs.empty()) {
    if (a.on->busy() && a.on->currentJob() == online::Job::CloudList) {
      c.drawTextCentered(*a.fSmall, content, "正在读取云盘…", th.textDim);
    } else {
      ui::emptyState(a, content, icons::cloud, "云盘是空的",
                     "按 Y 读取，或先在手机端上传音乐");
    }
    return;
  }

  constexpr int rowH = kTrackRowH;
  const int visible = std::max(1, content.h / rowH);
  a.cloudSel = std::clamp(a.cloudSel, 0, int(songs.size()) - 1);

  if (a.cloudSel < a.cloudScroll) a.cloudScroll = a.cloudSel;
  if (a.cloudSel >= a.cloudScroll + visible) a.cloudScroll = a.cloudSel - visible + 1;
  a.cloudScroll = std::clamp(a.cloudScroll, 0, std::max(0, int(songs.size()) - visible));

  Rect panel = content;
  c.fillRoundRect(panel, 14, th.surface);
  c.pushClip(Rect(panel.x + 4, panel.y + 6, panel.w - 8, panel.h - 12));
  ui::publishRows(a, Rect(panel.x + 6, panel.y + 6, panel.w - 12, rowH), rowH,
                  panel.w - 12, int(songs.size()), a.cloudScroll, visible);

  for (int i = 0; i < visible; ++i) {
    const int idx = a.cloudScroll + i;
    if (idx >= int(songs.size())) break;
    const netease::Song& s = songs[size_t(idx)];
    Rect row(panel.x + 6, panel.y + 6 + i * rowH, panel.w - 12, rowH - 4);
    const bool sel = idx == a.cloudSel;
    if (sel) c.fillRoundRect(row, 10, th.surfaceAlt);

    if (idx == a.on->pendingSong()) {
      const float p = a.on->downloadProgress();
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%d%%", int(p * 100.f));
      c.drawTextRight(*a.fTiny, Rect(row.x, row.y + (row.h - 16) / 2, 44, 16), buf,
                      th.accent);
    } else {
      c.drawTextRight(*a.fTiny, Rect(row.x, row.y + (row.h - 16) / 2, 44, 16),
                      std::to_string(idx + 1), sel ? th.accent : th.textFaint);
    }

    Rect art(row.x + 52, row.y + 7, row.h - 14, row.h - 14);
    c.fillRoundRect(art, 7, th.surfaceHi);
    icons::cloud(c, art.inset(8), sel ? th.accent : th.textFaint);

    c.drawTextEllipsis(*a.fSmall,
                       Rect(art.right() + 14, row.y + 8, row.w - 300, 22),
                       s.name.empty() ? "未知曲目" : s.name, sel ? th.text : th.textDim);
    c.drawTextEllipsis(*a.fTiny,
                       Rect(art.right() + 14, row.y + 30, row.w - 300, 18),
                       s.artist.empty() ? std::string("未知艺术家") : s.artist,
                       th.textFaint);
    c.drawTextRight(*a.fTiny, Rect(row.right() - 84, row.y + (row.h - 18) / 2, 70, 18),
                    formatTime(uint64_t(s.durationMs)), th.textFaint);
  }
  c.popClip();
}

void drawPlaylistPage(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  if (!a.on) {
    ui::emptyState(a, body, icons::queue, "在线功能未初始化", "");
    return;
  }

  int y = body.y + pad;
  const Rect head(body.x + pad, y, body.w - pad * 2, 34);
  const std::string name =
      a.on->playlistName().empty() ? "歌单" : a.on->playlistName();
  ui::sectionHeader(a, head.x, head.y, head.w, name,
                    a.on->busy() ? online::jobName(a.on->currentJob()) : "B 返回");
  y = head.bottom() + 10;

  const auto& songs = a.on->playlistSongs();
  Rect content(body.x + pad, y, body.w - pad * 2, body.bottom() - y - pad);

  if (songs.empty()) {
    if (a.on->busy()) {
      c.drawTextCentered(*a.fSmall, content, "正在读取歌单…", th.textDim);
    } else {
      ui::emptyState(a, content, icons::queue, "这个歌单是空的",
                     a.on->error().empty() ? std::string()
                                           : a.on->error());
    }
    return;
  }

  constexpr int rowH = kTrackRowH;
  const int visible = std::max(1, content.h / rowH);
  a.playlistSel = std::clamp(a.playlistSel, 0, int(songs.size()) - 1);

  if (a.playlistSel < a.playlistScroll) a.playlistScroll = a.playlistSel;
  if (a.playlistSel >= a.playlistScroll + visible)
    a.playlistScroll = a.playlistSel - visible + 1;
  a.playlistScroll =
      std::clamp(a.playlistScroll, 0, std::max(0, int(songs.size()) - visible));

  Rect panel = content;
  c.fillRoundRect(panel, 14, th.surface);
  c.pushClip(Rect(panel.x + 4, panel.y + 6, panel.w - 8, panel.h - 12));
  ui::publishRows(a, Rect(panel.x + 6, panel.y + 6, panel.w - 12, rowH), rowH,
                  panel.w - 12, int(songs.size()), a.playlistScroll, visible);

  for (int i = 0; i < visible; ++i) {
    const int idx = a.playlistScroll + i;
    if (idx >= int(songs.size())) break;
    const netease::Song& s = songs[size_t(idx)];
    Rect row(panel.x + 6, panel.y + 6 + i * rowH, panel.w - 12, rowH - 4);
    const bool sel = idx == a.playlistSel;
    if (sel) c.fillRoundRect(row, 10, th.surfaceAlt);

    if (idx == a.on->pendingSong()) {
      const float p = a.on->downloadProgress();
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%d%%", int(p * 100.f));
      c.drawTextRight(*a.fTiny, Rect(row.x, row.y + (row.h - 16) / 2, 44, 16), buf,
                      th.accent);
    } else {
      c.drawTextRight(*a.fTiny, Rect(row.x, row.y + (row.h - 16) / 2, 44, 16),
                      std::to_string(idx + 1), sel ? th.accent : th.textFaint);
    }

    Rect art(row.x + 52, row.y + 7, row.h - 14, row.h - 14);
    c.fillRoundRect(art, 7, th.surfaceHi);
    icons::note(c, art.inset(8), sel ? th.accent : th.textFaint);

    c.drawTextEllipsis(*a.fSmall,
                       Rect(art.right() + 14, row.y + 8, row.w - 300, 22),
                       s.name.empty() ? "未知曲目" : s.name, sel ? th.text : th.textDim);
    c.drawTextEllipsis(*a.fTiny,
                       Rect(art.right() + 14, row.y + 30, row.w - 300, 18),
                       (s.artist.empty() ? std::string("未知艺术家") : s.artist) +
                           (s.album.empty() ? std::string() : " · " + s.album),
                       th.textFaint);
    c.drawTextRight(*a.fTiny, Rect(row.right() - 84, row.y + (row.h - 18) / 2, 70, 18),
                    formatTime(uint64_t(s.durationMs)), th.textFaint);
  }
  c.popClip();
}

// ---------------------------------------------------------------------------
// Streaming: search + result list (desktop: pages/Search.vue, streaming mode)
// ---------------------------------------------------------------------------
void drawStreaming(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  if (!a.on) {
    ui::emptyState(a, body, icons::search, "在线功能未初始化", "");
    return;
  }

  int y = body.y + pad;

  // Search box, styled like the sidebar entry so the two read as one control.
  {
    Rect box(body.x + pad, y, body.w - pad * 2, 46);
    c.fillRoundRect(box, 12, th.surface);
    c.strokeRoundRect(box, 12, th.border, 1);
    icons::search(c, Rect(box.x + 14, box.y + 13, 20, 20), th.textDim);
    const std::string shown =
        a.on->query().empty() ? "按 Plus 输入关键字搜索结果" : a.on->query();
    c.drawTextEllipsis(*a.fSmall, Rect(box.x + 46, box.y + 12, box.w - 70, 24),
                       shown, a.on->query().empty() ? th.textFaint : th.text);
    if (a.on->busy() && a.on->currentJob() == online::Job::Search) {
      c.drawTextRight(*a.fTiny, Rect(box.x, box.y + 15, box.w - 18, 20),
                      online::jobName(a.on->currentJob()), th.accent);
    }
    y = box.bottom() + 12;
  }

  if (!a.on->loggedIn()) {
    drawLoginCard(a, Rect(body.x, y, body.w, body.bottom() - y));
    return;
  }

  const auto& songs = a.on->songs();
  Rect content(body.x + pad, y, body.w - pad * 2, body.bottom() - y - pad);

  if (songs.empty()) {
    if (a.on->busy()) {
      c.drawTextCentered(*a.fSmall, content,
                         std::string(online::jobName(a.on->currentJob())) + "…",
                         th.textDim);
    } else {
      ui::emptyState(a, content, icons::search, "还没有搜索结果",
                     "按 Plus 输入关键字搜索网易云音乐");
    }
    return;
  }

  constexpr int rowH = 58;
  const int visible = std::max(1, content.h / rowH);
  a.onlineSel = std::clamp(a.onlineSel, 0, int(songs.size()) - 1);

  int first = a.onlineScroll;
  if (a.onlineSel < first) first = a.onlineSel;
  if (a.onlineSel >= first + visible) first = a.onlineSel - visible + 1;
  first = std::clamp(first, 0, std::max(0, int(songs.size()) - visible));
  a.onlineScroll = first;

  Rect panel = content;
  c.fillRoundRect(panel, 14, th.surface);
  c.pushClip(Rect(panel.x + 4, panel.y + 6, panel.w - 8, panel.h - 12));
  ui::publishRows(a, Rect(panel.x + 6, panel.y + 6, panel.w - 12, rowH), rowH,
                  panel.w - 12, int(songs.size()), first, visible);

  for (int i = 0; i < visible; ++i) {
    const int idx = first + i;
    if (idx >= int(songs.size())) break;
    const netease::Song& s = songs[size_t(idx)];
    Rect row(panel.x + 6, panel.y + 6 + i * rowH, panel.w - 12, rowH - 4);
    const bool sel = idx == a.onlineSel;
    if (sel) c.fillRoundRect(row, 10, th.surfaceAlt);

    // Ordinal / playing indicator, matching the local row rhythm.
    if (idx == a.on->pendingSong()) {
      const float p = a.on->downloadProgress();
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%d%%", int(p * 100.f));
      c.drawTextRight(*a.fTiny, Rect(row.x, row.y + (row.h - 16) / 2, 44, 16), buf,
                      th.accent);
    } else {
      c.drawTextRight(*a.fTiny, Rect(row.x, row.y + (row.h - 16) / 2, 44, 16),
                      std::to_string(idx + 1), sel ? th.accent : th.textFaint);
    }

    Rect art(row.x + 52, row.y + 7, row.h - 14, row.h - 14);
    c.fillRoundRect(art, 7, th.surfaceHi);
    icons::note(c, art.inset(8), sel ? th.accent : th.textFaint);

    c.drawTextEllipsis(*a.fSmall,
                       Rect(art.right() + 14, row.y + 8, row.w - 300, 22),
                       s.name.empty() ? "未知曲目" : s.name,
                       sel ? th.text : th.textDim);
    c.drawTextEllipsis(*a.fTiny,
                       Rect(art.right() + 14, row.y + 30, row.w - 300, 18),
                       (s.artist.empty() ? std::string("未知艺术家") : s.artist) +
                           (s.album.empty() ? std::string() : " · " + s.album),
                       th.textFaint);
    c.drawTextRight(*a.fTiny, Rect(row.right() - 84, row.y + (row.h - 18) / 2, 70, 18),
                    formatTime(uint64_t(s.durationMs)), th.textFaint);
  }
  c.popClip();
}

}  // namespace spn
