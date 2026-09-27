#pragma once
#include "jsonValue.hpp"
#include "planPath.hpp"
#include "planSchema.hpp"

#include <optional>
#include <string>

// The key-spec checks, in the order of browser/js/llm/plan/opSchema.js: type, then caps, enums,
// ranges and grammars; an object defers to planFields. A failure is the model's fault and
// comes back as its message (JS SchemaError's text) — nothing here throws.
namespace stencil::core::opplan {

  using Why = std::optional<std::string>;

  // `parent` is the object holding `v` (a regexBy grammar reads a sibling), or null.
  Why checkValue(const Schema& s, const json::Value& v, const json::Value& spec, const Path& path,
                 const json::Value* parent);

  // JS truthiness of an optional registry value.
  bool truthy(const json::Value* v);

}  // namespace stencil::core::opplan
