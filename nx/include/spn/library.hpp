// SPDX-License-Identifier: AGPL-3.0
// Local music library scanning on the SD card.
#pragma once

#include <string>
#include <vector>

#include "spn/cover.hpp"
#include "spn/types.hpp"

namespace spn {

// Scans the supported music roots on the SD card.
//
// Lookup order (first existing directory wins per root):
//   sdmc:/switch/SPlayer-Next/music
//   sdmc:/SPlayer-Next/music
//   sdmc:/music
class Library {
 public:
  // Rescans from scratch. Returns the number of tracks found.
  size_t scan();

  const std::vector<Track>& tracks() const { return tracks_; }
  const std::string& root() const { return root_; }
  const std::string& lastError() const { return lastError_; }
  size_t totalBytes() const { return totalBytes_; }
  double totalDurationSec() const { return totalDuration_; }

  // Derived collections, rebuilt after each scan.
  const std::vector<Album>& albums() const { return albums_; }
  const std::vector<ArtistEntry>& artists() const { return artists_; }

  // Distinct folder names that contain music (used by the folders page).
  std::vector<std::string> folders() const;

  // Index of a track by path, or -1.
  int indexOf(const std::string& path) const;

  // Host-preview only: installs a synthetic track list so the layout can be
  // rendered without an SD card. Rebuilds albums/artists; no cover decoding.
  void setTracksForTest(std::vector<Track> tracks);

  CoverCache& covers() { return covers_; }

  // Locates the sidecar lyric next to a track. Supports .lrc / .LRC / .ttml / .ass.
  static std::string findLyricFile(const std::string& trackPath);

  static bool isSupportedExtension(const std::string& path);

 private:
  void scanDir(const std::string& dir, int depth);
  void buildAggregates();

  std::vector<Track> tracks_;
  std::vector<Album> albums_;
  std::vector<ArtistEntry> artists_;
  CoverCache covers_;
  std::string root_;
  std::string lastError_;
  size_t totalBytes_ = 0;
  double totalDuration_ = 0.0;
};

}  // namespace spn