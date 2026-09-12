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

  void SelectionPanel::setLines(const core::Lines& lines,
                                const std::vector<int>& selected) {
    if (!lines_) return;
    QSignalBlocker block(lines_);
    // clear() drops the current row, so a keyboard delete (which repopulates the list)
    // would lose its target and the next Delete would do nothing. Carry it across, clamped
    // to the new count — the browser re-focuses the equivalent row for the same reason.
    const int prevCurrent = lines_->currentRow();
    lines_->clear();
    linesSelected_ = selected;   // styleLineRow's selection snapshot
    canvasHoverPointRow_ = -1;   // rebuilt rows carry no stale hover tint
    canvasHoverLineRow_ = -1;
    if (lines.empty()) {
      auto* item = new QListWidgetItem("No lines yet.", lines_);
      item->setFlags(Qt::NoItemFlags);
      item->setTextAlignment(Qt::AlignCenter);
      return;
    }
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
      const core::Line& ln = lines[i];
      auto* item = new QListWidgetItem(lines_);

      auto* row = new QWidget(lines_);
      auto* rl = new QHBoxLayout(row);
      rl->setContentsMargins(6, 4, 6, 4);
      rl->setSpacing(8);

      // Color chip — transparent for an unfilled locked area (matches the browser swatch).
      auto* swatch = new QLabel(row);
      swatch->setFixedSize(14, 14);
      swatch->setAttribute(Qt::WA_TransparentForMouseEvents);
      const QString colName = QString::fromStdString(ln.color);
      const bool unfilledArea =
          ln.locked && (ln.fillColor.empty() || ln.fillColor == "transparent");
      swatch->setStyleSheet(
          QString("background:%1;border:1px solid %2;border-radius:3px;")
              .arg(unfilledArea ? QStringLiteral("transparent") : colName, colName));

      auto* label = new QLabel(row);
      label->setAttribute(Qt::WA_TransparentForMouseEvents);
      const int np = static_cast<int>(ln.points.size());
      QString text = QString("Line %1 · %2 pt%3")
                         .arg(i + 1).arg(np).arg(np == 1 ? "" : "s");
      if (ln.locked) text += " · area";
      label->setText(text);

      auto* rm = new QPushButton(row);
      rm->setObjectName("pointDelBtn");
      rm->setFlat(true);
      rm->setCursor(Qt::PointingHandCursor);
      rm->setToolTip("Remove line");
      rm->setIcon(themedIcon("trash", iconColor_, 14));
      connect(rm, &QPushButton::clicked, this,
              [this, i] {
                // The row scatters before setLines() rebuilds the list without it.
                if (QListWidgetItem* it = lines_->item(i))
                  DisintegrateOverlay::overRect(lines_->viewport(), lines_->visualItemRect(it), window());
                emit lineListRemoveRequested(i);
              });

      rl->addWidget(swatch);
      rl->addWidget(label, 1);
      rl->addWidget(rm);

      item->setSizeHint(row->sizeHint());
      lines_->setItemWidget(item, row);
      // Selected rows carry an accent outline (canvas-driven, since selection mode is Off);
      // styleLineRow also handles the canvas-hover tint.
      styleLineRow(i);
    }
    if (prevCurrent >= 0)
      lines_->setCurrentRow(std::min(prevCurrent, lines_->count() - 1));
  }

  // Selected outline > canvas-hover tint > plain. Kept in one place so setCanvasHover can
  // restyle two rows without rebuilding the list (and without scrolling it).
  void SelectionPanel::styleLineRow(int i) {
    if (!lines_ || i < 0 || i >= lines_->count()) return;
    QListWidgetItem* it = lines_->item(i);
    QWidget* w = it ? lines_->itemWidget(it) : nullptr;
    if (!w) return;
    const bool sel = std::find(linesSelected_.begin(), linesSelected_.end(), i) !=
                     linesSelected_.end();
    QString ss;
    if (sel) {
      ss = "background: palette(alternate-base);"
           "border:1px solid palette(highlight);border-radius:5px;";
    } else if (i == canvasHoverLineRow_) {
      // The line under the canvas cursor (browser .lines-row-hover).
      ss = "background: palette(alternate-base);border-radius:5px;";
    }
    // Scope to the row container so the colour chip's own sheet stays untouched.
    w->setStyleSheet(ss);
  }

  void SelectionPanel::setCanvasHover(int pointRow, int lineRow) {
    // Points table: tint the row of the point under the canvas cursor (browser
    // .row-highlighted). setBackground fires itemChanged, so updating_ guards it
    // from reading as a user coordinate edit.
    if (points_ && pointRow != canvasHoverPointRow_) {
      const bool wasUpdating = updating_;
      updating_ = true;
      QColor tint = palette().color(QPalette::Highlight);
      tint.setAlpha(45);
      const auto paintRow = [this, &tint](int r, bool on) {
        if (r < 0 || r >= points_->rowCount()) return;
        for (int c = 0; c < ColCount; ++c)
          if (auto* cell = points_->item(r, c))
            cell->setBackground(on ? QBrush(tint) : QBrush());
      };
      paintRow(canvasHoverPointRow_, false);
      paintRow(pointRow, true);
      canvasHoverPointRow_ = pointRow;
      updating_ = wasUpdating;
    }
    // Lines tab: tint the row of the hovered line. Never scrolls the list.
    if (lines_ && lineRow != canvasHoverLineRow_) {
      const int prev = canvasHoverLineRow_;
      canvasHoverLineRow_ = lineRow;
      styleLineRow(prev);
      styleLineRow(lineRow);
    }
  }

  void SelectionPanel::showLine(const core::Line* line, int selectedPoint,
                                const std::vector<PageRow>& pageRows) {
    points_->clearSpans();    // the empty-state row spans the table; a real one must not
    points_->setRowCount(0);  // clear rows (NOT clear() — that would drop the header labels)

    if (!line || line->points.empty()) { showEmptyPoints(); return; }

    // Build the editable points table; `updating_` suppresses itemChanged while
    // cells are set (only a USER edit should fire pointCoordChanged). X/Y editable
    // px, page (cm) read-only, each row ends with 🗑. Mirrors browser coordTable.js.
    updating_ = true;
    points_->setRowCount(static_cast<int>(line->points.size()));
    for (std::size_t i = 0; i < line->points.size(); ++i) {
      const auto& p = line->points[i];
      const int r = static_cast<int>(i);
      auto* idx = new QTableWidgetItem(QString::number(i + 1));
      idx->setFlags(Qt::ItemIsEnabled);
      idx->setTextAlignment(Qt::AlignCenter);
      points_->setItem(r, ColIndex, idx);
      auto* xi = new QTableWidgetItem(QString::number(p.x, 'f', 1));
      xi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
      xi->setToolTip("Double-click to edit X (px)");
      points_->setItem(r, ColX, xi);
      auto* yi = new QTableWidgetItem(QString::number(p.y, 'f', 1));
      yi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
      yi->setToolTip("Double-click to edit Y (px)");
      points_->setItem(r, ColY, yi);
      // Page coordinates as their OWN two columns, like the browser's `X cm` / `Y cm`
      // (coordTable.js) — one "x, y unit" string per row was this panel's own invention.
      const PageRow page = i < pageRows.size() ? pageRows[i] : PageRow{};
      for (const auto& [col, text] : {std::pair{ColPageX, page.x}, std::pair{ColPageY, page.y}}) {
        auto* pg = new QTableWidgetItem(text);
        pg->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        points_->setItem(r, col, pg);
      }
      auto* del = new QPushButton(points_);
      del->setObjectName("pointDelBtn");
      del->setFlat(true);
      del->setCursor(Qt::PointingHandCursor);
      del->setToolTip("Remove point");
      del->setIcon(themedIcon("trash", iconColor_, 14));
      connect(del, &QPushButton::clicked, this, [this, r] {
        // A QTableWidget row has no widget of its own — scatter its RECT instead.
        const QRect rowRect(0, points_->rowViewportPosition(r),
                            points_->viewport()->width(), points_->rowHeight(r));
        DisintegrateOverlay::overRect(points_->viewport(), rowRect, window());
        emit pointDeleteRequested(r);
      });
      points_->setCellWidget(r, ColDel, del);
    }
    if (selectedPoint >= 0 && selectedPoint < points_->rowCount())
      points_->selectRow(selectedPoint);
    points_->resizeRowsToContents();
    updating_ = false;
  }
}

