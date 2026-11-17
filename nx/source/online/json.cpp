// SPDX-License-Identifier: AGPL-3.0
// Recursive-descent JSON parser + compact serialiser (see json.hpp).

#include "spn/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace spn::json {
namespace {

// Shared null sentinel returned by Value::get() for missing keys. Never mutated.
const Value kNull{};

// UTF-8 encoding of a code point.
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

class Parser {
 public:
  Parser(const std::string& text, size_t* err) : s_(text), err_(err) {}

  bool run(Value& out) {
    skipWs();
    if (!parseValue(out)) return false;
    skipWs();
    // Trailing garbage is a hard error: it usually means a truncated or
    // concatenated response.
    if (i_ != s_.size()) return fail();
    return true;
  }

 private:
  const std::string& s_;
  size_t i_ = 0;
  size_t* err_;

  bool fail() {
    if (err_) *err_ = i_;
    return false;
  }

  void skipWs() {
    while (i_ < s_.size()) {
      const char c = s_[i_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++i_;
      } else {
        break;
      }
    }
  }

  bool eat(char c) {
    if (i_ < s_.size() && s_[i_] == c) {
      ++i_;
      return true;
    }
    return false;
  }

  bool parseValue(Value& out) {
    if (i_ >= s_.size()) return fail();
    switch (s_[i_]) {
      case '{':
        return parseObject(out);
      case '[':
        return parseArray(out);
      case '"': {
        std::string str;
        if (!parseString(str)) return false;
        out = Value::makeString(std::move(str));
        return true;
      }
      case 't':
        if (s_.compare(i_, 4, "true") == 0) {
          i_ += 4;
          out = Value::makeBool(true);
          return true;
        }
        return fail();
      case 'f':
        if (s_.compare(i_, 5, "false") == 0) {
          i_ += 5;
          out = Value::makeBool(false);
          return true;
        }
        return fail();
      case 'n':
        if (s_.compare(i_, 4, "null") == 0) {
          i_ += 4;
          out = Value::makeNull();
          return true;
        }
        return fail();
      default:
        return parseNumber(out);
    }
  }

  bool parseObject(Value& out) {
    if (!eat('{')) return fail();
    out = Value::makeObject();
    skipWs();
    if (eat('}')) return true;
    for (;;) {
      skipWs();
      std::string key;
      if (!parseString(key)) return false;
      skipWs();
      if (!eat(':')) return fail();
      skipWs();
      Value child;
      if (!parseValue(child)) return false;
      out.set(key, std::move(child));
      skipWs();
      if (eat(',')) continue;
      if (eat('}')) return true;
      return fail();
    }
  }

  bool parseArray(Value& out) {
    if (!eat('[')) return fail();
    out = Value::makeArray();
    skipWs();
    if (eat(']')) return true;
    for (;;) {
      skipWs();
      Value child;
      if (!parseValue(child)) return false;
      out.push(std::move(child));
      skipWs();
      if (eat(',')) continue;
      if (eat(']')) return true;
      return fail();
    }
  }

  bool parseHex4(uint32_t& v) {
    if (i_ + 4 > s_.size()) return false;
    v = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = s_[i_++];
      v <<= 4;
      if (c >= '0' && c <= '9') {
        v |= uint32_t(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        v |= uint32_t(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        v |= uint32_t(c - 'A' + 10);
      } else {
        return false;
      }
    }
    return true;
  }

  bool parseString(std::string& out) {
    if (!eat('"')) return fail();
    out.clear();
    while (i_ < s_.size()) {
      const char c = s_[i_++];
      if (c == '"') return true;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (i_ >= s_.size()) return fail();
      const char esc = s_[i_++];
      switch (esc) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          uint32_t cp = 0;
          if (!parseHex4(cp)) return fail();
          // Surrogate pair: the API does emit them inside lyric text.
          if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 1 < s_.size() && s_[i_] == '\\' &&
              s_[i_ + 1] == 'u') {
            i_ += 2;
            uint32_t lo = 0;
            if (!parseHex4(lo)) return fail();
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else {
              appendUtf8(out, cp);
              cp = lo;
            }
          }
          appendUtf8(out, cp);
          break;
        }
        default:
          return fail();
      }
    }
    return fail();  // unterminated
  }

  bool parseNumber(Value& out) {
    const size_t start = i_;
    if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
    while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
    if (i_ < s_.size() && s_[i_] == '.') {
      ++i_;
      while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
    }
    if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
      ++i_;
      if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
      while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
    }
    if (i_ == start) return fail();
    const std::string raw = s_.substr(start, i_ - start);
    out = Value::makeNumber(std::strtod(raw.c_str(), nullptr), raw);
    return true;
  }
};

