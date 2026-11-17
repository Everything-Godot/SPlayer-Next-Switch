// SPDX-License-Identifier: AGPL-3.0
// A small, dependency-free JSON parser.
//
// The desktop app gets this for free from `JSON.parse`. nx deliberately avoided
// a JSON dependency before (store.cpp uses a hand-rolled line format), but the
// NetEase API returns deeply nested objects, so a real parser is unavoidable
// here. This one is a straightforward recursive-descent parser producing a
// value tree; it is not trying to be fast, only correct and predictable.
//
// Design notes:
//   - Order-preserving objects (vector of pairs) so debugging output is stable.
//   - Numbers keep their original text alongside the parsed double, so big song
//     ids survive the trip without losing precision.
//   - Parse errors never throw: parse() returns false and sets an offset.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace spn::json {

class Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
 public:
  Value() = default;

  static Value makeNull();
  static Value makeBool(bool b);
  static Value makeNumber(double d, std::string raw = {});
  static Value makeString(std::string s);
  static Value makeArray();
  static Value makeObject();

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isBool() const { return type_ == Type::Bool; }
  bool isNumber() const { return type_ == Type::Number; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  // Scalar accessors, all tolerant: wrong types yield the default.
  bool asBool(bool def = false) const;
  double asDouble(double def = 0.0) const;
  int64_t asInt(int64_t def = 0) const;
  // Song ids are 64-bit and the API sometimes sends them as numbers, sometimes
  // as strings; this returns the decimal text either way.
  std::string asString(const std::string& def = {}) const;
  std::string numberRaw() const { return raw_; }

  // Array access.
  size_t size() const;
  const Value& at(size_t i) const;
  void push(Value v);

  // Object access. `get` returns a shared null sentinel when missing so callers
  // can chain without null checks: v.get("a").get("b").asString().
  const Value& get(const std::string& key) const;
  bool has(const std::string& key) const;
  void set(const std::string& key, Value v);
  const Object& object() const { return obj_; }
  const Array& array() const { return arr_; }

 private:
  Type type_ = Type::Null;
  bool bool_ = false;
  double num_ = 0.0;
  std::string raw_;
  std::string str_;
  Array arr_;
  Object obj_;
};

// Parses `text`. On failure returns false and writes the byte offset where
// parsing stopped into *errorOffset (when non-null).
bool parse(const std::string& text, Value& out, size_t* errorOffset = nullptr);

// Serialises a value back to compact JSON; used for request bodies.
std::string serialise(const Value& v);

// Convenience constructors for assembling request bodies inline, e.g.
//   auto body = json::obj({{"s", json::str("卡农")}, {"type", json::num(1)}});
Value arr();
Value obj(std::initializer_list<std::pair<const char*, Value>> fields);
Value str(std::string s);
Value num(double d);
Value boolean(bool b);

}  // namespace spn::json