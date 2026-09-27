#pragma once
#include "jsonValue.hpp"

#include <initializer_list>
#include <string>
#include <utility>

// The one document a plan walk yields — {status, reply, actions, variants, ask, warnings,
// error} — and the builders for its warnings and errors, whose key order the JS twin
// (browser/js/llm/plan/parser.js) shares: code, the fields, then detail and message.
namespace stencil::core::opplan {

  enum class Status { VALID = 0, CHAT_ONLY = 1, INVALID = 2 };

  // An object of the given members, in order.
  json::Value fieldsOf(std::initializer_list<std::pair<const char*, json::Value>> members);

  json::Value warningOf(const char* code, const json::Value& fields, std::string message);
  json::Value errorOf(const char* code, const json::Value& fields, std::string detail, std::string message);

  struct Result {
    Status status = Status::INVALID;
    json::Value doc;
    // The document as written once, well-formed UTF-8 (a lone surrogate is U+FFFD).
    std::string text;
  };

  Result resultOf(Status status, std::string reply, json::Value actions = json::Value::array(),
                  json::Value variants = json::Value::array(), json::Value ask = json::Value(),
                  json::Value warnings = json::Value::array(), json::Value error = json::Value());

}  // namespace stencil::core::opplan
