#include "MainWindow.hpp"
#include "StyleControls.hpp"
#include <QActionGroup>
#include <QComboBox>
#include "CanvasWidget.hpp"

// Line style, filter/tint and colour-swatch appliers — the shared apply paths behind the
// toolbar controls and their context-menu twins.

namespace stencil::gui {

  // Mirrors the browser change handlers (drawingApp.js:155-178). Defaults only — never the
  // selection.
  QColor StyleControls::effectiveDefaultPointColor() const {
    const QColor c(w.settings.defaultPointColor);
    return (!w.settings.defaultPointColor.isEmpty() && c.isValid()) ? c : w.tools.lineColorValue;
  }

  void StyleControls::onLineStyleControlChanged() {
    w.canvas->setDefaults(w.settings.defaultColor, w.settings.defaultThickness,
                         w.settings.defaultPointSize, w.settings.defaultStyle,
                         w.settings.defaultPointColor);
    w.persistSettings();
  }

  // One apply path for toolbar + context-menu twins. Setting an exclusive QAction's checked state
  // emits toggled(), not triggered(), so no re-entry. Transient view state, not persisted.
  void StyleControls::setCompareModeUi(const QString& mode) {
    w.canvas->setCompareMode(mode);
    if (w.tools.compareCombo) {
      const int idx = w.tools.compareCombo->findData(mode);
      if (idx >= 0 && idx != w.tools.compareCombo->currentIndex()) {
        QSignalBlocker b(w.tools.compareCombo);
        w.tools.compareCombo->setCurrentIndex(idx);
      }
    }
    if (w.ctxMenu.compareGroup) {
      for (QAction* a : w.ctxMenu.compareGroup->actions())
        if (a->data().toString() == mode) { a->setChecked(true); break; }
    }
    w.refreshActions();   // read-only view gates the editing actions + their shortcuts
  }

  void StyleControls::applyLineStyle(const QString& style) {
    w.settings.defaultStyle = style;
    if (w.tools.lineStyle) {  // sync toolbar combo by canonical data value
      const int idx = w.tools.lineStyle->findData(style);
      if (idx >= 0) {
        QSignalBlocker b(w.tools.lineStyle);
        w.tools.lineStyle->setCurrentIndex(idx);
      }
    }
    if (w.ctxMenu.lineStyleGroup) {  // sync context-menu radio group
      for (QAction* a : w.ctxMenu.lineStyleGroup->actions())
        if (a->data().toString() == style) { a->setChecked(true); break; }
    }
    onLineStyleControlChanged();
  }
}  // namespace stencil::gui
