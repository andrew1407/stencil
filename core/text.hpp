#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

// ASCII string helpers, plus the keyed word table every string -> value resolution in the
// core runs through. Header-only on purpose: a .cpp would have to be mirrored across the
// three build definitions. ASCII-only by design — the core's tokens (colour names, units,
// keys) are ASCII, matching the browser's `.toLowerCase()` / `.trim()`.
namespace stencil::core {

  // Spelled out rather than <cctype>: those follow the host locale (Qt sets one), where a
  // Latin-1 locale would fold or split the bytes of a UTF-8 sequence.
  constexpr bool isAsciiSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
  }

  constexpr char toLowerAsciiChar(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }

  inline std::string toLowerAscii(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) out.push_back(toLowerAsciiChar(c));
    return out;
  }

  inline std::string_view trimAscii(std::string_view in) {
    std::size_t a = 0, b = in.size();
    while (a < b && isAsciiSpace(in[a])) ++a;
    while (b > a && isAsciiSpace(in[b - 1])) --b;
    return in.substr(a, b - a);
  }

  inline std::string trimLowerAscii(std::string_view in) {
    return toLowerAscii(trimAscii(in));
  }

  // One row of a keyword table: the word and what it resolves to.
  template <class T>
  struct Keyed {
    std::string_view word;
    T value;
  };

  // The first row whose word equals `key`, else nullptr. Rows match in declaration order,
  // so a table keeps whatever precedence the chain it replaces relied on.
  template <class T, std::size_t N>
  constexpr const T* lookupPtr(const std::array<Keyed<T>, N>& rows, std::string_view key) {
    for (const Keyed<T>& row : rows)
      if (row.word == key) return &row.value;
    return nullptr;
  }

  template <class T, std::size_t N>
  constexpr T lookup(const std::array<Keyed<T>, N>& rows, std::string_view key, T fallback) {
    const T* hit = lookupPtr(rows, key);
    return hit ? *hit : fallback;
  }

  // Membership in a plain word table — the tables whose only answer is "yes".
  template <std::size_t N>
  constexpr bool contains(const std::array<std::string_view, N>& words, std::string_view key) {
    for (std::string_view word : words)
      if (word == key) return true;
    return false;
  }

}  // namespace stencil::core
