#pragma once
#include "jsonValue.hpp"

#include <cstddef>
#include <string>
#include <string_view>

// Where a checked value sits, for a message: `"x1" in spec`, `"label" in ask.options[2]`.
// Twin of where/label/child/item in browser/js/llm/plan/opSchemaBase.js; "" stands for JS null.
namespace stencil::core::opplan {

  struct Path {
    std::string root;
    std::string key;
    std::string container;
  };

  std::string where(const Path& p);
  std::string label(const Path& p);
  // `parent` null = a top-level action's own keys.
  Path child(const Path* parent, std::string_view key);
  Path item(const Path& p, std::size_t i);

  // String(x) for a registry value, and the `"a", "b"` / `1, 2` lists messages quote.
  std::string jsString(const json::Value& v);
  std::string quoteOne(const json::Value& v);
  std::string quoteList(const json::Value& list);

  // Array.prototype.includes (SameValueZero) of a value in a registry list; absent never is.
  bool includes(const json::Value* list, const json::Value* v);

}  // namespace stencil::core::opplan
