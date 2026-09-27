#pragma once
#include "jsonValue.hpp"

#include <string>
#include <string_view>

// JSON.stringify(value) byte for byte: no whitespace, JS Object.keys order, numbers in
// Number#toString form (a non-finite one as null) and QuoteJSONString's escapes. Iterative.
// The op-plan result is written with Replace, so its text is always well-formed UTF-8.
namespace stencil::core::json {

  // What a lone surrogate becomes: U+FFFD, or JSON.stringify's own "\udxxx" escape.
  enum class LoneSurrogates { REPLACE, ESCAPE };

  void writeString(std::string& out, std::string_view wtf8, LoneSurrogates lone = LoneSurrogates::REPLACE);
  void writeJson(std::string& out, const Value& v, LoneSurrogates lone = LoneSurrogates::REPLACE);
  std::string toJson(const Value& v, LoneSurrogates lone = LoneSurrogates::REPLACE);

}  // namespace stencil::core::json
