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
    // Chevron points toward the edge to hide (›) the panel — back at 0°, since any spin
    // from the last click ended with the panel (and this button) hidden.
    if (collapseBtn_) collapseBtn_->setIcon(themedIcon("chevron-right", iconColor, kToggleGlyph));
    // Re-theme the per-row 🗑 buttons too (new ones in showLine use the stored colour).
    iconColor_ = iconColor;
    if (points_) {
      for (int r = 0; r < points_->rowCount(); ++r)
        if (auto* b = qobject_cast<QPushButton*>(points_->cellWidget(r, ColDel)))
          b->setIcon(themedIcon("trash", iconColor_, 14));
    }
  }

  void SelectionPanel::spinCollapseChevron(qreal fromDeg, qreal toDeg, int ms) {
    if (collapseBtn_) spinIcon(collapseBtn_, "chevron-right", iconColor_, kToggleGlyph, fromDeg, toDeg, ms);
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

  // `#`, `X px`, `Y px`, `X <unit>`, `Y <unit>`, and the browser's own unnamed trailing
  // cell for the row's 🗑 (mainContent.js <thead>). The unit rides the app's setting, the
  // way drawingApp.js relabels ths[3]/ths[4] on every unit change.
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
    // Only the spanned empty row carries a baked foreground; a real row takes the table's.
    if (points_ && points_->rowCount() == 1 && points_->columnSpan(0, ColIndex) == ColCount)
      if (QTableWidgetItem* msg = points_->item(0, ColIndex))
        msg->setForeground(palette().color(QPalette::PlaceholderText));
  }

  // The browser's `<td colspan="6" class="empty-message">No points yet.</td>`: one italic,
  // muted row across the whole table, not a blank body that reads as a broken list.
  void SelectionPanel::showEmptyPoints() {
    points_->clearSpans();
    points_->setRowCount(1);
    auto* msg = new QTableWidgetItem(QStringLiteral("No points yet."));
    msg->setFlags(Qt::ItemIsEnabled);
    msg->setTextAlignment(Qt::AlignCenter);
    QFont f = msg->font();
    f.setItalic(true);
    msg->setFont(f);
    // PlaceholderText is the muted role theme.cpp maps to --text-muted (theme.cpp:
    // setColor(QPalette::PlaceholderText, p.textMuted)) — Disabled/WindowText is near the
    // background in the dark theme, which made this line invisible.
    msg->setForeground(palette().color(QPalette::PlaceholderText));
    points_->setItem(0, ColIndex, msg);
    points_->setSpan(0, ColIndex, 1, ColCount);
    points_->resizeRowsToContents();
  }

  bool SelectionPanel::eventFilter(QObject* obj, QEvent* event) {
    // The cursor left a list entirely → clear its row → canvas hover highlight.
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
      // Same key on the Lines tab removes the current line — the row's 🗑 path, and the
      // browser's focused lines-row Delete (drawingApp.js renderLinesList).
      if (isDelete && obj == lines_ && lines_->currentRow() >= 0 &&
          lines_->currentRow() < lines_->count()) {
        emit lineListRemoveRequested(lines_->currentRow());
        return true;
      }
    }
    return QDockWidget::eventFilter(obj, event);
  }
}

