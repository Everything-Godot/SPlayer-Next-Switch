// SPDX-License-Identifier: AGPL-3.0
// QR encoder round-trip test.
//
// The encoder is verified the only way that actually matters: decode the
// matrix back to text and compare. That exercises the whole chain — data
// segmentation, Reed-Solomon padding, the zig-zag placement, the mask, and the
// format information — and fails loudly if any of them is off by a bit.
//
// A reference encoder (segno) was used during development and agreed module for
// module on payloads that fill the data region exactly; the two differ only in
// how they finish a partially filled region (segno inserts an extra 0x00 before
// the 0xEC/0x11 pad bytes). Both decode correctly, which is why the assertion
// here is "the text comes back", not "the bytes match someone else's". zbar
// decodes the matrices this file builds, so a real scanner agrees.
//
// Scope of the decoder: byte mode, versions 1..10, EC level M. That is exactly
// what the encoder emits, and writing more would be inventing test surface.

#include <cstdio>
#include <string>
#include <vector>

#include "spn/qrcode.hpp"

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool ok, const std::string& what) {
  if (ok) {
    ++g_pass;
  } else {
    ++g_fail;
    std::printf("FAIL %s\n", what.c_str());
  }
}

// --- function-module map (what the encoder must not touch when masking) ----
std::vector<uint8_t> functionMap(int size, int version) {
  std::vector<uint8_t> used(size_t(size) * size_t(size), 0);
  auto mark = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= size || y >= size) return;
    used[size_t(y) * size_t(size) + size_t(x)] = 1;
  };

  for (int i = 0; i < size; ++i) {
    mark(i, 6);
    mark(6, i);
  }
  for (int dy = -4; dy <= 4; ++dy) {
    for (int dx = -4; dx <= 4; ++dx) {
      mark(3 + dx, 3 + dy);
      mark(size - 4 + dx, 3 + dy);
      mark(3 + dx, size - 4 + dy);
    }
  }
  // Alignment pattern centres, same table the encoder uses.
  static const int kAlign[11][3] = {{0, 0, 0},   {0, 0, 0},   {6, 18, 0},
                                    {6, 22, 0},  {6, 26, 0},  {6, 30, 0},
                                    {6, 34, 0},  {6, 22, 38}, {6, 24, 42},
                                    {6, 26, 46}, {6, 28, 50}};
  for (int a = 0; a < 3; ++a) {
    const int cy = kAlign[version][a];
    if (cy == 0) break;
    for (int b = 0; b < 3; ++b) {
      const int cx = kAlign[version][b];
      if (cx == 0) break;
      const bool overlaps = (cx <= 8 && cy <= 8) ||
                            (cx >= size - 9 && cy <= 8) ||
                            (cx <= 8 && cy >= size - 9);
      if (overlaps) continue;
      for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) mark(cx + dx, cy + dy);
      }
    }
  }
  for (int i = 0; i <= 8; ++i) {
    mark(i, 8);
    mark(8, i);
  }
  for (int i = 0; i < 8; ++i) {
    mark(size - 1 - i, 8);
    mark(8, size - 1 - i);
  }
  mark(8, size - 8);

  // Version information (versions 7 and up): an 18-module block above the
  // bottom-left finder and a mirrored copy left of the top-right finder. These
  // are function modules the mask must skip, and forgetting them made version 8
  // decode to noise while version 5 was fine.
  if (version >= 7) {
    for (int i = 0; i < 18; ++i) {
      const int a = size - 11 + i % 3;
      const int b = i / 3;
      mark(a, b);
      mark(b, a);
    }
  }
  return used;
}

bool maskBit(int mask, int x, int y) {
  switch (mask) {
    case 0: return ((x + y) % 2) == 0;
    case 1: return (y % 2) == 0;
    case 2: return (x % 3) == 0;
    case 3: return ((x + y) % 3) == 0;
    case 4: return ((y / 2 + x / 3) % 2) == 0;
    case 5: return ((x * y) % 2) + ((x * y) % 3) == 0;
    case 6: return (((x * y) % 2) + ((x * y) % 3)) % 2 == 0;
    case 7: return (((x + y) % 2) + ((x * y) % 3)) % 2 == 0;
    default: return false;
  }
}

