// SPDX-License-Identifier: AGPL-3.0
// NetEase Cloud Music client (see netease.hpp).

#include "spn/netease.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>
#include <sys/types.h>

#include "spn/crypto.hpp"
#include "spn/http.hpp"
#include "spn/json.hpp"

namespace spn::netease {
namespace {

// The protocol keys and the client identity the desktop app impersonates. These
// are not secrets: they ship inside every copy of the official client.
constexpr const char* kEapiKey = "e82ckenh8dichen8";
constexpr const char* kApiHost = "interfacepc.music.163.com";
constexpr const char* kUa = "NeteaseMusic 9.0.90/5038 (iPhone; iOS 16.2; zh_CN)";
constexpr const char* kOs = "iPhone OS";
constexpr const char* kOsVer = "16.2";
constexpr const char* kAppVer = "9.0.90";
constexpr const char* kChannel = "distribution";

std::string nowMillis() {
  // Only used for the requestId/buildver fields; a monotonic counter plus the
  // process start time is enough and keeps this file free of a clock dependency
  // beyond what the callers already have.
  static uint64_t seq = 0;
  return std::to_string(1794000000LL + (int64_t)(seq++));
}

// Pulls a named cookie value out of one Set-Cookie header line.
std::string cookieValue(const std::string& setCookie, const std::string& name) {
  const std::string needle = name + "=";
  size_t pos = 0;
  while (pos < setCookie.size()) {
    const size_t next = setCookie.find(';', pos);
    const std::string item = setCookie.substr(
        pos, (next == std::string::npos ? setCookie.size() : next) - pos);
    size_t a = 0;
    while (a < item.size() && (item[a] == ' ' || item[a] == '\t')) ++a;
    if (item.compare(a, needle.size(), needle) == 0) {
      return item.substr(a + needle.size());
    }
    if (next == std::string::npos) break;
    pos = next + 1;
  }
  return {};
}

// Joins the artist list into the "A / B" form the rest of the UI expects.
std::string joinArtists(const json::Value& artists) {
  std::string out;
  for (size_t i = 0; i < artists.size(); ++i) {
    const std::string name = artists.at(i).get("name").asString();
    if (name.empty()) continue;
    if (!out.empty()) out += " / ";
    out += name;
  }
  return out;
}

// Reads a Song out of either the search shape or the playlist-detail shape.
// The two differ: search uses `artists`/`album.picUrl`, playlistDetail uses
// `ar`/`al.picUrl` and wraps duration in `dt`. Handle both.
Song songFromJson(const json::Value& s) {
  Song song;
  song.id = s.get("id").asInt();

  // `name` is direct in search results; playlist tracks nest it under `track`.
  const json::Value& core = s.has("track") ? s.get("track") : s;
  song.name = core.get("name").asString();

  const json::Value& artists =
      core.has("artists") ? core.get("artists") : core.get("ar");
  song.artist = joinArtists(artists);
  if (song.artist.empty()) song.artist = "未知艺术家";

  const json::Value& album = core.has("album") ? core.get("album") : core.get("al");
  song.album = album.get("name").asString();
  song.coverUrl = album.get("picUrl").asString();
  if (song.coverUrl.empty()) song.coverUrl = album.get("img1v1Url").asString();
  song.albumId = album.get("id").asInt();

  song.durationMs = core.has("duration") ? core.get("duration").asInt()
                                        : core.get("dt").asInt();
  song.fee = int(core.get("fee").asInt());
  return song;
}

// Extracts the `c` array text of the new-style lyric format into LRC. The API
// returns `{"t":0,"c":[{"tx":"..."}]}` for a few tracks (notably instrumental
// entries); rather than teach the LRC parser a second syntax, flatten here.
std::string flattenNewLyric(const std::string& raw) {
  if (raw.empty() || raw[0] != '{') return raw;
  json::Value v;
  if (!json::parse(raw, v) || !v.has("c")) return raw;
  std::string out;
  const auto& lines = v.get("c");
  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string tx = lines.at(i).get("tx").asString();
    if (tx.empty()) continue;
    out += tx;
    out += "\n";
  }
  return out;
}

}  // namespace

const char* qrStateName(QrState s) {
  switch (s) {
    case QrState::Waiting: return "等待扫码";
    case QrState::Scanned: return "已扫描，请在手机上确认";
    case QrState::Confirmed: return "登录成功";
    case QrState::Expired: return "二维码已过期";
    case QrState::Failed: return "登录失败";
  }
  return "未知状态";
}

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

Client::Client() { ensureDeviceId(); }

