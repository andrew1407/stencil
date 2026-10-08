#include "SelectionPanel.hpp"
#include "selectionPanelParts.hpp"
#include "iconSet.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/skinPrefs.hpp"
#include <QPalette>
#include <QPushButton>
#include <QWidget>
#include <algorithm>

namespace stencil::gui {

  // Browser .row-highlighted: a soft accent wash; under the skin a picked row is the navy bar
  // and a hovered one the light face (webcore/menus.css).
  QBrush SelectionPanel::rowWash(bool picked) const {
    if (support::isWebcore()) return picked ? palette().color(QPalette::Highlight) : support::skinBevel().light;
    QColor tint = palette().color(QPalette::Highlight);
    tint.setAlpha(45);
    return QBrush(tint);
  }
  QBrush SelectionPanel::rowInk() const { return QBrush(palette().color(QPalette::HighlightedText)); }

  void SelectionPanel::setCanvasHover(int pointRow, int lineRow) {
    // setBackground fires itemChanged, so updating guards it from reading as a coordinate edit
    // (browser .row-highlighted).
    if (points && pointRow != canvasHoverPointRow) {
      const bool wasUpdating = updating;
      updating = true;
      const QBrush wash = isEmptyRow(points, pointRow) ? emptyWash() : rowWash(false);
      const auto paintRow = [this, &wash](int r, bool on) {
        if (r < 0 || r >= points->rowCount()) return;
        for (int c = 0; c < COL_COUNT; ++c)
          if (auto* cell = points->item(r, c)) cell->setBackground(on ? wash : QBrush());
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
    points->horizontalHeader()->show();   // …and back once there are rows to head
    points->setShowGrid(true);

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
      del->setIcon(themedIcon("trash", binColor, 14));
      connect(del, &QPushButton::clicked, this, [this, r] {
        // A QTableWidget row has no widget — scatter its rect.
        const QRect rowRect(0, points->rowViewportPosition(r),
                            points->viewport()->width(), points->rowHeight(r));
        DisintegrateOverlay::overRect(points->viewport(), rowRect, window());
        emit pointDeleteRequested(r);
      });
      points->setCellWidget(r, COL_DEL, centeredCell(del, points));
    }
    if (selectedPoint >= 0 && selectedPoint < points->rowCount())
      points->selectRow(selectedPoint);
    fitTableRows(points);
    updating = false;
  }
}

