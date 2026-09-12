#include "selectedLineBar.hpp"

#include "controlReveal.hpp"
#include "cssColor.hpp"
#include "../support/flowLayout.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/iconSet.hpp"
#include "../support/modalReveal.hpp"
#include "../support/numericInput.hpp"
#include "../support/searchCombo.hpp"
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
    if (fillClear_) fillClear_->setIcon(themedIcon("rect", iconColor, 13));
    if (deselectBtn_) deselectBtn_->setIcon(themedIcon("x", QColor("#ffffff"), 13));
    if (unchainBtn_) unchainBtn_->setIcon(themedIcon("link", iconColor, 13));
  }

  void SelectedLineBar::setDefaultFillColor(const QColor& color) { defaultFill_ = color; }

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
    updating_ = true;
    if (line) {
      currentColor_ = cssColor(line->color);
      setColorSwatch(colorSwatch_, currentColor_);
      // A line with no point colour shows its stroke (core::pointColorOr).
      currentPointColor_ = cssColor(core::pointColorOr(*line));
      setColorSwatch(pointColorSwatch_, currentPointColor_);
      thickness_->setValue(static_cast<int>(std::lround(line->thickness)));
      pointSize_->setValue(static_cast<int>(std::lround(line->pointSize)));
      const int sidx = style_->findData(QString::fromStdString(line->style));
      style_->setCurrentIndex(sidx >= 0 ? sidx : 0);

      // Fill controls only for locked areas, label included (browser #sel-fill-group
      // display:none); the group slides and dusts (controlReveal).
      revealControls(fillField_, line->locked);
      if (fillSep_) revealControls(fillSep_, line->locked);
      if (line->locked) {
        const QString fc = QString::fromStdString(line->fillColor);
        const bool hasFill = !fc.isEmpty() && fc != "transparent";
        // An unfilled area shows the default colour at zero alpha, so the well says "none".
        currentFill_ = hasFill ? cssColor(fc)
                               : QColor(defaultFill_.red(), defaultFill_.green(),
                                        defaultFill_.blue(), 0);
        setColorSwatch(fillSwatch_, currentFill_);
      }
    }
    updating_ = false;
    // Losing the fill group can cost the flow layout a row; refit now and once the reveal has
    // finished.
    refitHeight();
    QTimer::singleShot(kControlRevealInMs + 80, this, [this] { refitHeight(); });
  }
}  // namespace stencil::gui