const std::string& Client::sessionPath() {
  static std::string path = "sdmc:/switch/SPlayer-Next/state/netease_session.txt";
  return path;
}

void Client::setStateRoot(const std::string& root) {
  // Session path is rebuilt from the root; the default matches store.cpp so the
  // Switch never needs to call this.
  const std::string base = root.empty() ? "sdmc:/switch/SPlayer-Next/state" : root;
  const_cast<std::string&>(sessionPath()) = base + "/netease_session.txt";
}

void Client::ensureDeviceId() {
  if (!session_.deviceId.empty()) return;
  // 52 uppercase hex chars, which is what the client sends.
  static const char* hexd = "0123456789ABCDEF";
  std::string id;
  id.reserve(52);
  for (int i = 0; i < 52; ++i) {
    id.push_back(hexd[(i * 11 + 5) & 15]);
  }
  session_.deviceId = id;
}

bool Client::loadSession() {
  std::FILE* f = std::fopen(sessionPath().c_str(), "rb");
  if (!f) return false;
  std::string text;
  char buf[512];
  while (size_t n = std::fread(buf, 1, sizeof(buf), f)) text.append(buf, n);
  std::fclose(f);

  // Flat "key=value" lines, matching store.cpp's approach.
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t eol = text.find('\n', pos);
    const std::string line =
        text.substr(pos, (eol == std::string::npos ? text.size() : eol) - pos);
    const size_t eq = line.find('=');
    if (eq != std::string::npos) {
      const std::string key = line.substr(0, eq);
      const std::string val = line.substr(eq + 1);
      if (key == "musicU") session_.musicU = val;
      else if (key == "userId") session_.userId = std::atoll(val.c_str());
      else if (key == "nickname") session_.nickname = val;
      else if (key == "avatarUrl") session_.avatarUrl = val;
      else if (key == "deviceId") session_.deviceId = val;
    }
    if (eol == std::string::npos) break;
    pos = eol + 1;
  }
  ensureDeviceId();
  return true;
}

bool Client::saveSession() const {
  // Make sure the state directory exists. On the Switch this is the shared
  // sdmc: path; on the host it is whatever setStateRoot() selected.
  const std::string& p = sessionPath();
  const size_t slash = p.rfind('/');
  if (slash != std::string::npos) {
    const std::string dir = p.substr(0, slash);
    ::mkdir(dir.c_str(), 0777);
  }

  std::FILE* f = std::fopen(sessionPath().c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "musicU=%s\n", session_.musicU.c_str());
  std::fprintf(f, "userId=%lld\n", (long long)session_.userId);
  std::fprintf(f, "nickname=%s\n", session_.nickname.c_str());
  std::fprintf(f, "avatarUrl=%s\n", session_.avatarUrl.c_str());
  std::fprintf(f, "deviceId=%s\n", session_.deviceId.c_str());
  std::fclose(f);
  return true;
}

std::string Client::cookieHeader() const {
  // The eapi endpoints read their parameters from the POST body's `header`
  // object, but the login cookie must still ride along in the Cookie header.
  std::string c = "__remember_me=true; os=" + std::string(kOs) + "; osver=" +
                  std::string(kOsVer) + "; appver=" + std::string(kAppVer) +
                  "; channel=" + std::string(kChannel) + "; deviceId=" + session_.deviceId +
                  "; buildver=1794000000; resolution=1920x1080; __csrf=";
  if (!session_.musicU.empty()) {
    c += "; MUSIC_U=";
    c += session_.musicU;
  }
  return c;
}

std::string Client::withHeader(const std::string& bodyJsonWithoutClose) const {
  // Callers pass the JSON object *without* its trailing '}' so the mandatory
  // header block can be spliced in.
  std::string out = bodyJsonWithoutClose;
  if (out.size() >= 2 && out.back() != '{' && out.back() != ',') out += ",";
  out += "\"header\":{";
  out += "\"osver\":\"" + std::string(kOsVer) + "\",";
  out += "\"deviceId\":\"" + session_.deviceId + "\",";
  out += "\"os\":\"" + std::string(kOs) + "\",";
  out += "\"appver\":\"" + std::string(kAppVer) + "\",";
  out += "\"versioncode\":\"140\",";
  out += "\"buildver\":\"1794000000\",";
  out += "\"resolution\":\"1920x1080\",";
  out += "\"__csrf\":\"\",";
  out += "\"channel\":\"" + std::string(kChannel) + "\",";
  out += "\"requestId\":\"" + nowMillis() + "_0001\"";
  out += "}}";
  return out;
}

