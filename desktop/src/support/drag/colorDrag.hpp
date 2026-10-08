#pragma once
// Dragging one colour swatch onto another copies its colour: past the press slop a chip of the colour
// rides beside the pointer, every swatch it can land on glows (the one under it brighter), and the one
// it is released on takes it through its own pick path. Browser twin: browser/js/ui/drag/colorDrag.js
// over colorDragSwatches.js.
#include <QColor>
#include <functional>
#include <utility>

class QAbstractButton;
class QTableWidget;

namespace stencil::support {

  // Set true on a control painted out but keeping its slot (the browser's `visibility: hidden`):
  // no drop lands on what the user cannot see.
  inline constexpr char PAINTED_OUT_PROPERTY[] = "stencilPaintedOut";

  struct ColorSwatch {
    ColorSwatch() = default;
    ColorSwatch(std::function<QColor()> reader, std::function<void(const QColor&)> applier,
                bool withAlpha = false, std::function<bool()> isEnabled = {})
        : read(std::move(reader)), apply(std::move(applier)), alpha(withAlpha), enabled(std::move(isEnabled)) {}

    std::function<QColor()> read;               // the colour it shows; invalid when it has none
    std::function<void(const QColor&)> apply;   // its own pick path, as a picked colour lands
    bool alpha = false;                         // carries an alpha byte (a well with its opacity box)
    std::function<bool()> enabled;              // empty: always
  };

  // What `target` takes from `source`: its RGBA when both carry alpha, else its RGB over the target's
  // own alpha; invalid when the source shows none or the target already shows it (alpha 0 shows none).
  QColor colorToApply(const ColorSwatch& source, const ColorSwatch& target);

  void installColorDrag(QAbstractButton* chip, ColorSwatch swatch);
  // A table whose cells hold swatches: `at` answers a cell's (no `read` where it holds none), and
  // the cell's own widget is what glows.
  void installColorDragCells(QTableWidget* table, std::function<ColorSwatch(int row, int column)> at);

}  // namespace stencil::support
