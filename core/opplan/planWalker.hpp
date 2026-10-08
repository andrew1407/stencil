#pragma once
#include "jsonValue.hpp"
#include "planResult.hpp"
#include "planSchema.hpp"

#include <cstddef>
#include <optional>
#include <string>

// The walk over a parsed plan's three slots — actions, variants, the §11 card — for planWalk.
// Twin of walkActions / walkVariants / walkAsk in browser/js/llm/plan/parser.js. A method
// returns false once the plan has failed, leaving the failure in `error`.
namespace stencil::core::opplan {

  // Inside a variant or an ask preview: a top-level op there is misplaced, not fatal.
  struct Nested {
    const char* scope;
    std::size_t index;
    const char* reason;
  };

  struct Walker {
    explicit Walker(const Schema& schema) : s(schema) {}

    const Schema& s;
    json::Value warnings = json::Value::array();
    std::optional<json::Value> error;

    // `misplaced` gets the entry of a top-level op found inside a `nested` list.
    bool actions(const json::Value* list, json::Value& out, const Nested* nested, const Entry** misplaced,
                 std::string* reason);
    bool variants(const json::Value* raw, json::Value& out);
    bool ask(const json::Value* raw, json::Value& out);

   private:
    bool accept(const json::Value& a, const Entry& entry, json::Value& out);
    bool fail(const char* code, const json::Value& fields, std::string detail, std::string message);
    bool planFail(const json::Value& fields, std::string detail);
  };

}  // namespace stencil::core::opplan