bool Client::eapi(const std::string& uri, const std::string& bodyJson, std::string& jsonOut,
                  std::string& err) {
  ++requests_;

  // 1) signature: md5("nobody" + uri + "use" + body + "md5forencrypt")
  const std::string digest =
      crypto::md5Hex("nobody" + uri + "use" + bodyJson + "md5forencrypt");
  // 2) envelope: uri -36cd479b6b5- body -36cd479b6b5- digest
  const std::string data = uri + "-36cd479b6b5-" + bodyJson + "-36cd479b6b5-" + digest;
  // 3) AES-128-ECB with the eapi key, hex-encoded.
  const crypto::Bytes ct =
      crypto::aesEcbEncrypt(crypto::toBytes(kEapiKey), crypto::toBytes(data));
  const std::string params = crypto::toHex(ct, true);

  const std::string url = std::string("http://") + kApiHost + "/eapi/" + uri.substr(5);
  const auto res = http::postForm(url, "params=" + crypto::urlEncode(params),
                                  {{"User-Agent", kUa}, {"Cookie", cookieHeader()}}, 20000);

  if (res.status == 0) {
    err = "网络错误: " + res.error;
    return false;
  }
  if (res.status != 200) {
    err = "服务器返回 " + std::to_string(res.status);
    return false;
  }
  jsonOut = res.body;
  return true;
}

// --- login -----------------------------------------------------------------

bool Client::qrCreate(QrState& state, std::string& err) {
  std::string body;
  if (!eapi("/api/login/qrcode/unikey", withHeader("{\"type\":3"), body, err)) {
    state = QrState::Failed;
    return false;
  }
  json::Value v;
  if (!json::parse(body, v)) {
    err = "无法解析登录响应";
    state = QrState::Failed;
    return false;
  }
  if (v.get("code").asInt() != 200) {
    err = "获取二维码失败 (code " + v.get("code").asString() + ")";
    state = QrState::Failed;
    return false;
  }
  session_.unikey = v.get("unikey").asString();
  state = QrState::Waiting;
  return !session_.unikey.empty();
}

QrState Client::qrCheck(std::string& err) {
  if (session_.unikey.empty()) {
    err = "没有待验证的二维码";
    return QrState::Failed;
  }

  const std::string uri = "/api/login/qrcode/client/login";
  const std::string bodyJson =
      withHeader("{\"key\":\"" + session_.unikey + "\",\"type\":3");
  const std::string digest =
      crypto::md5Hex("nobody" + uri + "use" + bodyJson + "md5forencrypt");
  const std::string data = uri + "-36cd479b6b5-" + bodyJson + "-36cd479b6b5-" + digest;
  const crypto::Bytes ct =
      crypto::aesEcbEncrypt(crypto::toBytes(kEapiKey), crypto::toBytes(data));
  const std::string params = crypto::toHex(ct, true);

  ++requests_;
  const std::string url = std::string("http://") + kApiHost + "/eapi/" + uri.substr(5);
  const auto res = http::postForm(url, "params=" + crypto::urlEncode(params),
                                  {{"User-Agent", kUa}, {"Cookie", cookieHeader()}}, 20000);
  if (res.status == 0) {
    err = "网络错误: " + res.error;
    return QrState::Failed;
  }

  json::Value v;
  if (!json::parse(res.body, v)) {
    err = "无法解析登录响应";
    return QrState::Failed;
  }
  const int64_t code = v.get("code").asInt();

  // The token arrives as a Set-Cookie header on the 803 reply, so scan every
  // set-cookie line rather than trusting the JSON body.
  for (const auto& kv : res.headers) {
    if (kv.first != "set-cookie") continue;
    const std::string musicU = cookieValue(kv.second, "MUSIC_U");
    if (!musicU.empty()) session_.musicU = musicU;
  }

  switch (code) {
    case 801:
      return QrState::Waiting;
    case 802:
      return QrState::Scanned;
    case 800:
      session_.unikey.clear();
      return QrState::Expired;
    case 803: {
      if (session_.musicU.empty()) {
        err = "登录成功但未取到凭据";
        return QrState::Failed;
      }
      session_.unikey.clear();
      saveSession();
      std::string profileErr;
      refreshProfile(profileErr);  // best effort; failure is not fatal
      saveSession();
      return QrState::Confirmed;
    }
    default: {
      err = "登录返回未知状态 (code " + std::to_string(code) + ")";
      return QrState::Failed;
    }
  }
}

