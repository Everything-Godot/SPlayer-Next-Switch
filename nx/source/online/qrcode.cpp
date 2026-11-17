// SPDX-License-Identifier: AGPL-3.0
// Minimal QR code encoder — byte mode, versions 1..10, EC level M.
//
// Follows ISO/IEC 18004 for the parts it implements: Reed-Solomon over
// GF(2^8) with the 0x11D primitive, the byte-mode segment layout, the
// standard format/version information BCH codes, and the eight data masks with
// the spec's penalty scoring.
//
// encode() trusts nobody: it sizes the smallest version that fits, then picks
// the mask with the lowest penalty. encodeExact() exists so the test suite can
// force a specific (version, mask) and compare the matrix against segno
// bit-for-bit, which catches errors that a "looks like a QR" screenshot never
// would.

#include "spn/qrcode.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace spn::qr {

namespace {

// --- EC level M block layout, versions 1..10 -------------------------------
// Sizes come from ISO/IEC 18004 table 9. Getting one of these wrong produces a
// code that scans but decodes to garbage, so they are spelled out per version
// rather than derived.
struct BlockSpec {
  int ecLen;
  int g1Blocks;
  int g1Data;
  int g2Blocks;
  int g2Data;
};

const BlockSpec kSpec[10] = {
    {10, 1, 16, 0, 0},   // v1
    {16, 1, 28, 0, 0},   // v2
    {26, 1, 44, 0, 0},   // v3
    {18, 2, 32, 0, 0},   // v4
    {24, 2, 43, 0, 0},   // v5
    {16, 4, 27, 0, 0},   // v6
    {18, 4, 31, 0, 0},   // v7
    {22, 2, 38, 2, 39},  // v8
    {22, 3, 36, 2, 37},  // v9
    {26, 4, 43, 1, 44},  // v10
};

// Alignment pattern centres, ISO/IEC 18004 annex E table E.1. Index 0 is a
// placeholder so kAlign[version] reads naturally. The table is literal on
// purpose: an off-by-one row here shifts every alignment pattern from version
// 2 to 6 and the codes still look plausible while decoding to noise.
const std::vector<int> kAlign[11] = {
    {},           {},           {6, 18},      {6, 22},      {6, 26},
    {6, 30},      {6, 34},      {6, 22, 38},  {6, 24, 42},  {6, 26, 46},
    {6, 28, 50},
};

int dataCodewordsFor(int version) {
  const BlockSpec& s = kSpec[version - 1];
  return s.g1Blocks * s.g1Data + s.g2Blocks * s.g2Data;
}

// --- GF(2^8) arithmetic ----------------------------------------------------
uint8_t gfMul(uint8_t x, uint8_t y) {
  int z = 0;
  for (int i = 7; i >= 0; --i) {
    z = (z << 1) ^ ((z >> 7) * 0x11D);
    z ^= ((y >> i) & 1) * x;
  }
  return uint8_t(z & 0xFF);
}

// Generator polynomial for a degree-`degree` Reed-Solomon code, in the
// descending order the division loop below expects.
std::vector<uint8_t> rsDivisor(int degree) {
  std::vector<uint8_t> result(size_t(degree), 0);
  result[size_t(degree - 1)] = 1;
  uint8_t root = 1;
  for (int i = 0; i < degree; ++i) {
    for (int j = 0; j < degree; ++j) {
      result[size_t(j)] = gfMul(result[size_t(j)], root);
      if (j + 1 < degree) result[size_t(j)] ^= result[size_t(j + 1)];
    }
    root = gfMul(root, 0x02);
  }
  return result;
}

std::vector<uint8_t> rsRemainder(const std::vector<uint8_t>& data,
                                 const std::vector<uint8_t>& divisor) {
  std::vector<uint8_t> result(divisor.size(), 0);
  for (uint8_t b : data) {
    const uint8_t factor = uint8_t(b ^ result.front());
    result.erase(result.begin());
    result.push_back(0);
    for (size_t i = 0; i < result.size(); ++i) {
      result[i] ^= gfMul(divisor[i], factor);
    }
  }
  return result;
}

// --- bit helpers -----------------------------------------------------------
bool bitAt(uint32_t v, int i) { return ((v >> i) & 1u) != 0; }

// Cleaner bit writer: the QR path needs exact byte boundaries, so accumulate
// bits explicitly instead of a floating shift register.
class BitWriter {
 public:
  void push(uint32_t value, int len) {
    for (int i = len - 1; i >= 0; --i) {
      if (bits_ == 0) bytes_.push_back(0);
      if (bitAt(value, i)) bytes_.back() = uint8_t(bytes_.back() | (0x80 >> bits_));
      bits_ = (bits_ + 1) & 7;
    }
  }
  int bitCount() const {
    return int(bytes_.size()) * 8 - (bits_ == 0 ? 0 : 8 - bits_);
  }
  void padToByte() {
    if (bits_ != 0) bits_ = 0;
  }
  const std::vector<uint8_t>& bytes() const { return bytes_; }
  std::vector<uint8_t>& bytes() { return bytes_; }

