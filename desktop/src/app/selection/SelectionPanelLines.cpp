// The Lines tab of the selection panel: its table, built once, and its rows, rebuilt from the
// canvas's lines — per line its number, its colour and thickness, its points' colour and size, its
// point count and its bin (browser ui/panel/lines/list.js and lines/events.js).
#include "SelectionPanel.hpp"
#include "linesTableParts.hpp"
#include "cssColor.hpp"
#include "iconSet.hpp"
#include "../../support/control/dblReset.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/skinPrefs.hpp"
#include "defaultVisuals.hpp"
#include <QGuiApplication>
#include <QHeaderView>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace stencil::gui {

  namespace {
    // Every cell names itself (browser lines/list.js TIPS); a pair's second half shares its heading.
    const QString LINE_TIP = QStringLiteral("Line color\nDouble-click: the toolbar's line color");
    const QString AREA_TIP = QStringLiteral("Line color — an area shows its fill inside\n"
                                            "Double-click: the toolbar's line color");
    const QString POINT_TIP = QStringLiteral("Point color\nDouble-click: the line's own color");
    const QString LINE_HEAD = QStringLiteral("Line — its own color and thickness");
    const QString POINT_HEAD = QStringLiteral("Point — its points' color and size");

    QString rangeTip(const char* what, int lo, int hi) {
      return QStringLiteral("%1\nDouble-click to edit (%2–%3 px)").arg(QLatin1String(what)).arg(lo).arg(hi);
    }
  }  // namespace

  void SelectionPanel::buildLinesTab() {
    auto* linesTab = new QWidget(tabs);
    auto* linesLay = new QVBoxLayout(linesTab);
    linesLay->setContentsMargins(0, 0, 0, 0);
    // The points table again, with the lines' own columns: one widget is one grid, one header
    // and one cell padding across both tabs (in the browser it is the same table).
    lines = new FitTable(0, LCOL_COUNT, linesTab);
    lines->setObjectName("linesList");
    lines->viewport()->setCursor(Qt::PointingHandCursor);   // browser .lines-row
    lines->setItemDelegate(new LineCellDelegate(lines, [this] { return readOnly && readOnly(); }));
    lines->setHorizontalHeader(new PairedHeader(lines));
    lines->setHorizontalHeaderLabels({"#", "Line", QString(), "Point", QString(), "Pts", QString()});
    const QStringList heads{"Line number", LINE_HEAD, LINE_HEAD, POINT_HEAD, POINT_HEAD, "Points", QString()};
    for (int c = 0; c < LCOL_COUNT; ++c) lines->horizontalHeaderItem(c)->setToolTip(heads[c]);
    lines->verticalHeader()->setVisible(false);
    lines->setSelectionMode(QAbstractItemView::NoSelection);  // selection is driven by the canvas
    // Only a thickness or a point size is editable, in place (browser lines/numEdit.js).
    lines->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    lines->setWordWrap(false);
    lines->setShowGrid(true);
    // ClickFocus so a bare Delete/Backspace scopes here; selection stays canvas-driven.
    lines->setFocusPolicy(Qt::ClickFocus);
    lines->installEventFilter(this);
    // Hover cross-highlight, row → canvas (browser renderLinesList row mouseenter).
    lines->setMouseTracking(true);
    lines->viewport()->setMouseTracking(true);
    connect(lines, &QTableWidget::cellEntered, this,
            [this](int row, int col) {
              cellCursor(lines, row, col);
              setCanvasHover(canvasHoverPointRow, row);
              emit lineRowHovered(row);
            });
    auto* lh = lines->horizontalHeader();
    // The ordinal and the bin are the points table's own, so both tabs share their edges.
    lh->setSectionResizeMode(LCOL_INDEX, QHeaderView::ResizeToContents);
    for (int col : {LCOL_THICK, LCOL_SIZE}) lh->setSectionResizeMode(col, QHeaderView::Stretch);
    for (const auto& [col, w] : {std::pair{LCOL_COLOR, LINE_COL_CHIP}, std::pair{LCOL_POINT, LINE_COL_CHIP},
                                 std::pair{LCOL_PTS, LINE_COL_PTS}, std::pair{LCOL_DEL, 28}}) {
      lh->setSectionResizeMode(col, QHeaderView::Fixed);
      lines->setColumnWidth(col, w);
    }
    lh->setHighlightSections(false);
    lh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    lines->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);   // as tall as its rows, as the browser's
    lines->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    linesLay->addWidget(lines);
    linesLay->addStretch(1);
    tabs->addTab(linesTab, "Lines");
    // A click selects its line, Ctrl/⌘+Shift toggles it; a number edits on a double-click and never
    // selects, and a chip times its own clicks (setLines).
    connect(lines, &QTableWidget::cellClicked, this, [this](int idx, int col) {
      if (idx < 0) return;
      lines->setCurrentCell(idx, LCOL_INDEX);   // the row Delete/Backspace will act on
      const auto mods = QGuiApplication::keyboardModifiers();
      const bool multi = (mods & (Qt::ControlModifier | Qt::MetaModifier)) &&
                         (mods & Qt::ShiftModifier);
      if (!multi && (col == LCOL_THICK || col == LCOL_SIZE)) return;
      emit lineListActivated(idx, multi);
    });
    // Queued: the edit rebuilds this table, which must not happen inside the editor's own commit.
    connect(lines, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
      const int col = it->column();
      if (col != LCOL_THICK && col != LCOL_SIZE) return;
      const int row = it->row();
      const int value = it->data(Qt::EditRole).toInt();
      QTimer::singleShot(0, this, [this, row, col, value] {
        if (col == LCOL_THICK) emit lineThicknessEdited(row, value);
        else emit linePointSizeEdited(row, value);
      });
    });
  }

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
    const support::lineLimits::Table& limits = support::lineLimits::table();
    const QString thickTip = rangeTip("Line thickness", limits.thickMin, limits.thickMax);
    const QString sizeTip = rangeTip("Point size", limits.pointMin, limits.pointMax);
    const QColor stroke = cssColor(defaultVisuals::table().color);
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
      const core::Line& ln = lines[i];
      // A size is an int, so its editor is a spin box; the rest is text, never a locale's 1,234.
      const auto cell = [this, i](int col, const QVariant& value, const QString& tip, bool editable = false) {
        auto* it = new QTableWidgetItem();
        it->setData(Qt::EditRole, value);
        it->setFlags(editable ? Qt::ItemFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable) : Qt::ItemFlags(Qt::ItemIsEnabled));
        it->setToolTip(tip);
        this->lines->setItem(i, col, it);
      };
      cell(LCOL_INDEX, QString::number(i + 1), QStringLiteral("Line %1").arg(i + 1));
      cell(LCOL_COLOR, QString(), ln.locked ? AREA_TIP : LINE_TIP);
      cell(LCOL_THICK, int(std::lround(ln.thickness)), thickTip, true);
      cell(LCOL_POINT, QString(), POINT_TIP);
      cell(LCOL_SIZE, int(std::lround(ln.pointSize)), sizeTip, true);
      cell(LCOL_PTS, QString::number(int(ln.points.size())), QStringLiteral("Points"));

      // A chip's click opens its picker once the double-click window passes; a second one resets.
      const auto chip = [this, i](int col, const QColor& face, const QColor& rim, const QString& tip) {
        const bool point = col == LCOL_POINT;
        auto* c = new LineChip(point, this->lines);
        c->setObjectName(point ? QStringLiteral("linesPointSwatch") : QStringLiteral("linesSwatch"));
        c->setColors(face, rim);
        c->setToolTip(tip);
        support::wireColorChip(c, [this, i, point] { if (point) emit pointSwatchPick(i); else emit lineSwatchPick(i); },
                               [this, i, point] { if (point) emit pointSwatchReset(i); else emit lineSwatchReset(i); });
        this->lines->setCellWidget(i, col, centeredCell(c, this->lines));
      };
      // What the line LOOKS like: its fill when it has one, its stroke otherwise — the stroke
      // always as the rim (browser lines/list.js over core/layout.js fillState).
      const QColor rim = ln.color.empty() ? stroke : cssColor(ln.color);
      const bool filled = !ln.fillColor.empty() && ln.fillColor != "transparent";
      const QColor face = filled ? cssColor(ln.fillColor) : ln.locked ? QColor(Qt::transparent) : rim;
      chip(LCOL_COLOR, face, rim, ln.locked ? AREA_TIP : LINE_TIP);
      const std::string& dots = core::pointColorOr(ln);
      chip(LCOL_POINT, dots.empty() ? stroke : cssColor(dots), QColor(), POINT_TIP);

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

  // Kept in one place so setCanvasHover can restyle two rows without rebuilding or scrolling.
  void SelectionPanel::styleLineRow(int i) {
    if (!lines || i < 0 || i >= lines->rowCount()) return;
    if (isEmptyRow(lines, i)) {
      if (QTableWidgetItem* it = lines->item(0, 0)) it->setBackground(i == canvasHoverLineRow ? emptyWash() : QBrush());
      return;
    }
    const bool sel = std::find(linesSelected.begin(), linesSelected.end(), i) !=
                     linesSelected.end();
    // A restyle is no edit: itemChanged would read a size cell's new wash as a typed value.
    QSignalBlocker quiet(lines);
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

}
