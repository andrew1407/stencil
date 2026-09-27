#include "doctest.h"
#include "text.hpp"

#include <clocale>

using namespace stencil::core;

// The ASCII helpers must not depend on the host locale: Qt calls setlocale(LC_ALL, "").
TEST_CASE("text: toLowerAscii and trimAscii leave non-ASCII bytes alone under any locale") {
  const char* const locales[] = {"C", "", "de_DE.ISO8859-1", "en_US.ISO8859-1", "en_US.UTF-8"};
  for (const char* name : locales) {
    if (std::setlocale(LC_ALL, name) == nullptr) continue;
    CHECK(toLowerAscii("AbC\xC3\x89Z") == "abc\xC3\x89z");
    CHECK(trimAscii(" \t\xA0x\xA0\r\n") == "\xA0x\xA0");
    CHECK(trimLowerAscii("\x85Q\x85") == "\x85q\x85");
  }
  std::setlocale(LC_ALL, "C");
}

TEST_CASE("text: isAsciiSpace is exactly the six C whitespace bytes") {
  for (int c = 0; c < 256; ++c) {
    const bool expected = c == ' ' || (c >= '\t' && c <= '\r');
    CHECK(isAsciiSpace(static_cast<char>(c)) == expected);
  }
}
