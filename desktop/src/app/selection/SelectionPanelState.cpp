#include "SelectionPanel.hpp"
#include "linesTableParts.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../../support/theme/themeTokens.hpp"
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QTableWidget>
#include <QPushButton>
#include <QToolButton>
#include <QShowEvent>
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
      for (QPushButton* eye : lines->findChildren<QPushButton*>(QStringLiteral("linesEyeBtn")))
        static_cast<LineEyeButton*>(eye)->restyle();   // the name is only ever a LineEyeButton's
    // The chips paint their corners by the skin, so a switch repaints them.
    if (lines)
      for (const char* name : {"linesSwatch", "linesPointSwatch"})
        for (QWidget* chip : lines->findChildren<QWidget*>(QLatin1String(name))) chip->update();
  }

  void SelectionPanel::setCollapseChevronVisible(bool on) { if (collapseBtn) collapseBtn->setVisible(on); }

  QWidget* SelectionPanel::lineSwatchCell(int index) const {
    return index >= 0 && index < lines->rowCount() ? lines->cellWidget(index, LCOL_COLOR) : nullptr;
  }

  QWidget* SelectionPanel::pointSwatchCell(int index) const {
    return index >= 0 && index < lines->rowCount() ? lines->cellWidget(index, LCOL_POINT) : nullptr;
  }

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

  void SelectionPanel::cellCursor(QTableWidget* t, int row, int col) const {
    const QTableWidgetItem* it = t->item(row, col);
    const bool edits = it && (it->flags() & Qt::ItemIsEditable) && !(readOnly && readOnly());
    t->viewport()->setCursor(isEmptyRow(t, row) ? Qt::ArrowCursor : edits ? Qt::IBeamCursor : Qt::PointingHandCursor);
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
    // Browser coordTable.js / lines/events.js: Delete or Backspace, whatever the modifiers, on a focused
    // row removes that row's point or line, and no window shortcut ever sees the key.
    const QEvent::Type t = event->type();
    auto* table = obj == points ? points : obj == lines ? lines : nullptr;
    if (table && (t == QEvent::KeyPress || t == QEvent::ShortcutOverride)) {
      const int key = static_cast<QKeyEvent*>(event)->key();
      const int row = table->currentRow();
      // The empty state's message row spans the table, and is no row to remove.
      const bool onRow = row >= 0 && row < table->rowCount() && table->columnSpan(row, 0) == 1;
      if ((key == Qt::Key_Delete || key == Qt::Key_Backspace) && onRow && !(readOnly && readOnly())) {
        event->accept();   // an accepted override keeps the key from the window's shortcuts
        if (t == QEvent::ShortcutOverride) return true;
        if (table == lines) {
          emit lineListRemoveRequested(row);   // the repopulate keeps the current row on its slot
          return true;
        }
        emit pointDeleteRequested(row);
        // …and focus moves to the row that took its place (coordTable.js focusRowAfterRemoval).
        const int n = points->rowCount();
        if (n > 0 && points->columnSpan(0, 0) == 1) {
          const QModelIndex next = points->model()->index(std::min(row, n - 1), COL_X);
          points->selectionModel()->setCurrentIndex(next, QItemSelectionModel::NoUpdate);
        }
        return true;
      }
    }
    return QDockWidget::eventFilter(obj, event);
  }
}

