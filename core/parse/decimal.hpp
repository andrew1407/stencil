#pragma once
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

// Locale-neutral, exception-free decimal parsing: the browser's parseFloat at every call site
// that reads a number. The wasm build has no exceptions, so std::stod there aborts the module.
namespace stencil::core {

  inline bool isDecimalByte(char c) {
    return (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-';
  }

  /* The longest decimal prefix of `text` (strtod's grammar without hex, inf or nan), or nullopt
   * when none parses; `used` gets its byte length. An overflow is ±inf, exactly as parseFloat. */
  inline std::optional<double> parseDecimalPrefix(std::string_view text,
                                                  std::size_t* used = nullptr) {
    std::size_t run = 0;
    while (run < text.size() && isDecimalByte(text[run])) ++run;
    if (run == 0) return std::nullopt;
    // strtod reads the C locale's point, and a host that called setlocale may have made it ','.
    const char* point = std::localeconv()->decimal_point;
    const std::size_t pointLen = point && point[0] != '\0' ? std::strlen(point) : 0;
    std::string buf;
    buf.reserve(run + 8);
    for (std::size_t i = 0; i < run; ++i) {
      if (text[i] == '.' && pointLen > 0) buf.append(point, pointLen);
      else buf.push_back(text[i]);
    }
    char* end = nullptr;
    const double v = std::strtod(buf.c_str(), &end);
    std::size_t taken = static_cast<std::size_t>(end - buf.c_str());
    if (taken == 0) return std::nullopt;
    if (used) {
      std::size_t n = 0;
      for (std::size_t b = 0; b < taken; ++n) b += (text[n] == '.' && pointLen > 0) ? pointLen : 1;
      *used = n;
    }
    return v;
  }

  // All of `text` as a decimal, or nullopt when a byte is left over.
  inline std::optional<double> parseDecimal(std::string_view text) {
    std::size_t used = 0;
    const auto v = parseDecimalPrefix(text, &used);
    if (!v || used != text.size()) return std::nullopt;
    return v;
  }

}  // namespace stencil::core
