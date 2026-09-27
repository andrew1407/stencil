// JS string and number semantics over UTF-8: String#length, String#trim's set, toWellFormed,
// the WHATWG decoder's replacement, and Number#toString against every pair in
// generated/numbers.json (binade edges included) that the JS reference wrote.
#include "doctest.h"

#include "jsNumber.hpp"
#include "jsText.hpp"
#include "../opplan/opplanCorpus.hpp"

#include <cstdint>
#include <cstring>
#include <string>

using namespace stencil::core::json;

TEST_CASE("jsText: String#length counts UTF-16 units") {
  CHECK(utf16Length("") == 0);
  CHECK(utf16Length("a\xc3\xa9\xe2\x82\xac") == 3);
  CHECK(utf16Length("\xf0\x9f\x98\x80") == 2);
  CHECK(utf16Length("\xed\xa0\x80") == 1);
}

TEST_CASE("jsText: trim strips exactly JS WhiteSpace and LineTerminator") {
  const char* spaces[] = {"\t", "\n", "\v", "\f", "\r", " ", "\xc2\xa0", "\xe1\x9a\x80", "\xe2\x80\x80",
                          "\xe2\x80\x8a", "\xe2\x80\xa8", "\xe2\x80\xa9", "\xe2\x80\xaf", "\xe2\x81\x9f",
                          "\xe3\x80\x80", "\xef\xbb\xbf"};
  for (const char* s : spaces) CHECK_MESSAGE(jsTrim(std::string(s) + "x" + s) == "x", "not trimmed: " << s);
  const char* kept[] = {"\xe2\x80\x8b", "\xc2\x85", "\xe1\xa0\x8e"};
  for (const char* s : kept) CHECK_MESSAGE(jsTrim(s) == s, "trimmed: " << s);
  CHECK(jsTrim(" \xe3\x80\x80 ").empty());
}

TEST_CASE("jsText: toWellFormed and the WHATWG decoder") {
  CHECK(wellFormed("a\xed\xa0\x80" "b") == "a\xef\xbf\xbd" "b");
  const std::string fffd = "\xef\xbf\xbd";
  CHECK(decodeUtf8("\x80") == fffd);
  CHECK(decodeUtf8("\xc0\xaf") == fffd + fffd);
  CHECK(decodeUtf8("\xe2\x82") == fffd);
  CHECK(decodeUtf8("\xe2\x82x") == fffd + "x");
  CHECK(decodeUtf8("\xed\xa0\x80") == fffd + fffd + fffd);
  CHECK(decodeUtf8("\xf4\x90\x80\x80") == fffd + fffd + fffd + fffd);
  CHECK(decodeUtf8("\xf0\x9f\x98\x80") == "\xf0\x9f\x98\x80");
  CHECK(isUtf8("h\xc3\xa9"));
  CHECK_FALSE(isUtf8("\xed\xa0\x80"));
}

TEST_CASE("jsNumber: every numbers.json pair is Number#toString's") {
  const Value doc = opplanCorpus::readFile(opplanCorpus::llmDir() / "fixtures/opPlan/generated/numbers.json");
  const Value* cases = doc.get("cases");
  REQUIRE(cases);
  CHECK(cases->items.size() >= 5000);
  for (const Value& c : cases->items) {
    const std::uint64_t bits = std::strtoull(c.items[0].text.c_str(), nullptr, 16);
    double x = 0;
    std::memcpy(&x, &bits, sizeof x);
    CHECK_MESSAGE(jsNumberToString(x) == c.items[1].text, c.items[0].text);
  }
}
