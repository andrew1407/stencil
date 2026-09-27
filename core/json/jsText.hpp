#pragma once
#include <cstddef>
#include <string>
#include <string_view>

// JavaScript string semantics over UTF-8 bytes: the twin of what the browser's op-plan walk
// (browser/js/llm/plan/) gets from String.length, .trim(), TextDecoder and Number#toString.
// Strings inside core/json are WTF-8 — UTF-8 that may carry a lone surrogate from a \u escape.
namespace stencil::core::json {

  struct CodePoint {
    char32_t value = 0;
    std::size_t bytes = 1;
  };

  // One code point at `at` of a WTF-8 string; a byte that starts no sequence reads as U+FFFD.
  CodePoint codePointAt(std::string_view s, std::size_t at);

  // WTF-8 encoding: a surrogate code point becomes its own three bytes.
  void appendCodePoint(std::string& out, char32_t cp);

  // The WHATWG UTF-8 decoder: every maximal ill-formed subpart becomes one U+FFFD.
  std::string decodeUtf8(std::string_view bytes);
  bool isUtf8(std::string_view bytes);

  // String.prototype.length: an astral code point is 2, a lone surrogate 1.
  std::size_t utf16Length(std::string_view s);

  // ECMAScript WhiteSpace + LineTerminator, the set String.prototype.trim strips.
  bool isJsSpace(char32_t cp);
  std::string_view jsTrim(std::string_view s);

  // String.prototype.toWellFormed: each lone surrogate becomes U+FFFD.
  std::string wellFormed(std::string_view s);

}  // namespace stencil::core::json
