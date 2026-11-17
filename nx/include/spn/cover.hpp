// SPDX-License-Identifier: AGPL-3.0
// Lazy, bounded cover-art cache.
//
// Scanning reads tags for every track, but decoding thousands of embedded
// covers up-front would blow the Switch's memory budget. Instead a track only
// records whether the container *has* a picture; the pixels are decoded on
// first use and kept in a small LRU set.
#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "spn/image.hpp"

namespace spn {

class CoverCache {
 public:
  // Maximum number of decoded images kept resident. 48 covers at 256px RGBA is
  // roughly 12 MB, which fits comfortably next to the audio ring buffers.
  static constexpr size_t kCapacity = 48;

  // Registers a track whose container carries an embedded picture and returns
  // a stable id for it. Returns -1 when the file has no usable picture.
  int registerTrack(const std::string& path);

  // Same, but the caller already knows whether the container has a picture
  // (saves re-reading the tag block during a library scan).
  int add(const std::string& path, bool hasCover);

  // Decoded image for `id`, or nullptr when unavailable.
  const Image* get(int id);

  // Accent colour derived from the cover; falls back to the UI accent.
  uint32_t accentFor(int id, uint32_t fallback);

  void reset();
  size_t registeredCount() const { return entries_.size(); }
  size_t residentCount() const { return resident_; }

 private:
  struct Entry {
    std::string path;
    bool loaded = false;
    bool failed = false;
    Image image;
    uint32_t accent = 0;
  };

  void evictIfNeeded();

  std::vector<Entry> entries_;
  size_t resident_ = 0;
  std::unordered_map<int, std::list<int>::iterator> lruPos_;
  std::list<int> lru_;  // front == most recently used
};

}  // namespace spn