// SPDX-License-Identifier: AGPL-3.0
// Host-side layout preview: renders every page to a PPM so the visual result
// can be inspected without a Switch. Never compiled into the NRO — the
// Makefile filters this file out.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "spn/canvas.hpp"
#include "spn/font.hpp"
#include "spn/icons.hpp"
#include "spn/image.hpp"
#include "spn/store.hpp"
#include "spn/types.hpp"
#include "spn/ui.hpp"

using namespace spn;

namespace {

std::string gFontPath;
bool gFontOk = false;

void writePpm(const char* path, const std::vector<uint32_t>& px, int w, int h) {
  FILE* fp = std::fopen(path, "wb");
  if (!fp) {
    std::fprintf(stderr, "cannot write %s\n", path);
    return;
  }
  std::fprintf(fp, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; ++i) {
    const uint32_t p = px[size_t(i)];
    const unsigned char rgb[3] = {static_cast<unsigned char>(p & 0xFF),
                                  static_cast<unsigned char>((p >> 8) & 0xFF),
                                  static_cast<unsigned char>((p >> 16) & 0xFF)};
    std::fwrite(rgb, 1, 3, fp);
  }
  std::fclose(fp);
  std::printf("wrote %s\n", path);
}

// Generates a plausible synthetic cover so the artwork path is exercised.
Image makeCover(int seed) {
  Image img;
  img.w = 256;
  img.h = 256;
  img.px.resize(size_t(img.w) * size_t(img.h));

  const uint8_t base[3] = {uint8_t(60 + (seed * 47) % 150),
                           uint8_t(40 + (seed * 83) % 170),
                           uint8_t(90 + (seed * 131) % 140)};

  for (int y = 0; y < img.h; ++y) {
    for (int x = 0; x < img.w; ++x) {
      const float fx = float(x) / float(img.w);
      const float fy = float(y) / float(img.h);
      const float d = std::sqrt((fx - 0.5f) * (fx - 0.5f) + (fy - 0.5f) * (fy - 0.5f));
      const float glow = std::max(0.f, 1.f - d * 1.7f);

      const uint8_t r = uint8_t(std::min(255.f, float(base[0]) * (0.45f + glow * 1.3f)));
      const uint8_t g = uint8_t(std::min(255.f, float(base[1]) * (0.40f + glow * 1.5f)));
      const uint8_t b = uint8_t(std::min(255.f, float(base[2]) * (0.55f + glow * 1.2f)));

      // Faint diagonal stripes so scaling artefacts would be obvious.
      const uint8_t shade = ((x + y + seed * 23) / 26) % 2 ? 6 : 0;
      img.px[size_t(y) * size_t(img.w) + size_t(x)] =
          uint32_t(uint8_t(std::min(255, r + shade))) |
          (uint32_t(uint8_t(std::min(255, g + shade))) << 8) |
          (uint32_t(uint8_t(std::min(255, b + shade))) << 16) | (uint32_t(0xFF) << 24);
    }
  }
  return img;
}

}  // namespace

