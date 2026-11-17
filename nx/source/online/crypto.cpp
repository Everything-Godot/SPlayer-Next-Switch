// SPDX-License-Identifier: AGPL-3.0
// Dependency-free MD5 / AES-128 / encoding primitives (see crypto.hpp).
//
// Implementations follow RFC 1321 and FIPS-197 literally. They are compiled
// for the host as well, so nxtest/crypto_test.cpp can diff them against Node's
// node:crypto output on known vectors.

#include "spn/crypto.hpp"

#include <cstring>

namespace spn::crypto {
namespace {

// ---------------------------------------------------------------------------
// base64
// ---------------------------------------------------------------------------

constexpr char kStd[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char kUrl[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string encode(const uint8_t* data, size_t len, const char* table, bool pad) {
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < len; i += 3) {
    uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
    out.push_back(table[(v >> 18) & 0x3F]);
    out.push_back(table[(v >> 12) & 0x3F]);
    out.push_back(table[(v >> 6) & 0x3F]);
    out.push_back(table[v & 0x3F]);
  }
  if (i + 1 == len) {
    uint32_t v = uint32_t(data[i]) << 16;
    out.push_back(table[(v >> 18) & 0x3F]);
    out.push_back(table[(v >> 12) & 0x3F]);
    if (pad) {
      out.push_back('=');
      out.push_back('=');
    }
  } else if (i + 2 == len) {
    uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
    out.push_back(table[(v >> 18) & 0x3F]);
    out.push_back(table[(v >> 12) & 0x3F]);
    out.push_back(table[(v >> 6) & 0x3F]);
    if (pad) out.push_back('=');
  }
  return out;
}

int decodeChar(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}

// ---------------------------------------------------------------------------
// MD5
// ---------------------------------------------------------------------------

constexpr uint32_t kMd5K[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au,
    0xa8304613u, 0xfd469501u, 0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u, 0xf61e2562u, 0xc040b340u,
    0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u,
    0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u, 0x289b7ec6u, 0xeaa127fau,
    0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u,
    0xffeff47du, 0x85845dd1u, 0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};

constexpr uint8_t kMd5S[64] = {7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22,
                               5,  9,  14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 5,  9,  14, 20,
                               4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
                               6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21};

inline uint32_t rotl32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

void md5Block(uint32_t state[4], const uint8_t block[64]) {
  uint32_t w[16];
  for (int i = 0; i < 16; ++i) {
    w[i] = uint32_t(block[i * 4]) | (uint32_t(block[i * 4 + 1]) << 8) |
           (uint32_t(block[i * 4 + 2]) << 16) | (uint32_t(block[i * 4 + 3]) << 24);
  }
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  for (int i = 0; i < 64; ++i) {
    uint32_t f;
    int g;
    if (i < 16) {
      f = (b & c) | (~b & d);
      g = i;
    } else if (i < 32) {
      f = (d & b) | (~d & c);
      g = (5 * i + 1) % 16;
    } else if (i < 48) {
      f = b ^ c ^ d;
      g = (3 * i + 5) % 16;
    } else {
      f = c ^ (b | ~d);
      g = (7 * i) % 16;
    }
    uint32_t tmp = d;
    d = c;
    c = b;
    b = b + rotl32(a + f + kMd5K[i] + w[g], kMd5S[i]);
    a = tmp;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

// ---------------------------------------------------------------------------
// AES
// ---------------------------------------------------------------------------

constexpr uint8_t kSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

constexpr uint8_t kInvSbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d};

constexpr uint8_t kRcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

inline uint8_t gmul(uint8_t a, uint8_t b) {
  uint8_t p = 0;
  for (int i = 0; i < 8; ++i) {
    p ^= uint8_t(-(b & 1) & a);
    uint8_t hi = uint8_t(a & 0x80);
    a = uint8_t(a << 1);
    if (hi) a ^= 0x1B;
    b = uint8_t(b >> 1);
  }
  return p;
}

struct AesKey {
  uint8_t round[176];
};

void aesExpandKey(const uint8_t key[16], AesKey& ks) {
  std::memcpy(ks.round, key, 16);
  for (int i = 4; i < 44; ++i) {
    uint8_t t[4];
    std::memcpy(t, ks.round + (i - 1) * 4, 4);
    if (i % 4 == 0) {
      const uint8_t tmp = t[0];
      t[0] = uint8_t(kSbox[t[1]] ^ kRcon[i / 4]);
      t[1] = kSbox[t[2]];
      t[2] = kSbox[t[3]];
      t[3] = kSbox[tmp];
    }
    for (int j = 0; j < 4; ++j) {
      ks.round[i * 4 + j] = uint8_t(ks.round[(i - 4) * 4 + j] ^ t[j]);
    }
  }
}

// State layout is column-major: s[col * 4 + row], matching the AES spec.
void aesEncryptBlock(const AesKey& ks, const uint8_t in[16], uint8_t out[16]) {
  uint8_t s[16];
  for (int i = 0; i < 16; ++i) s[i] = uint8_t(in[i] ^ ks.round[i]);

  for (int round = 1; round <= 10; ++round) {
    uint8_t t[16];
    for (int c = 0; c < 4; ++c) {
      for (int r = 0; r < 4; ++r) {
        t[c * 4 + r] = kSbox[s[((c + r) & 3) * 4 + r]];
      }
    }
    if (round != 10) {
      for (int c = 0; c < 4; ++c) {
        const uint8_t a0 = t[c * 4 + 0], a1 = t[c * 4 + 1];
        const uint8_t a2 = t[c * 4 + 2], a3 = t[c * 4 + 3];
        s[c * 4 + 0] = uint8_t(gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3);
        s[c * 4 + 1] = uint8_t(a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3);
        s[c * 4 + 2] = uint8_t(a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3));
        s[c * 4 + 3] = uint8_t(gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2));
      }
    } else {
      std::memcpy(s, t, 16);
    }
    for (int i = 0; i < 16; ++i) s[i] = uint8_t(s[i] ^ ks.round[round * 16 + i]);
  }
  std::memcpy(out, s, 16);
}

void aesDecryptBlock(const AesKey& ks, const uint8_t in[16], uint8_t out[16]) {
  uint8_t s[16];
  for (int i = 0; i < 16; ++i) s[i] = uint8_t(in[i] ^ ks.round[160 + i]);

  for (int round = 9; round >= 0; --round) {
    uint8_t t[16];
    for (int c = 0; c < 4; ++c) {
      for (int r = 0; r < 4; ++r) {
        t[((c + r) & 3) * 4 + r] = kInvSbox[s[c * 4 + r]];
      }
    }
    for (int i = 0; i < 16; ++i) t[i] = uint8_t(t[i] ^ ks.round[round * 16 + i]);
    if (round != 0) {
      for (int c = 0; c < 4; ++c) {
        const uint8_t a0 = t[c * 4 + 0], a1 = t[c * 4 + 1];
        const uint8_t a2 = t[c * 4 + 2], a3 = t[c * 4 + 3];
        s[c * 4 + 0] = uint8_t(gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9));
        s[c * 4 + 1] = uint8_t(gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13));
        s[c * 4 + 2] = uint8_t(gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11));
        s[c * 4 + 3] = uint8_t(gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14));
      }
    } else {
      std::memcpy(s, t, 16);
    }
  }
  std::memcpy(out, s, 16);
}

