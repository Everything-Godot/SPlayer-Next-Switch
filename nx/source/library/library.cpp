// SPDX-License-Identifier: AGPL-3.0
// Local music library scanning on the SD card.
#include "spn/library.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <dirent.h>
#include <sys/stat.h>

#include <switch.h>

#include "spn/audio.hpp"

namespace spn {

namespace {

const char* kRootCandidates[] = {
    "sdmc:/switch/SPlayer-Next/music",
    "sdmc:/SPlayer-Next/music",
    "sdmc:/music",
};

std::string toLower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string baseNameNoExt(const std::string& path) {
  size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return name;
}

bool dirExists(const std::string& path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// "<artist> - <title>" is the conventional naming; fall back to the raw name.
void splitArtistTitle(const std::string& stem, Track& t) {
  const size_t sep = stem.find(" - ");
  if (sep != std::string::npos && sep > 0 && sep + 3 < stem.size()) {
    t.artist = stem.substr(0, sep);
    t.title = stem.substr(sep + 3);
  } else {
    t.artist = "Unknown Artist";
    t.title = stem;
  }
}
std::string trackDurationLabel(double) { return {}; }

}  // namespace
bool Library::isSupportedExtension(const std::string& path) {
  const std::string ext = toLower(path.substr(path.find_last_of('.') + 1));
  return ext == "mp3" || ext == "flac" || ext == "wav" || ext == "wave";
}

std::string Library::findLyricFile(const std::string& trackPath) {
  const size_t dot = trackPath.find_last_of('.');
  if (dot == std::string::npos) return {};
  baseNameNoExt(trackPath);
  const std::string stem = trackPath.substr(0, dot);
  const std::string lrcLower = stem + ".lrc";
  const std::string lrcUpper = stem + ".LRC";
  struct stat st {};
  if (stat(lrcLower.c_str(), &st) == 0) return lrcLower;
  if (stat(lrcUpper.c_str(), &st) == 0) return lrcUpper;
  return {};
}

void Library::scanDir(const std::string& dir, int depth) {
  if (depth > 8) return;
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  struct dirent* ent = nullptr;
  while ((ent = readdir(d)) != nullptr) {
    const std::string name = ent->d_name;
    if (name == "." || name == "..") continue;
    const std::string full = dir + "/" + name;

    struct stat st {};
    if (stat(full.c_str(), &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      scanDir(full, depth + 1);
      continue;
    }
    if (!isSupportedExtension(full)) continue;

    Track t;
    t.path = full;
    splitArtistTitle(baseNameNoExt(full), t);
    t.album = dir.substr(dir.find_last_of('/') + 1);

    // Cheap duration probe; skipped for very large libraries on purpose.
    if (tracks_.size() < 2000) {
      Decoder probe;
      if (probe.open(full)) t.durationSec = probe.durationSec();
    }
    tracks_.push_back(std::move(t));
  }
  closedir(d);
}

size_t Library::scan() {
  tracks_.clear();
  root_.clear();
  lastError_.clear();

  for (const char* cand : kRootCandidates) {
    if (dirExists(cand)) {
      root_ = cand;
      break;
    }
  }
  if (root_.empty()) {
    lastError_ = "No music folder. Create sdmc:/switch/SPlayer-Next/music";
    return 0;
  }

  scanDir(root_, 0);
  std::sort(tracks_.begin(), tracks_.end(), [](const Track& a, const Track& b) {
    if (a.artist != b.artist) return a.artist < b.artist;
    return a.title < b.title;
  });
  return tracks_.size();
}

}  // namespace spn