#include "jsonScan.hpp"

#include "decimal.hpp"
#include "jsText.hpp"

namespace stencil::core::json::scan {

  namespace {
    int hexDigit(char c) {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    }

    bool isDigit(char c) { return c >= '0' && c <= '9'; }
  }  // namespace

  void Scanner::ws() {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
      ++pos;
  }

  bool Scanner::word(std::string_view w) {
    if (s.substr(pos, w.size()) != w) return false;
    pos += w.size();
    return true;
  }

  bool Scanner::string(std::string* out) {
    ++pos;
    // A \uXXXX escape at `at`, or -1.
    const auto unit = [this](std::size_t at) -> long {
      if (at + 6 > s.size() || s[at] != '\\' || s[at + 1] != 'u') return -1;
      long u = 0;
      for (std::size_t i = at + 2; i < at + 6; ++i) {
        const int h = hexDigit(s[i]);
        if (h < 0) return -1;
        u = u * 16 + h;
      }
      return u;
    };
    while (pos < s.size()) {
      const char c = s[pos];
      if (c == '"') {
        ++pos;
        return true;
      }
      if (static_cast<unsigned char>(c) < 0x20) return false;
      if (c != '\\') {
        if (out) out->push_back(c);
        ++pos;
        continue;
      }
      if (pos + 1 >= s.size()) return false;
      const char e = s[pos + 1];
      const char* simple = "\"\\/bfnrt";
      const char* decoded = "\"\\/\b\f\n\r\t";
      bool plain = false;
      for (int i = 0; simple[i] != '\0'; ++i) {
        if (e != simple[i]) continue;
        if (out) out->push_back(decoded[i]);
        plain = true;
      }
      if (plain) {
        pos += 2;
        continue;
      }
      long u = unit(pos);
      if (u < 0) return false;
      pos += 6;
      const long low = u >= 0xD800 && u <= 0xDBFF ? unit(pos) : -1;
      if (low >= 0xDC00 && low <= 0xDFFF) {
        u = 0x10000 + ((u - 0xD800) << 10) + (low - 0xDC00);
        pos += 6;
      }
      if (out) appendCodePoint(*out, static_cast<char32_t>(u));
    }
    return false;
  }

  bool Scanner::number(double* x, std::string* lexeme) {
    const std::size_t start = pos;
    if (is('-')) ++pos;
    if (is('0')) {
      ++pos;
    } else {
      if (pos >= s.size() || s[pos] < '1' || s[pos] > '9') return false;
      while (pos < s.size() && isDigit(s[pos])) ++pos;
    }
    if (is('.')) {
      ++pos;
      if (pos >= s.size() || !isDigit(s[pos])) return false;
      while (pos < s.size() && isDigit(s[pos])) ++pos;
    }
    if (is('e') || is('E')) {
      ++pos;
      if (is('+') || is('-')) ++pos;
      if (pos >= s.size() || !isDigit(s[pos])) return false;
      while (pos < s.size() && isDigit(s[pos])) ++pos;
    }
    const std::string_view text = s.substr(start, pos - start);
    const auto v = parseDecimal(text);
    if (!v) return false;
    *x = *v;
    lexeme->assign(text.data(), text.size());
    return true;
  }

  void Scanner::skipValue() {
    std::size_t depth = 0;
    while (pos < s.size()) {
      const char c = s[pos];
      if (c == '"') {
        string(nullptr);
        if (depth == 0) return;
        continue;
      }
      if (c == '{' || c == '[') {
        ++depth;
      } else if (c == '}' || c == ']') {
        if (depth == 0) return;
        if (--depth == 0) {
          ++pos;
          return;
        }
      } else if (depth == 0 && (c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
        return;
      }
      ++pos;
    }
  }

}  // namespace stencil::core::json::scan
