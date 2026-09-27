#pragma once
#include "jsonValue.hpp"
#include "planChecks.hpp"
#include "planSchema.hpp"

// The native rules: an entry's `rules` (cropAspectFold, run before its key checks) and the
// registry's `surfaceRules` (run on the normalized action). Twin of RULES / SURFACE_RULES in
// browser/js/llm/plan/opSchemaBase.js.
namespace stencil::core::opplan {

  // §3.2: "aspect" beside "spec" folds into a spec without one; a conflicting duplicate fails.
  Why cropAspectFold(json::Value& action);

  // This surface's rules for `op` over the normalized action; the failure is the row's message.
  Why surfaceRules(const Schema& s, const std::string& op, const json::Value& out);

}  // namespace stencil::core::opplan
