// The Lines tab's colour chips: a click picks the line's colour through the Selected Line bar's
// path (preview, then one undo step), a double-click resets it to the toolbar's line colour
// (browser ui/panel/linesList.js + swatchPicker.js).
#include "WindowAssembly.hpp"
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "SelectionPanel.hpp"
#include "cssColor.hpp"
#include "modalReveal.hpp"

namespace stencil::gui {

  void WindowAssembly::wireLineSwatches() {
    const auto lineAt = [this](int idx) -> const core::Line* {
      const core::Lines& all = w.canvas->getLines();
      if (idx < 0 || idx >= static_cast<int>(all.size()) || w.canvas->compareReadOnly()) return nullptr;
      return &all[static_cast<std::size_t>(idx)];
    };
    QObject::connect(w.selPanel, &SelectionPanel::lineSwatchPick, &w, [this, lineAt](int idx) {
      const core::Line* line = lineAt(idx);
      if (!line) return;
      const QColor was = cssColor(line->color.empty() ? w.settings.defaultColor.toStdString() : line->color);
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
  }

}  // namespace stencil::gui
