// SPDX-License-Identifier: AGPL-3.0
// Online service: owns the NetEase client and the track cache, and keeps every
// blocking call off the render thread.
//
// The renderer polls a small immutable snapshot (state + lists); each network
// action runs on a worker and publishes its result under a mutex. That way a
// slow CDN shows as "downloading 42%" instead of a frozen UI, which is the
// whole reason this layer exists rather than calling the client directly from
// the page code.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "spn/netease.hpp"
#include "spn/online_player.hpp"
#include "spn/qrcode.hpp"

namespace spn::online {

// What the login panel should show.
enum class LoginState {
  LoggedOut,   // no session on disk (or it was rejected)
  WaitingQr,   // a unikey exists, the QR is on screen, polling for the scan
  Scanned,     // the phone scanned it, waiting for the confirm tap
  LoggedIn,    // MUSIC_U captured and validated
};

const char* loginStateName(LoginState s);

// A single background job at a time; the UI shows the busy flag.
enum class Job {
  None,
  LoginCreate,
  LoginPoll,
  Search,
  Playlists,
  PlaylistOpen,
  Resolve,   // songUrl + download
  Lyrics,
};

const char* jobName(Job j);

class Service {
 public:
  Service();
  ~Service();

  Service(const Service&) = delete;
  Service& operator=(const Service&) = delete;

  // --- lifecycle ----------------------------------------------------------
  // Loads the cached session. Cheap; does not touch the network.
  void init();
  // Called once per frame from the UI. Joins a finished worker and publishes
  // its results, so the caller never blocks.
  void poll();
  void shutdown();

  // --- login --------------------------------------------------------------
  void beginLogin();                  // asks for a unikey, then polls
  // Drives one QR poll. The UI calls this on its own timer (the desktop panel
  // uses 1500 ms); doing it here would need a clock this layer does not own.
  void pollLogin();
  void cancelLogin();
  LoginState loginState() const { return login_.load(); }
  // The URL to render as a QR. Empty until beginLogin() succeeds.
  std::string loginUrl() const;
  bool loggedIn() const { return login_.load() == LoginState::LoggedIn; }
  const std::string& nickname() const { return nickname_; }
  void logout();

  // --- catalog ------------------------------------------------------------
  // Starts a search. Results land in songs() when the job finishes.
  void search(const std::string& keyword);
  const std::string& query() const { return query_; }
  const std::vector<netease::Song>& songs() const { return songs_; }

  void loadPlaylists();
  const std::vector<netease::PlaylistSummary>& playlists() const { return playlists_; }
  void openPlaylist(int index);

  // --- playback -----------------------------------------------------------
  // Resolves a song and downloads it; `onReady` path arrives via
  // takeReadyPath(). Returns false when a different job is already running.
  bool playSong(int index);
  // Song index whose download is in flight, or -1.
  int pendingSong() const { return pendingSong_.load(); }
  float downloadProgress() const { return cache_.progress(); }
  // Moves a finished download path out; empty when there is nothing new.
  std::string takeReadyPath();
  // Lyrics text for the song that finished resolving, consumed once.
  std::string takeLyrics();

  // --- diagnostics --------------------------------------------------------
  Job currentJob() const { return job_.load(); }
  const std::string& error() const { return error_; }
  bool busy() const { return job_.load() != Job::None; }

 private:
  void runLoginCreate();
  void runLoginPoll();
  void runSearch();
  void runPlaylists();
  void runPlaylistOpen();
  void runResolve();
  void start(std::thread t);

  // Results are staged here by the worker and moved into the published fields
  // by poll() *after* the join. That ordering is what makes the published
  // fields race-free without the UI taking a lock on every frame.
  struct Staged {
    std::string loginUrl;
    std::string nickname;
    std::string readyPath;
    std::string lyrics;
    std::string error;
    LoginState login = LoginState::LoggedOut;
    bool hasLogin = false;
    std::vector<netease::Song> songs;
    bool hasSongs = false;
    std::vector<netease::PlaylistSummary> playlists;
    bool hasPlaylists = false;
  };

  void publishStaged();

  netease::Client client_;
  TrackCache cache_;

  std::atomic<Job> job_{Job::None};
  std::atomic<LoginState> login_{LoginState::LoggedOut};
  std::atomic<int> pendingSong_{-1};
  std::thread worker_;
  std::atomic<bool> stopping_{false};

  Staged stage_;

  std::string loginUrl_;
  std::string nickname_;
  std::string query_;
  std::vector<netease::Song> songs_;
  std::vector<netease::PlaylistSummary> playlists_;
  std::string readyPath_;
  std::string lyrics_;
  std::string error_;

  int64_t playlistId_ = 0;
  int pendingIndex_ = -1;
  // Snapshot of the song being resolved, taken on the UI thread so the worker
  // never reads a list the next search could replace mid-flight.
  netease::Song pendingSongData_;
};

}  // namespace spn::online