// MainWindow construction, phase 1 of 4 — the order is pinned by the composition GUI test; reorder
// nothing.
#include "MainWindow.hpp"
#include "../../support/tip/tipContent.hpp"
#include "CanvasWidget.hpp"
#include "mainWindowHelpers.hpp"   // CENTRAL_SIDE_MARGIN
#include "OverlayScrollArea.hpp"
#include "ChatDock.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "../../support/control/swap/controlSwap.hpp"
#include "../../support/icon/iconMotion.hpp"
#include "../../support/modal/modalReveal.hpp"
#include <QApplication>
#include <QDockWidget>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace stencil::gui {

  void MainWindow::setupCanvasArea() {
    canvas = new CanvasWidget(this);
    // OverlayScrollArea: theme.cpp disables Qt's transient bars app-wide, so mirror bars float
    // over the viewport (browser parity).
    scroll = new OverlayScrollArea(this);
    scroll->setWidget(canvas);
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setFrameShape(QFrame::NoFrame);
    // Named for theme.cpp's boxed canvas-viewport rule.
    scroll->setObjectName("canvasViewport");
    auto* central = new QWidget(this);
    centralLayout = new QVBoxLayout(central);
    // The Image Size dock owns the top gap; only the canvas gets a left inset (wrapper below);
    // right clears the panel chevron.
    centralLayout->setContentsMargins(0, 0, CENTRAL_SIDE_MARGIN, 14);
    centralLayout->setSpacing(10);
    auto* scrollRow = new QHBoxLayout();
    scrollRow->setContentsMargins(6, 0, 0, 0);
    scrollRow->addWidget(scroll);
    centralLayout->addLayout(scrollRow, 1);

    // Drop hint (browser .drop-hint, mainContent.js); the icon is its own label so applyTheme can
    // re-tint it.
    dropHint = new QWidget(central);
    dropHint->setObjectName("dropHintBar");
    dropHint->setAttribute(Qt::WA_StyledBackground, true);
    auto* dropHintLay = new QHBoxLayout(dropHint);
    dropHintLay->setContentsMargins(10, 6, 10, 6);
    dropHintLay->setSpacing(6);
    dropHintIcon = new QLabel(dropHint);
    dropHintText = new QLabel(dropHint);
    dropHintText->setObjectName("dropHintLabel");
    dropHintText->setTextFormat(Qt::RichText);
    refreshDropHint();
    dropHintLay->addWidget(dropHintIcon, 0, Qt::AlignVCenter);
    dropHintLay->addWidget(dropHintText, 1);
    centralLayout->addWidget(dropHint);

    setCentralWidget(central);
    // The viewport margin around a zoomed-out image must still take Ctrl+wheel / pinch zoom.
    scroll->viewport()->installEventFilter(this);
    // Pan persistence and the scrollbar reveal ride the scrollbar value (browser: storage.js
    // scroll listener).
    connect(scroll->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { revealCanvasScrollbars(); scheduleViewSave(); });
    connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { revealCanvasScrollbars(); scheduleViewSave(); });
    // The effects sit on the floating overlay bars, not the hidden model bars.
    QScrollBar* vOverlay = canvasScrollBar(Qt::Vertical);
    QScrollBar* hOverlay = canvasScrollBar(Qt::Horizontal);
    vScrollOpacity = new QGraphicsOpacityEffect(vOverlay);
    vScrollOpacity->setOpacity(0.0);
    vOverlay->setGraphicsEffect(vScrollOpacity);
    hScrollOpacity = new QGraphicsOpacityEffect(hOverlay);
    hScrollOpacity->setOpacity(0.0);
    hOverlay->setGraphicsEffect(hScrollOpacity);
    // A faded-out bar must not swallow clicks or drag-pans on the edge strip.
    vOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    hOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    scrollbarHideTimer = new QTimer(this);
    scrollbarHideTimer->setSingleShot(true);
    connect(scrollbarHideTimer, &QTimer::timeout, this, [this] {
      if (scrollbarHovered) return;   // the pointer is still on one — stay up
      if (vScrollOpacity) vScrollOpacity->setOpacity(0.0);
      if (hScrollOpacity) hScrollOpacity->setOpacity(0.0);
      canvasScrollBar(Qt::Vertical)->setAttribute(Qt::WA_TransparentForMouseEvents, true);
      canvasScrollBar(Qt::Horizontal)->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    });
    // A hovered bar never fades out from under the cursor (eventFilter's Enter/Leave branch).
    hOverlay->installEventFilter(this);
    vOverlay->installEventFilter(this);
    // Diagonal keyboard panning combines the held arrows per tick (browser: controlsBinder.js
    // arrowPanTick).
    arrowPanTimer = new QTimer(this);
    arrowPanTimer->setInterval(16);
    connect(arrowPanTimer, &QTimer::timeout, this, [this] {
      if (!panLeftHeld && !panRightHeld && !panUpHeld && !panDownHeld) {
        arrowPanTimer->stop();
        return;
      }
      const int speed = panShiftHeld ? 22 : 7;
      int dx = 0, dy = 0;
      if (panLeftHeld) dx -= 1;
      if (panRightHeld) dx += 1;
      if (panUpHeld) dy -= 1;
      if (panDownHeld) dy += 1;
      if (dx || dy)
        scrollTo(scroll->horizontalScrollBar()->value() + dx * speed,
                 scroll->verticalScrollBar()->value() + dy * speed);
    });
  }

  void MainWindow::installWindowFilters() {
    // App-wide filter so Escape can leave fullscreen from any focus (see eventFilter).
    qApp->installEventFilter(this);
    // The canvas↔panel separator is QMainWindow chrome: its hover only reaches the window's own
    // HoverMove.
    setAttribute(Qt::WA_Hover, true);
    installIconMotion();
    installControlSwap();
    support::installDialogReveal();
    support::installModalDismiss();   // a press outside a modal closes it (browser parity)
  }

  void MainWindow::setupDocks() {
    selPanel = new SelectionPanel(this);
    selPanel->setMinimumWidth(PANEL_MIN_WIDTH);   // the dock's drag handle stops here
    // Named so QMainWindow::saveState() persists the dock layout.
    selPanel->setObjectName("selectionPanelDock");
    addDockWidget(Qt::RightDockWidgetArea, selPanel);
    // Nesting: Qt offers no drop slot in an area whose sole occupant has a fixed width, so the
    // chat could not dock right.
    setDockNestingEnabled(true);

    // A top dock spans the full window width, unlike a central child; the empty title widget
    // suppresses Qt's.
    selectedLineBar = new SelectedLineBar(this);
    selectedLineDock = new QDockWidget(this);
    selectedLineDock->setObjectName("selectedLineDock");
    selectedLineDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    selectedLineDock->setTitleBarWidget(new QWidget(selectedLineDock));
    selectedLineDock->setWidget(selectedLineBar);
    addDockWidget(Qt::TopDockWidgetArea, selectedLineDock);
    selectedLineDock->setVisible(false);   // shown only once a line is actually selected
  }

}  // namespace stencil::gui
