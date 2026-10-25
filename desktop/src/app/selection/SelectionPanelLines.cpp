// The Lines tab of the selection panel: its table, built once, and its rows, rebuilt from the
// canvas's lines — per line its number, its name, its colour and thickness, its points' colour and
// size, its point count, its eye and its bin (browser ui/panel/lines/list.js and lines/events.js).
#include "SelectionPanel.hpp"
#include "linesTableParts.hpp"
#include "cssColor.hpp"
#include "defaultVisuals.hpp"
#include <QGuiApplication>
#include <QHeaderView>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <tuple>

namespace stencil::gui {

  namespace {
    // Every cell names itself (browser lines/list.js TIPS); a pair's second half shares its heading.
    const QString LINE_HEAD = QStringLiteral("Line — its own color and thickness");
    const QString POINT_HEAD = QStringLiteral("Point — its points' color and size");
    const QString NAME_HEAD = QStringLiteral("Line name — double-click a row's to rename it");

    // Everything a row shows: its style, name and eye, and how many points, never where they are.
    auto rowLook(const core::Line& l) {
      return std::tie(l.color, l.thickness, l.pointSize, l.style, l.locked, l.fillColor, l.pointColor, l.name, l.hidden);
    }

    bool sameRow(const core::Line& line, const std::pair<core::Line, size_t>& built) {
      return line.points.size() == built.second && rowLook(line) == rowLook(built.first);
    }

    bool sameRows(const core::Lines& lines, const std::vector<std::pair<core::Line, size_t>>& built) {
      if (lines.size() != built.size()) return false;
      for (size_t i = 0; i < lines.size(); ++i)
        if (!sameRow(lines[i], built[i])) return false;
      return true;
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
    watchRowsShown();
    // Queued: the rename rebuilds this table, which must not happen inside the editor's own commit.
    const auto renamed = [this](int row, const QString& name) {
      QTimer::singleShot(0, this, [this, row, name] { emit lineNameEdited(row, name); });
    };
    lines->setItemDelegate(new LineCellDelegate(lines, [this] { return readOnly && readOnly(); }, renamed));
    lines->setHorizontalHeader(new PairedHeader(lines));
    lines->setHorizontalHeaderLabels({"#", "Name", "Line", QString(), "Point", QString(), "Pts", QString(), QString()});
    const QStringList heads{"Line number", NAME_HEAD, LINE_HEAD, LINE_HEAD, POINT_HEAD, POINT_HEAD, "Points",
                            QString(), QString()};
    for (int c = 0; c < LCOL_COUNT; ++c) lines->horizontalHeaderItem(c)->setToolTip(heads[c]);
    lines->verticalHeader()->setVisible(false);
    lines->setSelectionMode(QAbstractItemView::NoSelection);  // selection is driven by the canvas
    // Only a name, a thickness or a point size is editable, in place (browser lines/nameEdit.js, numEdit.js).
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
    lh->setSectionResizeMode(LCOL_NAME, QHeaderView::Stretch);
    for (const auto& [col, w] : {std::pair{LCOL_COLOR, LINE_COL_CHIP}, std::pair{LCOL_POINT, LINE_COL_CHIP},
                                 std::pair{LCOL_THICK, LINE_COL_NUM}, std::pair{LCOL_SIZE, LINE_COL_NUM},
                                 std::pair{LCOL_PTS, LINE_COL_PTS}, std::pair{LCOL_EYE, LINE_COL_BTN},
                                 std::pair{LCOL_DEL, LINE_COL_BTN}}) {
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
    // A click selects its line, Ctrl/⌘+Shift toggles it; a name or a number edits on a double-click
    // and never selects, and a chip times its own clicks (setLines).
    connect(lines, &QTableWidget::cellClicked, this, [this](int idx, int col) {
      if (idx < 0) return;
      lines->setCurrentCell(idx, LCOL_INDEX);   // the row Delete/Backspace will act on
      const auto mods = QGuiApplication::keyboardModifiers();
      const bool multi = (mods & (Qt::ControlModifier | Qt::MetaModifier)) &&
                         (mods & Qt::ShiftModifier);
      if (!multi && (col == LCOL_NAME || col == LCOL_THICK || col == LCOL_SIZE)) return;
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
    // A stroke reaches here from the canvas change and again from its selection change.
    const qint64 paletteKey = palette().cacheKey();
    if (paletteKey == builtPalette && selected == linesSelected && sameRows(lines, builtRows)) return;
    const QColor stroke = cssColor(defaultVisuals::table().color);
    // A row still showing its line's look and point count stays (browser lines/list.js keepable); a
    // new palette or default stroke, or the empty state either side, builds every row again.
    const bool keepRows = paletteKey == builtPalette && stroke == builtStroke && !lines.empty() && !builtRows.empty();
    const std::vector<std::pair<core::Line, size_t>> was = std::move(builtRows);
    builtPalette = paletteKey;
    builtStroke = stroke;
    builtRows.clear();
    for (const core::Line& ln : lines)
      builtRows.push_back({{{}, ln.color, ln.thickness, ln.pointSize, ln.style, ln.locked, ln.fillColor,
                            ln.pointColor, ln.name, ln.hidden}, ln.points.size()});
    QSignalBlocker block(this->lines);
    // clear() drops the current row; carry it across, clamped, or the next keyboard Delete does
    // nothing (browser re-focuses the row too).
    const int prevCurrent = this->lines->currentRow();
    if (!keepRows) {
      this->lines->clearSpans();
      clearRows(this->lines);
    }
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
    const int n = static_cast<int>(lines.size());
    for (int r = n; r < this->lines->rowCount(); ++r) clearRowWidgets(this->lines, r);
    this->lines->setRowCount(n);
    for (int i = 0; i < n; ++i) {
      if (!keepRows || i >= static_cast<int>(was.size()) || i == eyeFlipRow || !sameRow(lines[i], was[i])) {
        clearRowWidgets(this->lines, i);
        buildLineRow(i, lines[i], stroke);
      }
      styleLineRow(i);
    }
    materializeLineRows();
    eyeFlipRow = -1;
    if (prevCurrent >= 0)
      this->lines->setCurrentCell(std::min(prevCurrent, this->lines->rowCount() - 1), LCOL_INDEX);
  }

}
