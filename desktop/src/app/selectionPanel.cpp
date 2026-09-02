#include "selectionPanel.hpp"
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

  namespace {
    // Points-table columns, one for one with the browser's coordinates table
    // (mainContent.js <thead>): index · X px (editable) · Y px (editable) · X page ·
    // Y page (both read-only, in the app's current unit) · 🗑.
    enum PointCol { ColIndex = 0, ColX, ColY, ColPageX, ColPageY, ColDel, ColCount };

    // The header chevron's box + glyph. Also the floating re-open chevron's, which has to
    // read as the same button (mainWindow kPanelToggleBox).
    constexpr int kToggleBox = 24;
    constexpr int kToggleGlyph = 15;


    // Paints a selected row as a flat accent OUTLINE (not a filled background); hover tint + cell
    // text come from QSS / the base. Mirrors the browser row treatment but with an outline.
    class PointRowDelegate : public QStyledItemDelegate {
     public:
      using QStyledItemDelegate::QStyledItemDelegate;
      void paint(QPainter* p, const QStyleOptionViewItem& opt,
                 const QModelIndex& idx) const override {
        // The selection FILL is made transparent via QSS (selection-background-color); here we
        // just stroke an accent outline around the selected row on top of the normal item paint.
        QStyledItemDelegate::paint(p, opt, idx);
        if (!(opt.state & QStyle::State_Selected)) return;
        p->save();
        p->setRenderHint(QPainter::Antialiasing, false);
        p->setPen(QPen(opt.palette.color(QPalette::Highlight), 2));
        const QRect r = opt.rect.adjusted(0, 1, 0, -1);
        p->drawLine(r.topLeft(), r.topRight());
        p->drawLine(r.bottomLeft(), r.bottomRight());
        if (idx.column() == ColIndex) p->drawLine(r.topLeft(), r.bottomLeft());
        if (idx.column() == ColCount - 1) p->drawLine(r.topRight(), r.bottomRight());
        p->restore();
      }
    };
  }

  SelectionPanel::SelectionPanel(QWidget* parent)
      : QDockWidget("Selection", parent) {
    // Pinned in place: NOT floatable/movable (the browser panel can't be torn off / unpinned) — it
    // can only be hidden/shown via the toggle. Removes the unwanted drag-to-float behaviour.
    setFeatures(QDockWidget::NoDockWidgetFeatures);

    // Custom title bar with a right-aligned chevron that hides the panel — mirrors the browser
    // panel header's #toggle-coord-panel chevron (placed IN the panel, not floating over the canvas).
    auto* titleBar = new QWidget(this);
    // Named + WA_StyledBackground so theme.cpp can give it the panel surface: a bare QWidget
    // paints nothing and let the window backdrop through, which is why the header read as a
    // black band across the top of the panel.
    titleBar->setObjectName("selPanelTitle");
    titleBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* titleRow = new QHBoxLayout(titleBar);
    titleRow->setContentsMargins(8, 2, 5, 0);
    // The Points | Lines tabs live IN the header, beside the chevron — the browser has no
    // panel title above them (mainContent.js .coord-panel-header holds the tabs and the
    // toggle and nothing else); a "Points" label here read as a second, redundant heading
    // over a tab already called Points. The QTabWidget below keeps the PAGES and hides its
    // own bar, so this one is the only tab strip on screen.
    tabBar_ = new QTabBar(titleBar);
    tabBar_->setObjectName("selectionTabBar");
    tabBar_->setDrawBase(false);
    tabBar_->setExpanding(false);
    tabBar_->setFocusPolicy(Qt::NoFocus);
    tabBar_->addTab("Points");
    tabBar_->addTab("Lines");
    collapseBtn_ = new QToolButton(titleBar);
    collapseBtn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    collapseBtn_->setCursor(Qt::PointingHandCursor);
    collapseBtn_->setToolTip("Hide panel");
    // Sits ON the panel surface, so it takes the browser's #toggle-coord-panel treatment
    // (transparent, themed hairline border) rather than the floating chevron's dark slab —
    // that one overlays the canvas, this one would be a dark hole in a light panel.
    collapseBtn_->setObjectName("panelCollapseBtn");   // styled in theme.cpp
    // A fold chevron's angle is STATE (open/closed), not hover feedback — the browser's
    // `[id^="toggle-"]` icon-motion opt-out (iconMotion.json trigger.excluded).
    collapseBtn_->setProperty(kNoIconMotionProperty, true);
    collapseBtn_->setFocusPolicy(Qt::NoFocus);   // no macOS focus halo around the chevron
    collapseBtn_->setFixedSize(kToggleBox, kToggleBox);
    collapseBtn_->setIconSize(QSize(kToggleGlyph, kToggleGlyph));
    connect(collapseBtn_, &QToolButton::clicked, this, [this] { emit collapseRequested(); });
    titleRow->addWidget(tabBar_);
    titleRow->addStretch(1);
    titleRow->addWidget(collapseBtn_, 0, Qt::AlignVCenter);

    setTitleBarWidget(titleBar);

    auto* body = new QWidget(this);
    body->setObjectName("selPanelBody");
    body->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(8, 8, 8, 8);

    // Shown while 2+ lines are multi-selected (Ctrl+Shift+click): the "Selected Line:"
    // bar above the canvas is ambiguous for a multi-selection, so it stays hidden and
    // this note explains the mode instead.
    multiLabel_ = new QLabel(body);
    multiLabel_->setWordWrap(true);
    multiLabel_->setStyleSheet("color: palette(highlight); font-weight: 600;");
    multiLabel_->setVisible(false);
    layout->addWidget(multiLabel_);

    // Points | Lines tabs (browser mainContent.js coord-tabs). The Points tab holds the
    // per-line coordinate table + measurements (the existing panel body); the Lines tab lists
    // every committed line for select/inspect/remove (browser renderLinesList / #lines-list).
    tabs_ = new QTabWidget(body);
    tabs_->setObjectName("selectionTabs");
    tabs_->tabBar()->hide();   // the header's own strip drives it (see tabBar_ above)
    layout->addWidget(tabs_, 1);

    auto* ptsTab = new QWidget(tabs_);
    auto* ptsLay = new QVBoxLayout(ptsTab);
    ptsLay->setContentsMargins(0, 6, 0, 0);
    points_ = new QTableWidget(0, ColCount, ptsTab);
    points_->setObjectName("pointsTable");
    points_->setItemDelegate(new PointRowDelegate(points_));  // outline-style selection
    applyUnitHeaders();
    points_->verticalHeader()->setVisible(false);
    points_->setSelectionBehavior(QAbstractItemView::SelectRows);
    points_->setSelectionMode(QAbstractItemView::SingleSelection);
    points_->setShowGrid(false);
    points_->setAlternatingRowColors(true);
    points_->setWordWrap(false);
    // Only the X/Y px cells are editable (via double-click); # / page / 🗑 stay read-only.
    points_->setEditTriggers(QAbstractItemView::DoubleClicked |
                             QAbstractItemView::EditKeyPressed);
    points_->installEventFilter(this);
    // Hover cross-highlight, row → canvas: entering a row rings that point on the
    // canvas (browser coordTable.js row mouseenter). Leave is caught in eventFilter.
    points_->setMouseTracking(true);
    points_->viewport()->setMouseTracking(true);
    connect(points_, &QTableWidget::cellEntered, this,
            [this](int row, int) { emit pointRowHovered(row); });
    auto* hh = points_->horizontalHeader();
    hh->setSectionResizeMode(ColIndex, QHeaderView::ResizeToContents);
    for (int c : {ColX, ColY, ColPageX, ColPageY}) hh->setSectionResizeMode(c, QHeaderView::Stretch);
    hh->setSectionResizeMode(ColDel, QHeaderView::Fixed);
    // The browser's own trailing cell (mainContent.js: `width:28px;padding:4px`).
    points_->setColumnWidth(ColDel, 28);
    hh->setHighlightSections(false);
    // Left-aligned like every browser th (layout.css .coordinates-table th { text-align: left }).
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // The browser table draws a hairline around every cell (border: 1px solid
    // --border-coord); the grid is how Qt says the same thing.
    points_->setShowGrid(true);
    ptsLay->addWidget(points_, 1);

    tabs_->addTab(ptsTab, "Points");

    // Lines tab — a flat list of every committed line (browser #lines-list). Each row: color
    // chip · "Line N · M pts" · 🗑. Rows single-select on click (Ctrl/⌘+Shift toggles the
    // multi-select set); the 🗑 removes the line. Populated by setLines().
    auto* linesTab = new QWidget(tabs_);
    auto* linesLay = new QVBoxLayout(linesTab);
    linesLay->setContentsMargins(0, 6, 0, 0);
    lines_ = new QListWidget(linesTab);
    lines_->setObjectName("linesList");
    lines_->setSelectionMode(QAbstractItemView::NoSelection);  // selection is driven by the canvas
    // ClickFocus (not NoFocus) so a bare Delete/Backspace can be scoped to this list the way
    // it already is for the points table. Selection stays canvas-driven — only the CURRENT
    // row moves on click, which is what the key acts on.
    lines_->setFocusPolicy(Qt::ClickFocus);
    lines_->installEventFilter(this);
    // Hover cross-highlight, row → canvas: entering a row glows that line on the
    // canvas (browser renderLinesList row mouseenter). Leave is caught in eventFilter.
    lines_->setMouseTracking(true);
    lines_->viewport()->setMouseTracking(true);
    connect(lines_, &QListWidget::itemEntered, this,
            [this](QListWidgetItem* it) { emit lineRowHovered(lines_->row(it)); });
    linesLay->addWidget(lines_, 1);
    tabs_->addTab(linesTab, "Lines");
    // One selection, two widgets: the header strip is what the user clicks, the stack is
    // what it shows. Bound both ways so a programmatic page change turns the strip too.
    connect(tabBar_, &QTabBar::currentChanged, tabs_, &QTabWidget::setCurrentIndex);
    connect(tabs_, &QTabWidget::currentChanged, tabBar_, &QTabBar::setCurrentIndex);

    // Row click → select that line (multi = Ctrl/⌘+Shift held, mirroring the canvas modifier).
    connect(lines_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
      const int idx = lines_->row(it);
      if (idx < 0) return;
      lines_->setCurrentRow(idx);   // the row Delete/Backspace will act on
      const auto mods = QGuiApplication::keyboardModifiers();
      const bool multi = (mods & (Qt::ControlModifier | Qt::MetaModifier)) &&
                         (mods & Qt::ShiftModifier);
      emit lineListActivated(idx, multi);
    });

    setWidget(body);
    restyleIcons(palette().color(QPalette::WindowText));

    // Click a row (not the 🗑 column) → focus that point on the canvas.
    connect(points_, &QTableWidget::cellClicked, this, [this](int row, int col) {
      if (col != ColDel) emit pointActivated(row);
    });
    // A committed X/Y edit → forward to the canvas (guarded against showLine's repopulation).
    connect(points_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
      if (updating_ || !it) return;
      const int col = it->column();
      if (col != ColX && col != ColY) return;
      bool ok = false;
      const double v = it->text().toDouble(&ok);
      if (ok) emit pointCoordChanged(it->row(), col == ColX ? 0 : 1, v);
    });

    showLine(nullptr, -1);
  }

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