// Builds a fake App, renders one page, writes it out.
bool renderPage(Page page, const char* outPath, const Image& cover,
                bool populate) {
  constexpr int W = 1280;
  constexpr int H = 720;

  std::vector<uint32_t> pixels(size_t(W) * size_t(H), 0);
  Canvas c(pixels.data(), W, H, W);

  Font fTiny, fSmall, fBody, fTitle, fHuge;
  if (!fTiny.loadFromFile(gFontPath, 15.f)) return false;
  fSmall.loadFromFile(gFontPath, 19.f);
  fBody.loadFromFile(gFontPath, 23.f);
  fTitle.loadFromFile(gFontPath, 31.f);
  fHuge.loadFromFile(gFontPath, 74.f);

  static App a;
  a.c = &c;
  a.fTiny = &fTiny;
  a.fSmall = &fSmall;
  a.fBody = &fBody;
  a.fTitle = &fTitle;
  a.fHuge = &fHuge;
  a.th = &themeAt(0);
  a.page = page;

  if (populate && a.library.tracks().empty()) {
    std::vector<Track> fake;
    struct Row {
      const char* title;
      const char* artist;
      const char* album;
      const char* fmt;
      double dur;
      int year;
    };
    const Row rows[] = {
        {"晴天", "周杰伦", "叶惠美", "FLAC", 269.0, 2003},
        {"夜曲", "周杰伦", "十一月的萧邦", "FLAC", 226.0, 2005},
        {"富士山下", "陈奕迅", "What's Going On...?", "MP3", 262.0, 2006},
        {"Lemon", "米津玄師", "STRAY SHEEP", "FLAC", 256.0, 2018},
        {"夜に駆ける", "YOASOBI", "THE BOOK", "MP3", 261.0, 2019},
        {"Someone Like You", "Adele", "21", "FLAC", 285.0, 2011},
        {"Bohemian Rhapsody", "Queen", "A Night At The Opera", "FLAC", 354.0, 1975},
        {"残酷な天使のテーゼ", "高橋洋子", "Neon Genesis Evangelion", "MP3", 245.0, 1995},
        {"海阔天空", "Beyond", "乐与怒", "FLAC", 326.0, 1993},
        {"平凡之路", "朴树", "猎户星座", "MP3", 303.0, 2014},
        {"小城故事", "邓丽君", "小城故事", "WAV", 196.0, 1979},
        {"Yellow", "Coldplay", "Parachutes", "MP3", 269.0, 2000},
        {"Viva La Vida", "Coldplay", "Viva la Vida", "FLAC", 242.0, 2008},
    };
    int i = 0;
    for (const Row& r : rows) {
      Track t;
      t.title = r.title;
      t.artist = r.artist;
      t.album = r.album;
      t.albumArtist = r.artist;
      t.format = r.fmt;
      t.durationSec = r.dur;
      t.year = r.year;
      t.sampleRate = 44100;
      t.channels = 2;
      t.bitDepth = 16;
      t.bitrateKbps = 1411;
      t.trackNo = i + 1;
      t.fileSize = 40u * 1024u * 1024u;
      t.path = std::string("sdmc:/switch/SPlayer-Next/music/") + r.artist + "/" +
               r.album + "/" + r.title + ".flac";
      t.coverId = a.library.covers().add(t.path, true);
      fake.push_back(t);
      ++i;
    }
    a.library.setTracksForTest(std::move(fake));

    // Fake likes / history so those pages are not empty.
    a.store.setLiked(a.library.tracks()[0].path, true);
    a.store.setLiked(a.library.tracks()[3].path, true);
    a.store.setLiked(a.library.tracks()[8].path, true);
    for (int k = 0; k < 5; ++k) {
      a.store.recordPlay(a.library.tracks()[size_t(k)].path, 1000 + uint64_t(k));
    }
    a.playingIndex = 0;
    a.now = 5000.0;
    a.homeSel = 1;
    a.libSel = 2;
    a.likedSel = 1;
    a.histSel = 1;
    a.albumSel = 3;
    a.folderSel = 1;
  } else if (!populate) {
    a.playingIndex = -1;
  }

  // Content for the lyric panel, if the page shows one.
  if (a.lyrics.empty()) a.lyrics.parse(
      "[00:00.00]故事的小黄花\n[00:04.20]从出生那年就飘着\n"
      "[00:08.40]童年的荡秋千\n[00:12.60]随记忆一直晃到现在\n"
      "[00:16.80]吹着前奏望着天空\n[00:21.00]我想起花瓣试着掉落\n"
      "[00:25.20]为你翘课的那一天\n[00:29.40]花落的那一天\n"
      "[00:33.60]教室的那一间\n[00:37.80]我怎么看不见\n");

  // Wrap the real render path used by main.cpp.
  const int sideW = uiSidebarWidth();
  const int topH = uiTopBarHeight();
  const int barH = uiPlayerBarHeight();

  c.verticalGradient(Rect(0, 0, W, H), a.th->bgTop, a.th->bgBottom);
  drawSidebar(a, Rect(0, 0, sideW, H));
  const Rect content(sideW, 0, W - sideW, H);
  drawTopBar(a, Rect(content.x, 0, content.w, topH));
  const int bodyTop = topH;
  const int bodyBottom = H - barH;
  const Rect body(content.x, bodyTop, content.w, bodyBottom - bodyTop);

  switch (page) {
    case Page::Home: drawHome(a, body); break;
    case Page::Library: drawLibrary(a, body); break;
    case Page::Artists: drawArtists(a, body); break;
    case Page::Albums: drawAlbums(a, body); break;
    case Page::Liked: drawLiked(a, body); break;
    case Page::History: drawHistory(a, body); break;
    case Page::Downloads: drawDownloads(a, body); break;
    case Page::Favorites: drawFavorites(a, body); break;
    case Page::Folders: drawFolders(a, body); break;
    case Page::Stats: drawStats(a, body); break;
    // Cloud / Streaming need a live online::Service; the preview leaves a.on
    // null and these renderers fall back to their "not initialised" state,
    // which is exactly what a console with no network shows.
    case Page::Cloud: drawCloud(a, body); break;
    case Page::Streaming: drawStreaming(a, body); break;
    case Page::Search: a.searchQuery = "cold"; drawSearch(a, body); break;
    case Page::Settings: drawSettings(a, body); break;
    case Page::Player: drawPlayer(a, body); break;
    default: break;
  }

  drawPlayerBar(a, Rect(content.x, bodyBottom, content.w, barH));

  (void)cover;
  writePpm(outPath, pixels, W, H);
  return true;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <font.ttc> <outdir>\n", argv[0]);
    return 2;
  }
  gFontPath = argv[1];
  const std::string outDir = argv[2];
  gFontOk = true;

  const Image cover = makeCover(3);

  // Exercise the pure-drawing paths (icons, gradients, images, wrapping).
  {
    constexpr int W = 1280;
    constexpr int H = 720;
    std::vector<uint32_t> px(size_t(W) * size_t(H), 0);
    Canvas c(px.data(), W, H, W);
    Font fSmall, fBody;
    fSmall.loadFromFile(gFontPath, 19.f);
    fBody.loadFromFile(gFontPath, 23.f);

    c.clear(Color::rgb(0x101620));
    c.horizontalGradient(Rect(40, 40, 600, 120), Color::rgb(0x2B4A82),
                         Color::rgb(0x5B9DFF));
    c.verticalGradient(Rect(40, 180, 600, 120), Color::rgb(0x5B9DFF),
                       Color::rgb(0x101620));
    c.dropShadow(Rect(700, 60, 200, 160), 18, 20, 140);
    c.fillRoundRect(Rect(700, 60, 200, 160), 18, Color::rgb(0x1E2739));

    c.drawImageRounded(cover, Rect(940, 60, 180, 180), 24);
    c.drawImage(cover, Rect(700, 260, 160, 90));

    // Icon gallery.
    struct Item {
      const char* name;
      void (*fn)(Canvas&, Rect, Color);
    };
    const Item items[] = {
        {"home", icons::home},     {"disc", icons::disc},
        {"clock", icons::clock},   {"download", icons::download},
        {"folder", icons::folder}, {"chart", icons::chart},
        {"search", icons::search}, {"gear", icons::gear},
        {"note", icons::note},     {"play", icons::play},
        {"pause", icons::pause},   {"next", icons::next},
        {"prev", icons::prev},     {"shuffle", icons::shuffle},
        {"volume", icons::volume}, {"plus", icons::plus},
        {"check", icons::check},   {"trash", icons::trash},
        {"back", icons::back},     {"queue", icons::queue},
    };
    int x = 60;
    int y = 380;
    for (const Item& it : items) {
      it.fn(c, Rect(x, y, 40, 40), Color::rgb(0xEAF0FF));
      c.drawText(fSmall, x - 4, y + 48, it.name, Color::rgb(0x93A0BA));
      x += 110;
      if (x > 1180) {
        x = 60;
        y += 110;
      }
    }
    icons::heart(c, Rect(60, 620, 40, 40), Color::rgb(0xFF6B81), true);
    icons::heart(c, Rect(120, 620, 40, 40), Color::rgb(0xFF6B81), false);
    icons::star(c, Rect(180, 620, 40, 40), Color::rgb(0xFFCC44), true);
    icons::star(c, Rect(240, 620, 40, 40), Color::rgb(0xFFCC44), false);
    icons::repeat(c, Rect(300, 620, 40, 40), Color::rgb(0xEAF0FF), false);
    icons::repeat(c, Rect(360, 620, 40, 40), Color::rgb(0xEAF0FF), true);

    const auto lines = c.wrapText(fBody,
                                  "这是一段用于测试自动换行的长文本，Mixed with English words "
                                  "to make sure the greedy breaker does not split words.",
                                  420);
    int ly = 240;
    for (const std::string& l : lines) {
      c.drawText(fBody, 60, ly, l, Color::rgb(0xEAF0FF));
      ly += fBody.lineHeight() + 4;
    }

    writePpm((outDir + "/00-primitives.ppm").c_str(), px, W, H);
  }

  // Every page in sidebar order, so the generated contact sheet doubles as a
// visual diff against the desktop layout.
  const Page pages[] = {
      Page::Home,      Page::Library,   Page::Artists,  Page::Albums,
      Page::Folders,   Page::Stats,     Page::Liked,    Page::Favorites,
      Page::Cloud,     Page::Downloads, Page::Streaming, Page::History,
      Page::Search,    Page::Settings,  Page::Player};
  char buf[256];
  int n = 1;
  for (Page p : pages) {
    std::snprintf(buf, sizeof(buf), "%s/%02d-%s.ppm", outDir.c_str(), n,
                  pageTitle(p));
    if (!renderPage(p, buf, cover, true)) return 1;
    ++n;
  }
  return 0;
}