Bytes pkcs7Pad(const Bytes& in) {
  Bytes out = in;
  const uint8_t pad = uint8_t(16 - (in.size() % 16));
  out.insert(out.end(), pad, pad);
  return out;
}

Bytes pkcs7Unpad(const Bytes& in) {
  if (in.empty() || in.size() % 16 != 0) return in;
  const uint8_t pad = in.back();
  if (pad == 0 || pad > 16 || size_t(pad) > in.size()) return in;
  for (size_t i = in.size() - pad; i < in.size(); ++i) {
    if (in[i] != pad) return in;
  }
  return Bytes(in.begin(), in.end() - pad);
}

AesKey makeKey(const Bytes& key) {
  AesKey ks{};
  uint8_t k[16] = {};
  std::memcpy(k, key.data(), key.size() < 16 ? key.size() : 16);
  aesExpandKey(k, ks);
  return ks;
}

}  // namespace

// ---------------------------------------------------------------------------
// encodings
// ---------------------------------------------------------------------------

std::string toBase64(const uint8_t* data, size_t len) { return encode(data, len, kStd, true); }
std::string toBase64(const Bytes& b) { return toBase64(b.data(), b.size()); }

std::string toBase64Url(const uint8_t* data, size_t len) { return encode(data, len, kUrl, false); }
std::string toBase64Url(const Bytes& b) { return toBase64Url(b.data(), b.size()); }

Bytes fromBase64(const std::string& s) {
  Bytes out;
  out.reserve(s.size() * 3 / 4 + 3);
  uint32_t acc = 0;
  int bits = 0;
  for (char c : s) {
    const int v = decodeChar(c);
    if (v < 0) continue;  // skips '=', whitespace and anything else
    acc = (acc << 6) | uint32_t(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(uint8_t((acc >> bits) & 0xFF));
    }
  }
  return out;
}

