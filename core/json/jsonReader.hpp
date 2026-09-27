#pragma once
#include "jsonValue.hpp"

#include <cstddef>
#include <string_view>

// A strict RFC 8259 reader with JSON.parse's semantics (the op-plan twin is JSON.parse in
// browser/js/llm/plan/parser.js). Iterative: nesting costs heap, never stack, so any depth is
// read safely inside the 64 KB wasm stack. Never throws; ill-formed UTF-8 reads as U+FFFD.
namespace stencil::core::json {

  // The §1 caps (opRegistry.json limits.json); 0 disables one.
  struct Caps {
    std::size_t bytes = 0;
    std::size_t depth = 0;
    std::size_t nodes = 0;
  };

  enum class ReadStatus { OK, SYNTAX, LIMIT };
  enum class CapHit { NONE, BYTES, DEPTH, NODES };

  struct ReadResult {
    ReadStatus status = ReadStatus::SYNTAX;
    // Checked once the text parses, as the JS twin does: bytes, then depth, then values —
    // measured on the value JSON.parse would return, duplicates already dropped.
    CapHit cap = CapHit::NONE;
    Value value;
  };

  ReadResult readJson(std::string_view text, const Caps& caps = {});

  // Container depth (the root container is 1, scalars add none) and value count of a tree,
  // walked without recursion — the numbers the caps judge.
  void measure(const Value& v, std::size_t* depth, std::size_t* nodes);

}  // namespace stencil::core::json
