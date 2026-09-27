#pragma once
#include "strokeGrowth.hpp"

#include <QElapsedTimer>
#include <algorithm>
#include <vector>

// What the live view paints over a line and an export never does: the pointer's hover, the
// selection and its focused point, and a vertex still in flight. CanvasScene's paint path reads it.
namespace stencil::gui {

  // Hover under the cursor (-1 = none; lineIdx -1 with a valid pointIdx = the in-progress line),
  // and from the panel lists (browser hoveredPtIdx / listHoverLineIdx).
  struct HoverMarks {
    int lineIdx = -1;
    int pointIdx = -1;
    int overLineIdx = -1;
    int listPointIdx = -1;
    int listLineIdx = -1;
  };

  struct LiveMarks {
    std::vector<int> selected;   // ascending
    const core::Line* panelLine = nullptr;
    int selectedPoint = -1;
    HoverMarks hover;
    const stroke::Fx* flights = nullptr;
    const QElapsedTimer* clock = nullptr;

    bool isSelected(int i) const { return std::binary_search(selected.begin(), selected.end(), i); }
    // ms on the shared clock. Every pass takes it here so they all agree on one instant.
    double now() const { return static_cast<double>(clock->elapsed()); }
  };

}  // namespace stencil::gui
