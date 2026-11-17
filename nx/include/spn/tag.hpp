// SPDX-License-Identifier: AGPL-3.0
// Embedded tag reading: ID3v2 (MP3), Vorbis comments + PICTURE (FLAC),
// RIFF LIST/INFO (WAV). Written from scratch so the port keeps zero binary
// dependencies beyond dr_libs and stb.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spn {

struct TagInfo {
  std::string title;
  std::string artist;
  std::string album;
  std::string albumArtist;
  std::string genre;
  int year = 0;
  int trackNo = 0;
  int discNo = 0;

  // Raw embedded cover bytes (JPEG/PNG). Decoded lazily by CoverCache.
  std::vector<uint8_t> cover;
  std::string coverMime;

  bool hasCover() const { return !cover.empty(); }
  void clear() { *this = TagInfo(); }
};

// Reads whatever tags the container provides. Unknown fields stay empty, so a
// caller can fall back to filename parsing. Never throws; returns false only
// when the file could not be opened at all.
bool readTags(const std::string& path, TagInfo& out);

// --- small helpers reused elsewhere ---------------------------------------
std::string utf16ToUtf8(const uint8_t* data, size_t bytes, bool bigEndian);
// Strips trailing NULs / whitespace and normalises full-width spaces.
std::string trimTagString(const std::string& s);

}  // namespace spn
