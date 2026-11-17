// SPDX-License-Identifier: AGPL-3.0
// Embedded tag reading: ID3v2/ID3v1 (MP3), Vorbis comments + PICTURE (FLAC),
// RIFF LIST/INFO (WAV).
#include "spn/tag.hpp"

#include <cstdio>
#include <cstring>

namespace spn {
namespace {

constexpr size_t kMaxTagBlock = 24u * 1024u * 1024u;  // sanity cap
constexpr size_t kMaxCover = 12u * 1024u * 1024u;

inline uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
         uint32_t(p[3]);
}

inline uint32_t be24(const uint8_t* p) {
  return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[2]);
}

inline uint32_t le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}

inline uint32_t syncsafe32(const uint8_t* p) {
  return (uint32_t(p[0] & 0x7F) << 21) | (uint32_t(p[1] & 0x7F) << 14) |
         (uint32_t(p[2] & 0x7F) << 7) | uint32_t(p[3] & 0x7F);
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(char(cp));
  } else if (cp < 0x800) {
    out.push_back(char(0xC0 | (cp >> 6)));
    out.push_back(char(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(char(0xE0 | (cp >> 12)));
    out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(char(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(char(0xF0 | (cp >> 18)));
    out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(char(0x80 | (cp & 0x3F)));
  }
}

std::string latin1ToUtf8(const uint8_t* p, size_t n) {
  std::string out;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) appendUtf8(out, p[i]);
  return out;
}

int parseLeadingInt(const std::string& s) {
  int v = 0;
  bool any = false;
  for (char c : s) {
    if (c >= '0' && c <= '9') {
      v = v * 10 + (c - '0');
      any = true;
    } else if (any) {
      break;
    } else if (c != ' ' && c != '\t') {
      break;
    }
  }
  return any ? v : 0;
}

int parseYear(const std::string& s) {
  const int v = parseLeadingInt(s);
  if (v >= 1000 && v <= 2999) return v;
  return 0;
}

bool readAt(FILE* fp, long offset, void* dst, size_t n) {
  if (std::fseek(fp, offset, SEEK_SET) != 0) return false;
  return std::fread(dst, 1, n, fp) == n;
}

// --- base64 (for FLAC METADATA_BLOCK_PICTURE in Vorbis comments) -----------

std::vector<uint8_t> base64Decode(const std::string& in) {
  static int8_t table[256];
  static bool init = false;
  if (!init) {
    for (int i = 0; i < 256; ++i) table[i] = -1;
    const char* alpha =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; ++i) table[uint8_t(alpha[i])] = int8_t(i);
    init = true;
  }

  std::vector<uint8_t> out;
  out.reserve(in.size() * 3 / 4 + 4);
  uint32_t acc = 0;
  int bits = 0;
  for (char ch : in) {
    if (ch == '=') break;
    const int8_t v = table[uint8_t(ch)];
    if (v < 0) continue;
    acc = (acc << 6) | uint32_t(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(uint8_t((acc >> bits) & 0xFF));
    }
  }
  return out;
}

// --- ID3v2 ----------------------------------------------------------------

uint64_t decodeTextFrame(uint8_t enc, const uint8_t* p, size_t n,
                         std::string& out) {
  size_t used = 0;
  switch (enc) {
    case 0:
      used = n;
      out = latin1ToUtf8(p, n);
      break;
    case 1: {
      // UTF-16 with BOM.
      if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        out = utf16ToUtf8(p + 2, n - 2, false);
      } else if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        out = utf16ToUtf8(p + 2, n - 2, true);
      } else {
        out = utf16ToUtf8(p, n, false);
      }
      used = n;
      break;
    }
    case 2:
      out = utf16ToUtf8(p, n, true);
      used = n;
      break;
    default:
      used = n;
      out.assign(reinterpret_cast<const char*>(p), n);
      break;
  }
  // Cut a multi-value frame at the first NUL.
  const size_t nul = out.find('\0');
  if (nul != std::string::npos) out.resize(nul);
  out = trimTagString(out);
  return used;
}

