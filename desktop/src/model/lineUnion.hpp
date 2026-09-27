#pragma once
#include "models.hpp"

// The desktop's seam onto core/state/lineMerge: two layouts joined into one, for the co-edit
// conflict retry, the .stencil "Merge lines" choice and a layout Combine, cut at the layout caps.
// Browser twins: mergeLines, capLayoutPoints.
namespace stencil::model {

  struct LineUnion {
    core::Lines lines;
    // A peer line none of the local ones matches: the merge brought something in.
    bool peerAdded = false;
  };

  // The peer's lines first, then each local line no earlier line matches.
  LineUnion unionLines(const core::Lines& peer, const core::Lines& local);

  // Two capped layouts joined can pass the caps: cut where they run out, as fileStore::linesFromJson
  // cuts a stored one (the crossing line keeps what the budget allows, every later one dropped).
  core::Lines capLayout(core::Lines lines);

}  // namespace stencil::model
