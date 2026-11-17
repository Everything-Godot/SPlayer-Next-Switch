// SPDX-License-Identifier: AGPL-3.0
// Persisted user state: likes, play history, playlists and settings.
//
// Everything is stored as a small plain-text file under
// sdmc:/switch/SPlayer-Next/state/ so it survives across launches without
// pulling in a JSON dependency.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "spn/types.hpp"

namespace spn {

// What the desktop app calls "我喜欢的音乐" and "播放历史".
struct HistoryEntry {
  std::string path;
  uint64_t playedAt = 0;  // unix seconds
  uint32_t playCount = 1;
};

struct SavedPlaylist {
  std::string name;
  std::vector<std::string> paths;
};

struct Settings {
  float volume = 0.8f;
  bool shuffle = false;
  int repeatMode = 0;      // 0 off, 1 all, 2 one
  bool crossfade = false;
  int themeIndex = 0;      // 0 dark, 1 midnight, 2 aurora
  bool showLyrics = true;
  int lyricFontSize = 22;
};

class Store {
 public:
  static constexpr int kMaxHistory = 400;

  // Loads from disk (creating defaults when absent). Returns false only when
  // the state directory itself could not be created.
  bool load();

  bool save() const;
  bool saveSettings() const;
  bool saveLikes() const;
  bool saveHistory() const;

  // --- likes ---------------------------------------------------------------
  bool isLiked(const std::string& path) const;
  void setLiked(const std::string& path, bool liked);
  void toggleLiked(const std::string& path);
  const std::vector<std::string>& likedPaths() const { return likes_; }
  size_t likeCount() const { return likes_.size(); }

  // --- history -------------------------------------------------------------
  void recordPlay(const std::string& path, uint64_t unixNow);
  const std::vector<HistoryEntry>& history() const { return history_; }
  size_t totalPlayCount() const;
  void clearHistory();

  // --- playlists -----------------------------------------------------------
  const std::vector<SavedPlaylist>& playlists() const { return playlists_; }
  bool createPlaylist(const std::string& name);
  bool addToPlaylist(const std::string& name, const std::string& path);
  bool removePlaylist(const std::string& name);

  // --- settings ------------------------------------------------------------
  Settings& settings() { return settings_; }
  const Settings& settings() const { return settings_; }

  // Last played track, restored on boot.
  const std::string& lastTrack() const { return lastTrack_; }
  void setLastTrack(const std::string& path) { lastTrack_ = path; }

  // Total listening time accumulated across sessions (seconds).
  uint64_t listeningSeconds() const { return listeningSec_; }
  void addListeningSeconds(uint64_t s) { listeningSec_ += s; }

  static const std::string& stateDir();

 private:
  std::vector<std::string> likes_;
  std::vector<HistoryEntry> history_;
  std::vector<SavedPlaylist> playlists_;
  Settings settings_;
  std::string lastTrack_;
  uint64_t listeningSec_ = 0;
};

}  // namespace spn