bool Client::refreshProfile(std::string& err) {
  if (!session_.loggedIn()) {
    err = "尚未登录";
    return false;
  }
  std::string body;
  if (!eapi("/api/w/nuser/account/get", withHeader("{"), body, err)) return false;

  json::Value v;
  if (!json::parse(body, v)) {
    err = "无法解析账号信息";
    return false;
  }
  const json::Value& profile = v.get("profile");
  if (profile.isNull()) {
    // The token was rejected: clear it so the UI shows the login button again.
    session_.musicU.clear();
    err = "登录已失效，请重新扫码";
    return false;
  }
  session_.userId = profile.get("userId").asInt();
  session_.nickname = profile.get("nickname").asString();
  session_.avatarUrl = profile.get("avatarUrl").asString();
  return true;
}

void Client::logout() {
  if (session_.loggedIn()) {
    std::string body;
    std::string err;
    eapi("/api/logout", withHeader("{"), body, err);  // best effort
  }
  session_.musicU.clear();
  session_.userId = 0;
  session_.nickname.clear();
  session_.avatarUrl.clear();
  session_.unikey.clear();
  saveSession();
}

// --- catalog ---------------------------------------------------------------

bool Client::search(const std::string& keyword, int limit, int offset,
                    std::vector<Song>& out, std::string& err) {
  out.clear();
  // The keyword goes through JSON escaping, so build it with the serialiser.
  const json::Value body = json::obj({
      {"s", json::str(keyword)},
      {"type", json::num(1)},
      {"limit", json::num(limit)},
      {"offset", json::num(offset)},
  });
  // serialise() closes the object; reopen it to splice the header in.
  std::string raw = json::serialise(body);
  if (!raw.empty() && raw.back() == '}') raw.pop_back();

  std::string reply;
  if (!eapi("/api/search/get", withHeader(raw), reply, err)) return false;

  json::Value v;
  if (!json::parse(reply, v)) {
    err = "无法解析搜索结果";
    return false;
  }
  if (v.get("code").asInt() != 200) {
    err = "搜索失败 (code " + v.get("code").asString() + ")";
    return false;
  }
  const json::Value& songs = v.get("result").get("songs");
  for (size_t i = 0; i < songs.size(); ++i) out.push_back(songFromJson(songs.at(i)));
  return true;
}

bool Client::userPlaylists(std::vector<PlaylistSummary>& out, std::string& err) {
  out.clear();
  if (!session_.loggedIn()) {
    err = "需要登录";
    return false;
  }
  std::string raw = "{\"uid\":" + std::to_string(session_.userId) + ",\"limit\":1000";
  std::string reply;
  if (!eapi("/api/user/playlist", withHeader(raw), reply, err)) return false;

  json::Value v;
  if (!json::parse(reply, v)) {
    err = "无法解析歌单";
    return false;
  }
  const json::Value& list = v.get("playlist");
  for (size_t i = 0; i < list.size(); ++i) {
    const auto& p = list.at(i);
    PlaylistSummary s;
    s.id = p.get("id").asInt();
    s.name = p.get("name").asString();
    s.coverUrl = p.get("coverImgUrl").asString();
    s.trackCount = int(p.get("trackCount").asInt());
    s.creatorId = p.get("userId").asInt();
    out.push_back(std::move(s));
  }
  return true;
}

// --- cloud drive ------------------------------------------------------------
// `/api/v1/cloud` returns { count, size, maxSize, hasMore, data:[{songId,
// fileName, fileSize, simpleSong}] }. The playable entry is `simpleSong`, which
// carries the same shape search results use, so songFromJson handles it.
bool Client::userCloud(int offset, int limit, std::vector<Song>& out, std::string& err) {
  out.clear();
  if (!session_.loggedIn()) {
    err = "需要登录";
    return false;
  }
  if (limit <= 0) limit = 200;
  if (limit > 500) limit = 500;
  const std::string raw = "{\"offset\":" + std::to_string(offset) + ",\"limit\":" +
                          std::to_string(limit);
  std::string reply;
  if (!eapi("/api/v1/cloud", withHeader(raw), reply, err)) return false;

  json::Value v;
  if (!json::parse(reply, v)) {
    err = "无法解析云盘";
    return false;
  }
  const json::Value& data = v.get("data");
  if (!data.isArray()) {
    // `code: 200` with no data means an empty cloud, not a failure.
    if (v.get("code").asInt() == 200) return true;
    err = "云盘返回异常";
    return false;
  }
  for (size_t i = 0; i < data.size(); ++i) {
    const json::Value& item = data.at(i);
    const json::Value& s = item.get("simpleSong");
    if (s.isNull()) continue;
    Song song = songFromJson(s);
    // Cloud uploads sometimes carry an empty album name; the file name is the
    // only human-readable hint, so fall back to it.
    if (song.name.empty()) song.name = item.get("fileName").asString();
    out.push_back(std::move(song));
  }
  return true;
}

