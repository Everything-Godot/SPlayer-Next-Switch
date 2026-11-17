// SPDX-License-Identifier: AGPL-3.0
// Multi-format lyric parsing for the Switch port.
//
// The desktop build delegates this to the `lyric-kit` npm package, which is not
// vendorable here (npm + AGPL JS runtime). The eight formats it accepts are
// therefore re-implemented against the same grammars the desktop app feeds it,
// and the same priority order (shared/types/lyrics.ts:
// DEFAULT_LYRIC_FORMAT_ORDER = ttml, lys, qrc, krc, yrc, lrc, ass, srt).
//
// Grammar summary (all line-timed unless noted):
//   LRC  [mm:ss.xxx]text              + <mm:ss.xxx>word (enhanced) / inline tags
//   LYS  [prop]word(startMs,durMs)…    word-timed, prop = duet/bg flags
//   QRC  [startMs,durMs]word(startMs,durMs)… plus XML/CDATA wrapping
//   KRC  [startMs,durMs]<offMs,durMs,0>word… (offsets relative to the line)
//   YRC  [startMs,durMs](startMs,durMs,0)word… (offsets absolute)
//   TTML <p begin end><span begin end>word</span>… plus <head> metadata
//   ASS  [Events] Dialogue: …,,,{\kNN}word… karaoke centiseconds
//   SRT  HH:MM:SS,mmm --> HH:MM:SS,mmm blocks
#include "spn/lyrics.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace spn {

namespace {

// ---------------------------------------------------------------------------
// Small string helpers
// ---------------------------------------------------------------------------

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::string lowercase(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  }
  return s;
}

bool startsWith(const std::string& s, const char* prefix) {
  const size_t n = std::strlen(prefix);
  return s.size() >= n && std::memcmp(s.data(), prefix, n) == 0;
}

void trimInPlace(std::string& s) {
  size_t b = 0;
  while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' ||
                          s[b] == '\n' || s[b] == '\v' || s[b] == '\f')) {
    ++b;
  }
  size_t e = s.size();
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' ||
                   s[e - 1] == '\n' || s[e - 1] == '\v' || s[e - 1] == '\f')) {
    --e;
  }
  s = s.substr(b, e - b);
}

bool isSpaceChar(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' ||
         c == '\f';
}

std::string normalizeSpaces(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool pendingSpace = false;
  for (char c : s) {
    if (isSpaceChar(c)) {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) {
      out += ' ';
      pendingSpace = false;
    }
    out += c;
  }
  return out;
}

// Collapses runs of whitespace to a single space but *keeps* leading/trailing
// spaces, because word-level lyric payloads rely on them ("Hel" + "lo" +
// " world" must join back into "Hello world").
std::string collapseSpaces(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool lastWasSpace = false;
  for (char c : s) {
    if (isSpaceChar(c)) {
      if (!lastWasSpace) {
        out += ' ';
        lastWasSpace = true;
      }
      continue;
    }
    out += c;
    lastWasSpace = false;
  }
  return out;
}

std::vector<std::string> splitLines(const std::string& content) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (true) {
    const size_t nl = content.find('\n', pos);
    const size_t end = (nl == std::string::npos) ? content.size() : nl;
    std::string line = content.substr(pos, end - pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(std::move(line));
    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
  return out;
}

void appendUtf8(std::string& out, unsigned cp) {
  if (cp < 0x80) {
    out += char(cp);
  } else if (cp < 0x800) {
    out += char(0xC0 | (cp >> 6));
    out += char(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += char(0xE0 | (cp >> 12));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  } else if (cp < 0x110000) {
    out += char(0xF0 | (cp >> 18));
    out += char(0x80 | ((cp >> 12) & 0x3F));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  }
}

std::string decodeEntities(const std::string& in) {
  if (in.find('&') == std::string::npos) return in;
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] != '&') {
      out += in[i];
      continue;
    }
    const size_t semi = in.find(';', i + 1);
    if (semi == std::string::npos || semi - i > 12) {
      out += in[i];
      continue;
    }
    const std::string ent = in.substr(i + 1, semi - i - 1);
    bool consumed = true;
    if (ent == "amp" || ent == "AMP") {
      out += '&';
    } else if (ent == "lt" || ent == "LT") {
      out += '<';
    } else if (ent == "gt" || ent == "GT") {
      out += '>';
    } else if (ent == "quot" || ent == "QUOT") {
      out += '"';
    } else if (ent == "apos") {
      out += '\'';
    } else if (ent == "nbsp") {
      out += ' ';
    } else if (ent.size() > 1 && ent[0] == '#') {
      unsigned cp = 0;
      bool ok = true;
      if (ent.size() > 2 && (ent[1] == 'x' || ent[1] == 'X')) {
        for (size_t k = 2; k < ent.size(); ++k) {
          const char c = ent[k];
          if (isDigit(c)) {
            cp = cp * 16 + unsigned(c - '0');
          } else if (c >= 'a' && c <= 'f') {
            cp = cp * 16 + unsigned(c - 'a' + 10);
          } else if (c >= 'A' && c <= 'F') {
            cp = cp * 16 + unsigned(c - 'A' + 10);
          } else {
            ok = false;
            break;
          }
        }
      } else {
        for (size_t k = 1; k < ent.size(); ++k) {
          if (!isDigit(ent[k])) {
            ok = false;
            break;
          }
          cp = cp * 10 + unsigned(ent[k] - '0');
        }
      }
      if (ok && cp > 0 && cp < 0x110000) {
        appendUtf8(out, cp);
      } else {
        consumed = false;
      }
    } else {
      consumed = false;
    }
    if (consumed) {
      i = semi;
    } else {
      out += in[i];
    }
  }
  return out;
}

std::string stripTags(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '<') {
      const size_t close = s.find('>', i);
      if (close == std::string::npos) break;
      const std::string tag = lowercase(s.substr(i + 1, close - i - 1));
      if (tag == "br" || tag == "br/" || startsWith(tag, "br ")) out += ' ';
      i = close + 1;
      continue;
    }
    out += s[i];
    ++i;
  }
  return decodeEntities(out);
}

uint32_t shiftMs(uint32_t value, int64_t delta) {
  const int64_t shifted = int64_t(value) + delta;
  return shifted > 0 ? uint32_t(shifted) : 0u;
}

uint32_t clampToMs(long long value) {
  if (value <= 0) return 0u;
  if (value > 0xFFFFFFFLL) return uint32_t(0xFFFFFFF);
  return uint32_t(value);
}

int scanNumbers(const std::string& s, size_t begin, size_t end, long* out,
                int maxOut) {
  int n = 0;
  size_t k = begin;
  while (k < end && n < maxOut) {
    if (isDigit(s[k])) {
      long long v = 0;
      while (k < end && isDigit(s[k])) {
        v = v * 10 + (s[k] - '0');
        if (v > 0xFFFFFFF) v = 0xFFFFFFF;
        ++k;
      }
      out[n++] = long(v);
    } else {
      ++k;
    }
  }
  return n;
}

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------

uint32_t lrcTime(const std::string& mm, const std::string& ss,
                 const std::string& frac) {
  const long long minutes = std::atoll(mm.c_str());
  const long long seconds = std::atoll(ss.c_str());
  long long millis = 0;
  if (!frac.empty()) {
    millis = std::atoll(frac.c_str());
    if (frac.size() == 1) millis *= 100;
    else if (frac.size() == 2) millis *= 10;
  }
  return clampToMs(minutes * 60000 + seconds * 1000 + millis);
}