void mappingId3v2Frame(const char* id, const std::string& value, TagInfo& t) {
  const std::string key(id, 4);
  if (key == "TIT2") {
    if (t.title.empty()) t.title = value;
  } else if (key == "TPE1") {
    if (t.artist.empty()) t.artist = value;
  } else if (key == "TALB") {
    if (t.album.empty()) t.album = value;
  } else if (key == "TPE2") {
    if (t.albumArtist.empty()) t.albumArtist = value;
  } else if (key == "TCON") {
    if (t.genre.empty()) t.genre = value;
  } else if (key == "TYER" || key == "TDRC" || key == "TDOR") {
    if (!t.year) t.year = parseYear(value);
  } else if (key == "TRCK") {
    if (!t.trackNo) t.trackNo = parseLeadingInt(value);
  } else if (key == "TPOS") {
    if (!t.discNo) t.discNo = parseLeadingInt(value);
  }
}

void parseApicFrame(const uint8_t* p, size_t n, TagInfo& t) {
  if (n < 4) return;
  const uint8_t enc = p[0];
  size_t i = 1;
  // MIME type: always latin1, NUL terminated.
  const size_t mimeStart = i;
  while (i < n && p[i] != 0) ++i;
  const std::string mime(reinterpret_cast<const char*>(p + mimeStart), i - mimeStart);
  if (i < n) ++i;
  if (i >= n) return;
  const uint8_t picType = p[i++];

  // Description, terminated according to the text encoding.
  if (enc == 1 || enc == 2) {
    while (i + 1 < n && !(p[i] == 0 && p[i + 1] == 0)) i += 2;
    i += 2;
  } else {
    while (i < n && p[i] != 0) ++i;
    ++i;
  }
  if (i >= n) return;

  const size_t dataLen = n - i;
  if (dataLen == 0 || dataLen > kMaxCover) return;
  // Prefer the front cover (type 3) but accept anything if it is the only one.
  if (t.hasCover() && picType != 3) return;
  t.cover.assign(p + i, p + n);
  t.coverMime = mime;
}

void parseId3v2(FILE* fp, TagInfo& t) {
  uint8_t hdr[10];
  if (!readAt(fp, 0, hdr, 10)) return;
  if (std::memcmp(hdr, "ID3", 3) != 0) return;

  const uint8_t version = hdr[3];
  const uint8_t flags = hdr[5];
  const uint32_t size = syncsafe32(hdr + 6);
  if (size < 10 || size > kMaxTagBlock) return;

  // The tag body starts after the 10-byte header; an extended header may follow.
  size_t bodyOff = 10;
  uint32_t bodyLen = size;
  if ((flags & 0x40) != 0 && bodyLen >= 4) {
    // Extended header size is syncsafe in v2.4 and plain in v2.3.
    uint8_t ext[4];
    if (!readAt(fp, 10, ext, 4)) return;
    uint32_t extSize = (version >= 4) ? syncsafe32(ext) : be32(ext);
    if (extSize >= bodyLen) return;
    bodyOff += extSize;
    bodyLen -= extSize;
  }
  if (bodyLen == 0) return;

  std::vector<uint8_t> buf(bodyLen);
  if (!readAt(fp, long(bodyOff), buf.data(), bodyLen)) return;

  size_t i = 0;
  const bool v22 = (version <= 2);
  const size_t frameHdrLen = v22 ? 6 : 10;

  while (i + frameHdrLen <= buf.size()) {
    const uint8_t* fh = buf.data() + i;
    if (fh[0] == 0) break;  // padding

    std::string id;
    if (v22) {
      id.assign(reinterpret_cast<const char*>(fh), 3);
      // v2.2 uses 3-char synonyms.
      if (id == "TT2") id = "TIT2";
      else if (id == "TP1") id = "TPE1";
      else if (id == "TAL") id = "TALB";
      else if (id == "TP2") id = "TPE2";
      else if (id == "TCO") id = "TCON";
      else if (id == "TYE") id = "TYER";
      else if (id == "TRK") id = "TRCK";
      else if (id == "TPA") id = "TPOS";
      else if (id == "PIC") id = "APIC";
    } else {
      id.assign(reinterpret_cast<const char*>(fh), 4);
    }

    uint32_t frameSize = 0;
    if (v22) {
      frameSize = be24(fh + 3);
    } else if (version >= 4) {
      frameSize = syncsafe32(fh + 4);
    } else {
      frameSize = be32(fh + 4);
    }
    if (frameSize == 0) break;
    i += frameHdrLen;
    if (i + frameSize > buf.size()) break;

    const uint8_t* data = buf.data() + i;
    const size_t avail = frameSize;

    if (id == "APIC") {
      parseApicFrame(data, avail, t);
    } else if (!id.empty() && id[0] == 'T' && avail >= 1) {
      std::string value;
      decodeTextFrame(data[0], data + 1, avail - 1, value);
      mappingId3v2Frame(id.c_str(), value, t);
    }
    i += frameSize;
  }
}

