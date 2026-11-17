// SPDX-License-Identifier: AGPL-3.0
// Online track cache (see online_player.hpp).

#include "spn/online_player.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "spn/http.hpp"

namespace spn::online {
namespace {

// Picks the container extension from the stream URL. The CDN puts the real
// extension at the end of the path, before any query string.
std::string extensionOf(const std::string& url) {
  std::string path = url;
  const size_t q = path.find('?');
  if (q != std::string::npos) path.resize(q);
  const size_t slash = path.rfind('/');
  const size_t dot = path.rfind('.');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
    std::string ext = path.substr(dot);
    if (ext == ".mp3" || ext == ".flac" || ext == ".m4a" || ext == ".wav") return ext;
  }
  return ".mp3";  // exhigh/standard always yield mp3
}

size_t writeFile(void* handle, const void* data, size_t n) {
  return std::fwrite(data, 1, n, static_cast<std::FILE*>(handle));
}

struct ProgressCtx {
  int64_t expected = 0;
  std::atomic<float>* out = nullptr;
  std::atomic<bool>* cancel = nullptr;
};

void onProgress(void* user, size_t bytes) {
  auto* ctx = static_cast<ProgressCtx*>(user);
  if (ctx->expected > 0) {
    const float p = float(bytes) / float(ctx->expected);
    ctx->out->store(p > 1.f ? 1.f : p);
  }
}

}  // namespace

const char* cacheStateName(CacheState s) {
  switch (s) {
    case CacheState::Idle: return "空闲";
    case CacheState::Downloading: return "缓冲中";
    case CacheState::Ready: return "就绪";
    case CacheState::Failed: return "失败";
  }
  return "未知";
}

const std::string& TrackCache::cacheDir() {
  static std::string dir = "sdmc:/switch/SPlayer-Next/cache";
  return dir;
}

void TrackCache::setCacheDir(const std::string& dir) {
  const_cast<std::string&>(cacheDir()) = dir.empty() ? "sdmc:/switch/SPlayer-Next/cache" : dir;
}

std::string TrackCache::pathFor(int64_t songId, const std::string& url) {
  return cacheDir() + "/" + std::to_string(songId) + extensionOf(url);
}

bool TrackCache::lookup(int64_t songId, std::string& path) {
  // Probe the extensions rather than guessing, since the caller rarely carries
  // the original url around by the time it wants to replay something.
  static const char* exts[] = {".mp3", ".flac", ".m4a", ".wav"};
  for (const char* e : exts) {
    const std::string p = cacheDir() + "/" + std::to_string(songId) + e;
    struct stat st{};
    if (::stat(p.c_str(), &st) == 0 && st.st_size > 1024) {
      path = p;
      return true;
    }
  }
  return false;
}

TrackCache::~TrackCache() {
  cancel();
  if (thread_.joinable()) thread_.join();
}

void TrackCache::cancel() {
  cancel_.store(true);
  if (thread_.joinable()) thread_.join();
  cancel_.store(false);
}

void TrackCache::begin(int64_t songId, const std::string& url) {
  cancel();

  // A cached file means play starts instantly, so short-circuit before
  // spawning anything at all.
  std::string existing;
  if (lookup(songId, existing)) {
    std::lock_guard<std::mutex> lock(errMutex_);
    resultPath_ = existing;
    error_.clear();
    progress_.store(1.f);
    state_.store(CacheState::Ready);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(errMutex_);
    resultPath_.clear();
    error_.clear();
  }
  progress_.store(0.f);
  state_.store(CacheState::Downloading);
  cancel_.store(false);
  thread_ = std::thread(&TrackCache::threadMain, this, songId, url);
}

void TrackCache::threadMain(int64_t songId, std::string url) {
  const std::string finalPath = pathFor(songId, url);
  const std::string partPath = finalPath + ".part";

  // The cache directory may not exist yet on first use.
  ::mkdir(cacheDir().c_str(), 0777);

  std::FILE* f = std::fopen(partPath.c_str(), "wb");
  if (!f) {
    std::lock_guard<std::mutex> lock(errMutex_);
    error_ = "无法写入缓存目录";
    state_.store(CacheState::Failed);
    return;
  }

  // First ask for the headers so the progress bar knows the denominator.
  int64_t expected = 0;
  {
    const auto head = http::get(url, {{"User-Agent", "Mozilla/5.0"}}, "bytes=0-0", 15000);
    const std::string cr = head.header("content-range");  // "bytes 0-0/8941485"
    const size_t slash = cr.find('/');
    if (slash != std::string::npos) expected = std::atoll(cr.c_str() + slash + 1);
    if (!head.header("content-length").empty() && expected == 0) {
      expected = std::atoll(head.header("content-length").c_str());
    }
  }

  ProgressCtx ctx;
  ctx.expected = expected;
  ctx.out = &progress_;
  ctx.cancel = &cancel_;

  std::string err;
  const int status =
      http::downloadToStream(url, f, {{"User-Agent", "Mozilla/5.0"}}, 25000, writeFile,
                             onProgress, &ctx, err);
  std::fclose(f);

  const bool cancelled = cancel_.load();
  if (status != 200 || cancelled) {
    ::unlink(partPath.c_str());
    std::lock_guard<std::mutex> lock(errMutex_);
    if (cancelled) {
      state_.store(CacheState::Idle);
    } else {
      error_ = err.empty() ? "下载失败" : err;
      state_.store(CacheState::Failed);
    }
    return;
  }

  // Only a completed download becomes visible to lookup().
  if (std::rename(partPath.c_str(), finalPath.c_str()) != 0) {
    ::unlink(partPath.c_str());
    std::lock_guard<std::mutex> lock(errMutex_);
    error_ = "缓存重命名失败";
    state_.store(CacheState::Failed);
    return;
  }

  std::lock_guard<std::mutex> lock(errMutex_);
  resultPath_ = finalPath;
  progress_.store(1.f);
  state_.store(CacheState::Ready);
}

std::string TrackCache::error() const {
  std::lock_guard<std::mutex> lock(errMutex_);
  return error_;
}

std::string TrackCache::resultPath() const {
  std::lock_guard<std::mutex> lock(errMutex_);
  return resultPath_;
}

uint64_t TrackCache::cacheBytes() {
  uint64_t total = 0;
  DIR* d = ::opendir(cacheDir().c_str());
  if (!d) return 0;
  while (dirent* e = ::readdir(d)) {
    const std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    const std::string full = cacheDir() + "/" + name;
    struct stat st{};
    if (::stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
      total += uint64_t(st.st_size);
    }
  }
  ::closedir(d);
  return total;
}

void TrackCache::clearCache() {
  DIR* d = ::opendir(cacheDir().c_str());
  if (!d) return;
  std::vector<std::string> doomed;
  while (dirent* e = ::readdir(d)) {
    const std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    doomed.push_back(cacheDir() + "/" + name);
  }
  ::closedir(d);
  for (const auto& p : doomed) ::unlink(p.c_str());
}

}  // namespace spn::online