bool parseBracketTimeTag(const std::string& s, size_t i, size_t* after,
                         uint32_t* ms) {
  const size_t n = s.size();
  if (i >= n || s[i] != '[') return false;
  size_t k = i + 1;
  const size_t m0 = k;
  while (k < n && isDigit(s[k])) ++k;
  if (k == m0 || k >= n || s[k] != ':') return false;
  const std::string mm = s.substr(m0, k - m0);
  ++k;
  const size_t s0 = k;
  while (k < n && isDigit(s[k])) ++k;
  if (k == s0) return false;
  const std::string ss = s.substr(s0, k - s0);
  std::string frac;
  if (k < n && (s[k] == '.' || s[k] == ':')) {
    ++k;
    const size_t f0 = k;
    while (k < n && isDigit(s[k]) && k - f0 < 3) ++k;
    if (k == f0) return false;
    frac = s.substr(f0, k - f0);
  }
  if (k >= n || s[k] != ']') return false;
  *after = k + 1;
  *ms = lrcTime(mm, ss, frac);
  return true;
}

bool parseAngleTimeTag(const std::string& s, size_t i, size_t* after,
                       uint32_t* ms) {
  const size_t n = s.size();
  if (i >= n || s[i] != '<') return false;
  size_t k = i + 1;
  const size_t m0 = k;
  while (k < n && isDigit(s[k])) ++k;
  if (k == m0 || k >= n || s[k] != ':') return false;
  const std::string mm = s.substr(m0, k - m0);
  ++k;
  const size_t s0 = k;
  while (k < n && isDigit(s[k])) ++k;
  if (k == s0) return false;
  const std::string ss = s.substr(s0, k - s0);
  std::string frac;
  if (k < n && (s[k] == '.' || s[k] == ':')) {
    ++k;
    const size_t f0 = k;
    while (k < n && isDigit(s[k]) && k - f0 < 3) ++k;
    if (k == f0) return false;
    frac = s.substr(f0, k - f0);
  }
  if (k >= n || s[k] != '>') return false;
  *after = k + 1;
  *ms = lrcTime(mm, ss, frac);
  return true;
}

uint32_t parseTtmlTime(const std::string& value) {
  std::string text;
  text.reserve(value.size());
  for (char c : value) {
    if (!isSpaceChar(c)) text += c;
  }
  if (text.empty()) return 0;
  if (!text.empty() && !isDigit(text.back()) && text.back() != '.') {
    const size_t numEnd = text.find_last_of("0123456789.");
    if (numEnd != std::string::npos) {
      const std::string num = text.substr(0, numEnd + 1);
      const std::string unit = lowercase(text.substr(numEnd + 1));
      const double v = std::atof(num.c_str());
      double scale = 0.0;
      if (unit == "h") scale = 3600000.0;
      else if (unit == "m") scale = 60000.0;
      else if (unit == "s") scale = 1000.0;
      else if (unit == "ms") scale = 1.0;
      if (scale > 0.0) return clampToMs((long long)std::llround(v * scale));
    }
  }
  std::vector<double> parts;
  size_t pos = 0;
  while (true) {
    const size_t colon = text.find(':', pos);
    const std::string piece =
        (colon == std::string::npos) ? text.substr(pos)
                                     : text.substr(pos, colon - pos);
    if (!piece.empty()) parts.push_back(std::atof(piece.c_str()));
    if (colon == std::string::npos) break;
    pos = colon + 1;
  }
  if (parts.empty()) return clampToMs((long long)std::llround(std::atof(text.c_str()) * 1000.0));
  double ms = 0.0;
  if (parts.size() == 1) {
    ms = parts[0] * 1000.0;
  } else if (parts.size() == 2) {
    ms = (parts[0] * 60.0 + parts[1]) * 1000.0;
  } else if (parts.size() == 3) {
    ms = ((parts[0] * 60.0 + parts[1]) * 60.0 + parts[2]) * 1000.0;
  } else {
    ms = ((parts[0] * 60.0 + parts[1]) * 60.0 + parts[2]) * 1000.0 +
         parts[3] / 30.0 * 1000.0;
  }
  return clampToMs((long long)std::llround(ms));
}

uint32_t parseAssTime(const std::string& value) {
  std::vector<std::string> parts;
  size_t pos = 0;
  while (true) {
    const size_t colon = value.find(':', pos);
    if (colon == std::string::npos) {
      parts.push_back(value.substr(pos));
      break;
    }
    parts.push_back(value.substr(pos, colon - pos));
    pos = colon + 1;
  }
  if (parts.size() < 3) return 0;
  long long ms = 0;
  ms += std::atoll(parts[0].c_str()) * 3600000LL;
  ms += std::atoll(parts[1].c_str()) * 60000LL;
  const std::string secPart = parts[2];
  const size_t dot = secPart.find('.');
  if (dot == std::string::npos) {
    ms += std::atoll(secPart.c_str()) * 1000LL;
  } else {
    ms += std::atoll(secPart.substr(0, dot).c_str()) * 1000LL;
    std::string frac = secPart.substr(dot + 1);
    if (frac.size() > 3) frac = frac.substr(0, 3);
    long long f = std::atoll(frac.c_str());
    if (frac.size() == 1) f *= 100;
    else if (frac.size() == 2) f *= 10;
    ms += f;
  }
  return clampToMs(ms);
}

bool parseSrtTime(const std::string& value, uint32_t* out) {
  std::string text;
  text.reserve(value.size());
  for (char c : value) {
    if (!isSpaceChar(c)) text += c;
  }
  if (text.empty()) return false;
  std::vector<std::string> parts;
  size_t pos = 0;
  while (true) {
    const size_t colon = text.find(':', pos);
    if (colon == std::string::npos) {
      parts.push_back(text.substr(pos));
      break;
    }
    parts.push_back(text.substr(pos, colon - pos));
    pos = colon + 1;
  }
  if (parts.size() < 2) return false;
  for (const std::string& p : parts) {
    if (p.empty()) return false;
  }
  const std::string last = parts.back();
  const size_t sep = last.find_first_of(",.");
  const std::string secStr = (sep == std::string::npos) ? last : last.substr(0, sep);
  long long total = 0;
  if (parts.size() >= 3) {
    total += std::atoll(parts[0].c_str()) * 3600000LL;
    total += std::atoll(parts[1].c_str()) * 60000LL;
  } else {
    total += std::atoll(parts[0].c_str()) * 60000LL;
  }
  total += std::atoll(secStr.c_str()) * 1000LL;
  if (sep != std::string::npos) {
    std::string frac = last.substr(sep + 1);
    if (frac.size() > 3) frac = frac.substr(0, 3);
    for (char c : frac) {
      if (!isDigit(c)) {
        frac.clear();
        break;
      }
    }
    long long f = std::atoll(frac.c_str());
    if (frac.size() == 1) f *= 100;
    else if (frac.size() == 2) f *= 10;
    total += f;
  }
  *out = clampToMs(total);
  return true;
}

// ---------------------------------------------------------------------------
// Shared line assembly
// ---------------------------------------------------------------------------

std::string joinWords(const std::vector<LyricWord>& words) {
  std::string out;
  for (const LyricWord& w : words) out += w.text;
  return collapseSpaces(out);
}

void pushWord(std::vector<LyricWord>& words, const std::string& raw,
              uint32_t startMs, uint32_t endMs) {
  if (raw.empty()) return;
  if (endMs > 0 && endMs < startMs) std::swap(startMs, endMs);
  LyricWord w;
  w.startMs = startMs;
  w.endMs = endMs;
  w.text = raw;
  words.push_back(std::move(w));
}

struct ParsedLyric {
  std::vector<LyricLine> lines;
  std::string title;
  std::string artist;
  std::string album;
  std::string offsetTag;
  int offsetMs = 0;
  bool wordTimed = false;
};

