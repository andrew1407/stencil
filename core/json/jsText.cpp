#include "jsText.hpp"

namespace stencil::core::json {

  namespace {
    constexpr char32_t REPLACEMENT_CHAR = 0xFFFD;

    bool isContinuation(unsigned char b) { return (b & 0xC0) == 0x80; }
    bool isSurrogate(char32_t cp) { return cp >= 0xD800 && cp <= 0xDFFF; }
  }  // namespace

  CodePoint codePointAt(std::string_view s, std::size_t at) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(s[at + i]); };
    const unsigned char b0 = byte(0);
    if (b0 < 0x80) return {b0, 1};
    const std::size_t left = s.size() - at;
    if (b0 >= 0xC2 && b0 <= 0xDF && left >= 2 && isContinuation(byte(1)))
      return {static_cast<char32_t>(((b0 & 0x1F) << 6) | (byte(1) & 0x3F)), 2};
    if (b0 >= 0xE0 && b0 <= 0xEF && left >= 3 && isContinuation(byte(1)) &&
        isContinuation(byte(2)) && (b0 != 0xE0 || byte(1) >= 0xA0))
      return {static_cast<char32_t>(((b0 & 0x0F) << 12) | ((byte(1) & 0x3F) << 6) |
                                    (byte(2) & 0x3F)),
              3};
    if (b0 >= 0xF0 && b0 <= 0xF4 && left >= 4 && isContinuation(byte(1)) &&
        isContinuation(byte(2)) && isContinuation(byte(3)) &&
        (b0 != 0xF0 || byte(1) >= 0x90) && (b0 != 0xF4 || byte(1) <= 0x8F))
      return {static_cast<char32_t>(((b0 & 0x07) << 18) | ((byte(1) & 0x3F) << 12) |
                                    ((byte(2) & 0x3F) << 6) | (byte(3) & 0x3F)),
              4};
    return {REPLACEMENT_CHAR, 1};
  }

  void appendCodePoint(std::string& out, char32_t cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  // encoding.spec.whatwg.org "UTF-8 decoder": a byte outside [lower, upper] ends the sequence
  // with one U+FFFD and is then read again on its own.
  std::string decodeUtf8(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size());
    char32_t cp = 0;
    int needed = 0, seen = 0;
    unsigned char lower = 0x80, upper = 0xBF;
    for (std::size_t i = 0; i < bytes.size();) {
      const auto b = static_cast<unsigned char>(bytes[i]);
      if (needed == 0) {
        ++i;
        if (b < 0x80) { out.push_back(static_cast<char>(b)); continue; }
        if (b >= 0xC2 && b <= 0xDF) { needed = 1; cp = b & 0x1F; continue; }
        if (b >= 0xE0 && b <= 0xEF) {
          if (b == 0xE0) lower = 0xA0;
          if (b == 0xED) upper = 0x9F;
          needed = 2;
          cp = b & 0x0F;
          continue;
        }
        if (b >= 0xF0 && b <= 0xF4) {
          if (b == 0xF0) lower = 0x90;
          if (b == 0xF4) upper = 0x8F;
          needed = 3;
          cp = b & 0x07;
          continue;
        }
        appendCodePoint(out, REPLACEMENT_CHAR);
        continue;
      }
      if (b < lower || b > upper) {
        cp = 0;
        needed = seen = 0;
        lower = 0x80;
        upper = 0xBF;
        appendCodePoint(out, REPLACEMENT_CHAR);
        continue;
      }
      ++i;
      lower = 0x80;
      upper = 0xBF;
      cp = (cp << 6) | (b & 0x3F);
      if (++seen == needed) {
        appendCodePoint(out, cp);
        cp = 0;
        needed = seen = 0;
      }
    }
    if (needed != 0) appendCodePoint(out, REPLACEMENT_CHAR);
    return out;
  }

  bool isUtf8(std::string_view bytes) {
    for (std::size_t i = 0; i < bytes.size();) {
      const CodePoint c = codePointAt(bytes, i);
      if ((c.value == REPLACEMENT_CHAR && c.bytes == 1) || isSurrogate(c.value)) return false;
      i += c.bytes;
    }
    return true;
  }

  std::size_t utf16Length(std::string_view s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size();) {
      const CodePoint c = codePointAt(s, i);
      n += c.value >= 0x10000 ? 2 : 1;
      i += c.bytes;
    }
    return n;
  }

  bool isJsSpace(char32_t cp) {
    switch (cp) {
      case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20: case 0xA0:
      case 0x1680: case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
        return true;
      default:
        return cp >= 0x2000 && cp <= 0x200A;
    }
  }

  std::string_view jsTrim(std::string_view s) {
    std::size_t begin = s.size(), end = 0;
    for (std::size_t i = 0; i < s.size();) {
      const CodePoint c = codePointAt(s, i);
      if (!isJsSpace(c.value)) {
        if (begin == s.size()) begin = i;
        end = i + c.bytes;
      }
      i += c.bytes;
    }
    return begin == s.size() ? std::string_view() : s.substr(begin, end - begin);
  }

  std::string wellFormed(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
      const CodePoint c = codePointAt(s, i);
      if (isSurrogate(c.value)) appendCodePoint(out, REPLACEMENT_CHAR);
      else out.append(s.data() + i, c.bytes);
      i += c.bytes;
    }
    return out;
  }

}  // namespace stencil::core::json
