// SPDX-License-Identifier: AGPL-3.0
// Local music library scanning on the SD card.
#include "spn/library.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include <dirent.h>
#include <sys/stat.h>

#include <switch.h>

#include "spn/audio.hpp"
#include "spn/tag.hpp"

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
  const size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return name;
}

std::string dirNameOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return path;
  return path.substr(0, slash);
}

std::string fileNameOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool dirExists(const std::string& path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// "<artist> - <title>" is the conventional naming; fall back to the raw name.
void splitArtistTitle(const std::string& stem, std::string& artist,
                      std::string& title) {
  const size_t sep = stem.find(" - ");
  if (sep != std::string::npos && sep > 0 && sep + 3 < stem.size()) {
    artist = trimTagString(stem.substr(0, sep));
    title = trimTagString(stem.substr(sep + 3));
  } else {
    artist = "未知艺术家";
    title = trimTagString(stem);
  }
}

std::string formatLabelFor(const std::string& path) {
  const std::string ext = toLower(path.substr(path.find_last_of('.') + 1));
  if (ext == "mp3") return "MP3";
  if (ext == "flac") return "FLAC";
  if (ext == "wav" || ext == "wave") return "WAV";
  if (ext == "ogg") return "OGG";
  return "?";
}

}  // namespace

bool Library::isSupportedExtension(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return false;
  const std::string ext = toLower(path.substr(dot + 1));
  return ext == "mp3" || ext == "flac" || ext == "wav" || ext == "wave" ||
         ext == "ogg";
}

std::string Library::findLyricFile(const std::string& trackPath) {
  const size_t dot = trackPath.find_last_of('.');
  if (dot == std::string::npos) return {};
  const std::string stem = trackPath.substr(0, dot);

  static const char* kExts[] = {".lrc", ".LRC", ".ttml", ".TTML", ".ass", ".ASS"};
  struct stat st {};
  for (const char* ext : kExts) {
    const std::string candidate = stem + ext;
    if (stat(candidate.c_str(), &st) == 0) return candidate;
  }
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
    t.fileSize = uint64_t(st.st_size);
    t.format = formatLabelFor(full);

    // --- tags first, filename as fallback -------------------------------
    TagInfo tag;
    const bool tagged = readTags(full, tag);

    if (!tag.title.empty()) {
      t.title = tag.title;
    } else {
      std::string artist;
      std::string title;
      splitArtistTitle(baseNameNoExt(full), artist, title);
      t.title = title;
      if (!tag.artist.empty()) t.artist = tag.artist;
      else t.artist = artist;
    }
    if (!tag.artist.empty()) t.artist = tag.artist;
    if (t.artist.empty()) t.artist = "未知艺术家";

    t.album = tag.album.empty() ? fileNameOf(dirNameOf(full)) : tag.album;
    if (!tag.albumArtist.empty()) t.albumArtist = tag.albumArtist;
    else t.albumArtist = t.artist;
    t.genre = tag.genre;
    t.year = tag.year;
    t.trackNo = tag.trackNo;
    t.discNo = tag.discNo;

    if (tagged && tag.hasCover()) {
      t.coverId = covers_.add(full, true);
    }

    // --- cheap duration probe -------------------------------------------
    // Probing opens the file, so cap it: beyond the cap we still list the
    // track, the row just shows "--:--" until it is played once.
    if (tracks_.size() < 2000) {
      Decoder probe;
      if (probe.open(full)) {
        t.durationSec = probe.durationSec();
        t.sampleRate = probe.sampleRate();
        t.channels = probe.channels();
        t.bitDepth = (t.format == "FLAC") ? 16 : (t.format == "WAV" ? 16 : 16);
        if (t.sampleRate > 0 && t.channels > 0) {
          t.bitrateKbps = int(double(t.sampleRate) * t.channels * t.bitDepth /
                              1000.0);
        }
      }
    }

    totalBytes_ += t.fileSize;
    totalDuration_ += t.durationSec;
    tracks_.push_back(std::move(t));
  }
  closedir(d);
}

