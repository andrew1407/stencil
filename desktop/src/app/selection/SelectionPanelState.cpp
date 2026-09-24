#include "SelectionPanel.hpp"
#include "selectionPanelParts.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../../support/theme/themeTokens.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/icon/iconMotion.hpp"
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QTabWidget>
#include <QPainter>
#include <QPalette>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

namespace stencil::gui {

  void SelectionPanel::setMultiSelectCount(int n) {
    if (!multiLabel) return;
    if (n >= 2) {
      multiLabel->setText(QString("%1 lines selected — Ctrl+Shift+click to add/remove · "
                                   "Alt+Shift+drag to move all · Ctrl+Shift+scroll to rotate all · "
                                   "Alt+Shift+arrows to flip / rotate 90°")
                               .arg(n));
      multiLabel->setVisible(true);
    } else {
      multiLabel->setVisible(false);
    }
  }

  void SelectionPanel::restyleIcons(const QColor& iconColor, const QColor& binColor) {
    // Back at 0° (›): the last spin ended with the panel hidden.
    if (collapseBtn) collapseBtn->setIcon(themedIcon("chevron-right", iconColor, TOGGLE_GLYPH));
    this->iconColor = iconColor;
    this->binColor = binColor.isValid() ? binColor : iconColor;
    // Both tabs' rows, built before a skin switch as often as after it.
    for (QTableWidget* table : {points, lines})
      if (table)
        for (QPushButton* b : table->findChildren<QPushButton*>(QStringLiteral("pointDelBtn")))
          b->setIcon(themedIcon("trash", this->binColor, 14));
    if (lines)
      for (QLabel* chip : lines->findChildren<QLabel*>(QStringLiteral("linesSwatch")))
        chip->setStyleSheet(swatchSheet(chip->property("face").toString(), chip->property("rim").toString()));
  }

  void SelectionPanel::setCollapseChevronVisible(bool on) { if (collapseBtn) collapseBtn->setVisible(on); }

  void SelectionPanel::spinCollapseChevron(qreal fromDeg, qreal toDeg, int ms) {
    if (collapseBtn) spinIcon(collapseBtn, "chevron-right", iconColor, TOGGLE_GLYPH, fromDeg, toDeg, ms);
  }

  void SelectionPanel::showEvent(QShowEvent* event) {
    QDockWidget::showEvent(event);
    spinCollapseChevron(0, 0, 0);
  }

  void SelectionPanel::setToggleHint(const QString& hint) {
    if (!collapseBtn) return;
    collapseBtn->setToolTip(hint.isEmpty() ? QStringLiteral("Hide panel")
                                            : QStringLiteral("Hide panel (%1)").arg(hint));
  }

  // The browser's <thead> (mainContent.js), the unit relabelled like drawingApp.js ths[3]/ths[4].
  void SelectionPanel::applyUnitHeaders() {
    if (!points) return;
    points->setHorizontalHeaderLabels({"#", "X px", "Y px",
                                        QStringLiteral("X %1").arg(unitLabel),
                                        QStringLiteral("Y %1").arg(unitLabel), QString()});
  }

  void SelectionPanel::setUnitLabel(const QString& label) {
    if (label.isEmpty() || label == unitLabel) return;
    unitLabel = label;
    applyUnitHeaders();
  }

  void SelectionPanel::changeEvent(QEvent* event) {
    QDockWidget::changeEvent(event);
    if (event->type() != QEvent::PaletteChange && event->type() != QEvent::StyleChange) return;
    for (QTableWidget* t : {points, lines})
      if (isEmptyRow(t, 0))
        if (QTableWidgetItem* msg = t->item(0, 0)) msg->setForeground(palette().color(QPalette::PlaceholderText));
  }

  bool SelectionPanel::isEmptyRow(const QTableWidget* t, int row) {
    return t && row == 0 && t->rowCount() == 1 && t->columnSpan(0, 0) > 1;
  }

  QBrush SelectionPanel::emptyWash() const {
    if (support::isWebcore()) return rowWash(false);
    return themeToken("--bg-coord-hover", palette().color(QPalette::Window).lightness() < 128);
  }

  QTableWidgetItem* SelectionPanel::emptyMessage(const QString& text) const {
    auto* msg = new QTableWidgetItem(text);
    msg->setFlags(Qt::ItemIsEnabled);
    msg->setTextAlignment(Qt::AlignCenter);
    QFont f = msg->font();
    f.setItalic(true);
    msg->setFont(f);
    // PlaceholderText is the role theme.cpp maps to --text-muted; Disabled/WindowText is invisible
    // in the dark theme.
    msg->setForeground(palette().color(QPalette::PlaceholderText));
    return msg;
  }

  // The browser's `<td colspan="6" class="empty-message">No points yet.</td>`.
  void SelectionPanel::showEmptyPoints() {
    points->clearSpans();
    points->setRowCount(1);
    points->setItem(0, COL_INDEX, emptyMessage(QStringLiteral("No points yet.")));
    points->setSpan(0, COL_INDEX, 1, COL_COUNT);
    fitTableRows(points);
    // The message stands alone, as the Lines tab's does: column headings over nothing read as a
    // table that failed to load (browser .coordinates-table:has(.empty-message) thead).
    points->horizontalHeader()->hide();
    points->setShowGrid(false);   // a lone message has no cells to divide
  }

  bool SelectionPanel::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Leave) {
      if (obj == points) {
        setCanvasHover(-1, canvasHoverLineRow);
        emit pointRowHovered(-1);
      } else if (obj == lines) {
        setCanvasHover(canvasHoverPointRow, -1);
        emit lineRowHovered(-1);
      }
    }
    if (event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      const bool isDelete =
          ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace;
      if (isDelete && obj == points && points->currentRow() >= 0) {
        emit pointDeleteRequested(points->currentRow());
        return true;
      }
      // Same key on the Lines tab removes the current line (browser focused lines-row Delete).
      if (isDelete && obj == lines && lines->currentRow() >= 0 &&
          lines->currentRow() < lines->rowCount()) {
        emit lineListRemoveRequested(lines->currentRow());
        return true;
      }
    }
    return QDockWidget::eventFilter(obj, event);
  }
}