// ---------------------------------------------------------------------------
// LRC
// ---------------------------------------------------------------------------

bool parseAngleWords(const std::string& content, std::vector<LyricWord>* words) {
  if (content.find('<') == std::string::npos) return false;
  struct Tag {
    uint32_t ms;
    size_t tagStart;
    size_t tagEnd;
  };
  std::vector<Tag> tags;
  size_t i = 0;
  while (i < content.size()) {
    const size_t lt = content.find('<', i);
    if (lt == std::string::npos) break;
    size_t after = 0;
    uint32_t ms = 0;
    if (parseAngleTimeTag(content, lt, &after, &ms)) {
      tags.push_back(Tag{ms, lt, after});
      i = after;
    } else {
      i = lt + 1;
    }
  }
  if (tags.empty()) return false;
  words->clear();
  for (size_t k = 0; k < tags.size(); ++k) {
    const size_t textStart = tags[k].tagEnd;
    const size_t textEnd =
        (k + 1 < tags.size()) ? tags[k + 1].tagStart : content.size();
    if (textEnd <= textStart) continue;
    pushWord(*words,
             collapseSpaces(content.substr(textStart, textEnd - textStart)),
             tags[k].ms, (k + 1 < tags.size()) ? tags[k + 1].ms : 0u);
  }
  if (words->empty()) return false;
  for (size_t k = 0; k + 1 < words->size(); ++k) {
    if ((*words)[k].endMs <= (*words)[k].startMs) {
      (*words)[k].endMs = std::max((*words)[k].startMs + 1,
                                   (*words)[k + 1].startMs);
    }
  }
  return true;
}

bool parseInlineBracketWords(const std::string& content, uint32_t initialStart,
                             std::vector<LyricWord>* words) {
  struct Tag {
    uint32_t ms;
    size_t tagStart;
    size_t tagEnd;
  };
  std::vector<Tag> tags;
  size_t i = 0;
  while (i < content.size()) {
    const size_t lb = content.find('[', i);
    if (lb == std::string::npos) break;
    size_t after = 0;
    uint32_t ms = 0;
    if (parseBracketTimeTag(content, lb, &after, &ms)) {
      tags.push_back(Tag{ms, lb, after});
      i = after;
    } else {
      i = lb + 1;
    }
  }
  if (tags.empty()) return false;
  words->clear();
  uint32_t prevMs = initialStart;
  size_t prevText = 0;
  for (size_t k = 0; k < tags.size(); ++k) {
    const std::string raw = content.substr(prevText, tags[k].tagStart - prevText);
    pushWord(*words, collapseSpaces(raw), prevMs, tags[k].ms);
    prevMs = tags[k].ms;
    prevText = tags[k].tagEnd;
  }
  if (prevText < content.size()) {
    pushWord(*words, collapseSpaces(content.substr(prevText)), prevMs, 0u);
  }
  return !words->empty();
}

void parseLrcPayload(const std::string& line, std::vector<LyricLine>* out) {
  std::vector<uint32_t> times;
  size_t cursor = 0;
  while (true) {
    size_t after = 0;
    uint32_t ms = 0;
    if (!parseBracketTimeTag(line, cursor, &after, &ms)) break;
    times.push_back(ms);
    cursor = after;
  }
  if (times.empty()) return;

  std::string content = line.substr(cursor);
  trimInPlace(content);
  if (content.empty()) return;

  std::vector<LyricWord> words;
  if (parseAngleWords(content, &words) ||
      parseInlineBracketWords(content, times.front(), &words)) {
    const std::vector<LyricWord> base = words;
    for (uint32_t t : times) {
      const int64_t delta = int64_t(t) - int64_t(times.front());
      LyricLine lyric;
      lyric.timeMs = t;
      lyric.words = base;
      if (delta != 0) {
        for (LyricWord& w : lyric.words) {
          w.startMs = shiftMs(w.startMs, delta);
          w.endMs = shiftMs(w.endMs, delta);
        }
      }
      lyric.text = joinWords(lyric.words);
      if (lyric.text.empty()) continue;
      lyric.endMs = lyric.words.empty() ? 0 : lyric.words.back().endMs;
      out->push_back(std::move(lyric));
    }
    return;
  }

  for (uint32_t t : times) {
    LyricLine lyric;
    lyric.timeMs = t;
    lyric.text = content;
    out->push_back(std::move(lyric));
  }
}

bool parseLrc(const std::string& text, ParsedLyric* out) {
  for (std::string raw : splitLines(text)) {
    trimInPlace(raw);
    if (raw.empty()) continue;

    if (raw[0] == '[') {
      const size_t close = raw.find(']');
      if (close != std::string::npos) {
        const std::string tag = raw.substr(1, close - 1);
        const size_t colon = tag.find(':');
        if (colon > 0) {
          bool keyOk = true;
          for (size_t i = 0; i < colon; ++i) {
            if (!isAlpha(tag[i])) keyOk = false;
          }
          if (keyOk) {
            const std::string key = lowercase(tag.substr(0, colon));
            const std::string val = tag.substr(colon + 1);
            if (key == "ti") out->title = normalizeSpaces(decodeEntities(val));
            else if (key == "ar") out->artist = normalizeSpaces(decodeEntities(val));
            else if (key == "al") out->album = normalizeSpaces(decodeEntities(val));
            else if (key == "by") out->offsetTag = normalizeSpaces(val);
            else if (key == "offset") out->offsetMs = int(std::atol(val.c_str()));
            else if (key == "bg") parseLrcPayload(raw.substr(close + 1), &out->lines);
          }
        }
      }
    }
    if (raw[0] == '{') continue;
    parseLrcPayload(raw, &out->lines);
  }
  return !out->lines.empty();
}

// ---------------------------------------------------------------------------
// LYS (Lyricify Syllable): [prop]word(startMs,durMs)…
// ---------------------------------------------------------------------------

void parseTrailingTimedWords(const std::string& rest,
                             std::vector<LyricWord>* words) {
  size_t i = 0;
  size_t textStart = 0;
  const size_t n = rest.size();
  while (i < n) {
    const size_t lp = rest.find('(', i);
    if (lp == std::string::npos) break;
    const size_t rp = rest.find(')', lp);
    if (rp == std::string::npos) break;
    long vals[3] = {0, 0, 0};
    const int count = scanNumbers(rest, lp + 1, rp, vals, 3);
    if (count < 2) {
      i = lp + 1;
      continue;
    }
    const std::string raw = rest.substr(textStart, lp - textStart);
    textStart = rp + 1;
    i = textStart;
    if (raw.empty() && vals[1] == 0) continue;
    const uint32_t start = clampToMs(vals[0]);
    const uint32_t dur = clampToMs(vals[1]);
    pushWord(*words, raw, start, start + dur);
  }
}

