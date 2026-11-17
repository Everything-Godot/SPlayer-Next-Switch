// SPDX-License-Identifier: AGPL-3.0
// Minimal HTTP/1.1 client over POSIX sockets (see http.hpp).

#include "spn/http.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace spn::http {
namespace {

std::string toLower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  }
  return s;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
  return s.substr(a, b - a);
}

// Decodes a chunked transfer-encoded body.
bool decodeChunked(const std::string& in, std::string& out, size_t maxBytes) {
  size_t i = 0;
  while (i < in.size()) {
    const size_t eol = in.find("\r\n", i);
    if (eol == std::string::npos) return false;
    const std::string sizeLine = in.substr(i, eol - i);
    // Chunk extensions after ';' are legal but never used by this API.
    size_t semi = sizeLine.find(';');
    const std::string hex = trim(semi == std::string::npos ? sizeLine : sizeLine.substr(0, semi));
    if (hex.empty()) return false;
    char* end = nullptr;
    const unsigned long long chunk = std::strtoull(hex.c_str(), &end, 16);
    if (end == hex.c_str()) return false;
    i = eol + 2;
    if (chunk == 0) return true;  // last chunk
    if (i + chunk > in.size()) return false;
    out.append(in, i, size_t(chunk));
    if (maxBytes && out.size() >= maxBytes) {
      out.resize(maxBytes);
      return true;
    }
    i += size_t(chunk) + 2;  // skip payload + CRLF
  }
  return true;
}

// --- signal-safe socket wrappers -------------------------------------------
// A blocking connect()/send()/recv() returns EINTR whenever a signal lands on
// the thread. That happens in practice (SIGCHLD from system()/fork, alarm
// timers, the Switch's own late-initialised services), and treating it as a
// hard failure made downloads die with "Interrupted system call" before the
// first byte. Every socket call therefore retries on EINTR instead of bailing.

int connectRetry(int fd, const sockaddr* addr, socklen_t len) {
  for (;;) {
    if (::connect(fd, addr, len) == 0) return 0;
    if (errno != EINTR) return -1;
  }
}

ssize_t sendRetry(int fd, const char* data, size_t n) {
  for (;;) {
    // MSG_NOSIGNAL keeps a dropped CDN connection from raising SIGPIPE and
    // killing the whole app; not every libc defines it, so fall back to 0.
#ifdef MSG_NOSIGNAL
    const ssize_t r = ::send(fd, data, n, MSG_NOSIGNAL);
#else
    const ssize_t r = ::send(fd, data, n, 0);
#endif
    if (r < 0 && errno == EINTR) continue;
    return r;
  }
}

ssize_t recvRetry(int fd, char* buf, size_t n) {
  for (;;) {
    const ssize_t r = ::recv(fd, buf, n, 0);
    if (r < 0 && errno == EINTR) continue;
    return r;
  }
}

}  // namespace

std::string Response::header(const std::string& name) const {
  const std::string key = toLower(name);
  for (const auto& kv : headers) {
    if (kv.first == key) return kv.second;
  }
  return {};
}

bool parseUrl(const std::string& url, std::string& host, uint16_t& port, std::string& path) {
  const std::string scheme = "http://";
  if (url.compare(0, scheme.size(), scheme) != 0) return false;
  const size_t hostStart = scheme.size();
  const size_t slash = url.find('/', hostStart);
  std::string authority = (slash == std::string::npos)
                              ? url.substr(hostStart)
                              : url.substr(hostStart, slash - hostStart);
  path = (slash == std::string::npos) ? "/" : url.substr(slash);

  port = 80;
  const size_t colon = authority.rfind(':');
  if (colon != std::string::npos) {
    host = authority.substr(0, colon);
    port = uint16_t(std::atoi(authority.c_str() + colon + 1));
  } else {
    host = authority;
  }
  return !host.empty() && port != 0;
}

