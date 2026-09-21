#include "SelectedLineBar.hpp"

#include "controlReveal.hpp"
#include "cssColor.hpp"
#include "../support/FlowLayout.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/iconSet.hpp"
#include "../support/modalReveal.hpp"
#include "../support/numericInput.hpp"
#include "../support/SearchCombo.hpp"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QSize>
#include <QTimer>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

namespace stencil::gui {

  void SelectedLineBar::restyleIcons(const QColor& iconColor) {
    if (fillClear) fillClear->setIcon(themedIcon("rect", iconColor, 13));
    if (deselectBtn) deselectBtn->setIcon(themedIcon("x", QColor("#ffffff"), 13));
    if (unchainBtn) unchainBtn->setIcon(themedIcon("link", iconColor, 13));
  }

  void SelectedLineBar::setDefaultFillColor(const QColor& color) { defaultFill = color; }

  // QDockWidget reserves a height from an early narrow width guess and never re-asks, so the bar
  // asserts its own on every resize and content change.
  void SelectedLineBar::refitHeight() {
    const int wantHeight = heightForWidth(width());
    if (wantHeight > 0 && wantHeight != height()) setFixedHeight(wantHeight);
  }

  void SelectedLineBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    refitHeight();
  }

  void SelectedLineBar::showLine(const core::Line* line) {
    // MainWindow shows/hides the whole row via its dock; this only repopulates.
    updating = true;
    if (line) {
      currentColor = cssColor(line->color);
      setColorSwatch(colorSwatch, currentColor);
      // A line with no point colour shows its stroke (core::pointColorOr).
      currentPointColor = cssColor(core::pointColorOr(*line));
      setColorSwatch(pointColorSwatch, currentPointColor);
      thickness->setValue(static_cast<int>(std::lround(line->thickness)));
      pointSize->setValue(static_cast<int>(std::lround(line->pointSize)));
      const int sidx = style->findData(QString::fromStdString(line->style));
      style->setCurrentIndex(sidx >= 0 ? sidx : 0);

      // Fill controls only for locked areas, label included (browser #sel-fill-group
      // display:none); the group slides and dusts (controlReveal).
      revealControls(fillField, line->locked);
      if (fillSep) revealControls(fillSep, line->locked);
      if (line->locked) {
        const QString fc = QString::fromStdString(line->fillColor);
        const bool hasFill = !fc.isEmpty() && fc != "transparent";
        // An unfilled area shows the default colour at zero alpha, so the well says "none".
        currentFill = hasFill ? cssColor(fc)
                               : QColor(defaultFill.red(), defaultFill.green(),
                                        defaultFill.blue(), 0);
        setColorSwatch(fillSwatch, currentFill);
      }
    }
    updating = false;
    // Losing the fill group can cost the flow layout a row; refit now and once the reveal has
    // finished.
    refitHeight();
    QTimer::singleShot(CONTROL_REVEAL_IN_MS + 80, this, [this] { refitHeight(); });
  }
}  // namespace stencil::gui

