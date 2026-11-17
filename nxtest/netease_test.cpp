// SPDX-License-Identifier: AGPL-3.0
// Host tests for the NetEase client: pure logic offline, plus live calls.
//
// The live section is deliberately tolerant of individual track availability
// (the catalogue changes), but strict about protocol success: a null url on a
// VIP track is an expected outcome, a failed request is not.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "spn/json.hpp"
#include "spn/netease.hpp"

using namespace spn;

static int g_fail = 0;
static int g_pass = 0;

static void check(const char* what, bool ok, const std::string& detail = {}) {
  if (ok) {
    ++g_pass;
  } else {
    ++g_fail;
    std::printf("FAIL %s  [%s]\n", what, detail.c_str());
  }
}

static void liveTests() {
  netease::Client c;

  // --- QR login bootstrap --------------------------------------------------
  {
    netease::QrState st = netease::QrState::Failed;
    std::string err;
    const bool ok = c.qrCreate(st, err);
    check("qr create", ok, err);
    check("qr state waiting", st == netease::QrState::Waiting,
          netease::qrStateName(st));
    check("qr unikey stored", c.session().unikey.size() > 20, c.session().unikey);
    std::printf("  unikey: %s\n", c.session().unikey.c_str());

    // Polling immediately must report 801 (nobody scanned yet).
    const netease::QrState poll = c.qrCheck(err);
    check("qr poll waiting", poll == netease::QrState::Waiting,
          std::string(netease::qrStateName(poll)) + " " + err);
  }

  // --- search --------------------------------------------------------------
  {
    std::vector<netease::Song> songs;
    std::string err;
    const bool ok = c.search("周杰伦", 10, 0, songs, err);
    check("search ok", ok, err);
    check("search non-empty", !songs.empty(), std::to_string(songs.size()));
    if (!songs.empty()) {
      const auto& s = songs.front();
      check("search name", !s.name.empty(), s.name);
      check("search artist", !s.artist.empty(), s.artist);
      check("search id", s.id > 0, std::to_string(s.id));
      std::printf("  hit: %s - %s (id=%lld, %llds)\n", s.name.c_str(), s.artist.c_str(),
                  (long long)s.id, (long long)(s.durationMs / 1000));
    }
  }

  // --- song url over several candidates -----------------------------------
  // Free tracks resolve; VIP tracks return a clean error. Both paths matter.
  {
    std::vector<netease::Song> songs;
    std::string err;
    if (c.search("纯音乐", 20, 0, songs, err)) {
      int resolved = 0, refused = 0;
      for (const auto& s : songs) {
        std::string url;
        int br = 0;
        std::string uerr;
        if (c.songUrl(s.id, "exhigh", url, br, uerr)) {
          ++resolved;
          check("song url is http", url.compare(0, 7, "http://") == 0, url.substr(0, 40));
          if (resolved == 1) {
            std::printf("  stream: %s  %dkbps\n", url.substr(0, 70).c_str(), br / 1000);
          }
        } else {
          ++refused;
          // A refusal must carry a readable reason, never an empty string.
          check("song url refusal has reason", !uerr.empty(), s.name);
        }
      }
      std::printf("  resolved %d / refused %d\n", resolved, refused);
      check("song url some resolved", resolved > 0,
            std::to_string(resolved) + "/" + std::to_string(songs.size()));
    }
  }

  // --- lyric ---------------------------------------------------------------
  {
    std::vector<netease::Song> songs;
    std::string err;
    if (c.search("卡农", 5, 0, songs, err) && !songs.empty()) {
      std::string lrc, yrc;
      std::string lerr;
      const bool ok = c.lyric(songs.front().id, lrc, yrc, lerr);
      check("lyric ok", ok, lerr);
      if (ok) {
        check("lyric has text", !lrc.empty() || !yrc.empty(),
              "lrc=" + std::to_string(lrc.size()) + " yrc=" + std::to_string(yrc.size()));
        std::printf("  lyric: lrc %zu bytes, yrc %zu bytes\n", lrc.size(), yrc.size());
        // If LRC came back it must look like LRC so the parser can eat it.
        if (!lrc.empty()) {
          check("lyric looks like lrc", lrc.find('[') != std::string::npos,
                lrc.substr(0, 60));
        }
      }
    }
  }

  // --- public playlist (no login needed) ----------------------------------
  {
    std::vector<netease::Song> songs;
    netease::PlaylistSummary meta;
    std::string err;
    // 3778678 is the well-known 热歌榜.
    const bool ok = c.playlistDetail(3778678, songs, &meta, err);
    check("playlist detail", ok, err);
    check("playlist name", !meta.name.empty(), meta.name);
    check("playlist tracks", !songs.empty(), std::to_string(songs.size()));
    if (!songs.empty()) {
      std::printf("  playlist: %s, %zu tracks, first: %s\n", meta.name.c_str(), songs.size(),
                  songs.front().name.c_str());
      check("playlist track artist", !songs.front().artist.empty(), songs.front().artist);
    }
  }

  // --- login-gated paths must refuse cleanly, not crash -------------------
  {
    std::vector<netease::PlaylistSummary> lists;
    std::string err;
    const bool ok = c.userPlaylists(lists, err);
    check("userPlaylists needs login", !ok && !err.empty(), err);
    check("userPlaylists empty", lists.empty(), std::to_string(lists.size()));
  }

  std::printf("\n  requests issued: %llu\n", (unsigned long long)c.requestCount());
}

int main(int argc, char** argv) {
  const bool offline = (argc > 1 && std::string(argv[1]) == "--offline");

  // Session round trip is pure logic, so it runs in both modes.
  {
    netease::Client::setStateRoot("/tmp/nxstate");
    (void)!::system("mkdir -p /tmp/nxstate");
    netease::Client c;
    c.session().musicU = "TEST_TOKEN_123";
    c.session().userId = 424242;
    c.session().nickname = "测试用户";
    const bool saved = c.saveSession();
    check("session save", saved, netease::Client::sessionPath());

    netease::Client c2;
    const bool loaded = c2.loadSession();
    check("session load", loaded, "");
    check("session musicU", c2.session().musicU == "TEST_TOKEN_123", c2.session().musicU);
    check("session userId", c2.session().userId == 424242,
          std::to_string(c2.session().userId));
    check("session nickname", c2.session().nickname == "测试用户", c2.session().nickname);
    check("session deviceId kept", c2.session().deviceId.size() == 52,
          std::to_string(c2.session().deviceId.size()));

    // Clean up so the real login state is not polluted.
    c2.logout();
    check("session logout clears", !c2.session().loggedIn(), c2.session().musicU);
  }

  if (!offline) {
    std::printf("--- live client ---\n");
    liveTests();
  }

  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}