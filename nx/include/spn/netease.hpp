// SPDX-License-Identifier: AGPL-3.0
// NetEase Cloud Music client for the Switch port.
//
// This mirrors the subset of electron/main/apis/netease that the port actually
// surfaces, and nothing more. The desktop app ships 84 modules (6212 lines)
// covering albums, artists, comments, cloud upload, radio and so on; the Switch
// UI only needs: log in, read my playlists, search, resolve a stream, fetch
// lyrics. Everything goes through the eapi envelope (see crypto.hpp).
//
// Threading: every call blocks on the network. The UI is expected to drive this
// from a worker so the render loop keeps its frame budget; see online_task.hpp.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spn::netease {

// --- wire types ------------------------------------------------------------

struct Song {
  int64_t id = 0;
  std::string name;
  std::string artist;   // "A / B" for collaborations, as the API returns them
  std::string album;
  std::string coverUrl;
  int64_t durationMs = 0;
  int fee = 0;          // 0/8 == free, 1 == VIP, 4 == paid album
  int64_t albumId = 0;
  bool playable = true; // pessimistic default flipped by songUrl()
};

struct PlaylistSummary {
  int64_t id = 0;
  std::string name;
  std::string coverUrl;
  int trackCount = 0;
  int64_t creatorId = 0;
};

// QR login states, matching the codes the endpoint returns.
enum class QrState {
  Waiting,    // 801 - not scanned yet
  Scanned,    // 802 - scanned, waiting for the user to confirm
  Confirmed,  // 803 - done, cookies captured
  Expired,    // 800 - the unikey timed out
  Failed,     // network or protocol error; see the error string
};

const char* qrStateName(QrState s);

// --- session ---------------------------------------------------------------

struct Session {
  std::string musicU;      // the MUSIC_U cookie: this *is* the login token
  int64_t userId = 0;
  std::string nickname;
  std::string avatarUrl;
  std::string deviceId;    // 52 hex chars, generated once and persisted
  std::string unikey;      // pending QR login key, if any

  bool loggedIn() const { return !musicU.empty(); }
};

// --- client ----------------------------------------------------------------

class Client {
 public:
  Client();

  // Session persistence lives in the same plain-text state directory the rest
  // of the port uses (sdmc:/switch/SPlayer-Next/state/). Cookies are written in
  // the clear on purpose: the device is offline-first and the alternative
  // (inventing a key store) buys nothing against physical access.
  bool loadSession();
  bool saveSession() const;
  static const std::string& sessionPath();
  // Host tests cannot write to sdmc:, so the state root is overridable. Pass an
  // empty string to restore the Switch default.
  static void setStateRoot(const std::string& root);

  Session& session() { return session_; }
  const Session& session() const { return session_; }

  // --- login --------------------------------------------------------------
  // Step 1: ask for a unikey. The UI renders it as a QR code pointing at
  // https://music.163.com/login?codekey=<unikey>.
  bool qrCreate(QrState& state, std::string& err);
  // Step 2: poll. On Confirmed the session cookies are captured and saved.
  QrState qrCheck(std::string& err);
  // Validates the stored MUSIC_U and refreshes the profile fields.
  bool refreshProfile(std::string& err);
  void logout();

  // --- catalog ------------------------------------------------------------
  bool search(const std::string& keyword, int limit, int offset, std::vector<Song>& out,
              std::string& err);

  // User playlists (needs login; returns false with an empty list otherwise).
  bool userPlaylists(std::vector<PlaylistSummary>& out, std::string& err);
  // Cloud drive (用户云盘): songs the user uploaded. Distinct from playlists.
  bool userCloud(int offset, int limit, std::vector<Song>& out, std::string& err);
  bool playlistDetail(int64_t playlistId, std::vector<Song>& out, PlaylistSummary* meta,
                      std::string& err);

  // --- playback -----------------------------------------------------------
  // Resolves a playable http URL. `level` is standard/exhigh/lossless/hires.
  // Note the API answers 200 with url == null for VIP-only tracks; that is
  // mapped to a clean false + a readable error instead of an empty URL.
  bool songUrl(int64_t id, const std::string& level, std::string& url, int& bitrate,
               std::string& err);

  // --- lyrics -------------------------------------------------------------
  // Returns the raw LRC text and the YRC (word-timed) text when available; the
  // existing eight-format parser takes it from there.
  bool lyric(int64_t id, std::string& lrc, std::string& yrc, std::string& err);

  // --- misc --------------------------------------------------------------
  // Records a play so the account's history stays consistent with the desktop
  // app. Fire-and-forget: failures are ignored by the caller.
  bool scrobble(int64_t songId, int64_t albumId, int64_t durationMs, int64_t playedMs);

  // Total request count, useful for the settings page diagnostics line.
  uint64_t requestCount() const { return requests_; }

 private:
  Session session_;
  mutable uint64_t requests_ = 0;

  // Posts one eapi request and parses the JSON reply.
  bool eapi(const std::string& uri, const std::string& bodyJson, std::string& jsonOut,
            std::string& err);

  // Builds the cookie header the endpoint expects, including the login cookie
  // when present.
  std::string cookieHeader() const;

  // Adds the mandatory `header` object (os/appver/deviceId/...) to a body.
  std::string withHeader(const std::string& bodyJsonWithoutClose) const;

  void ensureDeviceId();
};

}  // namespace spn::netease