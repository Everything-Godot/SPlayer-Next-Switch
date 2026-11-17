// SPDX-License-Identifier: AGPL-3.0
// Multi-format lyric parsing: TTML / LYS / QRC / KRC / YRC / LRC / ASS / SRT.
//
// The desktop app delegates this to the `lyric-kit` npm package. The Switch port
// cannot use npm, so the eight formats are implemented here against the same
// priority order the desktop app uses (shared/types/lyrics.ts
// DEFAULT_LYRIC_FORMAT_ORDER).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spn {

// One syllable inside a line. `startMs` is absolute; `endMs` is filled from the
// next word (or the line duration) once the whole file has been parsed.
struct LyricWord {
  uint32_t startMs = 0;
  uint32_t endMs = 0;
  std::string text;
};

struct LyricLine {
  uint64_t timeMs = 0;
  uint64_t endMs = 0;  // 0 when the format carries no line duration
  std::string text;
  // Per-syllable timings (TTML / QRC / KRC / YRC / enhanced LYS / ASS karaoke).
  // Empty when the format is line-timed only.
  std::vector<LyricWord> words;

  bool wordTimed() const { return !words.empty(); }
};

enum class LyricFormat {
  Unknown = 0,
  Ttml,
  Lys,
  Qrc,
  Krc,
  Yrc,
  Lrc,
  Ass,
  Srt,
};

const char* lyricFormatName(LyricFormat f);
LyricFormat lyricFormatFromExtension(const std::string& ext);

// Rank used to decide which sidecar to prefer: lower is better, mirroring
// DEFAULT_LYRIC_FORMAT_ORDER. Unknown formats rank last.
int lyricFormatRank(LyricFormat f);

// Sidecar extensions in priority order, as used by the desktop app.
// Terminated by nullptr.
const char* const* lyricSidecarExtensions();

class Lyrics {
 public:
  // Loads a sidecar file, auto-detecting the format from the extension (with a
  // content sniff when the extension is unhelpful).
  bool loadFile(const std::string& path);

  // Parses `content` as the given format. When `format` is Unknown the parser
  // sniffs the body.
  bool parse(const std::string& content, LyricFormat format = LyricFormat::Unknown);

  bool empty() const { return lines_.empty(); }
  const std::vector<LyricLine>& lines() const { return lines_; }

  LyricFormat format() const { return format_; }
  const std::string& offsetTag() const { return offsetTag_; }  // artist / title / ...
  const std::string& title() const { return title_; }
  const std::string& artist() const { return artist_; }
  const std::string& album() const { return album_; }
  bool wordTimed() const { return wordTimed_; }
  // Applied [offset:] correction in milliseconds.
  int offsetMs() const { return offsetMs_; }

  // Index of the line that should be highlighted at `positionMs`.
  // Returns -1 before the first line.
  int activeIndex(uint64_t positionMs) const;

  // Progress within the active line, 0..1. Returns -1 when the line has no
  // usable duration so callers can fall back to a per-line fade.
  float lineProgress(uint64_t positionMs) const;
  // Progress within the active word, 0..1 (word-timed formats only).
  float wordProgress(uint64_t positionMs, int* wordIndex) const;

  void clear();

 private:
  std::vector<LyricLine> lines_;
  LyricFormat format_ = LyricFormat::Unknown;
  std::string title_;
  std::string artist_;
  std::string album_;
  std::string offsetTag_;
  int offsetMs_ = 0;
  bool wordTimed_ = false;

  friend void finaliseLyricTimings(Lyrics&);
};

}  // namespace spn