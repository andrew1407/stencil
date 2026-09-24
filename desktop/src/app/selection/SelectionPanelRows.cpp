#include "SelectionPanel.hpp"
#include "selectionPanelParts.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/icon/iconMotion.hpp"
#include "../../support/skinPrefs.hpp"
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

  void SelectionPanel::setLines(const core::Lines& lines,
                                const std::vector<int>& selected) {
    if (!this->lines) return;
    QSignalBlocker block(this->lines);
    // clear() drops the current row; carry it across, clamped, or the next keyboard Delete does
    // nothing (browser re-focuses the row too).
    const int prevCurrent = this->lines->currentRow();
    this->lines->clearSpans();
    this->lines->setRowCount(0);
    fitTableRows(this->lines);
    linesSelected = selected;   // styleLineRow's selection snapshot
    canvasHoverPointRow = -1;   // rebuilt rows carry no stale hover tint
    canvasHoverLineRow = -1;
    if (lines.empty()) {
      this->lines->horizontalHeader()->hide();
      this->lines->setShowGrid(false);   // a lone message has no cells to divide
      this->lines->setRowCount(1);
      this->lines->setSpan(0, 0, 1, LCOL_COUNT);
      this->lines->setItem(0, 0, emptyMessage(QStringLiteral("No lines yet.")));
      fitTableRows(this->lines);
      return;
    }
    this->lines->horizontalHeader()->show();
    this->lines->setShowGrid(true);
    this->lines->setRowCount(static_cast<int>(lines.size()));
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
      const core::Line& ln = lines[i];
      const auto cell = [this, i](int col, const QString& text) {
        auto* it = new QTableWidgetItem(text);
        it->setFlags(Qt::ItemIsEnabled);
        this->lines->setItem(i, col, it);
      };
      cell(LCOL_INDEX, QString::number(i + 1));
      cell(LCOL_SWATCH, QString());
      cell(LCOL_NAME, ln.locked ? QString("Line %1 · area").arg(i + 1) : QString("Line %1").arg(i + 1));
      cell(LCOL_PTS, QString::number(int(ln.points.size())));

      // What the line LOOKS like: its fill when it has one, its stroke otherwise — the stroke
      // always as the rim (browser linesList.js over core/layout.js fillState).
      const QString rim = ln.color.empty() ? QStringLiteral("#ffff00")
                                           : QString::fromStdString(ln.color);
      const bool filled = !ln.fillColor.empty() && ln.fillColor != "transparent";
      const QString face = filled ? QString::fromStdString(ln.fillColor)
                         : ln.locked ? QStringLiteral("transparent") : rim;
      auto* swatch = new QLabel(this->lines);
      swatch->setObjectName("linesSwatch");
      swatch->setFixedSize(14, 14);
      swatch->setAttribute(Qt::WA_TransparentForMouseEvents);
      swatch->setProperty("face", face);
      swatch->setProperty("rim", rim);
      swatch->setStyleSheet(swatchSheet(face, rim));
      QWidget* swatchCell = centeredCell(swatch, this->lines);
      swatchCell->setAttribute(Qt::WA_TransparentForMouseEvents);
      this->lines->setCellWidget(i, LCOL_SWATCH, swatchCell);

      auto* rm = new QPushButton(this->lines);
      rm->setObjectName("pointDelBtn");
      rm->setFlat(true);
      rm->setCursor(Qt::PointingHandCursor);
      rm->setToolTip("Remove line");
      rm->setIcon(themedIcon("trash", binColor, 14));
      connect(rm, &QPushButton::clicked, this, [this, i] {
        const QRect rowRect(0, this->lines->rowViewportPosition(i),
                            this->lines->viewport()->width(), this->lines->rowHeight(i));
        DisintegrateOverlay::overRect(this->lines->viewport(), rowRect, window());
        emit lineListRemoveRequested(i);
      });
      this->lines->setCellWidget(i, LCOL_DEL, centeredCell(rm, this->lines));
      styleLineRow(i);
    }
    if (prevCurrent >= 0)
      this->lines->setCurrentCell(std::min(prevCurrent, this->lines->rowCount() - 1), LCOL_INDEX);
  }

  // Square under the skin, where nothing is rounded (browser webcore/chrome.css).
  QString SelectionPanel::swatchSheet(const QString& face, const QString& rim) {
    return QString("background:%1;border:1px solid %2;border-radius:%3px;").arg(face, rim).arg(support::isWebcore() ? 0 : 3);
  }

  // Kept in one place so setCanvasHover can restyle two rows without rebuilding or scrolling.
  void SelectionPanel::styleLineRow(int i) {
    if (!lines || i < 0 || i >= lines->rowCount()) return;
    if (isEmptyRow(lines, i)) {
      if (QTableWidgetItem* it = lines->item(0, 0)) it->setBackground(i == canvasHoverLineRow ? emptyWash() : QBrush());
      return;
    }
    const bool sel = std::find(linesSelected.begin(), linesSelected.end(), i) !=
                     linesSelected.end();
    // Browser .lines-row-selected (the delegate strokes the outline) and .lines-row-hover.
    const bool hot = i == canvasHoverLineRow;
    const QBrush wash = (sel || hot) ? rowWash(sel) : QBrush();
    for (int c = 0; c < LCOL_COUNT; ++c)
      if (QTableWidgetItem* it = lines->item(i, c)) {
        it->setBackground(wash);
        it->setForeground(sel && support::isWebcore() ? rowInk() : QBrush());
        it->setData(SELECTED_ROLE, sel);
      }
  }

  // Browser .row-highlighted: a soft accent wash; under the skin a picked row is the navy bar
  // and a hovered one the light face (webcore/windows.css).
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

