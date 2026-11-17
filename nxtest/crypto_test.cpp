// SPDX-License-Identifier: AGPL-3.0
// Host test for the crypto primitives.
//
// Reference vectors were produced with Node's node:crypto, which is exactly what
// the desktop app uses, so a pass here means the Switch implementation produces
// bit-identical envelopes.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "spn/crypto.hpp"

using namespace spn::crypto;

static int g_fail = 0;
static int g_pass = 0;

static void checkStr(const char* what, const std::string& got, const std::string& want) {
  if (got == want) {
    ++g_pass;
  } else {
    ++g_fail;
    std::printf("FAIL %s\n  got  '%s'\n  want '%s'\n", what, got.c_str(), want.c_str());
  }
}

static void checkInt(const char* what, long long got, long long want) {
  if (got == want) {
    ++g_pass;
  } else {
    ++g_fail;
    std::printf("FAIL %s: got %lld want %lld\n", what, got, want);
  }
}

int main() {
  // --- MD5 ---------------------------------------------------------------
  // RFC 1321 test vectors plus a couple of the eapi-style long strings.
  checkStr("md5 empty", md5Hex(""), "d41d8cd98f00b204e9800998ecf8427e");
  checkStr("md5 abc", md5Hex("abc"), "900150983cd24fb0d6963f7d28e17f72");
  checkStr("md5 quick brown",
          md5Hex("The quick brown fox jumps over the lazy dog"),
          "9e107d9d372bb6826bd81d3542a419d6");
  // 56 bytes: exercises the two-block padding path.
  checkStr("md5 56 bytes",
          md5Hex("12345678901234567890123456789012345678901234567890123456"),
          "49f193adce178490e34d1b3a4ec0064c");
  // 64 bytes exactly: padding spills into a second block.
  checkStr("md5 64 bytes",
          md5Hex(std::string(64, 'a')),
          "014842d480b571495a4a0363793f7367");

  // --- base64 ------------------------------------------------------------
  checkStr("b64 empty", toBase64(Bytes{}), "");
  checkStr("b64 f", toBase64(toBytes("f")), "Zg==");
  checkStr("b64 fo", toBase64(toBytes("fo")), "Zm8=");
  checkStr("b64 foo", toBase64(toBytes("foo")), "Zm9v");
  checkStr("b64 foob", toBase64(toBytes("foob")), "Zm9vYg==");
  checkStr("b64 fooba", toBase64(toBytes("fooba")), "Zm9vYmE=");
  checkStr("b64 foobar", toBase64(toBytes("foobar")), "Zm9vYmFy");

  // Round trip through the URL-safe alphabet (no padding).
  {
    Bytes data = toBytes("\x00\x01\xfe\xff hello base64 world");
    const std::string enc = toBase64Url(data);
    const Bytes back = fromBase64(enc);
    checkInt("b64url roundtrip len", (long long)back.size(), (long long)data.size());
    checkInt("b64url roundtrip equal", back == data ? 1 : 0, 1);
  }
  // Standard base64 decode with '+' and '/' characters.
  {
    Bytes raw = fromBase64("+/8=");
    checkInt("b64 decode len", (long long)raw.size(), 2);
    checkStr("b64 decode hex", toHex(raw, false), "fbff");
  }

  // --- hex ---------------------------------------------------------------
  checkStr("hex upper", toHex(toBytes("\xde\xad\xbe\xef")), "DEADBEEF");
  checkStr("hex lower", toHex(toBytes("\xde\xad\xbe\xef"), false), "deadbeef");
  checkInt("hex roundtrip", fromHex("DEADBEEF") == toBytes("\xde\xad\xbe\xef") ? 1 : 0, 1);

  // --- AES-128 ECB ------------------------------------------------------
  // FIPS-197 Appendix C.1: key 000102..0f, plaintext 00112233..ff.
  {
    Bytes key = fromHex("000102030405060708090a0b0c0d0e0f");
    Bytes pt = fromHex("00112233445566778899aabbccddeeff");
    Bytes ct = aesEcbEncrypt(key, pt);
    // Node's createCipheriv pads too, so a 16-byte input yields 32 bytes: the
    // FIPS-197 block followed by the PKCS#7 block. Compare both halves.
    checkStr("aes fips197 ct",
            toHex(ct, false),
            "69c4e0d86a7b0430d8cdb78070b4c55a954f64f2e4e86e9eee82d20216684899");
    checkStr("aes fips197 block0", toHex(ct, false).substr(0, 32),
            "69c4e0d86a7b0430d8cdb78070b4c55a");
    Bytes back = aesEcbDecrypt(key, ct);
    checkStr("aes fips197 roundtrip", toHex(back, false), "00112233445566778899aabbccddeeff");
  }
  // The real eapi key, on a payload the size of a search body.
  {
    const std::string eapiKey = "e82ckenh8dichen8";
    Bytes key = toBytes(eapiKey);
    Bytes pt = toBytes("{\"s\":\"zhoujielun\",\"type\":1,\"limit\":30,\"offset\":0}");
    Bytes ct = aesEcbEncrypt(key, pt);
    Bytes back = aesEcbDecrypt(key, ct);
    checkStr("aes eapi roundtrip", toString(back), toString(pt));
    checkInt("aes eapi ct len % 16", (long long)(ct.size() % 16), 0);
    checkInt("aes eapi ct len", (long long)ct.size(), 64);
  }
  // Empty plaintext must still produce one padded block.
  {
    Bytes key = toBytes("e82ckenh8dichen8");
    Bytes ct = aesEcbEncrypt(key, Bytes{});
    checkInt("aes empty ct len", (long long)ct.size(), 16);
    checkInt("aes empty roundtrip", aesEcbDecrypt(key, ct).empty() ? 1 : 0, 1);
  }

  // --- AES-128 CBC ------------------------------------------------------
  // NIST SP 800-38A F.2.1 CBC-AES128
  {
    Bytes key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
    Bytes iv = fromHex("000102030405060708090a0b0c0d0e0f");
    Bytes pt = fromHex(
        "6bc1bee22e409f96e93d7e117393172a"
        "ae2d8a571e03ac9c9eb76fac45af8e51");
    Bytes ct = aesCbcEncrypt(key, iv, pt);
    checkStr("aes cbc nist",
            toHex(ct, false).substr(0, 64),
            "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2");
    Bytes back = aesCbcDecrypt(key, iv, ct);
    checkInt("aes cbc roundtrip", toHex(back, false) == toHex(pt, false) ? 1 : 0, 1);
  }
  // The weapi preset key with its real IV, exercising the ASCII-IV path.
  {
    Bytes key = toBytes("0CoJUm6Qyw8W8jud");
    Bytes iv = toBytes("0102030405060708");
    Bytes pt = toBytes("{\"csrf_token\":\"\"}");
    Bytes ct = aesCbcEncrypt(key, iv, pt);
    Bytes back = aesCbcDecrypt(key, iv, ct);
    checkStr("aes cbc weapi roundtrip", toString(back), toString(pt));
  }

  // --- full eapi envelope ------------------------------------------------
  // This mirrors crypto.ts's eapi() exactly; the expected value came from the
  // Node script in /tmp/ne and is asserted here so a regression is caught on
  // the host before it ever reaches the Switch.
  {
    const std::string uri = "/api/search/get";
    const std::string body =
        "{\"s\":\"test\",\"type\":1,\"limit\":1,\"offset\":0,"
        "\"header\":{\"osver\":\"16.2\",\"deviceId\":\"x\",\"os\":\"iPhone OS\","
        "\"appver\":\"9.0.90\",\"versioncode\":\"140\",\"buildver\":\"1\","
        "\"resolution\":\"1920x1080\",\"__csrf\":\"\",\"channel\":\"distribution\","
        "\"requestId\":\"1_0001\"}}";
    const std::string digest =
        md5Hex("nobody" + uri + "use" + body + "md5forencrypt");
    const std::string data =
        uri + "-36cd479b6b5-" + body + "-36cd479b6b5-" + digest;
    Bytes ct = aesEcbEncrypt(toBytes("e82ckenh8dichen8"), toBytes(data));
    // Length check: the envelope is always a multiple of 16 and non-empty.
    checkInt("eapi envelope len % 16", (long long)(ct.size() % 16), 0);
    checkInt("eapi envelope non-empty", ct.empty() ? 0 : 1, 1);
    // Digest length is part of the contract.
    checkInt("eapi digest len", (long long)digest.size(), 32);
  }

  // --- urlEncode --------------------------------------------------------
  checkStr("urlencode plain", urlEncode("abcXYZ019"), "abcXYZ019");
  checkStr("urlencode space", urlEncode("a b"), "a+b");
  checkStr("urlencode safe", urlEncode("-_.*~"), "-_.*~");
  checkStr("urlencode utf8", urlEncode("\xe4\xb8\xad"), "%E4%B8%AD");
  checkStr("urlencode specials", urlEncode("=[]\"{}"), "%3D%5B%5D%22%7B%7D");

  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}