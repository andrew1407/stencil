#include "SelectionPanel.hpp"
#include "selectionPanelParts.hpp"
#include "iconMotionTypes.hpp"
#include "uiTimings.hpp"
#include <QGuiApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTabWidget>
#include <QPalette>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace stencil::gui {


  SelectionPanel::SelectionPanel(QWidget* parent)
      : QDockWidget("Selection", parent) {
    // Pinned: the browser panel cannot be torn off; only the toggle hides it.
    setFeatures(QDockWidget::NoDockWidgetFeatures);

    // Browser twin: #toggle-coord-panel, placed in the panel header.
    auto* titleBar = new QWidget(this);
    // Named + WA_StyledBackground so theme.cpp can paint the panel surface; a bare QWidget lets
    // the backdrop through.
    titleBar->setObjectName("selPanelTitle");
    titleBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* titleRow = new QHBoxLayout(titleBar);
    titleRow->setContentsMargins(12, 10, 12, 0);   // the card's own padding (browser .coordinates-panel)
    // The tabs live in the header beside the chevron (browser .coord-panel-header holds only tabs
    // + toggle); the QTabWidget hides its own bar.
    tabBar = new QTabBar(titleBar);
    tabBar->setObjectName("selectionTabBar");
    tabBar->setDrawBase(false);
    tabBar->setExpanding(false);
    tabBar->setUsesScrollButtons(false);   // their reserve widened the webcore hairline past the tabs
    tabBar->setFocusPolicy(Qt::NoFocus);
    tabBar->setCursor(Qt::PointingHandCursor);   // browser .coord-tab; the bar is only its two tabs
    tabBar->addTab("Points");
    tabBar->addTab("Lines");
    collapseBtn = new QToolButton(titleBar);
    collapseBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    collapseBtn->setCursor(Qt::PointingHandCursor);
    collapseBtn->setToolTip("Hide panel");
    // On the panel surface it takes the browser's #toggle-coord-panel treatment, not the floating
    // chevron's slab.
    collapseBtn->setObjectName("panelCollapseBtn");   // styled in theme.cpp
    // A fold chevron's angle is state, not hover feedback (iconMotion.json trigger.excluded).
    collapseBtn->setProperty(NO_ICON_MOTION_PROPERTY, true);
    collapseBtn->setFocusPolicy(Qt::NoFocus);   // no macOS focus halo around the chevron
    collapseBtn->setFixedSize(TOGGLE_BOX, TOGGLE_BOX);
    collapseBtn->setIconSize(QSize(TOGGLE_GLYPH, TOGGLE_GLYPH));
    connect(collapseBtn, &QToolButton::clicked, this, [this] { emit collapseRequested(); });
    titleRow->addWidget(tabBar);
    titleRow->addStretch(1);
    titleRow->addWidget(collapseBtn, 0, Qt::AlignVCenter);

    setTitleBarWidget(titleBar);

    // The browser panel is a card that hugs its rows, the page showing below it: the header above
    // is its top, this frame the rest, and the stretch under it is bare page.
    auto* body = new QWidget(this);
    body->setObjectName("selPanelBody");
    auto* bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->setSpacing(0);
    auto* card = new QFrame(body);
    card->setObjectName("selPanelCard");
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    bodyLay->addWidget(card);
    bodyLay->addStretch(1);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 8, 12, 10);

    // Shown while 2+ lines are multi-selected; the "Selected Line:" bar stays hidden then.
    multiLabel = new QLabel(card);
    multiLabel->setWordWrap(true);
    multiLabel->setStyleSheet("color: palette(highlight); font-weight: 600;");
    multiLabel->setVisible(false);
    layout->addWidget(multiLabel);

    // browser mainContent.js coord-tabs; the Lines tab mirrors renderLinesList / #lines-list.
    tabs = new OpenPageTabs(card);
    tabs->setObjectName("selectionTabs");
    tabs->tabBar()->hide();   // the header's own strip drives it (see tabBar above)
    tabs->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    layout->addWidget(tabs, 1);

    auto* ptsTab = new QWidget(tabs);
    auto* ptsLay = new QVBoxLayout(ptsTab);
    ptsLay->setContentsMargins(0, 0, 0, 0);
    points = new FitTable(0, COL_COUNT, ptsTab);
    points->setObjectName("pointsTable");
    points->viewport()->setCursor(Qt::PointingHandCursor);   // browser .coordinates-table tbody tr
    points->setItemDelegate(new PointRowDelegate(points));  // outline-style selection
    applyUnitHeaders();
    points->verticalHeader()->setVisible(false);
    points->setSelectionBehavior(QAbstractItemView::SelectRows);
    points->setSelectionMode(QAbstractItemView::SingleSelection);
    points->setShowGrid(false);
    points->setAlternatingRowColors(true);
    points->setWordWrap(false);
    points->setEditTriggers(QAbstractItemView::DoubleClicked |
                             QAbstractItemView::EditKeyPressed);
    points->installEventFilter(this);
    // Hover cross-highlight, row → canvas (browser coordTable.js row mouseenter); Leave is caught
    // in eventFilter.
    points->setMouseTracking(true);
    points->viewport()->setMouseTracking(true);
    connect(points, &QTableWidget::cellEntered, this,
            [this](int row, int) {
              setCanvasHover(row, canvasHoverLineRow);
              emit pointRowHovered(row);
            });
    auto* hh = points->horizontalHeader();
    hh->setSectionResizeMode(COL_INDEX, QHeaderView::ResizeToContents);
    for (int c : {COL_X, COL_Y, COL_PAGE_X, COL_PAGE_Y}) hh->setSectionResizeMode(c, QHeaderView::Stretch);
    hh->setSectionResizeMode(COL_DEL, QHeaderView::Fixed);
    // The browser's trailing cell (mainContent.js: `width:28px;padding:4px`).
    points->setColumnWidth(COL_DEL, 28);
    hh->setHighlightSections(false);
    // layout.css .coordinates-table th { text-align: left }
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // The browser table draws a hairline around every cell (--border-coord); the grid is Qt's way.
    points->setShowGrid(true);
    points->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);   // as tall as its rows, as the browser's
    points->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    ptsLay->addWidget(points);
    ptsLay->addStretch(1);

    tabs->addTab(ptsTab, "Points");

    // browser #lines-list: colour chip · "Line N · M pts" · 🗑; Ctrl/⌘+Shift toggles multi-select.
    auto* linesTab = new QWidget(tabs);
    auto* linesLay = new QVBoxLayout(linesTab);
    linesLay->setContentsMargins(0, 0, 0, 0);
    // The points table again, with the lines' own columns: one widget is one grid, one header
    // and one cell padding across both tabs (in the browser it is the same table).
    lines = new FitTable(0, LCOL_COUNT, linesTab);
    lines->setObjectName("linesList");
    lines->viewport()->setCursor(Qt::PointingHandCursor);   // browser .lines-row
    lines->setItemDelegate(new PointRowDelegate(lines, LCOL_COUNT - 1));
    lines->setHorizontalHeaderLabels({"#", "Color", "Line", "Pts", ""});
    lines->verticalHeader()->setVisible(false);
    lines->setSelectionMode(QAbstractItemView::NoSelection);  // selection is driven by the canvas
    lines->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lines->setWordWrap(false);
    lines->setShowGrid(true);
    // ClickFocus so a bare Delete/Backspace scopes here; selection stays canvas-driven.
    lines->setFocusPolicy(Qt::ClickFocus);
    lines->installEventFilter(this);
    // Hover cross-highlight, row → canvas (browser renderLinesList row mouseenter).
    lines->setMouseTracking(true);
    lines->viewport()->setMouseTracking(true);
    connect(lines, &QTableWidget::cellEntered, this,
            [this](int row, int) {
              setCanvasHover(canvasHoverPointRow, row);
              emit lineRowHovered(row);
            });
    auto* lh = lines->horizontalHeader();
    // The ordinal and the bin are the points table's own, so both tabs share their edges.
    lh->setSectionResizeMode(LCOL_INDEX, QHeaderView::ResizeToContents);
    lh->setSectionResizeMode(LCOL_NAME, QHeaderView::Stretch);
    for (const auto& [col, w] : {std::pair{LCOL_SWATCH, LINE_COL_SWATCH},
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
    // Bound both ways so a programmatic page change turns the strip too.
    connect(tabBar, &QTabBar::currentChanged, tabs, &QTabWidget::setCurrentIndex);
    connect(tabs, &QTabWidget::currentChanged, tabBar, &QTabBar::setCurrentIndex);
    connect(tabs, &QTabWidget::currentChanged, tabs, [this] { tabs->updateGeometry(); });

    swatchWait = new QTimer(this);
    swatchWait->setSingleShot(true);
    swatchWait->setInterval(support::uiTimings().doubleClickMs);
    connect(swatchWait, &QTimer::timeout, this, [this] { emit lineSwatchPick(swatchRow); });
    connect(lines, &QTableWidget::cellClicked, this, [this](int idx, int col) {
      if (idx < 0) return;
      lines->setCurrentCell(idx, LCOL_INDEX);   // the row Delete/Backspace will act on
      const auto mods = QGuiApplication::keyboardModifiers();
      const bool multi = (mods & (Qt::ControlModifier | Qt::MetaModifier)) &&
                         (mods & Qt::ShiftModifier);
      emit lineListActivated(idx, multi);
      if (col == LCOL_SWATCH && !multi) { swatchRow = idx; swatchWait->start(); }
    });
    connect(lines, &QTableWidget::cellDoubleClicked, this, [this](int idx, int col) {
      if (idx < 0 || col != LCOL_SWATCH) return;
      swatchWait->stop();
      emit lineSwatchReset(idx);
    });

    setWidget(body);
    restyleIcons(palette().color(QPalette::WindowText));

    connect(points, &QTableWidget::cellClicked, this, [this](int row, int col) {
      if (col != COL_DEL) emit pointActivated(row);
    });
    // Guarded against showLine's repopulation.
    connect(points, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
      if (updating || !it) return;
      const int col = it->column();
      if (col != COL_X && col != COL_Y) return;
      bool ok = false;
      const double v = it->text().toDouble(&ok);
      if (ok) emit pointCoordChanged(it->row(), col == COL_X ? 0 : 1, v);
    });

    showLine(nullptr, -1);
  }
}