void parseId3v1(FILE* fp, TagInfo& t) {
  if (std::fseek(fp, 0, SEEK_END) != 0) return;
  const long end = std::ftell(fp);
  if (end < 128) return;
  uint8_t tag[128];
  if (!readAt(fp, end - 128, tag, 128)) return;
  if (std::memcmp(tag, "TAG", 3) != 0) return;

  auto field = [&](int off, int len) {
    const uint8_t* p = tag + off;
    size_t n = size_t(len);
    while (n > 0 && (p[n - 1] == 0 || p[n - 1] == ' ')) --n;
    return trimTagString(latin1ToUtf8(p, n));
  };

  if (t.title.empty()) t.title = field(3, 30);
  if (t.artist.empty()) t.artist = field(33, 30);
  if (t.album.empty()) t.album = field(63, 30);
  if (!t.year) t.year = parseYear(field(93, 4));
  if (!t.trackNo && tag[125] == 0 && tag[126] != 0) t.trackNo = tag[126];
}

// --- FLAC -----------------------------------------------------------------

void parseFlacPicture(const uint8_t* p, size_t n, TagInfo& t) {
  if (n < 32) return;
  size_t i = 0;
  const uint32_t picType = be32(p + i);
  i += 4;
  const uint32_t mimeLen = be32(p + i);
  i += 4;
  if (i + mimeLen + 4 > n) return;
  const std::string mime(reinterpret_cast<const char*>(p + i), mimeLen);
  i += mimeLen;
  const uint32_t descLen = be32(p + i);
  i += 4;
  if (i + descLen + 20 > n) return;
  i += descLen;
  i += 16;  // width, height, depth, colors
  const uint32_t dataLen = be32(p + i);
  i += 4;
  if (dataLen == 0 || dataLen > kMaxCover || i + dataLen > n) return;
  if (t.hasCover() && picType != 3) return;
  t.cover.assign(p + i, p + i + dataLen);
  t.coverMime = mime;
}

void parseVorbisComment(const uint8_t* p, size_t n, TagInfo& t) {
  size_t i = 0;
  if (n < 4) return;
  const uint32_t vendorLen = le32(p + i);
  i += 4;
  if (i + vendorLen > n) return;
  i += vendorLen;
  if (i + 4 > n) return;
  const uint32_t count = le32(p + i);
  i += 4;

  for (uint32_t k = 0; k < count && i + 4 <= n; ++k) {
    const uint32_t len = le32(p + i);
    i += 4;
    if (i + len > n) break;
    const std::string entry(reinterpret_cast<const char*>(p + i), len);
    i += len;

    const size_t eq = entry.find('=');
    if (eq == std::string::npos) continue;
    std::string key = entry.substr(0, eq);
    for (char& c : key) c = char(std::toupper(static_cast<unsigned char>(c)));
    const std::string value = trimTagString(entry.substr(eq + 1));

    if (key == "TITLE") {
      if (t.title.empty()) t.title = value;
    } else if (key == "ARTIST") {
      if (t.artist.empty()) t.artist = value;
    } else if (key == "ALBUM") {
      if (t.album.empty()) t.album = value;
    } else if (key == "ALBUMARTIST") {
      if (t.albumArtist.empty()) t.albumArtist = value;
    } else if (key == "GENRE") {
      if (t.genre.empty()) t.genre = value;
    } else if (key == "DATE" || key == "YEAR") {
      if (!t.year) t.year = parseYear(value);
    } else if (key == "TRACKNUMBER") {
      if (!t.trackNo) t.trackNo = parseLeadingInt(value);
    } else if (key == "DISCNUMBER") {
      if (!t.discNo) t.discNo = parseLeadingInt(value);
    } else if (key == "METADATA_BLOCK_PICTURE" && !t.hasCover()) {
      const std::vector<uint8_t> box = base64Decode(value);
      if (!box.empty()) parseFlacPicture(box.data(), box.size(), t);
    }
  }
}

