#pragma once
#include <string>

// Number.prototype.toString() for the op-plan result and its messages, so a number core writes
// is byte-equal to JSON.stringify's (browser/js/llm/plan/ is the twin; generated/numbers.json pins it).
namespace stencil::core::json {

  // The shortest digits that round-trip, laid out by ECMA-262 Number::toString: plain up to
  // 21 integer digits, "0.000…" down to 1e-7, else d.ddde±n. NaN and ±Infinity by name.
  std::string jsNumberToString(double x);

}  // namespace stencil::core::json