bool parseLys(const std::string& text, ParsedLyric* out) {
  for (std::string raw : splitLines(text)) {
    trimInPlace(raw);
    if (raw.empty()) continue;
    if (raw[0] == '[') {
      const size_t close = raw.find(']');
      if (close != std::string::npos) {
        const std::string tag = raw.substr(1, close - 1);
        if (!tag.empty() && isDigit(tag[0])) {
          std::vector<LyricWord> words;
          parseTrailingTimedWords(raw.substr(close + 1), &words);
          if (!words.empty()) {
            LyricLine lyric;
            lyric.words = words;
            lyric.timeMs = words.front().startMs;
            lyric.endMs = words.back().endMs;
            lyric.text = joinWords(words);
            if (!lyric.text.empty()) out->lines.push_back(std::move(lyric));
          }
          continue;
        }
        const size_t colon = tag.find(':');
        if (colon > 0) {
          const std::string key = lowercase(tag.substr(0, colon));
          const std::string val = tag.substr(colon + 1);
          if (key == "ti") out->title = normalizeSpaces(val);
          else if (key == "ar") out->artist = normalizeSpaces(val);
          else if (key == "al") out->album = normalizeSpaces(val);
          else if (key == "offset") out->offsetMs = int(std::atol(val.c_str()));
          continue;
        }
      }
    }
    std::vector<LyricWord> words;
    parseTrailingTimedWords(raw, &words);
    if (words.empty()) continue;
    LyricLine lyric;
    lyric.words = words;
    lyric.timeMs = words.front().startMs;
    lyric.endMs = words.back().endMs;
    lyric.text = joinWords(words);
    if (!lyric.text.empty()) out->lines.push_back(std::move(lyric));
  }
  return !out->lines.empty();
}

// ---------------------------------------------------------------------------
// QRC (QQ Music): [startMs,durMs]word(startMs,durMs)…
// ---------------------------------------------------------------------------

bool extractQrcBody(const std::string& text, std::string* body,
                    ParsedLyric* out) {
  std::string trimmed = text;
  trimInPlace(trimmed);
  if (!trimmed.empty() && trimmed[0] != '<') {
    *body = text;
    return true;
  }
  if (trimmed.empty()) return false;

  const std::string low = lowercase(text);
  auto grabAttr = [&](const char* name, std::string* dest) {
    const std::string key = lowercase(std::string(name) + "=\"");
    const size_t at = low.find(key);
    if (at == std::string::npos) return;
    const size_t valueStart = at + key.size();
    const size_t valueEnd = text.find('"', valueStart);
    if (valueEnd == std::string::npos) return;
    *dest = normalizeSpaces(decodeEntities(text.substr(valueStart, valueEnd - valueStart)));
  };
  grabAttr("Title", &out->title);
  grabAttr("musicName", &out->title);
  grabAttr("Singer", &out->artist);
  grabAttr("Artist", &out->artist);
  grabAttr("Album", &out->album);

  const size_t cdata = low.find("<![cdata[");
  if (cdata != std::string::npos) {
    const size_t dataStart = cdata + 9;
    const size_t dataEnd = low.find("]]>", dataStart);
    if (dataEnd != std::string::npos) {
      *body = decodeEntities(text.substr(dataStart, dataEnd - dataStart));
      return true;
    }
  }
  const size_t lyricContent = low.find("lyriccontent=\"");
  if (lyricContent != std::string::npos) {
    const size_t dataStart = lyricContent + 14;
    const size_t dataEnd = text.find('"', dataStart);
    if (dataEnd != std::string::npos) {
      *body = decodeEntities(text.substr(dataStart, dataEnd - dataStart));
      return true;
    }
  }
  return false;
}

bool parseQrc(const std::string& text, ParsedLyric* out) {
  std::string body;
  if (!extractQrcBody(text, &body, out)) return false;

  for (std::string raw : splitLines(body)) {
    trimInPlace(raw);
    if (raw.empty()) continue;
    if (raw[0] == '[') {
      const size_t close = raw.find(']');
      if (close == std::string::npos) continue;
      long vals[2] = {0, 0};
      const int count = scanNumbers(raw, 1, close, vals, 2);
      if (count < 2) {
        const std::string tag = raw.substr(1, close - 1);
        const size_t colon = tag.find(':');
        if (colon > 0) {
          const std::string key = lowercase(tag.substr(0, colon));
          if (key == "offset") out->offsetMs = int(std::atol(tag.c_str() + colon + 1));
        }
        continue;
      }
      const uint32_t lineStart = clampToMs(vals[0]);
      const uint32_t lineDur = clampToMs(vals[1]);
      std::vector<LyricWord> words;
      parseTrailingTimedWords(raw.substr(close + 1), &words);
      if (words.empty()) continue;
      LyricLine lyric;
      lyric.words = words;
      lyric.timeMs = lineStart;
      lyric.endMs = lineStart + lineDur;
      lyric.text = joinWords(words);
      if (lyric.text.empty()) continue;
      out->lines.push_back(std::move(lyric));
    }
  }
  return !out->lines.empty();
}

// ---------------------------------------------------------------------------
// KRC (Kugou): [startMs,durMs]<offsetMs,durMs,0>word…
// ---------------------------------------------------------------------------

bool parseKrc(const std::string& text, ParsedLyric* out) {
  if (startsWith(text, "krc1")) return false;

  for (std::string raw : splitLines(text)) {
    trimInPlace(raw);
    if (raw.empty()) continue;

    uint32_t lineStart = 0;
    uint32_t lineDur = 0;
    size_t headerLen = 0;

    if (raw[0] == '[') {
      const size_t close = raw.find(']');
      if (close == std::string::npos) continue;
      long vals[2] = {0, 0};
      const int count = scanNumbers(raw, 1, close, vals, 2);
      const bool hasColon = raw.find(':', 1) < close;
      if (count >= 2 && !hasColon) {
        lineStart = clampToMs(vals[0]);
        lineDur = clampToMs(vals[1]);
        headerLen = close + 1;
      } else {
        const std::string tag = raw.substr(1, close - 1);
        const size_t colon = tag.find(':');
        if (colon > 0) {
          const std::string key = lowercase(tag.substr(0, colon));
          if (key == "offset") out->offsetMs = int(std::atol(tag.c_str() + colon + 1));
        }
        continue;
      }
    }
    if (headerLen == 0) continue;

    std::vector<LyricWord> words;
    const std::string rest = raw.substr(headerLen);
    size_t i = 0;
    while (i < rest.size()) {
      const size_t lt = rest.find('<', i);
      if (lt == std::string::npos) break;
      const size_t gt = rest.find('>', lt);
      if (gt == std::string::npos) break;
      long vals[3] = {0, 0, 0};
      const int count = scanNumbers(rest, lt + 1, gt, vals, 3);
      if (count < 2) {
        i = gt + 1;
        continue;
      }
      const size_t nextTag = rest.find('<', gt + 1);
      const size_t textEnd = (nextTag == std::string::npos) ? rest.size() : nextTag;
      const std::string body = rest.substr(gt + 1, textEnd - gt - 1);
      i = textEnd;
      if (body.empty()) continue;
      const uint32_t start = uint32_t(int64_t(lineStart) + vals[0]);
      pushWord(words, body, start, start + clampToMs(vals[1]));
    }
    if (words.empty()) continue;

    LyricLine lyric;
    lyric.words = words;
    lyric.timeMs = lineStart;
    lyric.endMs = lineDur > 0 ? lineStart + lineDur : words.back().endMs;
    lyric.text = joinWords(words);
    if (lyric.text.empty()) continue;
    out->lines.push_back(std::move(lyric));
  }
  return !out->lines.empty();
}

// ---------------------------------------------------------------------------
// YRC (NetEase): [startMs,durMs](startMs,durMs,0)word…
// ---------------------------------------------------------------------------

