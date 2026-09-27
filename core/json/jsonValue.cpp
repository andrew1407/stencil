#include "jsonValue.hpp"

#include <algorithm>
#include <utility>

namespace stencil::core::json {

  namespace {
    std::uint32_t hashKey(std::string_view key) {
      std::uint32_t h = 2166136261u;
      for (char c : key) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
      return h;
    }
  }  // namespace

  // Children are moved onto a worklist and dropped one level at a time.
  Value::~Value() {
    if (items.empty()) return;
    std::vector<Value> pending;
    pending.swap(items);
    while (!pending.empty()) {
      Value last = std::move(pending.back());
      pending.pop_back();
      for (Value& child : last.items) pending.push_back(std::move(child));
      last.items.clear();
    }
  }

  Value Value::boolean(bool b) {
    Value v;
    v.kind = Kind::BOOL;
    v.flag = b;
    return v;
  }

  Value Value::num(double x, std::string lexeme) {
    Value v;
    v.kind = Kind::NUMBER;
    v.number = x;
    v.text = std::move(lexeme);
    return v;
  }

  Value Value::str(std::string s) {
    Value v;
    v.kind = Kind::STRING;
    v.text = std::move(s);
    return v;
  }

  Value Value::array() {
    Value v;
    v.kind = Kind::ARRAY;
    return v;
  }

  Value Value::object() {
    Value v;
    v.kind = Kind::OBJECT;
    return v;
  }

  std::size_t Value::find(std::string_view key) const {
    if (keyIndex.empty()) {
      for (std::size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key) return i;
      return keys.size();
    }
    const std::size_t mask = keyIndex.size() - 1;
    for (std::size_t s = hashKey(key) & mask;; s = (s + 1) & mask) {
      if (keyIndex[s] == 0) return keys.size();
      if (keys[keyIndex[s] - 1] == key) return keyIndex[s] - 1;
    }
  }

  void Value::rebuildIndex() {
    keyIndex.clear();
    if (keys.size() <= INDEX_FROM) return;
    std::size_t size = 64;
    while (size < keys.size() * 2) size *= 2;
    keyIndex.assign(size, 0);
    const std::size_t mask = size - 1;
    for (std::size_t i = 0; i < keys.size(); ++i) {
      std::size_t s = hashKey(keys[i]) & mask;
      while (keyIndex[s] != 0) s = (s + 1) & mask;
      keyIndex[s] = static_cast<std::uint32_t>(i + 1);
    }
  }

  const Value* Value::get(std::string_view key) const {
    if (kind != Kind::OBJECT) return nullptr;
    const std::size_t i = find(key);
    return i < keys.size() ? &items[i] : nullptr;
  }

  Value* Value::get(std::string_view key) {
    if (kind != Kind::OBJECT) return nullptr;
    const std::size_t i = find(key);
    return i < keys.size() ? &items[i] : nullptr;
  }

  bool Value::present(std::string_view key) const {
    const Value* v = get(key);
    return v && !v->isNull();
  }

  void Value::set(std::string key, Value v) {
    const std::size_t at = find(key);
    if (at < keys.size()) {
      items[at] = std::move(v);
      return;
    }
    std::size_t pos = keys.size();
    std::uint32_t index = 0;
    if (isArrayIndex(key, &index)) {
      std::uint32_t other = 0;
      pos = 0;
      while (pos < keys.size() && isArrayIndex(keys[pos], &other) && other < index) ++pos;
    }
    keys.insert(keys.begin() + static_cast<std::ptrdiff_t>(pos), std::move(key));
    items.insert(items.begin() + static_cast<std::ptrdiff_t>(pos), std::move(v));
    if (keys.size() > INDEX_FROM) rebuildIndex();
  }

  void Value::append(std::string key, Value v) {
    const std::size_t at = find(key);
    if (at < keys.size()) {
      items[at] = std::move(v);
      return;
    }
    keys.push_back(std::move(key));
    items.push_back(std::move(v));
    if (keys.size() <= INDEX_FROM) return;
    if (keyIndex.size() < keys.size() * 2) {
      rebuildIndex();
      return;
    }
    const std::size_t mask = keyIndex.size() - 1;
    std::size_t s = hashKey(keys.back()) & mask;
    while (keyIndex[s] != 0) s = (s + 1) & mask;
    keyIndex[s] = static_cast<std::uint32_t>(keys.size());
  }

  // JS Object.keys order: array indices first in ascending order, then the rest as inserted.
  void Value::finishObject() {
    std::vector<std::pair<std::uint32_t, std::size_t>> indices;
    bool ordered = true;
    std::uint32_t n = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
      if (!isArrayIndex(keys[i], &n)) continue;
      if (indices.size() != i || (!indices.empty() && indices.back().first > n)) ordered = false;
      indices.emplace_back(n, i);
    }
    if (ordered) return;
    std::sort(indices.begin(), indices.end());
    std::vector<std::size_t> order;
    order.reserve(keys.size());
    for (const auto& p : indices) order.push_back(p.second);
    for (std::size_t i = 0; i < keys.size(); ++i)
      if (!isArrayIndex(keys[i])) order.push_back(i);
    std::vector<std::string> k;
    std::vector<Value> v;
    k.reserve(keys.size());
    v.reserve(items.size());
    for (std::size_t i : order) {
      k.push_back(std::move(keys[i]));
      v.push_back(std::move(items[i]));
    }
    keys.swap(k);
    items.swap(v);
    rebuildIndex();
  }

  bool isArrayIndex(std::string_view key, std::uint32_t* out) {
    if (key.empty() || key.size() > 10 || (key.size() > 1 && key[0] == '0')) return false;
    std::uint64_t n = 0;
    for (char c : key) {
      if (c < '0' || c > '9') return false;
      n = n * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (n > 4294967294ull) return false;
    if (out) *out = static_cast<std::uint32_t>(n);
    return true;
  }

  bool strictEquals(const Value& a, const Value& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
      case Kind::NIL: return true;
      case Kind::BOOL: return a.flag == b.flag;
      case Kind::NUMBER: return a.number == b.number;
      case Kind::STRING: return a.text == b.text;
      default: return false;
    }
  }

}  // namespace stencil::core::json
