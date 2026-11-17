// SPDX-License-Identifier: AGPL-3.0
// Host smoke test: JSON parser + HTTP client + eapi envelope, against the live
// NetEase API. Run with `--offline` to only exercise the pure parts.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "spn/crypto.hpp"
#include "spn/http.hpp"
#include "spn/json.hpp"

using namespace spn;

static int g_fail = 0;
static int g_pass = 0;

static void check(const char* what, bool ok, const std::string& detail = {}) {
  if (ok) {
    ++g_pass;
  } else {
    ++g_fail;
    std::printf("FAIL %s %s\n", what, detail.c_str());
  }
}

// --- JSON unit checks ------------------------------------------------------
static void jsonTests() {
  json::Value v;
  size_t err = 0;

  check("json parse object", json::parse(R"({"a":1,"b":"x","c":[1,2,{"d":true}]})", v, &err),
        "offset " + std::to_string(err));
  check("json int", v.get("a").asInt() == 1, v.get("a").asString());
  check("json string", v.get("b").asString() == "x", v.get("b").asString());
  check("json array len", v.get("c").size() == 3, std::to_string(v.get("c").size()));
  check("json nested bool", v.get("c").at(2).get("d").asBool());

  // Escapes and surrogate pairs.
  json::Value e;
  check("json escape", json::parse(R"({"s":"a\nb\"c\\d"})", e), "");
  check("json escape value", e.get("s").asString() == "a\nb\"c\\d",
        "[" + e.get("s").asString() + "]");
  check("json unicode", json::parse(R"({"u":"\u4e2d\u6587"})", e), "");
  check("json unicode value", e.get("u").asString() == "\xe4\xb8\xad\xe6\x96\x87",
        e.get("u").asString());
  check("json surrogate", json::parse(R"({"u":"\ud83d\ude00"})", e), "");
  check("json surrogate value", e.get("u").asString() == "\xf0\x9f\x98\x80",
        e.get("u").asString());

  // Big ids must not lose precision.
  check("json big id", json::parse(R"({"id":2038191895})", e), "");
  check("json big id value", e.get("id").asInt() == 2038191895LL,
        std::to_string(e.get("id").asInt()));
  check("json big id string", e.get("id").asString() == "2038191895",
        e.get("id").asString());

  // Nulls and missing keys chain safely.
  check("json null", json::parse(R"({"n":null})", e), "");
  check("json null is null", e.get("n").isNull());
  check("json missing chain", e.get("nope").get("deeper").asString() == "");

  // Malformed input must fail rather than loop.
  check("json truncated", !json::parse(R"({"a":)", e));
  check("json trailing", !json::parse(R"({} garbage)", e));

  // Serialiser round trip.
  json::Value built = json::obj({
      {"s", json::str("\xe5\x8d\xa1\xe5\x86\x9c")},
      {"type", json::num(1)},
      {"limit", json::num(30)},
  });
  const std::string text = json::serialise(built);
  check("json serialise", text.find("\"type\":1") != std::string::npos, text);
  json::Value again;
  check("json roundtrip", json::parse(text, again), text);
  check("json roundtrip value", again.get("s").asString() == "\xe5\x8d\xa1\xe5\x86\x9c");
}

// --- HTTP unit checks (url splitting) --------------------------------------
static void httpTests() {
  std::string host, path;
  uint16_t port = 0;
  check("url parse basic", http::parseUrl("http://a.b/c?d=1", host, port, path),
        "");
  check("url host", host == "a.b", host);
  check("url port", port == 80, std::to_string(port));
  check("url path", path == "/c?d=1", path);

  check("url parse port", http::parseUrl("http://h:8080/x", host, port, path), "");
  check("url port 8080", port == 8080, std::to_string(port));
  check("url path 8080", path == "/x", path);

  check("url reject https", !http::parseUrl("https://h/x", host, port, path));
}

// --- live eapi checks ------------------------------------------------------
static std::string eapiParams(const std::string& uri, const std::string& bodyJson) {
  const std::string digest =
      crypto::md5Hex("nobody" + uri + "use" + bodyJson + "md5forencrypt");
  const std::string data = uri + "-36cd479b6b5-" + bodyJson + "-36cd479b6b5-" + digest;
  const crypto::Bytes ct =
      crypto::aesEcbEncrypt(crypto::toBytes("e82ckenh8dichen8"), crypto::toBytes(data));
  return crypto::toHex(ct, true);
}

static const char* kUa = "NeteaseMusic 9.0.90/5038 (iPhone; iOS 16.2; zh_CN)";

static std::string makeCookie() {
  static const char* hexd = "0123456789abcdef";
  std::string nuid;
  // Deterministic-ish filler is fine here; the server only needs uniqueness.
  for (int i = 0; i < 32; ++i) nuid.push_back(hexd[(i * 7 + 3) & 15]);
  return "__remember_me=true; ntes_kaola_ad=1; _ntes_nuid=" + nuid +
         "; _ntes_nnid=" + nuid + ",1794000000000; WNMCID=abcdef.1794000000000.01.0;"
         " WEVNSM=1.0.0; os=iPhone OS; osver=16.2;"
         " deviceId=1A2B3C4D5E6F708192A3B4C5D6E7F8091A2B3C4D5E6F708192;"
         " appver=9.0.90; channel=distribution; buildver=1794000000;"
         " resolution=1920x1080; __csrf=";
}

