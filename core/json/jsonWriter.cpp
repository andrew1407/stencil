#include "jsonWriter.hpp"

#include "jsNumber.hpp"
#include "jsText.hpp"

#include <cmath>
#include <utility>
#include <vector>

namespace stencil::core::json {

  namespace {
    void hex4(std::string& out, unsigned u) {
      static const char* digits = "0123456789abcdef";
      out += "\\u";
      for (int shift = 12; shift >= 0; shift -= 4) out.push_back(digits[(u >> shift) & 0xF]);
    }

    bool isHigh(char32_t cp) { return cp >= 0xD800 && cp <= 0xDBFF; }
    bool isLow(char32_t cp) { return cp >= 0xDC00 && cp <= 0xDFFF; }
  }  // namespace

  // ECMA-262 QuoteJSONString; two adjacent surrogate halves are one character, as in JS.
  void writeString(std::string& out, std::string_view s, LoneSurrogates lone) {
    out.push_back('"');
    for (std::size_t i = 0; i < s.size();) {
      const CodePoint c = codePointAt(s, i);
      const char32_t cp = c.value;
      if (isHigh(cp) && i + c.bytes < s.size()) {
        const CodePoint next = codePointAt(s, i + c.bytes);
        if (isLow(next.value)) {
          appendCodePoint(out, 0x10000 + ((cp - 0xD800) << 10) + (next.value - 0xDC00));
          i += c.bytes + next.bytes;
          continue;
        }
      }
      i += c.bytes;
      switch (cp) {
        case '"': out += "\\\""; continue;
        case '\\': out += "\\\\"; continue;
        case '\b': out += "\\b"; continue;
        case '\f': out += "\\f"; continue;
        case '\n': out += "\\n"; continue;
        case '\r': out += "\\r"; continue;
        case '\t': out += "\\t"; continue;
        default: break;
      }
      if (cp < 0x20) hex4(out, static_cast<unsigned>(cp));
      else if (isHigh(cp) || isLow(cp)) {
        if (lone == LoneSurrogates::ESCAPE) hex4(out, static_cast<unsigned>(cp));
        else appendCodePoint(out, 0xFFFD);
      } else {
        appendCodePoint(out, cp);
      }
    }
    out.push_back('"');
  }

  void writeJson(std::string& out, const Value& root, LoneSurrogates lone) {
    // A container on the stack, and how many of its members are already written.
    std::vector<std::pair<const Value*, std::size_t>> stack;
    const Value* v = &root;
    for (;;) {
      if (v) {
        switch (v->kind) {
          case Kind::NIL: out += "null"; break;
          case Kind::BOOL: out += v->flag ? "true" : "false"; break;
          case Kind::NUMBER:
            out += std::isfinite(v->number) ? jsNumberToString(v->number) : "null";
            break;
          case Kind::STRING: writeString(out, v->text, lone); break;
          case Kind::ARRAY:
          case Kind::OBJECT:
            out.push_back(v->isArray() ? '[' : '{');
            stack.emplace_back(v, 0);
            break;
        }
        v = nullptr;
      }
      if (stack.empty()) return;
      auto& [box, done] = stack.back();
      if (done == box->items.size()) {
        out.push_back(box->isArray() ? ']' : '}');
        stack.pop_back();
        continue;
      }
      if (done > 0) out.push_back(',');
      if (box->isObject()) {
        writeString(out, box->keys[done], lone);
        out.push_back(':');
      }
      v = &box->items[done++];
    }
  }

  std::string toJson(const Value& v, LoneSurrogates lone) {
    std::string out;
    writeJson(out, v, lone);
    return out;
  }

}  // namespace stencil::core::json