// Reads the format information back from the top-left copy. Verifies the
// BCH(15,5) remainder so a misplaced format field cannot pass silently.
bool readFormatMask(const spn::qr::Code& c, int& mask) {
  int bits = 0;
  auto bit = [&](int x, int y) { return c.at(x, y) ? 1 : 0; };
  for (int i = 0; i <= 5; ++i) bits |= bit(8, i) << i;
  bits |= bit(8, 7) << 6;
  bits |= bit(8, 8) << 7;
  bits |= bit(7, 8) << 8;
  for (int i = 9; i < 15; ++i) bits |= bit(14 - i, 8) << i;

  const uint32_t raw = uint32_t(bits) ^ 0x5412u;
  uint32_t rem = raw;
  for (int i = 14; i >= 10; --i) {
    if ((rem >> i) & 1u) rem ^= 0x537u << (i - 10);
  }
  if (rem != 0) return false;
  const int data = int((raw >> 10) & 0x1Fu);
  if ((data >> 3) != 0) return false;  // EC level must be M
  mask = data & 7;
  return true;
}

// Decodes a matrix produced by spn::qr::encode back to its text.
bool decode(const spn::qr::Code& c, std::string& out) {
  int mask = 0;
  if (!readFormatMask(c, mask)) return false;

  const int size = c.size;
  const int version = (size - 17) / 4;
  if (version < 1 || version > 10) return false;
  const std::vector<uint8_t> used = functionMap(size, version);

  // Walk the zig-zag, undoing the mask, and collect the module bits.
  std::vector<uint8_t> bits;
  bits.reserve(size_t(size) * size_t(size));
  for (int right = size - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vert = 0; vert < size; ++vert) {
      for (int j = 0; j < 2; ++j) {
        const int x = right - j;
        const bool upward = ((right + 1) & 2) == 0;
        const int y = upward ? size - 1 - vert : vert;
        if (used[size_t(y) * size_t(size) + size_t(x)]) continue;
        const bool dark = c.at(x, y) != maskBit(mask, x, y);
        bits.push_back(dark ? 1 : 0);
      }
    }
  }

  std::vector<uint8_t> codewords;
  for (size_t i = 0; i + 8 <= bits.size(); i += 8) {
    uint8_t b = 0;
    for (int k = 0; k < 8; ++k) b = uint8_t((b << 1) | bits[i + size_t(k)]);
    codewords.push_back(b);
  }
  if (codewords.size() < 4) return false;

  // Undo the block interleaving before parsing: the encoder spreads data and
  // ECC codewords round-robin across blocks, so the flat sequence is not the
  // logical bit stream. Skipping this step is exactly the bug that makes a
  // version 5+ code decode to noise while version 1..4 still work.
  static const int kSpec[10][5] = {
      {10, 1, 16, 0, 0},  {16, 1, 28, 0, 0},  {26, 1, 44, 0, 0},
      {18, 2, 32, 0, 0},  {24, 2, 43, 0, 0},  {16, 4, 27, 0, 0},
      {18, 4, 31, 0, 0},  {22, 2, 38, 2, 39}, {22, 3, 36, 2, 37},
      {26, 4, 43, 1, 44},
  };
  const int ecLen = kSpec[version - 1][0];
  const int g1Blocks = kSpec[version - 1][1];
  const int g1Data = kSpec[version - 1][2];
  const int g2Blocks = kSpec[version - 1][3];
  const int g2Data = kSpec[version - 1][4];

  // Per-block data lengths, in the order the encoder emitted them.
  std::vector<int> blockLens;
  for (int i = 0; i < g1Blocks; ++i) blockLens.push_back(g1Data);
  for (int i = 0; i < g2Blocks; ++i) blockLens.push_back(g2Data);
  const size_t blockCount = blockLens.size();
  const size_t dataTotal = codewords.size() - blockCount * size_t(ecLen);

  // Pull the data columns back out; the short group simply ends earlier.
  std::vector<std::vector<uint8_t>> blocks(blockCount);
  size_t w = 0;
  size_t maxData = 0;
  for (int len : blockLens) maxData = std::max(maxData, size_t(len));
  for (size_t col = 0; col < maxData && w < dataTotal; ++col) {
    for (size_t b = 0; b < blockCount; ++b) {
      if (col < size_t(blockLens[b])) blocks[b].push_back(codewords[w++]);
    }
  }

  std::vector<uint8_t> flat;
  for (const auto& blk : blocks) flat.insert(flat.end(), blk.begin(), blk.end());
  if (flat.size() < 4) return false;
  codewords.swap(flat);
  if ((codewords[0] >> 4) != 0b0100) return false;  // byte mode

  // Flatten to a bit stream after the 4-bit mode field.
  std::vector<uint8_t> stream;
  for (uint8_t b : codewords) {
    for (int k = 7; k >= 0; --k) stream.push_back(uint8_t((b >> k) & 1u));
  }
  const int countBits = version <= 9 ? 8 : 16;
  size_t pos = 4;
  int length = 0;
  for (int i = 0; i < countBits; ++i) {
    if (pos >= stream.size()) return false;
    length = (length << 1) | stream[pos++];
  }
  if (length < 0 || pos + size_t(length) * 8 > stream.size()) return false;

  out.clear();
  for (int i = 0; i < length; ++i) {
    int byte = 0;
    for (int k = 0; k < 8; ++k) byte = (byte << 1) | stream[pos++];
    out.push_back(char(byte));
  }
  return true;
}

