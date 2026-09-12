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
}