void parseFlac(FILE* fp, TagInfo& t) {
  uint8_t hdr[4];
  if (!readAt(fp, 0, hdr, 4)) return;
  if (std::memcmp(hdr, "fLaC", 4) != 0) return;

  long off = 4;
  for (int guard = 0; guard < 128; ++guard) {
    uint8_t bh[4];
    if (!readAt(fp, off, bh, 4)) return;
    const bool last = (bh[0] & 0x80) != 0;
    const uint8_t type = bh[0] & 0x7F;
    const uint32_t len = be24(bh + 1);
    off += 4;
    if (len > kMaxTagBlock) return;

    if (type == 4 || type == 6) {
      std::vector<uint8_t> block(len);
      if (len && !readAt(fp, off, block.data(), len)) return;
      if (type == 4) {
        parseVorbisComment(block.data(), len, t);
      } else {
        parseFlacPicture(block.data(), len, t);
      }
    }
    off += long(len);
    if (last) break;
  }
}

// --- WAV ------------------------------------------------------------------

void parseWav(FILE* fp, TagInfo& t) {
  uint8_t riff[12];
  if (!readAt(fp, 0, riff, 12)) return;
  if (std::memcmp(riff, "RIFF", 4) != 0) return;

  long off = 12;
  uint8_t ch[8];
  for (int guard = 0; guard < 4096; ++guard) {
    if (!readAt(fp, off, ch, 8)) return;
    const uint32_t size = le32(ch + 4);
    if (std::memcmp(ch, "LIST", 4) == 0 && size >= 4 && size <= kMaxTagBlock) {
      std::vector<uint8_t> list(size);
      if (!readAt(fp, off + 8, list.data(), size)) return;
      if (std::memcmp(list.data(), "INFO", 4) == 0) {
        size_t i = 4;
        while (i + 8 <= list.size()) {
          const std::string id(reinterpret_cast<const char*>(list.data() + i), 4);
          const uint32_t len = le32(list.data() + i + 4);
          i += 8;
          if (i + len > list.size()) break;
          const std::string value =
              trimTagString(std::string(reinterpret_cast<const char*>(
                                             list.data() + i),
                                         len));
          i += len + (len & 1);
          if (id == "INAM") {
            if (t.title.empty()) t.title = value;
          } else if (id == "IART") {
            if (t.artist.empty()) t.artist = value;
          } else if (id == "IPRD") {
            if (t.album.empty()) t.album = value;
          } else if (id == "IGNR") {
            if (t.genre.empty()) t.genre = value;
          } else if (id == "ICRD") {
            if (!t.year) t.year = parseYear(value);
          } else if (id == "ITRK" || id == "IPRT") {
            if (!t.trackNo) t.trackNo = parseLeadingInt(value);
          }
        }
      }
    }
    off += 8 + long(size) + long(size & 1);
    if (off <= 12) break;
  }
}