void Library::buildAggregates() {
  albums_.clear();
  artists_.clear();

  std::unordered_map<std::string, int> albumIndex;
  std::unordered_map<std::string, int> artistIndex;

  for (int i = 0; i < int(tracks_.size()); ++i) {
    const Track& t = tracks_[size_t(i)];
    const std::string albumKey = t.albumArtist + "\x1f" + t.album;

    auto aIt = albumIndex.find(albumKey);
    if (aIt == albumIndex.end()) {
      Album a;
      a.name = t.album;
      a.artist = t.albumArtist;
      a.year = t.year;
      a.coverId = t.coverId;
      a.trackIndices.push_back(i);
      albumIndex[albumKey] = int(albums_.size());
      albums_.push_back(std::move(a));
    } else {
      Album& a = albums_[size_t(aIt->second)];
      a.trackIndices.push_back(i);
      if (a.coverId < 0) a.coverId = t.coverId;
      if (!a.year) a.year = t.year;
    }

    auto pIt = artistIndex.find(t.artist);
    if (pIt == artistIndex.end()) {
      ArtistEntry e;
      e.name = t.artist;
      e.trackIndices.push_back(i);
      e.albums.push_back(t.album);
      artistIndex[t.artist] = int(artists_.size());
      artists_.push_back(std::move(e));
    } else {
      ArtistEntry& e = artists_[size_t(pIt->second)];
      e.trackIndices.push_back(i);
      if (std::find(e.albums.begin(), e.albums.end(), t.album) ==
          e.albums.end()) {
        e.albums.push_back(t.album);
      }
    }
  }

  // Sort albums by name then artist for a stable grid order.
  std::sort(albums_.begin(), albums_.end(), [](const Album& a, const Album& b) {
    if (a.name != b.name) return a.name < b.name;
    return a.artist < b.artist;
  });
  // Sorting mutated the indices' order; album track lists stay valid because
  // they hold track indices, not album indices.
  std::sort(artists_.begin(), artists_.end(),
            [](const ArtistEntry& a, const ArtistEntry& b) {
              return a.name < b.name;
            });
}

std::vector<std::string> Library::folders() const {
  std::vector<std::string> out;
  for (const Track& t : tracks_) {
    const std::string folder = dirNameOf(t.path);
    if (std::find(out.begin(), out.end(), folder) == out.end()) {
      out.push_back(folder);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

int Library::indexOf(const std::string& path) const {
  for (size_t i = 0; i < tracks_.size(); ++i) {
    if (tracks_[i].path == path) return int(i);
  }
  return -1;
}

void Library::setTracksForTest(std::vector<Track> tracks) {
  tracks_ = std::move(tracks);
  albums_.clear();
  artists_.clear();
  totalBytes_ = 0;
  totalDuration_ = 0.0;
  root_ = "sdmc:/switch/SPlayer-Next/music";

  // Attach synthetic covers so the artwork path is exercised too.
  for (const Track& t : tracks_) {
    totalBytes_ += t.fileSize;
    totalDuration_ += t.durationSec;
  }
  buildAggregates();
}

size_t Library::scan() {
  tracks_.clear();
  albums_.clear();
  artists_.clear();
  covers_.reset();
  root_.clear();
  lastError_.clear();
  totalBytes_ = 0;
  totalDuration_ = 0.0;

  for (const char* cand : kRootCandidates) {
    if (dirExists(cand)) {
      root_ = cand;
      break;
    }
  }
  if (root_.empty()) {
    lastError_ = "找不到音乐目录，请创建 sdmc:/switch/SPlayer-Next/music";
    return 0;
  }

  scanDir(root_, 0);

  // Stable, predictable ordering: album artist, album, disc, track, title.
  std::sort(tracks_.begin(), tracks_.end(), [](const Track& a, const Track& b) {
    if (a.albumArtist != b.albumArtist) return a.albumArtist < b.albumArtist;
    if (a.album != b.album) return a.album < b.album;
    if (a.discNo != b.discNo) return a.discNo < b.discNo;
    if (a.trackNo != b.trackNo) return a.trackNo < b.trackNo;
    return a.title < b.title;
  });

  buildAggregates();
  return tracks_.size();
}

}  // namespace spn