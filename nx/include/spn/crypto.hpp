// SPDX-License-Identifier: AGPL-3.0
// Dependency-free crypto primitives for the NetEase (163) protocol layer.
//
// The desktop app delegates all of this to Node's `node:crypto`. On the Switch
// we cannot link OpenSSL/mbedTLS without dragging in a full TLS stack, and the
// measurement that made this port feasible is that **every** endpoint we use
// (login, search, song url, lyric, playlist, play log, account) is reachable
// over plain http and answers the `eapi` envelope. That envelope needs exactly
// two primitives:
//
//     AES-128-ECB(  "e82ckenh8dichen8",  url-36cd479b6b5-json-36cd479b6b5-md5  )
//     MD5(          "nobody"+url+"use"+json+"md5forencrypt" )
//
// so RSA (weapi's encSecKey) and X25519/AES-GCM (xeapi's S envelope) are not
// implemented at all. This was verified against the live API, not assumed:
// see the probe transcripts in nx/README.md.
//
// Everything here is table-driven and side-effect free, so the host test suite
// can diff it against Node's crypto byte for byte.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spn::crypto {

using Bytes = std::vector<uint8_t>;

// --- encodings -------------------------------------------------------------

std::string toBase64(const uint8_t* data, size_t len);
std::string toBase64(const Bytes& b);

// URL-safe alphabet, no padding (the xeapi `C` field convention; kept because
// it is the cheapest way to serialise binary and is also handy in tests).
std::string toBase64Url(const uint8_t* data, size_t len);
std::string toBase64Url(const Bytes& b);

// Decoding is lenient: both alphabets and missing padding are accepted.
Bytes fromBase64(const std::string& s);

std::string toHex(const uint8_t* data, size_t len, bool upper = true);
std::string toHex(const Bytes& b, bool upper = true);
Bytes fromHex(const std::string& s);

// --- digests ---------------------------------------------------------------

// 16-byte MD5 digest.
Bytes md5(const uint8_t* data, size_t len);
Bytes md5(const std::string& s);

// Lowercase hex MD5, the form the eapi signature uses.
std::string md5Hex(const std::string& s);

// --- AES-128 ---------------------------------------------------------------

// ECB mode, PKCS#7 padding, 16-byte key. The only mode the eapi envelope uses.
Bytes aesEcbEncrypt(const Bytes& key, const Bytes& plaintext);
Bytes aesEcbDecrypt(const Bytes& key, const Bytes& ciphertext);

// CBC mode, PKCS#7 padding. Unused by the current protocol but needed if weapi
// (the double-AES web envelope) is ever added; kept small and tested.
Bytes aesCbcEncrypt(const Bytes& key, const Bytes& iv, const Bytes& plaintext);
Bytes aesCbcDecrypt(const Bytes& key, const Bytes& iv, const Bytes& ciphertext);

// --- helpers ---------------------------------------------------------------

inline Bytes toBytes(const std::string& s) { return Bytes(s.begin(), s.end()); }
inline std::string toString(const Bytes& b) { return std::string(b.begin(), b.end()); }

// application/x-www-form-urlencoded value escaping, exactly as the protocol
// expects (space becomes '+', which is what URLSearchParams produces).
std::string urlEncode(const std::string& s);

// Random bytes from a caller-supplied generator, used for deviceId/nonce. The
// platform layer owns the RNG so this stays testable.
Bytes randomBytes(size_t n);

}  // namespace spn::crypto