std::string toHex(const uint8_t* data, size_t len, bool upper) {
  static const char* lo = "0123456789abcdef";
  static const char* up = "0123456789ABCDEF";
  const char* tab = upper ? up : lo;
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out.push_back(tab[data[i] >> 4]);
    out.push_back(tab[data[i] & 0x0F]);
  }
  return out;
}
std::string toHex(const Bytes& b, bool upper) { return toHex(b.data(), b.size(), upper); }

Bytes fromHex(const std::string& s) {
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  Bytes out;
  out.reserve(s.size() / 2);
  int hi = -1;
  for (char c : s) {
    const int v = nib(c);
    if (v < 0) continue;
    if (hi < 0) {
      hi = v;
    } else {
      out.push_back(uint8_t((hi << 4) | v));
      hi = -1;
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// MD5
// ---------------------------------------------------------------------------

Bytes md5(const uint8_t* data, size_t len) {
  uint32_t state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};

  size_t full = len / 64;
  for (size_t i = 0; i < full; ++i) md5Block(state, data + i * 64);

  uint8_t tail[128] = {};
  const size_t rem = len - full * 64;
  if (rem) std::memcpy(tail, data + full * 64, rem);
  tail[rem] = 0x80;
  const size_t tailLen = (rem + 1 + 8 <= 64) ? 64 : 128;
  const uint64_t bits = uint64_t(len) * 8;
  for (int i = 0; i < 8; ++i) {
    tail[tailLen - 8 + i] = uint8_t((bits >> (8 * i)) & 0xFF);
  }
  md5Block(state, tail);
  if (tailLen == 128) md5Block(state, tail + 64);

  Bytes out(16);
  for (int i = 0; i < 4; ++i) {
    out[i * 4 + 0] = uint8_t(state[i] & 0xFF);
    out[i * 4 + 1] = uint8_t((state[i] >> 8) & 0xFF);
    out[i * 4 + 2] = uint8_t((state[i] >> 16) & 0xFF);
    out[i * 4 + 3] = uint8_t((state[i] >> 24) & 0xFF);
  }
  return out;
}

Bytes md5(const std::string& s) {
  return md5(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

std::string md5Hex(const std::string& s) { return toHex(md5(s), false); }

// ---------------------------------------------------------------------------
// AES
// ---------------------------------------------------------------------------

Bytes aesEcbEncrypt(const Bytes& key, const Bytes& plaintext) {
  const AesKey ks = makeKey(key);
  const Bytes padded = pkcs7Pad(plaintext);
  Bytes out(padded.size());
  for (size_t i = 0; i < padded.size(); i += 16) {
    aesEncryptBlock(ks, padded.data() + i, out.data() + i);
  }
  return out;
}

Bytes aesEcbDecrypt(const Bytes& key, const Bytes& ciphertext) {
  if (ciphertext.empty() || ciphertext.size() % 16 != 0) return {};
  const AesKey ks = makeKey(key);
  Bytes out(ciphertext.size());
  for (size_t i = 0; i < ciphertext.size(); i += 16) {
    aesDecryptBlock(ks, ciphertext.data() + i, out.data() + i);
  }
  return pkcs7Unpad(out);
}

Bytes aesCbcEncrypt(const Bytes& key, const Bytes& iv, const Bytes& plaintext) {
  const AesKey ks = makeKey(key);
  const Bytes padded = pkcs7Pad(plaintext);
  uint8_t chain[16] = {};
  std::memcpy(chain, iv.data(), iv.size() < 16 ? iv.size() : 16);
  Bytes out(padded.size());
  for (size_t i = 0; i < padded.size(); i += 16) {
    uint8_t block[16];
    for (int j = 0; j < 16; ++j) block[j] = uint8_t(padded[i + j] ^ chain[j]);
    aesEncryptBlock(ks, block, out.data() + i);
    std::memcpy(chain, out.data() + i, 16);
  }
  return out;
}

Bytes aesCbcDecrypt(const Bytes& key, const Bytes& iv, const Bytes& ciphertext) {
  if (ciphertext.empty() || ciphertext.size() % 16 != 0) return {};
  const AesKey ks = makeKey(key);
  uint8_t chain[16] = {};
  std::memcpy(chain, iv.data(), iv.size() < 16 ? iv.size() : 16);
  Bytes out(ciphertext.size());
  for (size_t i = 0; i < ciphertext.size(); i += 16) {
    uint8_t block[16];
    aesDecryptBlock(ks, ciphertext.data() + i, block);
    for (int j = 0; j < 16; ++j) {
      out[i + j] = uint8_t(block[j] ^ chain[j]);
      chain[j] = ciphertext[i + j];
    }
  }
  return pkcs7Unpad(out);
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

std::string urlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '*' || c == '~') {
      out.push_back(char(c));
    } else if (c == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0x0F]);
    }
  }
  return out;
}

}  // namespace spn::crypto