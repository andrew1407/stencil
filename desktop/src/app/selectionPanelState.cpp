#include "selectionPanel.hpp"
#include "selectionPanelParts.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../support/disintegrateOverlay.hpp"
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
    if (!multiLabel_) return;
    if (n >= 2) {
      multiLabel_->setText(QString("%1 lines selected — Ctrl+Shift+click to add/remove · "
                                   "Alt+Shift+drag to move all · Ctrl+Shift+scroll to rotate all · "
                                   "Alt+Shift+arrows to flip / rotate 90°")
                               .arg(n));
      multiLabel_->setVisible(true);
    } else {
      multiLabel_->setVisible(false);
    }
  }

  void SelectionPanel::restyleIcons(const QColor& iconColor) {
    // Back at 0° (›): the last spin ended with the panel hidden.
    if (collapseBtn_) collapseBtn_->setIcon(themedIcon("chevron-right", iconColor, TOGGLE_GLYPH));
    iconColor_ = iconColor;
    if (points_) {
      for (int r = 0; r < points_->rowCount(); ++r)
        if (auto* b = qobject_cast<QPushButton*>(points_->cellWidget(r, ColDel)))
          b->setIcon(themedIcon("trash", iconColor_, 14));
    }
  }

  void SelectionPanel::spinCollapseChevron(qreal fromDeg, qreal toDeg, int ms) {
    if (collapseBtn_) spinIcon(collapseBtn_, "chevron-right", iconColor_, TOGGLE_GLYPH, fromDeg, toDeg, ms);
  }

  void SelectionPanel::showEvent(QShowEvent* event) {
    QDockWidget::showEvent(event);
    spinCollapseChevron(0, 0, 0);
  }

  void SelectionPanel::setToggleHint(const QString& hint) {
    if (!collapseBtn_) return;
    collapseBtn_->setToolTip(hint.isEmpty() ? QStringLiteral("Hide panel")
                                            : QStringLiteral("Hide panel (%1)").arg(hint));
  }

  // The browser's <thead> (mainContent.js), the unit relabelled like drawingApp.js ths[3]/ths[4].
  void SelectionPanel::applyUnitHeaders() {
    if (!points_) return;
    points_->setHorizontalHeaderLabels({"#", "X px", "Y px",
                                        QStringLiteral("X %1").arg(unitLabel_),
                                        QStringLiteral("Y %1").arg(unitLabel_), QString()});
  }

  void SelectionPanel::setUnitLabel(const QString& label) {
    if (label.isEmpty() || label == unitLabel_) return;
    unitLabel_ = label;
    applyUnitHeaders();
  }

  void SelectionPanel::changeEvent(QEvent* event) {
    QDockWidget::changeEvent(event);
    if (event->type() != QEvent::PaletteChange && event->type() != QEvent::StyleChange) return;
    if (points_ && points_->rowCount() == 1 && points_->columnSpan(0, ColIndex) == ColCount)
      if (QTableWidgetItem* msg = points_->item(0, ColIndex))
        msg->setForeground(palette().color(QPalette::PlaceholderText));
  }

  // The browser's `<td colspan="6" class="empty-message">No points yet.</td>`.
  void SelectionPanel::showEmptyPoints() {
    points_->clearSpans();
    points_->setRowCount(1);
    auto* msg = new QTableWidgetItem(QStringLiteral("No points yet."));
    msg->setFlags(Qt::ItemIsEnabled);
    msg->setTextAlignment(Qt::AlignCenter);
    QFont f = msg->font();
    f.setItalic(true);
    msg->setFont(f);
    // PlaceholderText is the role theme.cpp maps to --text-muted; Disabled/WindowText is invisible
    // in the dark theme.
    msg->setForeground(palette().color(QPalette::PlaceholderText));
    points_->setItem(0, ColIndex, msg);
    points_->setSpan(0, ColIndex, 1, ColCount);
    points_->resizeRowsToContents();
  }

  bool SelectionPanel::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Leave) {
      if (obj == points_) emit pointRowHovered(-1);
      else if (obj == lines_) emit lineRowHovered(-1);
    }
    if (event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      const bool isDelete =
          ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace;
      if (isDelete && obj == points_ && points_->currentRow() >= 0) {
        emit pointDeleteRequested(points_->currentRow());
        return true;
      }
      // Same key on the Lines tab removes the current line (browser focused lines-row Delete).
      if (isDelete && obj == lines_ && lines_->currentRow() >= 0 &&
          lines_->currentRow() < lines_->count()) {
        emit lineListRemoveRequested(lines_->currentRow());
        return true;
      }
    }
    return QDockWidget::eventFilter(obj, event);
  }
}

