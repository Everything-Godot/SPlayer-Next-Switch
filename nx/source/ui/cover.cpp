// SPDX-License-Identifier: AGPL-3.0
// Lazy, bounded cover-art cache.
#include "spn/cover.hpp"

#include <algorithm>

#include "spn/tag.hpp"

namespace spn {

int CoverCache::registerTrack(const std::string& path) {
  TagInfo tag;
  if (!readTags(path, tag) || !tag.hasCover()) return -1;
  return add(path, true);
}

int CoverCache::add(const std::string& path, bool hasCover) {
  if (!hasCover) return -1;
  Entry e;
  e.path = path;
  entries_.push_back(std::move(e));
  return int(entries_.size()) - 1;
}

void CoverCache::evictIfNeeded() {
  while (resident_ > kCapacity && !lru_.empty()) {
    const int victim = lru_.back();
    lru_.pop_back();
    lruPos_.erase(victim);
    Entry& e = entries_[size_t(victim)];
    if (e.loaded) {
      e.loaded = false;
      e.image.reset();
      if (resident_ > 0) --resident_;
    }
  }
}

const Image* CoverCache::get(int id) {
  if (id < 0 || id >= int(entries_.size())) return nullptr;
  Entry& e = entries_[size_t(id)];

  // Touch the LRU entry.
  auto it = lruPos_.find(id);
  if (it != lruPos_.end()) lru_.erase(it->second);
  lru_.push_front(id);
  lruPos_[id] = lru_.begin();

  if (e.loaded) return e.image.valid() ? &e.image : nullptr;
  if (e.failed) return nullptr;

  TagInfo tag;
  bool ok = false;
  if (readTags(e.path, tag) && tag.hasCover()) {
    ok = Image::decode(tag.cover.data(), tag.cover.size(), e.image);
  }
  if (!ok) {
    e.failed = true;
    return nullptr;
  }

  e.loaded = true;
  e.accent = e.image.dominantColor();
  ++resident_;
  evictIfNeeded();

  // `e` may have been invalidated only if `id` was the eviction victim, which
  // cannot happen here since we just touched it. Re-check defensively anyway.
  if (id >= int(entries_.size())) return nullptr;
  return entries_[size_t(id)].image.valid() ? &entries_[size_t(id)].image : nullptr;
}

uint32_t CoverCache::accentFor(int id, uint32_t fallback) {
  if (id < 0 || id >= int(entries_.size())) return fallback;
  const Image* img = get(id);
  if (!img) return fallback;
  Entry& e = entries_[size_t(id)];
  return e.accent ? e.accent : fallback;
}

void CoverCache::reset() {
  entries_.clear();
  lru_.clear();
  lruPos_.clear();
  resident_ = 0;
}

}  // namespace spn