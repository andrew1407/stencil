#pragma once
#include "jsonValue.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// The token layer of jsonReader and its one driver loop: RFC 8259 grammar over valid UTF-8,
// strings decoded to WTF-8 (an escaped surrogate pair joins into one code point). The driver
// keeps its own container stack, so nesting never recurses.
namespace stencil::core::json::scan {

  struct Scanner {
    std::string_view s;
    std::size_t pos = 0;

    bool is(char c) const { return pos < s.size() && s[pos] == c; }
    void ws();
    // `pos` on the opening quote; `out` may be null to validate only.
    bool string(std::string* out);
    bool number(double* x, std::string* lexeme);
    bool word(std::string_view w);
    // Past one value the first pass already proved well-formed.
    void skipValue();
  };

  // Walks one JSON text, reporting to `sink`: wants(start) (false = skip that value, then
  // skipped()), begin(object, start), key(k), scalar(v, start), end(). False on a syntax error.
  template <class Sink>
  bool drive(Scanner& sc, Sink& sink) {
    std::vector<char> open;
    sc.ws();
    for (;;) {
      const std::size_t start = sc.pos;
      if (!sink.wants(start)) {
        sc.skipValue();
        sink.skipped();
      } else if (sc.is('{') || sc.is('[')) {
        const bool object = sc.is('{');
        ++sc.pos;
        open.push_back(object ? '}' : ']');
        sink.begin(object, start);
        sc.ws();
        if (sc.is(open.back())) {
          ++sc.pos;
          open.pop_back();
          sink.end();
        } else {
          if (object) {
            std::string k;
            if (!sc.is('"') || !sc.string(&k)) return false;
            sc.ws();
            if (!sc.is(':')) return false;
            ++sc.pos;
            sc.ws();
            sink.key(std::move(k));
          }
          continue;
        }
      } else {
        Value v;
        if (sc.is('"')) {
          v.kind = Kind::STRING;
          if (!sc.string(&v.text)) return false;
        } else if (sc.is('t') || sc.is('f')) {
          const bool truth = sc.is('t');
          if (!sc.word(truth ? "true" : "false")) return false;
          v = Value::boolean(truth);
        } else if (sc.is('n')) {
          if (!sc.word("null")) return false;
        } else {
          v.kind = Kind::NUMBER;
          if (!sc.number(&v.number, &v.text)) return false;
        }
        sink.scalar(std::move(v), start);
      }
      for (;;) {
        sc.ws();
        if (open.empty()) return sc.pos == sc.s.size();
        if (sc.is(',')) {
          ++sc.pos;
          sc.ws();
          if (open.back() == '}') {
            std::string k;
            if (!sc.is('"') || !sc.string(&k)) return false;
            sc.ws();
            if (!sc.is(':')) return false;
            ++sc.pos;
            sc.ws();
            sink.key(std::move(k));
          }
          break;
        }
        if (!sc.is(open.back())) return false;
        ++sc.pos;
        open.pop_back();
        sink.end();
      }
    }
  }

}  // namespace stencil::core::json::scan
