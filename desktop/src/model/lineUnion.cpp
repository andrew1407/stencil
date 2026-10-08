#include "lineUnion.hpp"

#include "lineMerge.hpp"
#include "linesCodec.hpp"

#include <algorithm>

namespace stencil::model {

  LineUnion unionLines(const core::Lines& peer, const core::Lines& local) {
    const std::vector<bool> fresh = core::mergeKeep(local, peer);
    return {capLayout(core::mergeLines(peer, local)), std::find(fresh.begin(), fresh.end(), true) != fresh.end()};
  }

  core::Lines capLayout(core::Lines lines) {
    std::size_t budget = core::abi::MAX_LAYOUT_POINTS;
    for (std::size_t i = 0; i < lines.size(); ++i) {
      if (i >= static_cast<std::size_t>(core::abi::MAX_LAYOUT_LINES)) {
        lines.resize(i);
        break;
      }
      std::vector<core::Point>& pts = lines[i].points;
      if (pts.size() >= budget) {
        pts.resize(budget);
        lines.resize(i + 1);
        break;
      }
      budget -= pts.size();
    }
    return lines;
  }

  bool sameLines(const core::Lines& a, const core::Lines& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      const core::Line& x = a[i];
      const core::Line& y = b[i];
      if (x.color != y.color || x.pointColor != y.pointColor || x.thickness != y.thickness ||
          x.pointSize != y.pointSize || x.style != y.style || x.locked != y.locked ||
          x.fillColor != y.fillColor || x.points.size() != y.points.size())
        return false;
      for (size_t p = 0; p < x.points.size(); ++p)
        if (x.points[p].x != y.points[p].x || x.points[p].y != y.points[p].y) return false;
    }
    return true;
  }

}  // namespace stencil::model
