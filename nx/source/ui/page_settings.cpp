// SPDX-License-Identifier: AGPL-3.0
// Settings page.
#include <algorithm>
#include <cstdio>

#include "spn/icons.hpp"
#include "spn/ui.hpp"
#include "spn/widgets.hpp"

namespace spn {
namespace {

// Row kinds drive both rendering and cycling behaviour.
enum class RowKind { Info, Toggle, Choice, Slider, Action };

struct Row {
  RowKind kind;
  const char* label;
  std::string value;
  const char* hint;
};

int rowCount() { return 12; }

Row rowAt(App& a, int index) {
  const Settings& s = a.store.settings();
  switch (index) {
    case 0: return {RowKind::Choice, "主题", themeAt(s.themeIndex).name, "自定义配色的整体观感"};
    case 1: return {RowKind::Toggle, "显示歌词", s.showLyrics ? "开" : "关", "全屏播放页的歌词面板"};
    case 2: return {RowKind::Slider, "歌词字号", std::to_string(s.lyricFontSize) + " px", "左右微调"};
    case 3: return {RowKind::Toggle, "随机播放", a.shuffle ? "开" : "关", "按步长打乱播放顺序"};
    case 4: {
      const char* modes[3] = {"关闭", "列表循环", "单曲循环"};
      return {RowKind::Choice, "循环模式", modes[std::clamp(a.repeatMode, 0, 2)],
              "曲目播完后如何处理"};
    }
    case 5: return {RowKind::Slider, "音量", std::to_string(int(s.volume * 100)) + " %",
                    "也会同步到摇杆调节"};
    case 6: return {RowKind::Action, "重新扫描曲库", "", "ZR"};
    case 7: return {RowKind::Action, "清空播放历史", "", "不可撤销"};
    case 8: return {RowKind::Info, "音乐目录",
                    a.library.root().empty() ? "未找到" : a.library.root(), ""};
    case 9: return {RowKind::Info, "曲目数量",
                    std::to_string(a.library.tracks().size()) + " 首", ""};
    case 10: return {RowKind::Info, "封面缓存",
                     std::to_string(a.library.covers().residentCount()) + " / " +
                         std::to_string(a.library.covers().registeredCount()),
                     "已解码 / 已注册"};
    default: return {RowKind::Info, "版本", "1.2.1 (nx)", "对齐桌面版"};
  }
}

}  // namespace

// Applies one activation of row `index`; used by the input layer too.
void activateSetting(App& a, int index) {
  Settings& s = a.store.settings();
  switch (index) {
    case 0:
      s.themeIndex = (s.themeIndex + 1) % themeCount();
      a.th = &themeAt(s.themeIndex);
      a.store.saveSettings();
      a.toast(std::string("主题：") + themeAt(s.themeIndex).name);
      break;
    case 1:
      s.showLyrics = !s.showLyrics;
      a.store.saveSettings();
      break;
    case 3:
      a.shuffle = !a.shuffle;
      s.shuffle = a.shuffle;
      a.store.saveSettings();
      break;
    case 4:
      a.repeatMode = (a.repeatMode + 1) % 3;
      s.repeatMode = a.repeatMode;
      a.audio.setLooping(a.repeatMode == 2);
      a.store.saveSettings();
      break;
    case 6:
      a.library.scan();
      a.toast("曲库已重新扫描");
      break;
    case 7:
      a.store.clearHistory();
      a.toast("播放历史已清空");
      break;
    default:
      break;
  }
  a.dirty = true;
}

// Adjusts slider-style rows by `delta`.
void adjustSetting(App& a, int index, int delta) {
  Settings& s = a.store.settings();
  if (index == 2) {
    s.lyricFontSize = std::clamp(s.lyricFontSize + delta, 16, 40);
    a.store.saveSettings();
    a.toast("歌词字号 " + std::to_string(s.lyricFontSize) + " px");
  } else if (index == 5) {
    float v = s.volume + float(delta) * 0.05f;
    v = std::clamp(v, 0.0f, 1.0f);
    s.volume = v;
    a.audio.setVolume(v);
    a.store.saveSettings();
    a.toast("音量 " + std::to_string(int(v * 100)) + "%");
  }
  a.dirty = true;
}

int settingsRowCount() { return rowCount(); }

void drawSettings(App& a, Rect body) {
  Canvas& c = *a.c;
  const Theme& th = *a.th;
  const int pad = uiContentPad();

  const int total = rowCount();
  a.settingSel = std::clamp(a.settingSel, 0, total - 1);

  constexpr int rowH = 62;
  const int visible = std::max(1, (body.h - pad * 2) / rowH);
  int first = std::max(0, std::min(a.settingSel, total - visible));

  int y = body.y + pad;
  Rect panel(body.x + pad, y, body.w - pad * 2, body.bottom() - y - pad);
  c.fillRoundRect(panel, 16, th.surface);

  c.pushClip(Rect(panel.x + 2, panel.y + 4, panel.w - 4, panel.h - 8));
  for (int i = 0; i < visible; ++i) {
    const int idx = first + i;
    if (idx >= total) break;
    const Row row = rowAt(a, idx);
    Rect r(panel.x + 6, panel.y + 6 + i * rowH, panel.w - 12, rowH - 6);
    const bool sel = idx == a.settingSel;

    if (sel) {
      c.fillRoundRect(r, 10, th.surfaceAlt);
      c.fillRoundRect(Rect(r.x, r.y + 8, 3, r.h - 16), 2, th.accent);
    }

    c.drawText(*a.fSmall, r.x + 18, r.y + 10, row.label, th.text);
    if (row.hint && *row.hint) {
      c.drawText(*a.fTiny, r.x + 18, r.y + 32, row.hint, th.textFaint);
    }

    // Right-hand widget.
    const int rightX = r.right() - 18;
    switch (row.kind) {
      case RowKind::Toggle: {
        const bool on = row.value == "开";
        Rect sw(rightX - 48, r.y + (r.h - 26) / 2, 48, 26);
        c.fillRoundRect(sw, 13, on ? th.accent : th.surfaceHi);
        const int knobX = on ? sw.right() - 13 : sw.x + 13;
        c.fillCircle(knobX, sw.y + 13, 10, on ? th.bgTop : th.textDim);
        break;
      }
      case RowKind::Choice:
      case RowKind::Slider: {
        const int vw = c.measureText(*a.fSmall, row.value);
        Rect vb(rightX - vw - 20, r.y + (r.h - 30) / 2, vw + 20, 30);
        c.fillRoundRect(vb, 15, sel ? th.accentSoft : th.surfaceHi);
        c.drawTextCentered(*a.fSmall, vb, row.value,
                           sel ? th.text : th.textDim);
        break;
      }
      case RowKind::Action: {
        if (row.value.empty()) {
          Rect chip(rightX - 54, r.y + (r.h - 28) / 2, 54, 28);
          icons::check(c, Rect(chip.x + 16, chip.y + 8, 12, 12),
                       sel ? th.accent : th.textFaint);
          c.drawTextCentered(*a.fTiny, chip, "执行", sel ? th.accent : th.textFaint);
        }
        break;
      }
      case RowKind::Info:
      default: {
        c.drawTextEllipsis(*a.fSmall, Rect(r.right() - 340, r.y + (r.h - 22) / 2,
                                           322, 22),
                           row.value, th.textDim);
        break;
      }
    }
  }
  c.popClip();

  // Scroll indicator.
  if (total > visible) {
    Rect rail(panel.right() - 8, panel.y + 10, 4, panel.h - 20);
    c.fillRoundRect(rail, 2, th.surfaceHi);
    const int h = std::max(24, int(double(rail.h) * visible / total));
    const int y2 = rail.y + int(double(rail.h - h) * double(first) /
                                double(std::max(1, total - visible)));
    c.fillRoundRect(Rect(rail.x, y2, rail.w, h), 2, th.textFaint);
  }
}

}  // namespace spn