Response request(const Request& req) {
  Response res;

  std::string host;
  uint16_t port = 80;
  std::string path;
  if (!parseUrl(req.url, host, port, path)) {
    res.error = "unsupported url (only http:// is handled): " + req.url;
    return res;
  }

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* ai = nullptr;
  char portStr[8];
  std::snprintf(portStr, sizeof(portStr), "%u", unsigned(port));
  const int gai = ::getaddrinfo(host.c_str(), portStr, &hints, &ai);
  if (gai != 0 || ai == nullptr) {
    res.error = "dns failed for " + host;
    return res;
  }

  int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (fd < 0) {
    ::freeaddrinfo(ai);
    res.error = "socket() failed";
    return res;
  }

  // Connect/send/recv timeouts, so a dead CDN cannot wedge the UI thread.
  timeval tv{};
  tv.tv_sec = req.timeoutMs / 1000;
  tv.tv_usec = (req.timeoutMs % 1000) * 1000;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  if (connectRetry(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
    res.error = std::string("connect failed: ") + std::strerror(errno);
    ::close(fd);
    ::freeaddrinfo(ai);
    return res;
  }
  ::freeaddrinfo(ai);

  // --- build and send the request -----------------------------------------
  std::string out;
  out.reserve(req.body.size() + 512);
  out += req.method;
  out.push_back(' ');
  out += path;
  out += " HTTP/1.1\r\nHost: ";
  out += host;
  if (port != 80) {
    out.push_back(':');
    out += std::to_string(port);
  }
  out += "\r\nConnection: close\r\n";
  for (const auto& kv : req.headers) {
    out += kv.first;
    out += ": ";
    out += kv.second;
    out += "\r\n";
  }
  if (!req.body.empty() || req.method == "POST") {
    out += "Content-Length: ";
    out += std::to_string(req.body.size());
    out += "\r\n";
  }
  out += "\r\n";
  out += req.body;

  size_t sent = 0;
  while (sent < out.size()) {
    const ssize_t n = sendRetry(fd, out.data() + sent, out.size() - sent);
    if (n <= 0) {
      res.error = std::string("send failed: ") + std::strerror(errno);
      ::close(fd);
      return res;
    }
    sent += size_t(n);
  }

  // --- read everything ----------------------------------------------------
  std::string raw;
  char buf[8192];
  for (;;) {
    const ssize_t n = recvRetry(fd, buf, sizeof(buf));
    if (n > 0) {
      raw.append(buf, size_t(n));
      // Stop early when only a prefix was requested (audio probing).
      if (req.maxBodyBytes && raw.size() > req.maxBodyBytes + 2048) break;
      continue;
    }
    break;  // 0 == peer closed, <0 == timeout/error; either way we parse what we have
  }
  ::close(fd);

  if (raw.empty()) {
    res.error = "empty response";
    return res;
  }

  // --- split headers and body --------------------------------------------
  const size_t hdrEnd = raw.find("\r\n\r\n");
  if (hdrEnd == std::string::npos) {
    res.error = "malformed response (no header terminator)";
    return res;
  }
  const std::string head = raw.substr(0, hdrEnd);
  const std::string body = raw.substr(hdrEnd + 4);

  size_t lineEnd = head.find("\r\n");
  const std::string statusLine = head.substr(0, lineEnd == std::string::npos ? head.size() : lineEnd);
  {
    // "HTTP/1.1 200 OK"
    const size_t sp = statusLine.find(' ');
    if (sp != std::string::npos) res.status = std::atoi(statusLine.c_str() + sp + 1);
  }
  size_t pos = (lineEnd == std::string::npos) ? head.size() : lineEnd + 2;
  while (pos < head.size()) {
    const size_t eol = head.find("\r\n", pos);
    const std::string line = head.substr(pos, (eol == std::string::npos ? head.size() : eol) - pos);
    const size_t colon = line.find(':');
    if (colon != std::string::npos) {
      res.headers.emplace_back(toLower(trim(line.substr(0, colon))), trim(line.substr(colon + 1)));
    }
    if (eol == std::string::npos) break;
    pos = eol + 2;
  }

  // --- body framing ------------------------------------------------------
  std::string decoded;
  const std::string te = toLower(res.header("transfer-encoding"));
  if (te.find("chunked") != std::string::npos) {
    if (!decodeChunked(body, decoded, req.maxBodyBytes)) {
      // Fall back to the raw bytes: better a partial body than nothing.
      decoded = body;
    }
    res.body = std::move(decoded);
  } else {
    res.body = body;
    const std::string cl = res.header("content-length");
    if (!cl.empty()) {
      const size_t want = size_t(std::strtoul(cl.c_str(), nullptr, 10));
      if (res.body.size() > want) res.body.resize(want);
    }
  }
  if (req.maxBodyBytes && res.body.size() > req.maxBodyBytes) {
    res.body.resize(req.maxBodyBytes);
  }

  return res;
}

Response postForm(const std::string& url,
                  const std::string& formBody,
                  const std::vector<std::pair<std::string, std::string>>& headers,
                  int timeoutMs) {
  Request r;
  r.url = url;
  r.method = "POST";
  r.body = formBody;
  r.timeoutMs = timeoutMs;
  r.headers = headers;
  r.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
  return request(r);
}

Response get(const std::string& url,
             const std::vector<std::pair<std::string, std::string>>& headers,
             const std::string& rangeHeader,
             int timeoutMs) {
  Request r;
  r.url = url;
  r.method = "GET";
  r.timeoutMs = timeoutMs;
  r.headers = headers;
  if (!rangeHeader.empty()) r.headers.emplace_back("Range", rangeHeader);
  return request(r);
}

int downloadToStream(const std::string& url,
                     void* fileHandle,
                     const std::vector<std::pair<std::string, std::string>>& headers,
                     int timeoutMs,
                     size_t (*writeFn)(void* handle, const void* data, size_t n),
                     void (*onProgress)(void* user, size_t bytes),
                     void* user,
                     std::string& err) {
  std::string host;
  uint16_t port = 80;
  std::string path;
  if (!parseUrl(url, host, port, path)) {
    err = "unsupported url: " + url.substr(0, 60);
    return 0;
  }

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* ai = nullptr;
  char portStr[8];
  std::snprintf(portStr, sizeof(portStr), "%u", unsigned(port));
  if (::getaddrinfo(host.c_str(), portStr, &hints, &ai) != 0 || !ai) {
    err = "dns failed for " + host;
    return 0;
  }

  const int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (fd < 0) {
    ::freeaddrinfo(ai);
    err = "socket() failed";
    return 0;
  }

  timeval tv{};
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  if (connectRetry(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
    err = std::string("connect failed: ") + std::strerror(errno);
    ::close(fd);
    ::freeaddrinfo(ai);
    return 0;
  }
  ::freeaddrinfo(ai);

  std::string out;
  out += "GET ";
  out += path;
  out += " HTTP/1.1\r\nHost: ";
  out += host;
  if (port != 80) {
    out.push_back(':');
    out += std::to_string(port);
  }
  out += "\r\nConnection: close\r\n";
  for (const auto& kv : headers) {
    out += kv.first;
    out += ": ";
    out += kv.second;
    out += "\r\n";
  }
  out += "\r\n";

  size_t sent = 0;
  while (sent < out.size()) {
    const ssize_t n = sendRetry(fd, out.data() + sent, out.size() - sent);
    if (n <= 0) {
      err = std::string("send failed: ") + std::strerror(errno);
      ::close(fd);
      return 0;
    }
    sent += size_t(n);
  }

  // Read the header first, then stream the body straight into the file so a
  // 10 MB track never has to be held in memory.
  std::string head;
  char buf[16384];
  size_t bodyStart = std::string::npos;
  while (bodyStart == std::string::npos) {
    const ssize_t n = recvRetry(fd, buf, sizeof(buf));
    if (n <= 0) {
      ::close(fd);
      err = "connection closed before headers completed";
      return 0;
    }
    head.append(buf, size_t(n));
    bodyStart = head.find("\r\n\r\n");
  }

  const std::string headOnly = head.substr(0, bodyStart);
  size_t lineEnd = headOnly.find("\r\n");
  const std::string statusLine =
      headOnly.substr(0, lineEnd == std::string::npos ? headOnly.size() : lineEnd);
  int status = 0;
  const size_t sp = statusLine.find(' ');
  if (sp != std::string::npos) status = std::atoi(statusLine.c_str() + sp + 1);

  // Expected length drives the progress bar; 0 means unknown (chunked).
  size_t expected = 0;
  size_t pos = (lineEnd == std::string::npos) ? headOnly.size() : lineEnd + 2;
  while (pos < headOnly.size()) {
    const size_t eol = headOnly.find("\r\n", pos);
    const std::string line = headOnly.substr(
        pos, (eol == std::string::npos ? headOnly.size() : eol) - pos);
    const size_t colon = line.find(':');
    if (colon != std::string::npos) {
      const std::string key = toLower(trim(line.substr(0, colon)));
      if (key == "content-length") {
        expected = size_t(std::strtoul(trim(line.substr(colon + 1)).c_str(), nullptr, 10));
      }
    }
    if (eol == std::string::npos) break;
    pos = eol + 2;
  }

  if (status != 200) {
    ::close(fd);
    err = "http status " + std::to_string(status);
    return status;
  }

  size_t written = 0;
  auto flush = [&](const char* data, size_t n) -> bool {
    if (n == 0) return true;
    const size_t w = writeFn ? writeFn(fileHandle, data, n) : n;
    if (w != n) return false;
    written += n;
    if (onProgress) onProgress(user, written);
    return true;
  };

  // Bytes that arrived together with the header.
  const size_t firstChunk = head.size() - (bodyStart + 4);
  if (firstChunk && !flush(head.data() + bodyStart + 4, firstChunk)) {
    ::close(fd);
    err = "write failed";
    return 0;
  }

  for (;;) {
    const ssize_t n = recvRetry(fd, buf, sizeof(buf));
    if (n > 0) {
      if (!flush(buf, size_t(n))) {
        ::close(fd);
        err = "write failed";
        return 0;
      }
      continue;
    }
    break;
  }
  ::close(fd);

  if (expected && written != expected) {
    err = "truncated: got " + std::to_string(written) + " of " +
          std::to_string(expected);
    return 0;
  }
  return status;
}

}  // namespace spn::http