void serialiseTo(const Value& v, std::string& out) {
  switch (v.type()) {
    case Type::Null:
      out += "null";
      break;
    case Type::Bool:
      out += v.asBool() ? "true" : "false";
      break;
    case Type::Number: {
      // Prefer the original text so integers never gain a ".0" suffix.
      if (!v.numberRaw().empty()) {
        out += v.numberRaw();
      } else {
        char buf[40];
        const double d = v.asDouble();
        if (d == std::floor(d) && std::fabs(d) < 1e15) {
          std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
        } else {
          std::snprintf(buf, sizeof(buf), "%.17g", d);
        }
        out += buf;
      }
      break;
    }
    case Type::String: {
      out.push_back('"');
      for (unsigned char c : v.asString()) {
        switch (c) {
          case '"': out += "\\\""; break;
          case '\\': out += "\\\\"; break;
          case '\n': out += "\\n"; break;
          case '\r': out += "\\r"; break;
          case '\t': out += "\\t"; break;
          case '\b': out += "\\b"; break;
          case '\f': out += "\\f"; break;
          default:
            if (c < 0x20) {
              char buf[8];
              std::snprintf(buf, sizeof(buf), "\\u%04x", c);
              out += buf;
            } else {
              out.push_back(char(c));
            }
        }
      }
      out.push_back('"');
      break;
    }
    case Type::Array: {
      out.push_back('[');
      const auto& a = v.array();
      for (size_t i = 0; i < a.size(); ++i) {
        if (i) out.push_back(',');
        serialiseTo(a[i], out);
      }
      out.push_back(']');
      break;
    }
    case Type::Object: {
      out.push_back('{');
      const auto& o = v.object();
      for (size_t i = 0; i < o.size(); ++i) {
        if (i) out.push_back(',');
        out.push_back('"');
        for (unsigned char c : o[i].first) {
          if (c == '"' || c == '\\') out.push_back('\\');
          out.push_back(char(c));
        }
        out += "\":";
        serialiseTo(o[i].second, out);
      }
      out.push_back('}');
      break;
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

Value Value::makeNull() { return Value{}; }

Value Value::makeBool(bool b) {
  Value v;
  v.type_ = Type::Bool;
  v.bool_ = b;
  return v;
}

Value Value::makeNumber(double d, std::string raw) {
  Value v;
  v.type_ = Type::Number;
  v.num_ = d;
  v.raw_ = std::move(raw);
  return v;
}

Value Value::makeString(std::string s) {
  Value v;
  v.type_ = Type::String;
  v.str_ = std::move(s);
  return v;
}

Value Value::makeArray() {
  Value v;
  v.type_ = Type::Array;
  return v;
}

Value Value::makeObject() {
  Value v;
  v.type_ = Type::Object;
  return v;
}

bool Value::asBool(bool def) const {
  if (type_ == Type::Bool) return bool_;
  if (type_ == Type::Number) return num_ != 0.0;
  return def;
}

double Value::asDouble(double def) const {
  if (type_ == Type::Number) return num_;
  if (type_ == Type::Bool) return bool_ ? 1.0 : 0.0;
  if (type_ == Type::String) {
    const char* p = str_.c_str();
    char* end = nullptr;
    const double d = std::strtod(p, &end);
    if (end != p) return d;
  }
  return def;
}

int64_t Value::asInt(int64_t def) const {
  if (type_ == Type::Number) {
    // Integer-shaped raw text is the exact value; avoid a double round trip so
    // 64-bit song ids stay exact.
    if (!raw_.empty()) {
      bool integral = true;
      for (char c : raw_) {
        if (!((c >= '0' && c <= '9') || c == '-' || c == '+')) {
          integral = false;
          break;
        }
      }
      if (integral) {
        return std::strtoll(raw_.c_str(), nullptr, 10);
      }
    }
    return static_cast<int64_t>(num_);
  }
  if (type_ == Type::String) return std::strtoll(str_.c_str(), nullptr, 10);
  if (type_ == Type::Bool) return bool_ ? 1 : 0;
  return def;
}

std::string Value::asString(const std::string& def) const {
  switch (type_) {
    case Type::String:
      return str_;
    case Type::Number:
      // Keep the wire spelling for ids; fall back to a formatted double.
      if (!raw_.empty()) return raw_;
      {
        char buf[40];
        if (num_ == std::floor(num_) && std::fabs(num_) < 1e15) {
          std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(num_));
        } else {
          std::snprintf(buf, sizeof(buf), "%.17g", num_);
        }
        return buf;
      }
    case Type::Bool:
      return bool_ ? "true" : "false";
    case Type::Null:
      return def;
    default:
      return def;
  }
}

size_t Value::size() const {
  if (type_ == Type::Array) return arr_.size();
  if (type_ == Type::Object) return obj_.size();
  return 0;
}

const Value& Value::at(size_t i) const {
  if (type_ == Type::Array && i < arr_.size()) return arr_[i];
  return kNull;
}

void Value::push(Value v) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    arr_.clear();
  }
  arr_.push_back(std::move(v));
}

const Value& Value::get(const std::string& key) const {
  if (type_ == Type::Object) {
    for (const auto& kv : obj_) {
      if (kv.first == key) return kv.second;
    }
  }
  return kNull;
}

bool Value::has(const std::string& key) const {
  if (type_ != Type::Object) return false;
  for (const auto& kv : obj_) {
    if (kv.first == key) return true;
  }
  return false;
}

void Value::set(const std::string& key, Value v) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    obj_.clear();
  }
  for (auto& kv : obj_) {
    if (kv.first == key) {
      kv.second = std::move(v);
      return;
    }
  }
  obj_.emplace_back(key, std::move(v));
}

// ---------------------------------------------------------------------------
// free functions
// ---------------------------------------------------------------------------

bool parse(const std::string& text, Value& out, size_t* errorOffset) {
  Parser p(text, errorOffset);
  return p.run(out);
}

std::string serialise(const Value& v) {
  std::string out;
  out.reserve(256);
  serialiseTo(v, out);
  return out;
}

Value arr() { return Value::makeArray(); }

Value obj(std::initializer_list<std::pair<const char*, Value>> fields) {
  Value v = Value::makeObject();
  for (const auto& f : fields) v.set(f.first, f.second);
  return v;
}

Value str(std::string s) { return Value::makeString(std::move(s)); }
Value num(double d) { return Value::makeNumber(d, {}); }
Value boolean(bool b) { return Value::makeBool(b); }

}  // namespace spn::json