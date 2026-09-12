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

  // The height this bar's content needs at its current width. QDockWidget reserves a
  // height from an early narrow width guess and never re-asks, leaving a huge amber gap
  // under a row that already fits — so the bar asserts its own height, on every resize
  // and whenever its content changes shape (below).
  void SelectedLineBar::refitHeight() {
    const int wantHeight = heightForWidth(width());
    if (wantHeight > 0 && wantHeight != height()) setFixedHeight(wantHeight);
  }

  void SelectedLineBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    refitHeight();
  }

  void SelectedLineBar::showLine(const core::Line* line) {
    // No setVisible() here: MainWindow shows/hides the whole row via its dock
    // (selectedLineDock_); this only repopulates the controls.
    updating_ = true;
    if (line) {
      currentColor_ = cssColor(line->color);
      setColorSwatch(colorSwatch_, currentColor_);
      // A line with no point colour of its own shows the colour it actually draws in
      // (its stroke), via core::pointColorOr — not a blank or stale swatch.
      currentPointColor_ = cssColor(core::pointColorOr(*line));
      setColorSwatch(pointColorSwatch_, currentPointColor_);
      thickness_->setValue(static_cast<int>(std::lround(line->thickness)));
      pointSize_->setValue(static_cast<int>(std::lround(line->pointSize)));
      const int sidx = style_->findData(QString::fromStdString(line->style));
      style_->setCurrentIndex(sidx >= 0 ? sidx : 0);

      // Fill controls only for locked areas — the whole field, its "Fill:" label too
      // (browser #sel-fill-group display:none), or a bare label is left dangling. The
      // group slides open and closed and dusts as it goes (controlReveal), rather than
      // popping and making the bar jump; its separator travels with it.
      revealControls(fillField_, line->locked);
      if (fillSep_) revealControls(fillSep_, line->locked);
      if (line->locked) {
        const QString fc = QString::fromStdString(line->fillColor);
        const bool hasFill = !fc.isEmpty() && fc != "transparent";
        // No tick to mirror: an unfilled area shows the default colour at ZERO alpha, so
        // the well says "none" and picking a colour is a single move.
        currentFill_ = hasFill ? cssColor(fc)
                               : QColor(defaultFill_.red(), defaultFill_.green(),
                                        defaultFill_.blue(), 0);
        setColorSwatch(fillSwatch_, currentFill_);
      }
    }
    updating_ = false;
    // Losing (or gaining) the fill group can cost the flow layout a whole row, and nothing
    // else re-asks. Refit now, and again once the reveal has finished shrinking it away.
    refitHeight();
    QTimer::singleShot(kControlRevealInMs + 80, this, [this] { refitHeight(); });
  }
}  // namespace stencil::gui

