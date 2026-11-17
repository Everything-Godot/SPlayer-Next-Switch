// SPDX-License-Identifier: AGPL-3.0
// Minimal QR code encoder (byte mode, versions 1..10, EC level M).
//
// Why in-tree: the Switch has no QR library, and the login flow needs the user
// to scan a URL off the screen. A wrong matrix is indistinguishable from a
// dead API, so this is verified module-by-module against segno in nxtest.
//
// Scope is deliberately narrow: byte mode only (URLs are ASCII), versions
// 1..10, level M. That covers a ~200 character URL, which is far beyond the
// 60 or so the login endpoint hands out.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spn::qr {

// Row-major module grid. true == dark. Includes the quiet zone? No: callers
// draw the quiet zone themselves so they can pick the margin in pixels.
struct Code {
  int size = 0;                  // modules per side (21, 25, ...)
  int version = 0;               // 1..10
  std::vector<uint8_t> modules;  // size*size, 1 == dark

  bool at(int x, int y) const {
    if (x < 0 || y < 0 || x >= size || y >= size) return false;
    return modules[size_t(y) * size_t(size) + size_t(x)] != 0;
  }
};

// Encodes `text` (treated as raw bytes) at EC level M. Returns false only when
// the payload does not fit in version 10.
bool encode(const std::string& text, Code& out);

// Forces one (version, mask) pair. Used by the test suite to compare a matrix
// against a reference encoder module by module; the UI always calls encode().
bool encodeExact(const std::string& text, int version, int mask, Code& out);

}  // namespace spn::qr