static bool eapiCall(const std::string& uri, const std::string& bodyJson, json::Value& out,
                     std::string& detail) {
  const std::string params = eapiParams(uri, bodyJson);
  const std::string url = "http://interfacepc.music.163.com/eapi/" + uri.substr(5);
  const auto res = http::postForm(url, "params=" + crypto::urlEncode(params),
                                  {{"User-Agent", kUa}, {"Cookie", makeCookie()}}, 20000);
  if (res.status == 0) {
    detail = res.error;
    return false;
  }
  detail = "status " + std::to_string(res.status) + " body " + res.body.substr(0, 200);
  if (res.status != 200) return false;
  size_t err = 0;
  if (!json::parse(res.body, out, &err)) {
    detail += " (json err at " + std::to_string(err) + ")";
    return false;
  }
  return true;
}

static void liveTests() {
  // 1) QR login unikey.
  {
    json::Value v;
    std::string detail;
    const bool ok = eapiCall("/api/login/qrcode/unikey",
                             R"({"type":3,"header":{"osver":"16.2","deviceId":"x","os":"iPhone OS","appver":"9.0.90","versioncode":"140","buildver":"1794000000","resolution":"1920x1080","__csrf":"","channel":"distribution","requestId":"1_0001"}})",
                             v, detail);
    check("live unikey", ok, detail);
    check("live unikey code", ok && v.get("code").asInt() == 200,
          ok ? v.get("code").asString() : detail);
    check("live unikey value", ok && v.get("unikey").asString().size() > 20,
          ok ? v.get("unikey").asString() : detail);
  }

  // 2) Search.
  {
    json::Value v;
    std::string detail;
    const bool ok = eapiCall("/api/search/get",
                             R"({"s":"\u5361\u519c","type":1,"limit":3,"offset":0,"header":{"osver":"16.2","deviceId":"x","os":"iPhone OS","appver":"9.0.90","versioncode":"140","buildver":"1794000000","resolution":"1920x1080","__csrf":"","channel":"distribution","requestId":"1_0001"}})",
                             v, detail);
    check("live search", ok, detail);
    if (ok) {
      const auto& songs = v.get("result").get("songs");
      check("live search songs", songs.size() > 0, std::to_string(songs.size()));
      if (songs.size() > 0) {
        const auto& s0 = songs.at(0);
        check("live search name", !s0.get("name").asString().empty(),
              s0.get("name").asString());
        check("live search id", s0.get("id").asInt() > 0,
              std::to_string(s0.get("id").asInt()));
        std::printf("  first hit: %s / id=%lld / fee=%lld\n",
                    s0.get("name").asString().c_str(),
                    (long long)s0.get("id").asInt(),
                    (long long)s0.get("fee").asInt());
      }
    }
  }

  // 3) Song URL: the endpoint that feeds the player.
  {
    json::Value v;
    std::string detail;
    const bool ok = eapiCall("/api/song/enhance/player/url/v1",
                             R"({"ids":"[139774]","level":"exhigh","encodeType":"flac","header":{"osver":"16.2","deviceId":"x","os":"iPhone OS","appver":"9.0.90","versioncode":"140","buildver":"1794000000","resolution":"1920x1080","__csrf":"","channel":"distribution","requestId":"1_0001"}})",
                             v, detail);
    check("live song url", ok, detail);
    if (ok) {
      const auto& d0 = v.get("data").at(0);
      const std::string url = d0.get("url").asString();
      check("live song url present", !url.empty(), detail);
      check("live song url http", url.compare(0, 7, "http://") == 0, url.substr(0, 40));
      std::printf("  song url: %s\n", url.substr(0, 80).c_str());
    }
  }

  // 4) Lyric: proves the port can feed the eight-format parser from the network.
  {
    json::Value v;
    std::string detail;
    const bool ok = eapiCall("/api/song/lyric/v1",
                             R"({"id":139774,"cp":false,"tv":0,"lv":0,"rv":0,"kv":0,"yv":0,"ytv":0,"yrv":0,"header":{"osver":"16.2","deviceId":"x","os":"iPhone OS","appver":"9.0.90","versioncode":"140","buildver":"1794000000","resolution":"1920x1080","__csrf":"","channel":"distribution","requestId":"1_0001"}})",
                             v, detail);
    check("live lyric", ok, detail);
    if (ok) {
      check("live lyric code", v.get("code").asInt() == 200, v.get("code").asString());
    }
  }
}

int main(int argc, char** argv) {
  const bool offline = (argc > 1 && std::string(argv[1]) == "--offline");

  jsonTests();
  httpTests();

  if (!offline) {
    std::printf("--- live API ---\n");
    liveTests();
  }

  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}