bool parseYrc(const std::string& text, ParsedLyric* out) {
  for (std::string raw : splitLines(text)) {
    trimInPlace(raw);
    if (raw.empty()) continue;
    if (raw[0] != '[') continue;
    const size_t close = raw.find(']');
    if (close == std::string::npos) continue;

    long header[2] = {0, 0};
    const int count = scanNumbers(raw, 1, close, header, 2);
    if (count < 2) {
      const std::string tag = raw.substr(1, close - 1);
      const size_t colon = tag.find(':');
      if (colon > 0) {
        const std::string key = lowercase(tag.substr(0, colon));
        if (key == "offset") out->offsetMs = int(std::atol(tag.c_str() + colon + 1));
        else if (key == "ti") out->title = normalizeSpaces(tag.substr(colon + 1));
        else if (key == "ar") out->artist = normalizeSpaces(tag.substr(colon + 1));
        else if (key == "al") out->album = normalizeSpaces(tag.substr(colon + 1));
      }
      continue;
    }
    const uint32_t lineStart = clampToMs(header[0]);
    const uint32_t lineDur = clampToMs(header[1]);
    const std::string rest = raw.substr(close + 1);
    if (rest.find('(') == std::string::npos) continue;

    std::vector<LyricWord> words;
    size_t i = 0;
    while (i < rest.size()) {
      const size_t lp = rest.find('(', i);
      if (lp == std::string::npos) break;
      const size_t rp = rest.find(')', lp);
      if (rp == std::string::npos) break;
      long vals[3] = {0, 0, 0};
      const int got = scanNumbers(rest, lp + 1, rp, vals, 3);
      if (got < 2) {
        i = lp + 1;
        continue;
      }
      const size_t nextTag = rest.find('(', rp + 1);
      const size_t textEnd = (nextTag == std::string::npos) ? rest.size() : nextTag;
      const std::string body = rest.substr(rp + 1, textEnd - rp - 1);
      i = textEnd;
      if (body.empty()) continue;
      const uint32_t start = clampToMs(vals[0]);
      pushWord(words, body, start, start + clampToMs(vals[1]));
    }
    if (words.empty()) continue;

    LyricLine lyric;
    lyric.words = words;
    lyric.timeMs = lineStart;
    lyric.endMs = lineDur > 0 ? lineStart + lineDur : words.back().endMs;
    lyric.text = joinWords(words);
    if (lyric.text.empty()) continue;
    out->lines.push_back(std::move(lyric));
  }
  return !out->lines.empty();
}

// ---------------------------------------------------------------------------
// TTML (Apple Music / AMLL)
// ---------------------------------------------------------------------------

std::string attrValue(const std::string& tagBody, const char* name) {
  const size_t nameLen = std::strlen(name);
  size_t pos = 0;
  while (pos < tagBody.size()) {
    pos = tagBody.find(name, pos);
    if (pos == std::string::npos) break;
    const bool boundary =
        pos == 0 || !(isAlpha(tagBody[pos - 1]) || isDigit(tagBody[pos - 1]) ||
                      tagBody[pos - 1] == '-');
    size_t k = pos + nameLen;
    if (boundary && k < tagBody.size() && tagBody[k] == '=') {
      ++k;
      while (k < tagBody.size() && isSpaceChar(tagBody[k])) ++k;
      if (k < tagBody.size() && (tagBody[k] == '"' || tagBody[k] == '\'')) {
        const char quote = tagBody[k];
        const size_t valueStart = k + 1;
        const size_t valueEnd = tagBody.find(quote, valueStart);
        if (valueEnd != std::string::npos) {
          return tagBody.substr(valueStart, valueEnd - valueStart);
        }
      }
    }
    pos += nameLen;
  }
  return {};
}

bool roleIsAuxiliary(const std::string& role) {
  const std::string low = lowercase(role);
  return low.find("translation") != std::string::npos ||
         low.find("roman") != std::string::npos ||
         low.find("bg") != std::string::npos;
}

LyricLine buildTtmlLine(const std::string& inner, uint32_t pBegin,
                        uint32_t pEnd) {
  LyricLine lyric;
  const std::string low = lowercase(inner);

  struct Span {
    uint32_t begin;
    uint32_t end;
    size_t tagEnd;
  };
  std::vector<Span> spans;
  size_t pos = 0;
  while (pos < low.size()) {
    const size_t lt = low.find("<span", pos);
    if (lt == std::string::npos) break;
    const size_t gt = low.find('>', lt);
    if (gt == std::string::npos) break;
    const std::string attrs = inner.substr(lt + 5, gt - lt - 5);
    pos = gt + 1;
    const std::string role = attrValue(attrs, "role");
    if (roleIsAuxiliary(role)) continue;
    const std::string beginAttr = attrValue(attrs, "begin");
    if (beginAttr.empty()) continue;
    Span span;
    span.begin = parseTtmlTime(beginAttr);
    const std::string endAttr = attrValue(attrs, "end");
    span.end = endAttr.empty() ? 0u : parseTtmlTime(endAttr);
    span.tagEnd = gt + 1;
    spans.push_back(span);
  }

  for (const Span& span : spans) {
    const size_t textEnd = low.find('<', span.tagEnd);
    const size_t stop = (textEnd == std::string::npos) ? inner.size() : textEnd;
    if (stop <= span.tagEnd) continue;
    const std::string raw =
        collapseSpaces(stripTags(inner.substr(span.tagEnd, stop - span.tagEnd)));
    pushWord(lyric.words, raw, span.begin, span.end);
  }

  lyric.text = lyric.words.empty() ? normalizeSpaces(stripTags(inner))
                                   : joinWords(lyric.words);
  if (lyric.words.empty()) {
    lyric.timeMs = pBegin;
    lyric.endMs = pEnd;
  } else {
    lyric.timeMs = pBegin > 0 ? pBegin : lyric.words.front().startMs;
    lyric.endMs = pEnd > 0 ? pEnd : lyric.words.back().endMs;
  }
  return lyric;
}

bool parseTtml(const std::string& text, ParsedLyric* out) {
  const std::string low = lowercase(text);
  if (low.find("<tt") == std::string::npos && low.find("<p") == std::string::npos) {
    return false;
  }

  // <head>: ttm:title / ttm:agent > ttm:name.
  const size_t headOpen = low.find("<head");
  const size_t headClose = low.find("</head>");
  if (headOpen != std::string::npos && headClose != std::string::npos &&
      headClose > headOpen) {
    const std::string head = text.substr(headOpen, headClose - headOpen);
    const std::string headLow = lowercase(head);
    auto grabTextElement = [&](const char* tagName) -> std::string {
      const std::string open = std::string("<") + tagName;
      const std::string close = std::string("</") + tagName + ">";
      const size_t at = headLow.find(open);
      if (at == std::string::npos) return {};
      const size_t gt = headLow.find('>', at);
      if (gt == std::string::npos) return {};
      const size_t stop = headLow.find(close, gt);
      if (stop == std::string::npos) return {};
      return normalizeSpaces(stripTags(head.substr(gt + 1, stop - gt - 1)));
    };
    out->title = grabTextElement("ttm:title");
    if (out->title.empty()) out->title = grabTextElement("title");
    out->artist = grabTextElement("ttm:name");
  }

  size_t pos = 0;
  while (pos < low.size()) {
    const size_t pOpen = low.find("<p", pos);
    if (pOpen == std::string::npos) break;
    if (pOpen + 2 < low.size() &&
        !(isSpaceChar(low[pOpen + 2]) || low[pOpen + 2] == '>')) {
      pos = pOpen + 2;
      continue;
    }
    const size_t gt = low.find('>', pOpen);
    if (gt == std::string::npos) break;
    const std::string attrs = text.substr(pOpen + 2, gt - pOpen - 2);
    const std::string role = attrValue(attrs, "role");
    const uint32_t begin = parseTtmlTime(attrValue(attrs, "begin"));
    const uint32_t end = parseTtmlTime(attrValue(attrs, "end"));
    const size_t close = low.find("</p>", gt);
    size_t nextPos = low.size();
    size_t innerEnd = low.size();
    if (close != std::string::npos) {
      nextPos = close + 4;
      innerEnd = close;
    }
    const std::string inner = text.substr(gt + 1, innerEnd - gt - 1);
    pos = nextPos;
    if (roleIsAuxiliary(role)) continue;
    LyricLine lyric = buildTtmlLine(inner, begin, end);
    if (lyric.text.empty()) continue;
    out->lines.push_back(std::move(lyric));
  }
  return !out->lines.empty();
}