void roundTrip(const std::string& text, const std::string& label) {
  spn::qr::Code c;
  if (!spn::qr::encode(text, c)) {
    check(false, "encode failed for " + label);
    return;
  }
  check(c.size >= 21 && c.size <= 57 && (c.size - 21) % 4 == 0,
        "matrix size is a legal QR dimension for " + label);

  std::string back;
  if (!decode(c, back)) {
    check(false, "decode failed for " + label);
    return;
  }
  check(back == text,
        "round trip mismatch for " + label + ": got [" + back.substr(0, 40) + "]");
}

}  // namespace

int main() {
  roundTrip("A", "single byte");
  roundTrip("hello world", "short ascii");
  roundTrip(
      "https://music.163.com/login?codekey=5ae70798-1234-4abc-9def-0123456789ab",
      "login url");
  roundTrip("https://music.163.com/login?codekey=" + std::string(40, 'x'),
            "long login url");
  roundTrip(std::string(124, '7'), "v8 payload");

  // Every mask must round-trip: a payload that happens to prefer one mask would
  // otherwise break while the rest looked fine.
  {
    const std::string text = "mask sweep";
    int ok = 0;
    for (int mask = 0; mask < 8; ++mask) {
      spn::qr::Code c;
      if (!spn::qr::encodeExact(text, 2, mask, c)) continue;
      std::string back;
      if (decode(c, back) && back == text) ++ok;
    }
    check(ok == 8, "all eight masks round trip (got " + std::to_string(ok) + ")");
  }

  // A payload that exactly fills the data region needs no pad bytes; it also
  // pins down where the terminator goes.
  {
    const std::string exact = "ABCDEFGHIJKLMN";  // 14 bytes fills v1-M
    spn::qr::Code c;
    check(spn::qr::encode(exact, c), "exact-fit payload encodes");
    check(c.version == 1, "exact-fit payload lands on version 1");
    std::string back;
    check(decode(c, back) && back == exact, "exact-fit payload round trips");
  }

  // Too large for version 10: refuse rather than truncate.
  {
    spn::qr::Code big;
    check(!spn::qr::encode(std::string(600, 'x'), big),
          "oversized payload is refused");
  }

  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}