// SPDX-License-Identifier: AGPL-3.0
// Minimal HTTP/1.1 client over POSIX sockets.
//
// The measurement that made this port feasible: music.163.com's API answers on
// plain http (only the bare GET / redirects to https), and the audio CDN hands
// out http:// URLs with full Range support. So no TLS stack is needed.
//
// This file talks to BSD sockets directly and therefore compiles unchanged on
// the host and on the Switch — libnx exposes sockets through newlib, so the
// only platform-specific part is the one-time socketInitialize() in the Switch
// main(). That keeps every bit of protocol logic testable locally.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spn::http {

struct Response {
  int status = 0;                  // HTTP status code, 0 on transport failure
  std::string body;                // fully-read body (Content-Length or chunked)
  std::vector<std::pair<std::string, std::string>> headers;  // lower-cased keys
  std::string error;               // non-empty when status == 0

  std::string header(const std::string& name) const;
  bool ok() const { return status >= 200 && status < 300; }
};

struct Request {
  std::string url;                                   // http://host:port/path?query
  std::string method = "POST";
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  int timeoutMs = 20000;
  // For streaming: when non-zero, only the first N bytes of the body are read.
  size_t maxBodyBytes = 0;
};

// Performs one request. Never throws; transport problems land in Response::error.
Response request(const Request& req);

// Convenience: POST application/x-www-form-urlencoded.
Response postForm(const std::string& url,
                  const std::string& formBody,
                  const std::vector<std::pair<std::string, std::string>>& headers = {},
                  int timeoutMs = 20000);

// Convenience: GET with an optional Range header ("bytes=0-2047").
Response get(const std::string& url,
             const std::vector<std::pair<std::string, std::string>>& headers = {},
             const std::string& rangeHeader = {},
             int timeoutMs = 20000);

// Splits "http://host[:port]/path" into its pieces. Returns false on anything
// that is not plain http (https is deliberately unsupported: we never need it).
bool parseUrl(const std::string& url, std::string& host, uint16_t& port, std::string& path);

// Streaming download: GETs `url` and appends body bytes to `out` (an open
// FILE*), reporting progress via `onBytes(totalSoFar)`. Returns the HTTP status
// (0 on transport failure). Used by the track cache so a 10 MB file never has to
// sit in RAM.
int downloadToStream(const std::string& url,
                     void* fileHandle,
                     const std::vector<std::pair<std::string, std::string>>& headers,
                     int timeoutMs,
                     size_t (*writeFn)(void* handle, const void* data, size_t n),
                     void (*onProgress)(void* user, size_t bytes),
                     void* user,
                     std::string& err);

}  // namespace spn::http