// ---------------------------------------------------------------------------
// ASS / SSA
// ---------------------------------------------------------------------------

std::vector<std::string> splitCsv(const std::string& s, size_t maxFields) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (true) {
    const size_t comma = s.find(',', pos);
    if (comma == std::string::npos ||
        (maxFields > 0 && out.size() + 1 >= maxFields)) {
      out.push_back(s.substr(pos));
      break;
    }
    out.push_back(s.substr(pos, comma - pos));
    pos = comma + 1;
  }
  for (std::string& field : out) trimInPlace(field);
  return out;
}

// "\k50" / "\kf120" / "\ko30" → centiseconds, else -1.
int karaokeDuration(const std::string& tag) {
  size_t i = 0;
  while (i < tag.size() && isSpaceChar(tag[i])) ++i;
  if (i >= tag.size() || tag[i] != '\\') return -1;
  ++i;
  if (i >= tag.size() || (tag[i] != 'k' && tag[i] != 'K')) return -1;
  ++i;
  if (i < tag.size() &&
      (tag[i] == 'f' || tag[i] == 'F' || tag[i] == 'o' || tag[i] == 'O')) {
    ++i;
  }
  if (i >= tag.size() || !isDigit(tag[i])) return -1;
  int duration = 0;
  while (i < tag.size() && isDigit(tag[i])) {
    duration = duration * 10 + (tag[i] - '0');
    if (duration > 1000000) duration = 1000000;
    ++i;
  }
  return duration;
}

bool parseKaraokeLine(const std::string& text, uint32_t lineStart,
                      std::vector<LyricWord>* words) {
  bool sawKaraoke = false;
  uint32_t cursor = lineStart;
  size_t i = 0;
  while (i < text.size()) {
    const size_t brace = text.find('{', i);
    const std::string body = (brace == std::string::npos)
                                 ? text.substr(i)
                                 : text.substr(i, brace - i);
    if (!body.empty()) {
      if (!words->empty()) {
        (*words)[words->size() - 1].text += body;
      } else {
        LyricWord w;
        w.startMs = cursor;
        w.endMs = 0;
        w.text = body;
        words->push_back(std::move(w));
      }
    }
    if (brace == std::string::npos) break;
    const size_t close = text.find('}', brace);
    if (close == std::string::npos) break;
    const std::string tag = text.substr(brace + 1, close - brace - 1);
    const int cs = karaokeDuration(tag);
    if (cs < 0) {
      i = close + 1;
      continue;
    }
    sawKaraoke = true;
    const uint32_t span = uint32_t(cs) * 10u;
    const size_t nextBrace = text.find('{', close + 1);
    const std::string nextBody =
        (nextBrace == std::string::npos)
            ? text.substr(close + 1)
            : text.substr(close + 1, nextBrace - close - 1);
    if (!nextBody.empty()) {
      LyricWord w;
      w.startMs = cursor;
      w.endMs = cursor + span;
      w.text = nextBody;
      words->push_back(std::move(w));
    }
    cursor += span;
    i = (nextBrace == std::string::npos) ? text.size() : nextBrace;
  }
  if (!sawKaraoke) {
    words->clear();
    return false;
  }
  return !words->empty();
}

bool parseAss(const std::string& text, ParsedLyric* out) {
  bool inEvents = false;
  std::vector<std::string> format;
  int idxStart = 1;
  int idxEnd = 2;
  int idxStyle = 3;
  int idxName = 4;

  for (std::string raw : splitLines(text)) {
    trimInPlace(raw);
    if (raw.empty()) continue;
    const std::string low = lowercase(raw);
    if (raw[0] == '[') {
      inEvents = low.find("[events]") != std::string::npos;
      continue;
    }
    if (!inEvents) continue;
    if (low.rfind("format:", 0) == 0) {
      format = splitCsv(raw.substr(7), 0);
      for (size_t i = 0; i < format.size(); ++i) {
        const std::string key = lowercase(format[i]);
        if (key == "start") idxStart = int(i);
        else if (key == "end") idxEnd = int(i);
        else if (key == "style") idxStyle = int(i);
        else if (key == "name" || key == "actor") idxName = int(i);
      }
      continue;
    }
    if (low.rfind("dialogue:", 0) != 0) continue;

    const size_t fields = format.empty() ? 10 : format.size();
    const std::vector<std::string> parts = splitCsv(raw.substr(9), fields);
    if (parts.size() < fields) continue;
    if (idxStart < 0 || idxStart >= int(parts.size())) continue;
    if (idxEnd < 0 || idxEnd >= int(parts.size())) continue;

    const uint32_t startMs = parseAssTime(parts[size_t(idxStart)]);
    const uint32_t endMs = parseAssTime(parts[size_t(idxEnd)]);

    auto fieldAt = [&](int idx) -> std::string {
      if (idx < 0 || idx >= int(parts.size())) return {};
      return lowercase(parts[size_t(idx)]);
    };
    auto mentionsAux = [](const std::string& s) {
      return s.find("trans") != std::string::npos ||
             s.find("roman") != std::string::npos ||
             s.find("roma") != std::string::npos || s == "ts";
    };
    if (mentionsAux(fieldAt(idxStyle)) || mentionsAux(fieldAt(idxName))) continue;

    const size_t textIdx = fields - 1;
    if (textIdx >= parts.size()) continue;
    std::string payload;
    for (size_t i = textIdx; i < parts.size(); ++i) {
      if (i > textIdx) payload += ',';
      payload += parts[i];
    }

    LyricLine lyric;
    std::vector<LyricWord> words;
    if (parseKaraokeLine(payload, startMs, &words)) {
      lyric.words = words;
      lyric.text = joinWords(words);
    } else {
      lyric.text = normalizeSpaces(stripTags(payload));
    }
    if (lyric.text.empty()) continue;
    lyric.timeMs = startMs;
    lyric.endMs = endMs > startMs ? endMs : 0;
    out->lines.push_back(std::move(lyric));
  }
  return !out->lines.empty();
}

// ---------------------------------------------------------------------------
// SRT
// ---------------------------------------------------------------------------

bool parseSrt(const std::string& text, ParsedLyric* out) {
  const std::vector<std::string> lines = splitLines(text);
  size_t i = 0;
  while (i < lines.size()) {
    while (i < lines.size()) {
      std::string probe = lines[i];
      trimInPlace(probe);
      if (!probe.empty()) break;
      ++i;
    }
    if (i >= lines.size()) break;

    {
      std::string probe = lines[i];
      trimInPlace(probe);
      bool allDigits = !probe.empty();
      for (char c : probe) {
        if (!isDigit(c)) allDigits = false;
      }
      if (allDigits && i + 1 < lines.size()) ++i;
    }
    if (i >= lines.size()) break;

    const std::string timeLine = lines[i];
    ++i;
    const size_t arrow = timeLine.find("-->");
    if (arrow == std::string::npos) continue;
    uint32_t startMs = 0;
    uint32_t endMs = 0;
    if (!parseSrtTime(timeLine.substr(0, arrow), &startMs)) continue;
    if (!parseSrtTime(timeLine.substr(arrow + 3), &endMs)) continue;
    if (endMs < startMs) continue;

    std::string body;
    while (i < lines.size()) {
      std::string textLine = lines[i];
      trimInPlace(textLine);
      if (textLine.empty()) {
        ++i;
        break;
      }
      if (!body.empty()) body += ' ';
      body += textLine;
      ++i;
    }
    const std::string cleaned = normalizeSpaces(stripTags(body));
    if (cleaned.empty()) continue;
    LyricLine lyric;
    lyric.timeMs = startMs;
    lyric.endMs = endMs;
    lyric.text = cleaned;
    out->lines.push_back(std::move(lyric));
  }
  return !out->lines.empty();
}

