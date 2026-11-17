// SPDX-License-Identifier: AGPL-3.0
// Implementation of the threaded online layer.
//
// Threading contract, because it is the whole point of this file:
//   * Exactly one worker thread is alive at a time (job_ != None).
//   * The worker writes only `stage_`, plus the atomics and TrackCache's own
//     progress field, which the UI reads live on purpose.
//   * poll() joins a finished worker and THEN moves stage_ into the published
//     fields. The join is therefore the synchronisation point, so callers read
//     plain members with no lock and no torn state.
#include "spn/online_service.hpp"

#include <chrono>
#include <cstdio>

namespace spn::online {

namespace {

// The worker signals completion by clearing job_ as its very last action, so
// seeing None while the thread is still joinable means "finished, reap it".
// Joining then costs a handful of microseconds instead of a frame.

constexpr const char* kLoginUrlPrefix = "https://music.163.com/login?codekey=";

// Preferred stream quality, then the fallback tried when the account cannot
// play it. exhigh (~320k) is what the desktop app asks for by default.
constexpr const char* kLevels[] = {"exhigh", "standard"};

}  // namespace

const char* loginStateName(LoginState s) {
  switch (s) {
    case LoginState::LoggedOut: return "未登录";
    case LoginState::WaitingQr: return "等待扫码";
    case LoginState::Scanned: return "已扫码，等待确认";
    case LoginState::LoggedIn: return "已登录";
  }
  return "未知";
}

const char* jobName(Job j) {
  switch (j) {
    case Job::None: return "空闲";
    case Job::LoginCreate: return "获取二维码";
    case Job::LoginPoll: return "等待确认";
    case Job::Search: return "搜索中";
    case Job::Playlists: return "读取歌单";
    case Job::PlaylistOpen: return "打开歌单";
    case Job::Resolve: return "解析音源";
    case Job::Lyrics: return "读取歌词";
  }
  return "未知";
}

Service::Service() = default;

Service::~Service() { shutdown(); }

// --- lifecycle -------------------------------------------------------------

void Service::init() {
  // Reading the session file is a small local read; safe on the UI thread.
  if (client_.loadSession()) {
    if (client_.session().loggedIn()) {
      login_.store(LoginState::LoggedIn);
      nickname_ = client_.session().nickname;
    }
  }
}

void Service::publishStaged() {
  if (stage_.hasLogin) {
    login_.store(stage_.login);
    stage_.hasLogin = false;
  }
  if (stage_.hasSongs) {
    songs_.swap(stage_.songs);
    stage_.songs.clear();
    stage_.hasSongs = false;
  }
  if (stage_.hasPlaylists) {
    playlists_.swap(stage_.playlists);
    stage_.playlists.clear();
    stage_.hasPlaylists = false;
  }
  if (!stage_.loginUrl.empty()) {
    loginUrl_.swap(stage_.loginUrl);
    stage_.loginUrl.clear();
  }
  if (!stage_.nickname.empty()) {
    nickname_.swap(stage_.nickname);
    stage_.nickname.clear();
  }
  if (!stage_.readyPath.empty()) {
    readyPath_.swap(stage_.readyPath);
    stage_.readyPath.clear();
  }
  if (!stage_.lyrics.empty()) {
    lyrics_.swap(stage_.lyrics);
    stage_.lyrics.clear();
  }
  // The error string is sticky on purpose: it is a diagnostic the settings page
  // shows until the next successful action clears it, not a one-shot event.
  if (!stage_.error.empty()) {
    error_.swap(stage_.error);
    stage_.error.clear();
  }
}

void Service::poll() {
  if (!worker_.joinable()) return;
  if (job_.load() != Job::None) return;  // still running
  worker_.join();
  publishStaged();
}

void Service::shutdown() {
  stopping_.store(true);
  cache_.cancel();
  if (worker_.joinable()) worker_.join();
  job_.store(Job::None);
  stopping_.store(false);
}

void Service::start(std::thread t) { worker_ = std::move(t); }

// --- login -----------------------------------------------------------------

void Service::beginLogin() {
  if (busy()) return;
  stage_ = Staged{};
  loginUrl_.clear();
  error_.clear();
  login_.store(LoginState::WaitingQr);
  job_.store(Job::LoginCreate);
  start(std::thread([this] { runLoginCreate(); }));
}

void Service::runLoginCreate() {
  netease::QrState st = netease::QrState::Failed;
  std::string err;
  if (!client_.qrCreate(st, err)) {
    stage_.error = err.empty() ? "无法获取登录二维码" : err;
    stage_.login = LoginState::LoggedOut;
    stage_.hasLogin = true;
    job_.store(Job::None);
    return;
  }
  stage_.loginUrl = std::string(kLoginUrlPrefix) + client_.session().unikey;
  stage_.login = LoginState::WaitingQr;
  stage_.hasLogin = true;
  job_.store(Job::None);
}

void Service::pollLogin() {
  if (busy()) return;
  if (login_.load() != LoginState::WaitingQr && login_.load() != LoginState::Scanned) return;
  job_.store(Job::LoginPoll);
  start(std::thread([this] { runLoginPoll(); }));
}

void Service::runLoginPoll() {
  std::string err;
  const netease::QrState st = client_.qrCheck(err);

  switch (st) {
    case netease::QrState::Waiting:
      stage_.login = LoginState::WaitingQr;
      stage_.hasLogin = true;
      break;
    case netease::QrState::Scanned:
      stage_.login = LoginState::Scanned;
      stage_.hasLogin = true;
      break;
    case netease::QrState::Confirmed: {
      // Cookies are already captured by qrCheck(); pull the profile so the
      // panel can show a nickname instead of a user id.
      std::string perr;
      if (client_.refreshProfile(perr)) {
        stage_.nickname = client_.session().nickname;
      }
      client_.saveSession();
      stage_.login = LoginState::LoggedIn;
      stage_.hasLogin = true;
      stage_.loginUrl.clear();  // stop rendering a dead QR
      break;
    }
    case netease::QrState::Expired:
      stage_.login = LoginState::LoggedOut;
      stage_.hasLogin = true;
      stage_.error = "二维码已过期，请重新获取";
      break;
    case netease::QrState::Failed:
    default:
      // A transient poll failure must not throw the user back to the start:
      // keep the panel on the QR and let the next tick retry.
      stage_.error = err.empty() ? "登录状态查询失败" : err;
      break;
  }
  job_.store(Job::None);
}

void Service::cancelLogin() {
  if (job_.load() == Job::LoginPoll) {
    // The poll is a single short request; let it finish on its own rather than
    // pulling the socket out from under the worker.
  }
  if (login_.load() != LoginState::LoggedIn) {
    login_.store(LoginState::LoggedOut);
  }
  loginUrl_.clear();
  stage_.loginUrl.clear();
}

std::string Service::loginUrl() const { return loginUrl_; }

void Service::logout() {
  if (busy()) return;
  client_.logout();
  client_.saveSession();
  login_.store(LoginState::LoggedOut);
  nickname_.clear();
  loginUrl_.clear();
  songs_.clear();
  playlists_.clear();
  error_.clear();
}

// --- catalog ---------------------------------------------------------------

void Service::search(const std::string& keyword) {
  if (busy()) return;
  stage_ = Staged{};
  query_ = keyword;
  error_.clear();
  songs_.clear();
  job_.store(Job::Search);
  start(std::thread([this] { runSearch(); }));
}

void Service::runSearch() {
  std::vector<netease::Song> out;
  std::string err;
  if (!client_.search(query_, 50, 0, out, err)) {
    stage_.error = err.empty() ? "搜索失败" : err;
    stage_.hasSongs = true;
    job_.store(Job::None);
    return;
  }
  stage_.songs = std::move(out);
  stage_.hasSongs = true;
  stage_.error.clear();
  job_.store(Job::None);
}

void Service::loadPlaylists() {
  if (busy()) return;
  stage_ = Staged{};
  error_.clear();
  job_.store(Job::Playlists);
  start(std::thread([this] { runPlaylists(); }));
}

void Service::runPlaylists() {
  std::vector<netease::PlaylistSummary> out;
  std::string err;
  if (!client_.userPlaylists(out, err)) {
    stage_.error = err.empty() ? "读取歌单失败（需要登录）" : err;
    stage_.hasPlaylists = true;
    job_.store(Job::None);
    return;
  }
  stage_.playlists = std::move(out);
  stage_.hasPlaylists = true;
  job_.store(Job::None);
}

void Service::openPlaylist(int index) {
  if (busy()) return;
  if (index < 0 || index >= int(playlists_.size())) return;
  stage_ = Staged{};
  playlistId_ = playlists_[size_t(index)].id;
  error_.clear();
  job_.store(Job::PlaylistOpen);
  start(std::thread([this] { runPlaylistOpen(); }));
}

void Service::runPlaylistOpen() {
  std::vector<netease::Song> out;
  netease::PlaylistSummary meta;
  std::string err;
  if (!client_.playlistDetail(playlistId_, out, &meta, err)) {
    stage_.error = err.empty() ? "打开歌单失败" : err;
    job_.store(Job::None);
    return;
  }
  stage_.songs = std::move(out);
  stage_.hasSongs = true;
  job_.store(Job::None);
}

// --- playback --------------------------------------------------------------

bool Service::playSong(int index) {
  if (busy()) return false;
  if (index < 0 || index >= int(songs_.size())) return false;

  stage_ = Staged{};
  error_.clear();
  pendingIndex_ = index;
  pendingSong_.store(index);
  // Copy now: `songs_` may be replaced by a later search, and the worker must
  // not read memory the UI can swap out from under it.
  pendingSongData_ = songs_[size_t(index)];
  job_.store(Job::Resolve);
  start(std::thread([this] { runResolve(); }));
  return true;
}

void Service::runResolve() {
  const netease::Song song = pendingSongData_;

  // Already cached? Skip both the API call and the download.
  std::string cached;
  if (TrackCache::lookup(song.id, cached)) {
    stage_.readyPath = cached;
    pendingSong_.store(-1);
    job_.store(Job::None);
    return;
  }

  std::string url;
  std::string err;
  int bitrate = 0;
  bool ok = false;
  for (const char* level : kLevels) {
    if (client_.songUrl(song.id, level, url, bitrate, err) && !url.empty()) {
      ok = true;
      break;
    }
  }
  if (!ok || url.empty()) {
    stage_.error = err.empty() ? "该曲目无法播放（可能需要会员）" : err;
    pendingSong_.store(-1);
    job_.store(Job::None);
    return;
  }

  // The cache runs its own thread and reports progress through atomics; this
  // worker just waits for it to settle so the job stays "Resolve" meanwhile.
  cache_.begin(song.id, url);
  for (;;) {
    if (stopping_.load()) {
      cache_.cancel();
      pendingSong_.store(-1);
      job_.store(Job::None);
      return;
    }
    const CacheState st = cache_.state();
    if (st == CacheState::Ready) {
      stage_.readyPath = cache_.resultPath();
      break;
    }
    if (st == CacheState::Failed) {
      stage_.error = cache_.error();
      if (stage_.error.empty()) stage_.error = "下载失败";
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
  }

  // Lyrics are best-effort: a track plays fine with no words.
  {
    std::string lrc;
    std::string yrc;
    std::string lerr;
    if (client_.lyric(song.id, lrc, yrc, lerr)) {
      stage_.lyrics = lrc.empty() ? yrc : lrc;
    }
  }

  pendingSong_.store(-1);
  job_.store(Job::None);
}

std::string Service::takeReadyPath() {
  std::string out;
  out.swap(readyPath_);
  return out;
}

std::string Service::takeLyrics() {
  std::string out;
  out.swap(lyrics_);
  return out;
}

}  // namespace spn::online