 private:
  std::vector<uint8_t> bytes_;
  int bits_ = 0;
};

// --- matrix ----------------------------------------------------------------
struct Matrix {
  int size = 0;
  std::vector<uint8_t> don;   // dark modules
  std::vector<uint8_t> used;  // function modules; masking must skip these

  void init(int n) {
    size = n;
    don.assign(size_t(n) * size_t(n), 0);
    used.assign(size_t(n) * size_t(n), 0);
  }
  void set(int x, int y, bool dark) { don[size_t(y) * size_t(size) + size_t(x)] = dark ? 1 : 0; }
  bool get(int x, int y) const { return don[size_t(y) * size_t(size) + size_t(x)] != 0; }
  void mark(int x, int y) { used[size_t(y) * size_t(size) + size_t(x)] = 1; }
  bool isUsed(int x, int y) const { return used[size_t(y) * size_t(size) + size_t(x)] != 0; }
};

void drawFinder(Matrix& m, int cx, int cy) {
  for (int dy = -4; dy <= 4; ++dy) {
    for (int dx = -4; dx <= 4; ++dx) {
      const int x = cx + dx;
      const int y = cy + dy;
      if (x < 0 || y < 0 || x >= m.size || y >= m.size) continue;
      const int dist = std::max(std::abs(dx), std::abs(dy));
      m.set(x, y, dist != 2 && dist != 4);
      m.mark(x, y);
    }
  }
}

void drawAlignment(Matrix& m, int cx, int cy) {
  for (int dy = -2; dy <= 2; ++dy) {
    for (int dx = -2; dx <= 2; ++dx) {
      m.set(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
      m.mark(cx + dx, cy + dy);
    }
  }
}

bool alignmentOverlapsFinder(const Matrix& m, int cx, int cy) {
  // The three corner finder patterns already occupy these spots.
  const int s = m.size;
  return (cx <= 8 && cy <= 8) || (cx >= s - 9 && cy <= 8) || (cx <= 8 && cy >= s - 9);
}

void drawFunctionPatterns(Matrix& m, int version) {
  const int s = m.size;

  // Timing patterns.
  for (int i = 0; i < s; ++i) {
    if (!m.isUsed(i, 6)) { m.set(i, 6, (i % 2) == 0); m.mark(i, 6); }
    if (!m.isUsed(6, i)) { m.set(6, i, (i % 2) == 0); m.mark(6, i); }
  }

  drawFinder(m, 3, 3);
  drawFinder(m, s - 4, 3);
  drawFinder(m, 3, s - 4);

  if (version >= 2) {
    const std::vector<int>& pos = kAlign[version];
    for (int cy : pos) {
      for (int cx : pos) {
        if (alignmentOverlapsFinder(m, cx, cy)) continue;
        drawAlignment(m, cx, cy);
      }
    }
  }

  // Reserve the format information area (values are written per mask later).
  for (int i = 0; i <= 8; ++i) {
    if (!m.isUsed(i, 8)) m.mark(i, 8);
    if (!m.isUsed(8, i)) m.mark(8, i);
  }
  for (int i = 0; i < 8; ++i) {
    m.mark(s - 1 - i, 8);
    m.mark(8, s - 1 - i);
  }
  // Dark module, always set.
  m.set(8, s - 8, true);
  m.mark(8, s - 8);

  // Version information block (v7 and up).
  if (version >= 7) {
    int rem = version;
    for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    const uint32_t bits = uint32_t(version << 12 | rem);
    for (int i = 0; i < 18; ++i) {
      const bool bit = bitAt(bits, i);
      const int a = s - 11 + i % 3;
      const int b = i / 3;
      m.set(a, b, bit);
      m.mark(a, b);
      m.set(b, a, bit);
      m.mark(b, a);
    }
  }
}

void drawFormatBits(Matrix& m, int mask) {
  // EC level M is 0b00 in the two-bit field.
  const int data = (0 << 3) | mask;
  int rem = data;
  for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
  const uint32_t bits = uint32_t((data << 10 | rem) ^ 0x5412);
  const int s = m.size;

  // First copy: around the top-left finder pattern.
  for (int i = 0; i <= 5; ++i) m.set(8, i, bitAt(bits, i));
  m.set(8, 7, bitAt(bits, 6));
  m.set(8, 8, bitAt(bits, 7));
  m.set(7, 8, bitAt(bits, 8));
  for (int i = 9; i < 15; ++i) m.set(14 - i, 8, bitAt(bits, i));

  // Second copy: split between the bottom-left and top-right corners.
  // Bits 0..7 run right-to-left along row 8 from (s-1,8); bits 8..14 run
  // bottom-to-top along column 8 from (8,s-1) upward. The positions below are
  // the ones ISO/IEC 18004 figure 25 puts them in, not a mirror of the first
  // copy: an eye can't tell a mirrored format field from a correct one, but a
  // scanner reads the wrong EC level and mask off it.
  for (int i = 0; i < 8; ++i) m.set(s - 1 - i, 8, bitAt(bits, i));
  for (int i = 8; i < 15; ++i) m.set(8, s - 7 + (i - 8), bitAt(bits, i));
  // The dark module always sits here and is not part of either copy.
  m.set(8, s - 8, true);
}

// Places the interleaved codewords in the zig-zag order the spec prescribes.
void drawCodewords(Matrix& m, const std::vector<uint8_t>& codewords) {
  const int s = m.size;
  size_t i = 0;
  const size_t total = codewords.size() * 8;

  for (int right = s - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;  // skip the vertical timing column
    for (int vert = 0; vert < s; ++vert) {
      for (int j = 0; j < 2; ++j) {
        const int x = right - j;
        const bool upward = ((right + 1) & 2) == 0;
        const int y = upward ? s - 1 - vert : vert;
        if (m.isUsed(x, y)) continue;
        if (i < total) {
          m.set(x, y, bitAt(codewords[i >> 3], 7 - int(i & 7)));
          ++i;
        }
        // Remaining modules stay light (the "remainder bits").
      }
    }
  }
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

void applyMask(Matrix& m, int mask) {
  for (int y = 0; y < m.size; ++y) {
    for (int x = 0; x < m.size; ++x) {
      if (m.isUsed(x, y)) continue;
      if (maskBit(mask, x, y)) m.set(x, y, !m.get(x, y));
    }
  }
}

// --- penalty (ISO/IEC 18004 8.8.2) ----------------------------------------
int penalty(const Matrix& m) {
  const int s = m.size;
  int score = 0;

  // Rule 1: runs of five or more same-coloured modules.
  for (int pass = 0; pass < 2; ++pass) {
    for (int a = 0; a < s; ++a) {
      int run = 1;
      for (int b = 1; b < s; ++b) {
        const bool cur = pass == 0 ? m.get(b, a) : m.get(a, b);
        const bool prev = pass == 0 ? m.get(b - 1, a) : m.get(a, b - 1);
        if (cur == prev) {
          ++run;
          if (b == s - 1 && run >= 5) score += 3 + (run - 5);
        } else {
          if (run >= 5) score += 3 + (run - 5);
          run = 1;
        }
      }
    }
  }

  // Rule 2: 2x2 blocks of one colour.
  for (int y = 0; y + 1 < s; ++y) {
    for (int x = 0; x + 1 < s; ++x) {
      const bool c = m.get(x, y);
      if (c == m.get(x + 1, y) && c == m.get(x, y + 1) && c == m.get(x + 1, y + 1)) {
        score += 3;
      }
    }
  }

  // Rule 3: 1:1:3:1:1 finder-like patterns with four light modules beside them.
  for (int pass = 0; pass < 2; ++pass) {
    for (int a = 0; a < s; ++a) {
      for (int b = 0; b + 10 < s; ++b) {
        auto at = [&](int k) { return pass == 0 ? m.get(b + k, a) : m.get(a, b + k); };
        const bool fwd = at(0) && !at(1) && at(2) && at(3) && at(4) && !at(5) &&
                         at(6) && !at(7) && !at(8) && !at(9) && !at(10);
        if (fwd) { score += 40; continue; }
        const bool rev = !at(0) && !at(1) && !at(2) && !at(3) && at(4) && !at(5) &&
                         at(6) && at(7) && at(8) && !at(9) && at(10);
        if (rev) score += 40;
      }
    }
  }

  // Rule 4: deviation from an even dark/light split.
  int dark = 0;
  for (int y = 0; y < s; ++y) {
    for (int x = 0; x < s; ++x) {
      if (m.get(x, y)) ++dark;
    }
  }
  const int total = s * s;
  const int pct = dark * 100 / total;
  score += 10 * (std::abs(pct - 50) / 5);

  return score;
}

// Builds the data+ecc codeword sequence for one version.
bool buildCodewords(const std::string& text, int version, std::vector<uint8_t>& out) {
  const BlockSpec& spec = kSpec[version - 1];
  const int capacityBytes = dataCodewordsFor(version);
  const int countBits = version <= 9 ? 8 : 16;
  if (int(text.size()) >= (1 << countBits)) return false;

  BitWriter bw;
  bw.push(0b0100, 4);                         // byte mode
  bw.push(uint32_t(text.size()), countBits);  // character count
  for (unsigned char ch : text) bw.push(ch, 8);

  if (bw.bitCount() > capacityBytes * 8) return false;

  // Terminator (up to four zero bits), then pad to a byte boundary.
  bw.push(0, std::min(4, capacityBytes * 8 - bw.bitCount()));
  bw.padToByte();
  // Alternating pad bytes fill the rest of the data region. ISO/IEC 18004
  // 8.4.9 fixes the sequence as 11101100 (0xEC) then 00010001 (0x11),
  // alternating, starting with 0xEC.
  const int padBytes = capacityBytes - int(bw.bytes().size());
  for (int i = 0; i < padBytes; ++i) bw.bytes().push_back(i % 2 == 0 ? 0xEC : 0x11);

  const std::vector<uint8_t>& data = bw.bytes();
  if (int(data.size()) != capacityBytes) return false;

  // Split into blocks, compute ECC per block, then interleave. Interleaving
  // matters from version 8 on (two block groups); for versions 1..7 there is a
  // single group and this degenerates into "data then ecc", which is exactly
  // what the spec asks for.
  std::vector<std::vector<uint8_t>> blocks;
  size_t cursor = 0;
  for (int i = 0; i < spec.g1Blocks; ++i) {
    blocks.emplace_back(data.begin() + long(cursor),
                        data.begin() + long(cursor + size_t(spec.g1Data)));
    cursor += size_t(spec.g1Data);
  }
  for (int i = 0; i < spec.g2Blocks; ++i) {
    blocks.emplace_back(data.begin() + long(cursor),
                        data.begin() + long(cursor + size_t(spec.g2Data)));
    cursor += size_t(spec.g2Data);
  }
  if (cursor != size_t(capacityBytes)) return false;

  const std::vector<uint8_t> divisor = rsDivisor(spec.ecLen);
  std::vector<std::vector<uint8_t>> eccs;
  eccs.reserve(blocks.size());
  for (const auto& blk : blocks) eccs.push_back(rsRemainder(blk, divisor));

  out.clear();
  out.reserve(size_t(capacityBytes) + size_t(spec.ecLen) * blocks.size());

  // Interleave: one data codeword per block, round-robin. The short group (g2)
  // simply runs out one column earlier.
  size_t maxData = 0;
  for (const auto& blk : blocks) maxData = std::max(maxData, blk.size());
  for (size_t col = 0; col < maxData; ++col) {
    for (const auto& blk : blocks) {
      if (col < blk.size()) out.push_back(blk[col]);
    }
  }
  // Then the ECC codewords, same round-robin shape (all groups share ecLen).
  for (int col = 0; col < spec.ecLen; ++col) {
    for (const auto& ecc : eccs) out.push_back(ecc[size_t(col)]);
  }
  return true;
}

bool encodeInto(const std::string& text, int version, int mask, Code& out) {
  std::vector<uint8_t> codewords;
  if (!buildCodewords(text, version, codewords)) return false;

  Matrix m;
  const int size = 17 + 4 * version;
  m.init(size);
  drawFunctionPatterns(m, version);
  drawCodewords(m, codewords);
  applyMask(m, mask);
  drawFormatBits(m, mask);

  out.size = size;
  out.version = version;
  out.modules = m.don;
  return true;
}

}  // namespace

bool encodeExact(const std::string& text, int version, int mask, Code& out) {
  if (version < 1 || version > 10 || mask < 0 || mask > 7) return false;
  return encodeInto(text, version, mask, out);
}

bool encode(const std::string& text, Code& out) {
  for (int version = 1; version <= 10; ++version) {
    std::vector<uint8_t> probe;
    if (!buildCodewords(text, version, probe)) continue;

    int bestScore = -1;
    Code best;
    for (int mask = 0; mask < 8; ++mask) {
      Code candidate;
      if (!encodeInto(text, version, mask, candidate)) continue;
      Matrix m;
      m.init(candidate.size);
      m.don = candidate.modules;
      // Function modules are re-derived for scoring only; the penalty rules do
      // not care which modules were reserved.
      const int score = penalty(m);
      if (bestScore < 0 || score < bestScore) {
        bestScore = score;
        best = std::move(candidate);
      }
    }
    if (bestScore >= 0) {
      out = std::move(best);
      return true;
    }
  }
  return false;
}

}  // namespace spn::qr