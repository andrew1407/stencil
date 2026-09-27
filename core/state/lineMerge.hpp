#pragma once
#include "models.hpp"

#include <string>
#include <vector>

// The co-edit union merge: the peer's lines first, then each local line no earlier line
// matches. Port of mergeLines / lineDedupeKey in browser/js/core/layout.js.
namespace stencil::core {

  // color|pointColor|thickness|pointSize|style|locked|fillColor|x,y;x,y… with every number as
  // its exact bits: keys match exactly when the JS String(n) keys do, at full precision.
  std::string lineDedupeKey(const Line& line);

  // keep[i]: whether local line i joins the merge.
  std::vector<bool> mergeKeep(const Lines& server, const Lines& local);

  Lines mergeLines(const Lines& server, const Lines& local);

}
