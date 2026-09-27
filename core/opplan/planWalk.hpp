#pragma once
#include "planResult.hpp"
#include "planSchema.hpp"

#include <string_view>

// A model reply → the plan result, llm-contract.md §1 end to end: fences stripped, the first
// balanced object taken, the JSON caps, strict validation, the variant/preview drops, the
// reply tolerance. The twin of walkPlan in browser/js/llm/plan/parser.js. Never throws.
namespace stencil::core::opplan {

  // `text` is the reply's bytes; ill-formed UTF-8 reads as U+FFFD, as TextDecoder would.
  Result walkPlan(const Schema& s, std::string_view text);

}  // namespace stencil::core::opplan
