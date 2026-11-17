// SPDX-License-Identifier: AGPL-3.0
// Local music library scanning on the SD card.
#pragma once

#include <string>
#include <vector>

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

  // Locates the sidecar .lrc next to a track (same basename, .lrc/.LRC).
  static std::string findLyricFile(const std::string& trackPath);

  static bool isSupportedExtension(const std::string& path);

 private:
  void scanDir(const std::string& dir, int depth);

  std::vector<Track> tracks_;
  std::string root_;
  std::string lastError_;
};

}  // namespace spn