bool Client::playlistDetail(int64_t playlistId, std::vector<Song>& out,
                           PlaylistSummary* meta, std::string& err) {
  out.clear();
  const std::string raw = "{\"id\":" + std::to_string(playlistId) + ",\"n\":1000";
  std::string reply;
  if (!eapi("/api/v6/playlist/detail", withHeader(raw), reply, err)) return false;

  json::Value v;
  if (!json::parse(reply, v)) {
    err = "无法解析歌单详情";
    return false;
  }
  const json::Value& pl = v.get("playlist");
  if (pl.isNull()) {
    err = "歌单不存在";
    return false;
  }
  if (meta) {
    meta->id = pl.get("id").asInt();
    meta->name = pl.get("name").asString();
    meta->coverUrl = pl.get("coverImgUrl").asString();
    meta->trackCount = int(pl.get("trackCount").asInt());
    meta->creatorId = pl.get("userId").asInt();
  }
  const json::Value& tracks = pl.get("tracks");
  for (size_t i = 0; i < tracks.size(); ++i) out.push_back(songFromJson(tracks.at(i)));
  return true;
}

// --- playback --------------------------------------------------------------

bool Client::songUrl(int64_t id, const std::string& level, std::string& url, int& bitrate,
                     std::string& err) {
  url.clear();
  bitrate = 0;
  const std::string raw = "{\"ids\":\"[" + std::to_string(id) + "]\",\"level\":\"" + level +
                          "\",\"encodeType\":\"flac\"";
  std::string reply;
  if (!eapi("/api/song/enhance/player/url/v1", withHeader(raw), reply, err)) return false;

  json::Value v;
  if (!json::parse(reply, v)) {
    err = "无法解析播放地址";
    return false;
  }
  const json::Value& d0 = v.get("data").at(0);
  url = d0.get("url").asString();
  bitrate = int(d0.get("br").asInt());
  if (url.empty()) {
    // Distinguish the common causes so the UI can say something useful.
    const int64_t code = d0.get("code").asInt();
    if (code == 404) {
      err = "该歌曲无版权或仅 VIP 可听";
    } else if (!d0.get("freeTrialInfo").isNull()) {
      err = "仅可试听片段";
    } else {
      err = "无法获取播放地址 (code " + std::to_string(code) + ")";
    }
    return false;
  }
  if (url.compare(0, 7, "http://") != 0) {
    err = "播放地址不是明文 http，无法播放";
    url.clear();
    return false;
  }
  return true;
}

// --- lyrics ----------------------------------------------------------------

bool Client::lyric(int64_t id, std::string& lrc, std::string& yrc, std::string& err) {
  lrc.clear();
  yrc.clear();
  const std::string raw = "{\"id\":" + std::to_string(id) +
                          ",\"cp\":false,\"tv\":0,\"lv\":0,\"rv\":0,\"kv\":0,"
                          "\"yv\":0,\"ytv\":0,\"yrv\":0";
  std::string reply;
  if (!eapi("/api/song/lyric/v1", withHeader(raw), reply, err)) return false;

  json::Value v;
  if (!json::parse(reply, v)) {
    err = "无法解析歌词";
    return false;
  }
  lrc = flattenNewLyric(v.get("lrc").get("lyric").asString());
  yrc = flattenNewLyric(v.get("yrc").get("lyric").asString());
  if (lrc.empty() && yrc.empty()) {
    err = "该歌曲没有歌词";
    return false;
  }
  return true;
}

// --- misc ------------------------------------------------------------------

bool Client::scrobble(int64_t songId, int64_t albumId, int64_t durationMs,
                      int64_t playedMs) {
  if (!session_.loggedIn()) return false;
  const std::string raw =
      "{\"id\":" + std::to_string(songId) + ",\"sourceid\":" + std::to_string(albumId) +
      ",\"time\":" + std::to_string(durationMs) + ",\"total\":" + std::to_string(durationMs) +
      ",\"playedTime\":" + std::to_string(playedMs) + ",\"download\":0,\"vipType\":0";
  std::string reply;
  std::string err;
  return eapi("/api/feedback/weblog", withHeader(raw), reply, err);
}

}  // namespace spn::netease