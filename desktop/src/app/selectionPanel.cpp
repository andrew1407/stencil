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
    // Pinned: the browser panel cannot be torn off; only the toggle hides it.
    setFeatures(QDockWidget::NoDockWidgetFeatures);

    // Browser twin: #toggle-coord-panel, placed in the panel header.
    auto* titleBar = new QWidget(this);
    // Named + WA_StyledBackground so theme.cpp can paint the panel surface; a bare QWidget lets
    // the backdrop through.
    titleBar->setObjectName("selPanelTitle");
    titleBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* titleRow = new QHBoxLayout(titleBar);
    titleRow->setContentsMargins(8, 2, 5, 0);
    // The tabs live in the header beside the chevron (browser .coord-panel-header holds only tabs
    // + toggle); the QTabWidget hides its own bar.
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
    // On the panel surface it takes the browser's #toggle-coord-panel treatment, not the floating
    // chevron's slab.
    collapseBtn_->setObjectName("panelCollapseBtn");   // styled in theme.cpp
    // A fold chevron's angle is state, not hover feedback (iconMotion.json trigger.excluded).
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

    // Shown while 2+ lines are multi-selected; the "Selected Line:" bar stays hidden then.
    multiLabel_ = new QLabel(body);
    multiLabel_->setWordWrap(true);
    multiLabel_->setStyleSheet("color: palette(highlight); font-weight: 600;");
    multiLabel_->setVisible(false);
    layout->addWidget(multiLabel_);

    // browser mainContent.js coord-tabs; the Lines tab mirrors renderLinesList / #lines-list.
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
    points_->setEditTriggers(QAbstractItemView::DoubleClicked |
                             QAbstractItemView::EditKeyPressed);
    points_->installEventFilter(this);
    // Hover cross-highlight, row → canvas (browser coordTable.js row mouseenter); Leave is caught
    // in eventFilter.
    points_->setMouseTracking(true);
    points_->viewport()->setMouseTracking(true);
    connect(points_, &QTableWidget::cellEntered, this,
            [this](int row, int) { emit pointRowHovered(row); });
    auto* hh = points_->horizontalHeader();
    hh->setSectionResizeMode(ColIndex, QHeaderView::ResizeToContents);
    for (int c : {ColX, ColY, ColPageX, ColPageY}) hh->setSectionResizeMode(c, QHeaderView::Stretch);
    hh->setSectionResizeMode(ColDel, QHeaderView::Fixed);
    // The browser's trailing cell (mainContent.js: `width:28px;padding:4px`).
    points_->setColumnWidth(ColDel, 28);
    hh->setHighlightSections(false);
    // layout.css .coordinates-table th { text-align: left }
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // The browser table draws a hairline around every cell (--border-coord); the grid is Qt's way.
    points_->setShowGrid(true);
    ptsLay->addWidget(points_, 1);

    tabs_->addTab(ptsTab, "Points");

    // browser #lines-list: colour chip · "Line N · M pts" · 🗑; Ctrl/⌘+Shift toggles multi-select.
    auto* linesTab = new QWidget(tabs_);
    auto* linesLay = new QVBoxLayout(linesTab);
    linesLay->setContentsMargins(0, 6, 0, 0);
    lines_ = new QListWidget(linesTab);
    lines_->setObjectName("linesList");
    lines_->setSelectionMode(QAbstractItemView::NoSelection);  // selection is driven by the canvas
    // ClickFocus so a bare Delete/Backspace scopes to this list; selection stays canvas-driven,
    // only the current row moves.
    lines_->setFocusPolicy(Qt::ClickFocus);
    lines_->installEventFilter(this);
    // Hover cross-highlight, row → canvas (browser renderLinesList row mouseenter).
    lines_->setMouseTracking(true);
    lines_->viewport()->setMouseTracking(true);
    connect(lines_, &QListWidget::itemEntered, this,
            [this](QListWidgetItem* it) { emit lineRowHovered(lines_->row(it)); });
    linesLay->addWidget(lines_, 1);
    tabs_->addTab(linesTab, "Lines");
    // Bound both ways so a programmatic page change turns the strip too.
    connect(tabBar_, &QTabBar::currentChanged, tabs_, &QTabWidget::setCurrentIndex);
    connect(tabs_, &QTabWidget::currentChanged, tabBar_, &QTabBar::setCurrentIndex);

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

    connect(points_, &QTableWidget::cellClicked, this, [this](int row, int col) {
      if (col != ColDel) emit pointActivated(row);
    });
    // Guarded against showLine's repopulation.
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

