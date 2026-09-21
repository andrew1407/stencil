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

  void SelectionPanel::setLines(const core::Lines& lines,
                                const std::vector<int>& selected) {
    if (!this->lines) return;
    QSignalBlocker block(this->lines);
    // clear() drops the current row; carry it across, clamped, or the next keyboard Delete does
    // nothing (browser re-focuses the row too).
    const int prevCurrent = this->lines->currentRow();
    this->lines->clear();
    linesSelected = selected;   // styleLineRow's selection snapshot
    canvasHoverPointRow = -1;   // rebuilt rows carry no stale hover tint
    canvasHoverLineRow = -1;
    if (lines.empty()) {
      auto* item = new QListWidgetItem("No lines yet.", this->lines);
      item->setFlags(Qt::NoItemFlags);
      item->setTextAlignment(Qt::AlignCenter);
      return;
    }
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
      const core::Line& ln = lines[i];
      auto* item = new QListWidgetItem(this->lines);

      auto* row = new QWidget(this->lines);
      auto* rl = new QHBoxLayout(row);
      rl->setContentsMargins(6, 4, 6, 4);
      rl->setSpacing(8);

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
      rm->setIcon(themedIcon("trash", iconColor, 14));
      connect(rm, &QPushButton::clicked, this,
              [this, i] {
                if (QListWidgetItem* it = this->lines->item(i))
                  DisintegrateOverlay::overRect(this->lines->viewport(), this->lines->visualItemRect(it), window());
                emit lineListRemoveRequested(i);
              });

      rl->addWidget(swatch);
      rl->addWidget(label, 1);
      rl->addWidget(rm);

      item->setSizeHint(row->sizeHint());
      this->lines->setItemWidget(item, row);
      styleLineRow(i);
    }
    if (prevCurrent >= 0)
      this->lines->setCurrentRow(std::min(prevCurrent, this->lines->count() - 1));
  }

  // Kept in one place so setCanvasHover can restyle two rows without rebuilding or scrolling.
  void SelectionPanel::styleLineRow(int i) {
    if (!lines || i < 0 || i >= lines->count()) return;
    QListWidgetItem* it = lines->item(i);
    QWidget* w = it ? lines->itemWidget(it) : nullptr;
    if (!w) return;
    const bool sel = std::find(linesSelected.begin(), linesSelected.end(), i) !=
                     linesSelected.end();
    QString ss;
    if (sel) {
      ss = "background: palette(alternate-base);"
           "border:1px solid palette(highlight);border-radius:5px;";
    } else if (i == canvasHoverLineRow) {
      ss = "background: palette(alternate-base);border-radius:5px;";
    }
    w->setStyleSheet(ss);
  }

  void SelectionPanel::setCanvasHover(int pointRow, int lineRow) {
    // setBackground fires itemChanged, so updating guards it from reading as a coordinate edit
    // (browser .row-highlighted).
    if (points && pointRow != canvasHoverPointRow) {
      const bool wasUpdating = updating;
      updating = true;
      QColor tint = palette().color(QPalette::Highlight);
      tint.setAlpha(45);
      const auto paintRow = [this, &tint](int r, bool on) {
        if (r < 0 || r >= points->rowCount()) return;
        for (int c = 0; c < COL_COUNT; ++c)
          if (auto* cell = points->item(r, c))
            cell->setBackground(on ? QBrush(tint) : QBrush());
      };
      paintRow(canvasHoverPointRow, false);
      paintRow(pointRow, true);
      canvasHoverPointRow = pointRow;
      updating = wasUpdating;
    }
    if (lines && lineRow != canvasHoverLineRow) {
      const int prev = canvasHoverLineRow;
      canvasHoverLineRow = lineRow;
      styleLineRow(prev);
      styleLineRow(lineRow);
    }
  }

  void SelectionPanel::showLine(const core::Line* line, int selectedPoint,
                                const std::vector<PageRow>& pageRows) {
    points->clearSpans();    // the empty-state row spans the table; a real one must not
    points->setRowCount(0);  // clear rows (NOT clear() — that would drop the header labels)

    if (!line || line->points.empty()) { showEmptyPoints(); return; }

    // `updating` suppresses itemChanged while cells are set; X/Y editable px, page read-only.
    // Mirrors browser coordTable.js.
    updating = true;
    points->setRowCount(static_cast<int>(line->points.size()));
    for (std::size_t i = 0; i < line->points.size(); ++i) {
      const auto& p = line->points[i];
      const int r = static_cast<int>(i);
      auto* idx = new QTableWidgetItem(QString::number(i + 1));
      idx->setFlags(Qt::ItemIsEnabled);
      idx->setTextAlignment(Qt::AlignCenter);
      points->setItem(r, COL_INDEX, idx);
      auto* xi = new QTableWidgetItem(QString::number(p.x, 'f', 1));
      xi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
      xi->setToolTip("Double-click to edit X (px)");
      points->setItem(r, COL_X, xi);
      auto* yi = new QTableWidgetItem(QString::number(p.y, 'f', 1));
      yi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
      yi->setToolTip("Double-click to edit Y (px)");
      points->setItem(r, COL_Y, yi);
      // Page coordinates as their own two columns, like the browser's `X cm` / `Y cm`.
      const PageRow page = i < pageRows.size() ? pageRows[i] : PageRow{};
      for (const auto& [col, text] : {std::pair{COL_PAGE_X, page.x}, std::pair{COL_PAGE_Y, page.y}}) {
        auto* pg = new QTableWidgetItem(text);
        pg->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        points->setItem(r, col, pg);
      }
      auto* del = new QPushButton(points);
      del->setObjectName("pointDelBtn");
      del->setFlat(true);
      del->setCursor(Qt::PointingHandCursor);
      del->setToolTip("Remove point");
      del->setIcon(themedIcon("trash", iconColor, 14));
      connect(del, &QPushButton::clicked, this, [this, r] {
        // A QTableWidget row has no widget — scatter its rect.
        const QRect rowRect(0, points->rowViewportPosition(r),
                            points->viewport()->width(), points->rowHeight(r));
        DisintegrateOverlay::overRect(points->viewport(), rowRect, window());
        emit pointDeleteRequested(r);
      });
      points->setCellWidget(r, COL_DEL, del);
    }
    if (selectedPoint >= 0 && selectedPoint < points->rowCount())
      points->selectRow(selectedPoint);
    points->resizeRowsToContents();
    updating = false;
  }
}

