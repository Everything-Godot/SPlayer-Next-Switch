// SPDX-License-Identifier: AGPL-3.0
// Persisted user state: likes, play history, playlists and settings.
#include "spn/store.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>

namespace spn {
namespace {

constexpr const char* kRoot = "sdmc:/switch/SPlayer-Next";
constexpr const char* kStateDir = "sdmc:/switch/SPlayer-Next/state";
constexpr const char* kLikesFile = "sdmc:/switch/SPlayer-Next/state/likes.txt";
constexpr const char* kHistoryFile = "sdmc:/switch/SPlayer-Next/state/history.txt";
constexpr const char* kPlaylistsFile =
    "sdmc:/switch/SPlayer-Next/state/playlists.txt";
constexpr const char* kSettingsFile =
    "sdmc:/switch/SPlayer-Next/state/settings.txt";

// Paths may contain spaces or '|', so escape the separator characters when
// writing a record and unescape on read.
std::string escapeField(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\\') out += "\\\\";
    else if (c == '|') out += "\\p";
    else if (c == '\n') out += "\\n";
    else out.push_back(c);
  }
  return out;
}

std::string unescapeField(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char n = s[i + 1];
      if (n == '\\') {
        out.push_back('\\');
        ++i;
      } else if (n == 'p') {
        out.push_back('|');
        ++i;
      } else if (n == 'n') {
        out.push_back('\n');
        ++i;
      } else {
        out.push_back(s[i]);
      }
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

std::vector<std::string> splitFields(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '\\' && i + 1 < line.size()) {
      cur.push_back(line[i]);
      cur.push_back(line[i + 1]);
      ++i;
      continue;
    }
    if (line[i] == '|') {
      out.push_back(cur);
      cur.clear();
      continue;
    }
    cur.push_back(line[i]);
  }
  out.push_back(cur);
  return out;
}

bool readLines(const char* path, std::vector<std::string>& out) {
  FILE* fp = std::fopen(path, "rb");
  if (!fp) return false;
  char buf[4096];
  std::string line;
  while (std::fgets(buf, sizeof(buf), fp)) {
    line.assign(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
      line.pop_back();
    }
    if (!line.empty()) out.push_back(line);
    line.clear();
  }
  std::fclose(fp);
  return true;
}

bool writeLines(const char* path, const std::vector<std::string>& lines) {
  FILE* fp = std::fopen(path, "wb");
  if (!fp) return false;
  for (const std::string& l : lines) {
    std::fwrite(l.data(), 1, l.size(), fp);
    std::fputc('\n', fp);
  }
  std::fclose(fp);
  return true;
}

bool ensureDirs() {
  mkdir(kRoot, 0777);
  mkdir(kStateDir, 0777);
  struct stat st {};
  return stat(kStateDir, &st) == 0 && S_ISDIR(st.st_mode);
}

}  // namespace

const std::string& Store::stateDir() {
  static const std::string dir = kStateDir;
  return dir;
}

bool Store::load() {
  if (!ensureDirs()) return false;

  // --- likes ---
  likes_.clear();
  std::vector<std::string> lines;
  if (readLines(kLikesFile, lines)) {
    for (const std::string& l : lines) likes_.push_back(unescapeField(l));
  }
  lines.clear();

  // --- history: "<escaped path>|<unix>|<count>" ---
  history_.clear();
  if (readLines(kHistoryFile, lines)) {
    for (const std::string& l : lines) {
      const auto f = splitFields(l);
      if (f.size() < 3) continue;
      HistoryEntry e;
      e.path = unescapeField(f[0]);
      e.playedAt = uint64_t(std::strtoull(f[1].c_str(), nullptr, 10));
      e.playCount = uint32_t(std::strtoul(f[2].c_str(), nullptr, 10));
      if (!e.path.empty()) history_.push_back(std::move(e));
    }
  }
  lines.clear();

  // --- playlists: "name|path|path|..." ---
  playlists_.clear();
  if (readLines(kPlaylistsFile, lines)) {
    for (const std::string& l : lines) {
      const auto f = splitFields(l);
      if (f.empty()) continue;
      SavedPlaylist p;
      p.name = unescapeField(f[0]);
      for (size_t i = 1; i < f.size(); ++i) {
        const std::string path = unescapeField(f[i]);
        if (!path.empty()) p.paths.push_back(path);
      }
      if (!p.name.empty()) playlists_.push_back(std::move(p));
    }
  }
  lines.clear();

  // --- settings: "key=value" ---
  if (readLines(kSettingsFile, lines)) {
    for (const std::string& l : lines) {
      const size_t eq = l.find('=');
      if (eq == std::string::npos) continue;
      const std::string k = l.substr(0, eq);
      const std::string v = l.substr(eq + 1);
      if (k == "volume") settings_.volume = float(std::atof(v.c_str()));
      else if (k == "shuffle") settings_.shuffle = (v == "1");
      else if (k == "repeat") settings_.repeatMode = std::atoi(v.c_str());
      else if (k == "crossfade") settings_.crossfade = (v == "1");
      else if (k == "theme") settings_.themeIndex = std::atoi(v.c_str());
      else if (k == "showLyrics") settings_.showLyrics = (v == "1");
      else if (k == "lyricFontSize") settings_.lyricFontSize = std::atoi(v.c_str());
      else if (k == "lastTrack") lastTrack_ = unescapeField(v);
      else if (k == "listeningSec") {
        listeningSec_ = uint64_t(std::strtoull(v.c_str(), nullptr, 10));
      }
    }
  }

  if (settings_.volume < 0.f) settings_.volume = 0.f;
  if (settings_.volume > 1.f) settings_.volume = 1.f;
  return true;
}

