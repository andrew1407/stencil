// core/json against JSON.parse / JSON.stringify: strict RFC 8259, JS key order and duplicate
// handling, WTF-8 strings, the caps judged on the value JSON.parse returns, iterative depth,
// and a document cut at every seventh byte. Twin of what browser/js/llm/plan/ gets from V8.
#include "doctest.h"

#include "jsonReader.hpp"
#include "jsonWriter.hpp"

#include <cmath>
#include <string>

using namespace stencil::core::json;

namespace {
  std::string roundTrip(const std::string& text, LoneSurrogates lone = LoneSurrogates::ESCAPE) {
    const ReadResult r = readJson(text);
    return r.status == ReadStatus::OK ? toJson(r.value, lone) : "<" + std::to_string(static_cast<int>(r.status)) + ">";
  }
}  // namespace

TEST_CASE("json: JSON.parse accepts exactly RFC 8259") {
  CHECK(roundTrip(" {\"a\" : [1, true, false, null, \"s\", -0.5e+2, {}]}\r\n\t") ==
        "{\"a\":[1,true,false,null,\"s\",-50,{}]}");
  CHECK(roundTrip("\"top\"") == "\"top\"");
  CHECK(roundTrip("7") == "7");
  for (const char* bad : {"", " ", "{", "[1,]", "{\"a\":1,}", "01", "1.", ".5", "+1", "0x1", "NaN", "Infinity",
                          "'a'", "{a:1}", "[1 2]", "\"a\tb\"", "\"\\x41\"", "\"\\u12\"", "tru", "nul",
                          "{\"a\"}", "[1]]", "/*c*/1", "\xef\xbb\xbf{}", "1e", "-", "\"abc"}) {
    CHECK_MESSAGE(readJson(bad).status == ReadStatus::SYNTAX, "accepted: " << bad);
  }
}

TEST_CASE("json: keys iterate in JS Object.keys order; a duplicate keeps its first place") {
  CHECK(roundTrip("{\"b\":1,\"a\":2,\"b\":3}") == "{\"b\":3,\"a\":2}");
  CHECK(roundTrip("{\"z\":0,\"10\":0,\"9\":0,\"01\":0,\"-1\":0,\"4294967295\":0,\"4294967294\":0}") ==
        "{\"9\":0,\"10\":0,\"4294967294\":0,\"z\":0,\"01\":0,\"-1\":0,\"4294967295\":0}");
  std::string many = "{";
  for (int i = 0; i < 100; ++i) many += "\"k" + std::to_string(i) + "\":" + std::to_string(i) + ",";
  many += "\"k7\":\"again\"}";
  const ReadResult r = readJson(many);
  REQUIRE(r.status == ReadStatus::OK);
  CHECK(r.value.keys.size() == 100);
  CHECK(r.value.get("k7")->text == "again");
  CHECK(r.value.keys[7] == "k7");
  CHECK(r.value.get("k99")->number == 99);
  CHECK(r.value.get("nope") == nullptr);
}

TEST_CASE("json: numbers keep JSON.parse's double and come back in Number#toString form") {
  CHECK(roundTrip("[1e400,-1e400,1e-400,-0,2.0,1E2,0.1,1e21,123456789012345678901234]") ==
        "[null,null,0,0,2,100,0.1,1e+21,1.2345678901234569e+23]");
  const ReadResult r = readJson("[-0, 1e400, 2.50]");
  CHECK(std::signbit(r.value.items[0].number));
  CHECK(r.value.items[1].number > 1e308);
  CHECK(r.value.items[2].text == "2.50");
}

TEST_CASE("json: strings decode to WTF-8 and write back as JSON.stringify would") {
  CHECK(roundTrip("\"\\ud83d\\ude00\"") == "\"\xf0\x9f\x98\x80\"");
  CHECK(roundTrip("\"\\ud800x\\udc00\"") == "\"\\ud800x\\udc00\"");
  CHECK(roundTrip("\"\\ud800\"", LoneSurrogates::REPLACE) == "\"\xef\xbf\xbd\"");
  CHECK(roundTrip("\"\\ud83d\\u0041\"") == "\"\\ud83dA\"");
  CHECK(roundTrip("\"\\b\\f\\n\\r\\t\\u0001\\u001F\\\"\\\\\\/\\u007f\\u2028\"") ==
        "\"\\b\\f\\n\\r\\t\\u0001\\u001f\\\"\\\\/\x7f\xe2\x80\xa8\"");
  CHECK(roundTrip(std::string("\"a\x80\xc0\xaf\"")) == "\"a\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\"");
}

TEST_CASE("json: the caps are judged on the value JSON.parse returns, bytes first") {
  const Caps caps{64, 3, 5};
  CHECK(readJson("[[[1]]]", caps).status == ReadStatus::OK);
  const ReadResult deep = readJson("[[[[1]]]]", caps);
  CHECK((deep.status == ReadStatus::LIMIT && deep.cap == CapHit::DEPTH));
  const ReadResult wide = readJson("[1,2,3,4,5]", caps);
  CHECK((wide.status == ReadStatus::LIMIT && wide.cap == CapHit::NODES));
  CHECK(readJson("{\"a\":[[[[[[1,2,3,4,5,6]]]]]],\"a\":1}", caps).status == ReadStatus::OK);
  const ReadResult big = readJson("[" + std::string(80, '1') + "]", caps);
  CHECK((big.status == ReadStatus::LIMIT && big.cap == CapHit::BYTES));
  CHECK(readJson("[[[[1]]]] x", caps).status == ReadStatus::SYNTAX);
}

TEST_CASE("json: nesting costs heap, never stack") {
  const std::string deep = std::string(200000, '[') + std::string(200000, ']');
  const ReadResult r = readJson(deep);
  REQUIRE(r.status == ReadStatus::OK);
  std::size_t depth = 0, nodes = 0;
  measure(r.value, &depth, &nodes);
  CHECK(depth == 200000);
  CHECK(nodes == 200000);
  CHECK(toJson(r.value).size() == deep.size());
  const ReadResult capped = readJson(std::string(1000000, '[') + std::string(1000000, ']'), Caps{0, 64, 0});
  CHECK(capped.cap == CapHit::DEPTH);
}

TEST_CASE("json: a document cut at every seventh byte never crashes the reader") {
  const std::string doc = "{\"reply\":\"h\\u00e9 \\ud83d\\ude00\",\"actions\":[{\"op\":\"layout\",\"lines\":"
                          "[{\"points\":[{\"x\":1.5e2,\"y\":-0}],\"locked\":true}]}],\"n\":null}";
  REQUIRE(readJson(doc).status == ReadStatus::OK);
  for (std::size_t cut = 0; cut < doc.size(); cut += 7) CHECK(readJson(doc.substr(0, cut)).status == ReadStatus::SYNTAX);
}

TEST_CASE("json: set() places a new array index as JS does") {
  Value o = Value::object();
  o.set("b", Value::num(1));
  o.set("2", Value::num(2));
  o.set("1", Value::num(3));
  o.set("b", Value::num(4));
  CHECK(toJson(o) == "{\"1\":3,\"2\":2,\"b\":4}");
  CHECK(isArrayIndex("4294967294"));
  CHECK_FALSE(isArrayIndex("4294967295"));
  CHECK_FALSE(isArrayIndex("007"));
}
