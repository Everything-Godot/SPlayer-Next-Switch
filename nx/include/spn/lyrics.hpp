// SPDX-License-Identifier: AGPL-3.0
// LRC lyric parsing + active-line lookup.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spn {

struct LyricLine {
  uint64_t timeMs = 0;
  std::string text;
};

// Parses LRC (including multiple timestamps per line and [offset:] tags).
// Non-timestamp metadata tags are ignored.
class Lyrics {
 public:
  bool loadFile(const std::string& path);
  void parse(const std::string& content);

  bool empty() const { return lines_.empty(); }
  const std::vector<LyricLine>& lines() const { return lines_; }

  // Index of the line that should be highlighted at `positionMs`.
  // Returns -1 before the first line.
  int activeIndex(uint64_t positionMs) const;

 private:
  std::vector<LyricLine> lines_;
};

}  // namespace spn