bool Store::saveSettings() const {
  std::vector<std::string> lines;
  lines.push_back("volume=" + std::to_string(settings_.volume));
  lines.push_back(std::string("shuffle=") + (settings_.shuffle ? "1" : "0"));
  lines.push_back("repeat=" + std::to_string(settings_.repeatMode));
  lines.push_back(std::string("crossfade=") + (settings_.crossfade ? "1" : "0"));
  lines.push_back("theme=" + std::to_string(settings_.themeIndex));
  lines.push_back(std::string("showLyrics=") +
                  (settings_.showLyrics ? "1" : "0"));
  lines.push_back("lyricFontSize=" + std::to_string(settings_.lyricFontSize));
  lines.push_back("lastTrack=" + escapeField(lastTrack_));
  lines.push_back("listeningSec=" + std::to_string(listeningSec_));
  return writeLines(kSettingsFile, lines);
}

bool Store::saveLikes() const {
  std::vector<std::string> lines;
  lines.reserve(likes_.size());
  for (const std::string& p : likes_) lines.push_back(escapeField(p));
  return writeLines(kLikesFile, lines);
}

bool Store::saveHistory() const {
  std::vector<std::string> lines;
  lines.reserve(history_.size());
  for (const HistoryEntry& e : history_) {
    lines.push_back(escapeField(e.path) + "|" + std::to_string(e.playedAt) +
                    "|" + std::to_string(e.playCount));
  }
  return writeLines(kHistoryFile, lines);
}

bool Store::save() const {
  bool ok = ensureDirs();
  ok = saveLikes() && ok;
  ok = saveHistory() && ok;
  ok = saveSettings() && ok;

  std::vector<std::string> lines;
  for (const SavedPlaylist& p : playlists_) {
    std::string line = escapeField(p.name);
    for (const std::string& path : p.paths) line += "|" + escapeField(path);
    lines.push_back(std::move(line));
  }
  ok = writeLines(kPlaylistsFile, lines) && ok;
  return ok;
}

// --- likes -----------------------------------------------------------------

bool Store::isLiked(const std::string& path) const {
  for (const std::string& p : likes_) {
    if (p == path) return true;
  }
  return false;
}

void Store::setLiked(const std::string& path, bool liked) {
  const bool has = isLiked(path);
  if (liked && !has) {
    likes_.push_back(path);
    saveLikes();
  } else if (!liked && has) {
    for (size_t i = 0; i < likes_.size(); ++i) {
      if (likes_[i] == path) {
        likes_.erase(likes_.begin() + long(i));
        break;
      }
    }
    saveLikes();
  }
}

void Store::toggleLiked(const std::string& path) { setLiked(path, !isLiked(path)); }

// --- history ---------------------------------------------------------------

void Store::recordPlay(const std::string& path, uint64_t unixNow) {
  for (HistoryEntry& e : history_) {
    if (e.path == path) {
      e.playedAt = unixNow;
      ++e.playCount;
      saveHistory();
      return;
    }
  }
  HistoryEntry e;
  e.path = path;
  e.playedAt = unixNow;
  e.playCount = 1;
  history_.insert(history_.begin(), std::move(e));
  if (int(history_.size()) > kMaxHistory) {
    history_.resize(size_t(kMaxHistory));
  }
  saveHistory();
}

size_t Store::totalPlayCount() const {
  size_t n = 0;
  for (const HistoryEntry& e : history_) n += e.playCount;
  return n;
}

void Store::clearHistory() {
  history_.clear();
  saveHistory();
}

// --- playlists -------------------------------------------------------------

bool Store::createPlaylist(const std::string& name) {
  if (name.empty()) return false;
  for (const SavedPlaylist& p : playlists_) {
    if (p.name == name) return false;
  }
  playlists_.push_back(SavedPlaylist{name, {}});
  save();
  return true;
}

bool Store::addToPlaylist(const std::string& name, const std::string& path) {
  for (SavedPlaylist& p : playlists_) {
    if (p.name != name) continue;
    for (const std::string& existing : p.paths) {
      if (existing == path) return false;
    }
    p.paths.push_back(path);
    save();
    return true;
  }
  return false;
}

bool Store::removePlaylist(const std::string& name) {
  for (size_t i = 0; i < playlists_.size(); ++i) {
    if (playlists_[i].name == name) {
      playlists_.erase(playlists_.begin() + long(i));
      save();
      return true;
    }
  }
  return false;
}

}  // namespace spn