// ---------------------------------------------------------------------------
// Dispatch + sniffing
// ---------------------------------------------------------------------------

bool parseAsFormat(const std::string& text, LyricFormat format,
                   ParsedLyric* out) {
  switch (format) {
    case LyricFormat::Ttml: return parseTtml(text, out);
    case LyricFormat::Lys: return parseLys(text, out);
    case LyricFormat::Qrc: return parseQrc(text, out);
    case LyricFormat::Krc: return parseKrc(text, out);
    case LyricFormat::Yrc: return parseYrc(text, out);
    case LyricFormat::Lrc: return parseLrc(text, out);
    case LyricFormat::Ass: return parseAss(text, out);
    case LyricFormat::Srt: return parseSrt(text, out);
    case LyricFormat::Unknown: break;
  }
  return false;
}

LyricFormat detectFormat(const std::string& text) {
  std::string trimmed = text;
  trimInPlace(trimmed);
  const std::string low = lowercase(text);
  const std::string trimmedLow = lowercase(trimmed);

  if (trimmedLow.rfind("[script info]", 0) == 0 ||
      low.find("[v4+ styles]") != std::string::npos ||
      low.find("[v4 styles]") != std::string::npos ||
      low.find("\ndialogue:") != std::string::npos ||
      trimmedLow.rfind("dialogue:", 0) == 0) {
    return LyricFormat::Ass;
  }
  {
    // Optional cue index followed by "HH:MM:SS,mmm --> ...".
    const size_t firstLineEnd = trimmed.find('\n');
    std::string probe = (firstLineEnd == std::string::npos)
                            ? trimmed
                            : trimmed.substr(0, firstLineEnd);
    trimInPlace(probe);
    bool digitsOnly = !probe.empty();
    for (char c : probe) {
      if (!isDigit(c)) digitsOnly = false;
    }
    if (digitsOnly && firstLineEnd != std::string::npos) {
      const size_t secondEnd = trimmed.find('\n', firstLineEnd + 1);
      probe = trimmed.substr(firstLineEnd + 1,
                             (secondEnd == std::string::npos)
                                 ? std::string::npos
                                 : secondEnd - firstLineEnd - 1);
      trimInPlace(probe);
    }
    if (probe.find("-->") != std::string::npos) {
      const size_t arrow = probe.find("-->");
      uint32_t a = 0;
      uint32_t b = 0;
      if (parseSrtTime(probe.substr(0, arrow), &a) &&
          parseSrtTime(probe.substr(arrow + 3), &b)) {
        return LyricFormat::Srt;
      }
    }
  }
  if (!trimmed.empty() && trimmed[0] == '<') {
    if (low.find("lyriccontent=\"") != std::string::npos ||
        low.find("<qrcinfos") != std::string::npos ||
        low.find("<lyric_") != std::string::npos) {
      return LyricFormat::Qrc;
    }
    if (low.find("<tt") != std::string::npos) return LyricFormat::Ttml;
  }
  // [start,dur](start,dur,0) → YRC; [start,dur]word(start,dur) → QRC.
  {
    size_t pos = 0;
    while (true) {
      const size_t at = low.find('[', pos);
      if (at == std::string::npos) break;
      const size_t close = low.find(']', at);
      if (close == std::string::npos) break;
      pos = close + 1;
      if (low.find(':', at + 1) < close) continue;  // [mm:ss]
      long vals[2] = {0, 0};
      if (scanNumbers(low, at + 1, close, vals, 2) != 2) continue;
      const size_t lp = low.find('(', close + 1);
      if (lp == std::string::npos || lp - close > 4) continue;
      const size_t rp = low.find(')', lp);
      if (rp == std::string::npos) continue;
      long wv[3] = {0, 0, 0};
      const int got = scanNumbers(low, lp + 1, rp, wv, 3);
      if (got == 3) return LyricFormat::Yrc;
      if (got == 2) return LyricFormat::Qrc;
    }
  }
  // [prop]word(start,dur) → LYS.
  for (const std::string& line : splitLines(text)) {
    std::string probe = line;
    trimInPlace(probe);
    if (probe.size() < 4 || probe[0] != '[' || !isDigit(probe[1]) ||
        probe[2] != ']') {
      continue;
    }
    const size_t lp = probe.find('(', 3);
    if (lp == std::string::npos) continue;
    const size_t rp = probe.find(')', lp);
    if (rp == std::string::npos) continue;
    long vals[2] = {0, 0};
    if (scanNumbers(probe, lp + 1, rp, vals, 2) == 2) return LyricFormat::Lys;
  }
  if (low.find("[id:$") != std::string::npos) return LyricFormat::Krc;
  // <offset,dur[,0]>word → KRC.
  {
    size_t pos = 0;
    while (true) {
      const size_t lt = low.find('<', pos);
      if (lt == std::string::npos) break;
      const size_t gt = low.find('>', lt);
      if (gt == std::string::npos) break;
      pos = gt + 1;
      long vals[3] = {0, 0, 0};
      if (scanNumbers(low, lt + 1, gt, vals, 3) >= 2) return LyricFormat::Krc;
    }
  }
  return LyricFormat::Lrc;
}

