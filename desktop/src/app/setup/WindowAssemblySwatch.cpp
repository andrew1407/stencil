// The Lines tab's row edits: the line chip picks the line's colour through the Selected Line bar's
// path (preview, then one undo step) and its double-click resets it to the toolbar's; the point chip
// and a typed size edit that row's own line, the selection kept; a colour dragged onto a chip lands
// as its pick (browser ui/panel/lines/events.js + lines/swatchPicker.js).
#include "WindowAssembly.hpp"
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "SelectionPanel.hpp"
#include "cssColor.hpp"
#include "modalReveal.hpp"
#include "colorDrag.hpp"
#include <QTableWidget>

namespace stencil::gui {

  void WindowAssembly::wireLineRows() {
    const auto lineAt = [this](int idx) -> const core::Line* {
      const core::Lines& all = w.canvas->getLines();
      if (idx < 0 || idx >= static_cast<int>(all.size()) || w.canvas->compareReadOnly()) return nullptr;
      return &all[static_cast<std::size_t>(idx)];
    };
    const auto shown = [this](const std::string& css) {
      return cssColor(css.empty() ? w.settings.defaultColor.toStdString() : css);
    };
    QObject::connect(w.selPanel, &SelectionPanel::lineSwatchPick, &w, [this, lineAt, shown](int idx) {
      const core::Line* line = lineAt(idx);
      if (!line) return;
      const QColor was = shown(line->color);
      w.canvas->selectLineByIndex(idx);
      const QColor c = support::pickColorAnimated(
          was, &w, "Line color", w.selPanel->lineSwatchCell(idx), QRect(),
          [this](const QColor& p) { w.canvas->setSelectedLineColor(cssName(p), true); }, /*withAlpha=*/true);
      if (c.isValid()) w.canvas->setSelectedLineColor(cssName(c));
    });
    QObject::connect(w.selPanel, &SelectionPanel::lineSwatchReset, &w, [this, lineAt](int idx) {
      if (!lineAt(idx)) return;
      w.canvas->selectLineByIndex(idx);
      w.canvas->setSelectedLineColor(w.settings.defaultColor);
    });
    // Its reset leaves the points no colour of their own, so they draw in the line's.
    QObject::connect(w.selPanel, &SelectionPanel::pointSwatchPick, &w, [this, lineAt, shown](int idx) {
      const core::Line* line = lineAt(idx);
      if (!line) return;
      const QColor c = support::pickColorAnimated(
          shown(core::pointColorOr(*line)), &w, "Point color", w.selPanel->pointSwatchCell(idx), QRect(),
          [this, idx](const QColor& p) { w.canvas->setSelectedLinePointColor(cssName(p), true, idx); },
          /*withAlpha=*/true);
      if (c.isValid()) w.canvas->setSelectedLinePointColor(cssName(c), false, idx);
    });
    QObject::connect(w.selPanel, &SelectionPanel::pointSwatchReset, &w, [this, lineAt](int idx) {
      if (lineAt(idx)) w.canvas->setSelectedLinePointColor(QString(), false, idx);
    });
    QObject::connect(w.selPanel, &SelectionPanel::lineThicknessEdited, &w,
                     [this](int idx, int v) { w.canvas->setSelectedLineThickness(v, idx); });
    QObject::connect(w.selPanel, &SelectionPanel::linePointSizeEdited, &w,
                     [this](int idx, int v) { w.canvas->setSelectedLinePointSize(v, idx); });
    // A row's chip is a colour-drag swatch: its line's or its points' own colour, which a drop sets
    // as a pick does.
    QTableWidget* table = w.selPanel->linesTable();
    support::installColorDragCells(table, [this, lineAt, shown, table](int row, int column) {
      const QWidget* cell = table->cellWidget(row, column);
      const bool point = cell && cell->findChild<QWidget*>(QStringLiteral("linesPointSwatch"));
      if (!cell || (!point && !cell->findChild<QWidget*>(QStringLiteral("linesSwatch")))) return support::ColorSwatch{};
      const auto read = [lineAt, shown, row, point] {
        const core::Line* line = lineAt(row);
        return line ? shown(point ? core::pointColorOr(*line) : line->color) : QColor();
      };
      return support::ColorSwatch{read, [this, lineAt, row, point](const QColor& c) {
                                    if (!lineAt(row)) return;
                                    if (point) { w.canvas->setSelectedLinePointColor(cssName(c), false, row); return; }
                                    w.canvas->selectLineByIndex(row);
                                    w.canvas->setSelectedLineColor(cssName(c));
                                  }, /*alpha=*/true};
    });
  }

}  // namespace stencil::gui
