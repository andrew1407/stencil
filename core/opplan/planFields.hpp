#pragma once
#include "jsonValue.hpp"
#include "planChecks.hpp"
#include "planPath.hpp"
#include "planSchema.hpp"

#include <string_view>

// One object against a key map and its holder's presence rules (forms / together / exclusive /
// minFields / onlyWith / requiredWith), and the normalizing pick of the declared keys. Twin of
// checkFields in browser/js/llm/plan/opSchema.js and pickFields in opSchemaBase.js.
namespace stencil::core::opplan {

  // `skip` names the one key that is neither declared nor unknown (an action's "op"), or "".
  Why checkFields(const Schema& s, const json::Value& obj, const json::Value& fields,
                  const json::Value& holder, const Path* path, std::string_view skip);

  // The declared keys present (deep-picked, trims applied) plus the registry defaults.
  json::Value pickFields(const json::Value& obj, const json::Value& fields);

}  // namespace stencil::core::opplan