bool readWholeFile(const std::string& path, std::string* out) {
  FILE* fp = std::fopen(path.c_str(), "rb");
  if (!fp) return false;
  std::fseek(fp, 0, SEEK_END);
  const long size = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);
  if (size < 0) {
    std::fclose(fp);
    return false;
  }
  out->resize(size_t(size));
  const size_t got =
      out->empty() ? 0 : std::fread(&(*out)[0], 1, out->size(), fp);
  std::fclose(fp);
  if (got != out->size()) return false;
  if (out->size() >= 3 && static_cast<unsigned char>((*out)[0]) == 0xEF &&
      static_cast<unsigned char>((*out)[1]) == 0xBB &&
      static_cast<unsigned char>((*out)[2]) == 0xBF) {
    out->erase(0, 3);
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public format metadata
// ---------------------------------------------------------------------------

const char* lyricFormatName(LyricFormat f) {
  switch (f) {
    case LyricFormat::Ttml: return "ttml";
    case LyricFormat::Lys: return "lys";
    case LyricFormat::Qrc: return "qrc";
    case LyricFormat::Krc: return "krc";
    case LyricFormat::Yrc: return "yrc";
    case LyricFormat::Lrc: return "lrc";
    case LyricFormat::Ass: return "ass";
    case LyricFormat::Srt: return "srt";
    case LyricFormat::Unknown: break;
  }
  return "unknown";
}

LyricFormat lyricFormatFromExtension(const std::string& ext) {
  const std::string low = lowercase(ext);
  if (low == "ttml" || low == "xml") return LyricFormat::Ttml;
  if (low == "lys") return LyricFormat::Lys;
  if (low == "qrc") return LyricFormat::Qrc;
  if (low == "krc") return LyricFormat::Krc;
  if (low == "yrc") return LyricFormat::Yrc;
  if (low == "lrc" || low == "elrc") return LyricFormat::Lrc;
  if (low == "ass" || low == "ssa") return LyricFormat::Ass;
  if (low == "srt") return LyricFormat::Srt;
  return LyricFormat::Unknown;
}

int lyricFormatRank(LyricFormat f) {
  switch (f) {
    case LyricFormat::Ttml: return 0;
    case LyricFormat::Lys: return 1;
    case LyricFormat::Qrc: return 2;
    case LyricFormat::Krc: return 3;
    case LyricFormat::Yrc: return 4;
    case LyricFormat::Lrc: return 5;
    case LyricFormat::Ass: return 6;
    case LyricFormat::Srt: return 7;
    case LyricFormat::Unknown: break;
  }
  return 99;
}

const char* const* lyricSidecarExtensions() {
  static const char* const kExtensions[] = {"ttml", "lys",  "qrc", "krc",
                                            "yrc",  "lrc",  "ass", "srt",
                                            nullptr};
  return kExtensions;
}

// ---------------------------------------------------------------------------
// Lyrics
// ---------------------------------------------------------------------------

void finaliseLyricTimings(Lyrics& lyrics) {
  std::vector<LyricLine>& lines = lyrics.lines_;

  std::stable_sort(lines.begin(), lines.end(),
                   [](const LyricLine& a, const LyricLine& b) {
                     return a.timeMs < b.timeMs;
                   });
  lines.erase(std::unique(lines.begin(), lines.end(),
                          [](const LyricLine& a, const LyricLine& b) {
                            return a.timeMs == b.timeMs && a.text == b.text;
                          }),
              lines.end());

  for (size_t i = 0; i < lines.size(); ++i) {
    LyricLine& line = lines[i];
    if (line.text.empty()) line.text = joinWords(line.words);
    line.text = collapseSpaces(line.text);
    trimInPlace(line.text);
    for (LyricWord& word : line.words) {
      trimInPlace(word.text);
    }
    if (line.text.empty() && line.words.empty()) continue;

    for (size_t w = 0; w < line.words.size(); ++w) {
      LyricWord& word = line.words[w];
      if (word.endMs <= word.startMs) {
        word.endMs = (w + 1 < line.words.size())
                         ? std::max(word.startMs + 1, line.words[w + 1].startMs)
                         : 0u;
      }
    }

    if (line.endMs <= line.timeMs) {
      uint64_t next = 0;
      for (size_t j = i + 1; j < lines.size(); ++j) {
        if (lines[j].timeMs > line.timeMs) {
          next = lines[j].timeMs;
          break;
        }
      }
      line.endMs = next ? next : line.timeMs + 8000;
    }

    if (!line.words.empty()) {
      LyricWord& last = line.words.back();
      if (last.endMs <= last.startMs) {
        const uint64_t capped =
            std::min<uint64_t>(line.endMs, uint64_t(last.startMs) + 8000);
        last.endMs = uint32_t(std::max<uint64_t>(capped, last.startMs + 1));
      }
    }
  }
  lines.erase(std::remove_if(lines.begin(), lines.end(),
                             [](const LyricLine& line) {
                               return line.text.empty() && line.words.empty();
                             }),
              lines.end());

  lyrics.wordTimed_ = false;
  for (const LyricLine& line : lines) {
    if (line.words.size() > 1) {
      lyrics.wordTimed_ = true;
      break;
    }
  }
}

bool Lyrics::loadFile(const std::string& path) {
  std::string content;
  if (!readWholeFile(path, &content)) return false;
  LyricFormat format = LyricFormat::Unknown;
  const size_t dot = path.find_last_of('.');
  if (dot != std::string::npos) {
    format = lyricFormatFromExtension(path.substr(dot + 1));
  }
  if (format == LyricFormat::Unknown) return parse(content, LyricFormat::Unknown);
  const LyricFormat declared = format;
  if (parse(content, format) && !lines_.empty()) return true;
  // Extension lied (or the body is something else): sniff and retry.
  const LyricFormat sniffed = detectFormat(content);
  if (sniffed != declared) return parse(content, sniffed);
  return !lines_.empty();
}

bool Lyrics::parse(const std::string& content, LyricFormat format) {
  clear();
  if (content.empty()) return false;

  LyricFormat target = format;
  if (target == LyricFormat::Unknown) target = detectFormat(content);

  ParsedLyric parsed;
  bool ok = parseAsFormat(content, target, &parsed);
  if ((!ok || parsed.lines.empty()) && format == LyricFormat::Unknown) {
    static const LyricFormat kFallback[] = {
        LyricFormat::Ttml, LyricFormat::Lys, LyricFormat::Qrc,
        LyricFormat::Krc,  LyricFormat::Yrc, LyricFormat::Lrc,
        LyricFormat::Ass,  LyricFormat::Srt};
    for (LyricFormat candidate : kFallback) {
      if (candidate == target) continue;
      ParsedLyric retry;
      if (parseAsFormat(content, candidate, &retry) && !retry.lines.empty()) {
        parsed = std::move(retry);
        target = candidate;
        ok = true;
        break;
      }
    }
  }

  format_ = target;
  title_ = parsed.title;
  artist_ = parsed.artist;
  album_ = parsed.album;
  offsetTag_ = parsed.offsetTag;
  offsetMs_ = parsed.offsetMs;
  lines_ = std::move(parsed.lines);

  if (offsetMs_ != 0) {
    const int64_t delta = offsetMs_;
    for (LyricLine& line : lines_) {
      line.timeMs = uint64_t(shiftMs(
          uint32_t(std::min<uint64_t>(line.timeMs, 0xFFFFFFF)), delta));
      line.endMs = uint64_t(shiftMs(
          uint32_t(std::min<uint64_t>(line.endMs, 0xFFFFFFF)), delta));
      for (LyricWord& word : line.words) {
        word.startMs = shiftMs(word.startMs, delta);
        word.endMs = shiftMs(word.endMs, delta);
      }
    }
  }

  finaliseLyricTimings(*this);
  return !lines_.empty();
}

int Lyrics::activeIndex(uint64_t positionMs) const {
  if (lines_.empty()) return -1;
  int idx = -1;
  for (size_t i = 0; i < lines_.size(); ++i) {
    if (lines_[i].timeMs <= positionMs) {
      idx = int(i);
    } else {
      break;
    }
  }
  return idx;
}

float Lyrics::lineProgress(uint64_t positionMs) const {
  const int idx = activeIndex(positionMs);
  if (idx < 0) return -1.0f;
  const LyricLine& line = lines_[size_t(idx)];
  if (line.endMs <= line.timeMs + 120) return -1.0f;
  const double t =
      double(positionMs - line.timeMs) / double(line.endMs - line.timeMs);
  return float(std::min(1.0, std::max(0.0, t)));
}

float Lyrics::wordProgress(uint64_t positionMs, int* wordIndex) const {
  if (wordIndex) *wordIndex = -1;
  const int idx = activeIndex(positionMs);
  if (idx < 0) return -1.0f;
  const std::vector<LyricWord>& words = lines_[size_t(idx)].words;
  if (words.size() < 2) return -1.0f;
  for (size_t i = 0; i < words.size(); ++i) {
    if (positionMs < words[i].startMs) {
      if (wordIndex) *wordIndex = int(i);
      return 0.0f;
    }
    if (positionMs < words[i].endMs) {
      if (wordIndex) *wordIndex = int(i);
      const double span = double(words[i].endMs - words[i].startMs);
      if (span <= 0.0) return 1.0f;
      const double t = double(positionMs - words[i].startMs) / span;
      return float(std::min(1.0, std::max(0.0, t)));
    }
  }
  if (wordIndex) *wordIndex = int(words.size()) - 1;
  return 1.0f;
}

void Lyrics::clear() {
  lines_.clear();
  format_ = LyricFormat::Unknown;
  title_.clear();
  artist_.clear();
  album_.clear();
  offsetTag_.clear();
  offsetMs_ = 0;
  wordTimed_ = false;
}

}  // namespace spn