// --- OGG Vorbis -----------------------------------------------------------
//
// The identification header is followed by the comment header. We walk the
// first two Ogg pages, which is enough for real-world files.
void parseOgg(FILE* fp, TagInfo& t) {
  uint8_t page[27];
  if (!readAt(fp, 0, page, 27)) return;
  if (std::memcmp(page, "OggS", 4) != 0) return;

  const uint8_t segCount = page[26];
  std::vector<uint8_t> lacing(segCount);
  if (segCount && !readAt(fp, 27, lacing.data(), segCount)) return;
  size_t idLen = 0;
  for (uint8_t v : lacing) idLen += v;
  if (idLen < 30) return;

  std::vector<uint8_t> id(idLen);
  if (!readAt(fp, 27 + long(segCount), id.data(), idLen)) return;
  if (id.size() < 7 || id[0] != 1 || std::memcmp(&id[1], "vorbis", 6) != 0) return;

  // Next page: the comment header.
  const long nextPage = 27 + long(segCount) + long(idLen);
  uint8_t cpage[27];
  if (!readAt(fp, nextPage, cpage, 27)) return;
  if (std::memcmp(cpage, "OggS", 4) != 0) return;

  const uint8_t cSegCount = cpage[26];
  std::vector<uint8_t> cLacing(cSegCount);
  if (cSegCount && !readAt(fp, nextPage + 27, cLacing.data(), cSegCount)) return;

  size_t commentLen = 0;
  for (uint8_t v : cLacing) commentLen += v;
  if (commentLen < 15 || commentLen > kMaxTagBlock) return;

  std::vector<uint8_t> comment(commentLen);
  if (!readAt(fp, nextPage + 27 + long(cSegCount), comment.data(), commentLen)) {
    return;
  }
  // "\x03vorbis" then a Vorbis comment block.
  if (comment[0] != 3 || std::memcmp(&comment[1], "vorbis", 6) != 0) return;
  parseVorbisComment(comment.data() + 7, commentLen - 7, t);

  // Cover art lives in a METADATA_BLOCK_PICTURE comment, which is base64.
  // parseVorbisComment already handles it.
}

std::string extensionOf(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return {};
  std::string ext = path.substr(dot + 1);
  for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
  return ext;
}

}  // namespace

std::string trimTagString(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e) {
    const unsigned char c = static_cast<unsigned char>(s[b]);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0) ++b;
    else break;
  }
  while (e > b) {
    const unsigned char c = static_cast<unsigned char>(s[e - 1]);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0) --e;
    else break;
  }
  return s.substr(b, e - b);
}

std::string utf16ToUtf8(const uint8_t* data, size_t bytes, bool bigEndian) {
  std::string out;
  out.reserve(bytes);
  size_t i = 0;
  while (i + 1 < bytes) {
    uint32_t u = bigEndian ? (uint32_t(data[i]) << 8) | data[i + 1]
                           : (uint32_t(data[i + 1]) << 8) | data[i];
    i += 2;
    if (u == 0) break;
    if (u >= 0xD800 && u <= 0xDBFF) {
      if (i + 1 < bytes) {
        const uint32_t lo = bigEndian ? (uint32_t(data[i]) << 8) | data[i + 1]
                                      : (uint32_t(data[i + 1]) << 8) | data[i];
        if (lo >= 0xDC00 && lo <= 0xDFFF) {
          i += 2;
          u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
        }
      }
    }
    appendUtf8(out, u);
  }
  return out;
}

bool readTags(const std::string& path, TagInfo& out) {
  out.clear();
  FILE* fp = std::fopen(path.c_str(), "rb");
  if (!fp) return false;

  uint8_t magic[12] = {0};
  const size_t got = std::fread(magic, 1, sizeof(magic), fp);

  if (got >= 10 && std::memcmp(magic, "ID3", 3) == 0) {
    parseId3v2(fp, out);
  } else if (got >= 4 && std::memcmp(magic, "fLaC", 4) == 0) {
    parseFlac(fp, out);
  } else if (got >= 12 && std::memcmp(magic, "RIFF", 4) == 0) {
    parseWav(fp, out);
  } else if (got >= 4 && std::memcmp(magic, "OggS", 4) == 0) {
    parseOgg(fp, out);
  }

  // MP3s frequently carry only an ID3v1 block; also covers tag-less files.
  const std::string ext = extensionOf(path);
  if (ext == "mp3" && !out.hasCover()) {
    parseId3v1(fp, out);
  }

  std::fclose(fp);
  return true;
}

}  // namespace spn