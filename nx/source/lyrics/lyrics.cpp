// SPDX-License-Identifier: AGPL-3.0
// LRC lyric parsing + active-line lookup.
#include "spn/lyrics.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace spn {

namespace {

bool readWholeFile(const std::string& path, std::string& out) {
  FILE* fp = std::fopen(path.c_str(), "rb");
  if (!fp) return false;
  std::fseek(fp, 0, SEEK_END);
  const long size = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);
  if (size < 0) {
    std::fclose(fp);
    return false;
  }
  out.resize(static_cast<size_t>(size));
  const size_t got = out.empty() ? 0 : std::fread(&out[0], 1, out.size(), fp);
  std::fclose(fp);
  if (got != out.size()) return false;
  // Strip a UTF-8 BOM if present.
  if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
      static_cast<unsigned char>(out[1]) == 0xBB &&
      static_cast<unsigned char>(out[2]) == 0xBF) {
    out.erase(0, 3);
  }
  return true;
}

void trimInPlace(std::string& s) {
  size_t b = 0;
  while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
  size_t e = s.size();
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
  s = s.substr(b, e - b);
}

}  // namespace

bool Lyrics::loadFile(const std::string& path) {
  std::string content;
  if (!readWholeFile(path, content)) return false;
  parse(content);
  return true;
}

void Lyrics::parse(const std::string& content) {
  lines_.clear();
  int64_t offsetMs = 0;

  size_t pos = 0;
  while (pos <= content.size()) {
    size_t nl = content.find('\n', pos);
    if (nl == std::string::npos) nl = content.size();
    std::string raw = content.substr(pos, nl - pos);
    pos = nl + 1;
    trimInPlace(raw);
    if (raw.empty()) {
      if (pos > content.size()) break;
      continue;
    }

    // Collect every leading [mm:ss.xx] timestamp.
    std::vector<uint64_t> stamps;
    size_t i = 0;
    while (i < raw.size() && raw[i] == '[') {
      const size_t close = raw.find(']', i);
      if (close == std::string::npos) break;
      const std::string tag = raw.substr(i + 1, close - i - 1);

      // [offset:+250] style metadata.
      if (tag.rfind("offset:", 0) == 0) {
        offsetMs = std::atoll(tag.c_str() + 7);
        i = close + 1;
        continue;
      }

      const size_t colon = tag.find(':');
      if (colon == std::string::npos || colon == 0) break;
      const std::string mmStr = tag.substr(0, colon);
      const std::string ssStr = tag.substr(colon + 1);

      bool digits = !mmStr.empty();
      for (char c : mmStr) {
        if (c < '0' || c > '9') digits = false;
      }
      if (!digits) break;
      if (ssStr.empty()) break;
      for (char c : ssStr) {
        if (!((c >= '0' && c <= '9') || c == '.' || c == ':')) {
          digits = false;
        }
      }
      if (!digits) break;

      const long minutes = std::atol(mmStr.c_str());
      double seconds = 0.0;
      // "ss.xx" or "ss:xx" (some producers use a second colon).
      size_t sep = ssStr.find_first_of(".:");
      if (sep == std::string::npos) {
        seconds = std::atof(ssStr.c_str());
      } else {
        seconds = std::atof(ssStr.substr(0, sep).c_str());
        const std::string frac = ssStr.substr(sep + 1);
        double fracVal = std::atof(frac.c_str());
        if (frac.size() == 1) {
          fracVal /= 10.0;
        } else if (frac.size() == 2) {
          fracVal /= 100.0;
        } else if (frac.size() >= 3) {
          fracVal /= 1000.0;
        } else {
          fracVal = 0.0;
        }
        seconds += fracVal;
      }
      stamps.push_back(uint64_t((double(minutes) * 60.0 + seconds) * 1000.0));
      i = close + 1;
    }

    if (stamps.empty()) {
      if (pos > content.size()) break;
      continue;
    }

    std::string text = raw.substr(i);
    trimInPlace(text);
    for (uint64_t ts : stamps) {
      LyricLine line;
      line.timeMs = ts;
      line.text = text;
      lines_.push_back(std::move(line));
    }
    if (pos > content.size()) break;
  }

  if (offsetMs != 0) {
    for (auto& l : lines_) {
      const int64_t shifted = int64_t(l.timeMs) + offsetMs;
      l.timeMs = shifted > 0 ? uint64_t(shifted) : 0;
    }
  }

  std::stable_sort(lines_.begin(), lines_.end(),
                   [](const LyricLine& a, const LyricLine& b) {
                     return a.timeMs < b.timeMs;
                   });
  // Drop consecutive duplicates that appear when a file mixes formats.
  lines_.erase(std::unique(lines_.begin(), lines_.end(),
                           [](const LyricLine& a, const LyricLine& b) {
                             return a.timeMs == b.timeMs && a.text == b.text;
                           }),
               lines_.end());
}

int Lyrics::activeIndex(uint64_t positionMs) const {
  if (lines_.empty()) return -1;
  int idx = -1;
  for (size_t i = 0; i < lines_.size(); ++i) {
    if (lines_[i].timeMs <= positionMs) {
      idx = int(i);
    } else {
      break;
    }
  }
  return idx;
}

}  // namespace spn