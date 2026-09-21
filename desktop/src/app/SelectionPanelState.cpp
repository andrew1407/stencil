#include "SelectionPanel.hpp"
#include "selectionPanelParts.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "../support/iconMotion.hpp"
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
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

  void SelectionPanel::restyleIcons(const QColor& iconColor) {
    // Back at 0° (›): the last spin ended with the panel hidden.
    if (collapseBtn) collapseBtn->setIcon(themedIcon("chevron-right", iconColor, TOGGLE_GLYPH));
    this->iconColor = iconColor;
    if (points) {
      for (int r = 0; r < points->rowCount(); ++r)
        if (auto* b = qobject_cast<QPushButton*>(points->cellWidget(r, COL_DEL)))
          b->setIcon(themedIcon("trash", this->iconColor, 14));
    }
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
    if (points && points->rowCount() == 1 && points->columnSpan(0, COL_INDEX) == COL_COUNT)
      if (QTableWidgetItem* msg = points->item(0, COL_INDEX))
        msg->setForeground(palette().color(QPalette::PlaceholderText));
  }

  // The browser's `<td colspan="6" class="empty-message">No points yet.</td>`.
  void SelectionPanel::showEmptyPoints() {
    points->clearSpans();
    points->setRowCount(1);
    auto* msg = new QTableWidgetItem(QStringLiteral("No points yet."));
    msg->setFlags(Qt::ItemIsEnabled);
    msg->setTextAlignment(Qt::AlignCenter);
    QFont f = msg->font();
    f.setItalic(true);
    msg->setFont(f);
    // PlaceholderText is the role theme.cpp maps to --text-muted; Disabled/WindowText is invisible
    // in the dark theme.
    msg->setForeground(palette().color(QPalette::PlaceholderText));
    points->setItem(0, COL_INDEX, msg);
    points->setSpan(0, COL_INDEX, 1, COL_COUNT);
    points->resizeRowsToContents();
  }

  bool SelectionPanel::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Leave) {
      if (obj == points) emit pointRowHovered(-1);
      else if (obj == lines) emit lineRowHovered(-1);
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
          lines->currentRow() < lines->count()) {
        emit lineListRemoveRequested(lines->currentRow());
        return true;
      }
    }
    return QDockWidget::eventFilter(obj, event);
  }
}

