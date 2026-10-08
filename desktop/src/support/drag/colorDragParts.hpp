#pragma once
// The colour drag's private parts, shared by colorDrag.cpp (the drag, the chips) and colorDragCells.cpp
// (a table of swatch cells). Browser twin: browser/js/ui/drag/colorDragSwatches.js.
#include "colorDrag.hpp"
#include "iconDrag.hpp"
#include <QHash>
#include <QPointer>
#include <QTableWidget>
#include <optional>

namespace stencil::support::colorDragParts {

  // A swatch where the pointer is: a chip, or a cell of a table of swatches, which `glow` lights.
  struct Spot {
    QPointer<QWidget> glow;
    ColorSwatch swatch;
    bool live() const {
      return glow && glow->isVisible() && !glow->property(PAINTED_OUT_PROPERTY).toBool() && glow->isEnabled() && (!swatch.enabled || swatch.enabled()) &&
             swatch.read && swatch.read().isValid();
    }
  };

  struct Cells {
    QPointer<QTableWidget> table;
    std::function<ColorSwatch(int, int)> at;
  };

  QHash<QWidget*, Cells>& tables();   // keyed by the table's viewport, where its presses land
  std::optional<Spot> cellSpot(const Cells& cells, int row, int column);
  // The drag hooks of one source: `lift` answers the swatch the press took hold of.
  IconDragHooks colorHooks(std::function<std::optional<Spot>()> lift, QWidget* owner);

}  // namespace stencil::support::colorDragParts
