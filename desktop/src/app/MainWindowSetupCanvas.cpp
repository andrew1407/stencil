// MainWindow construction, phase 1 of 4 — the order is pinned by the composition GUI test; reorder
// nothing.
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "mainWindowHelpers.hpp"   // CENTRAL_SIDE_MARGIN
#include "OverlayScrollArea.hpp"
#include "ChatDock.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "../support/controlSwap.hpp"
#include "../support/iconMotion.hpp"
#include "../support/modalReveal.hpp"
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
    canvas_ = new CanvasWidget(this);
    // OverlayScrollArea: theme.cpp disables Qt's transient bars app-wide, so mirror bars float
    // over the viewport (browser parity).
    scroll_ = new OverlayScrollArea(this);
    scroll_->setWidget(canvas_);
    scroll_->setAlignment(Qt::AlignCenter);
    scroll_->setFrameShape(QFrame::NoFrame);
    // Named for theme.cpp's boxed canvas-viewport rule.
    scroll_->setObjectName("canvasViewport");
    auto* central = new QWidget(this);
    centralLayout_ = new QVBoxLayout(central);
    // The Image Size dock owns the top gap; only the canvas gets a left inset (wrapper below);
    // right clears the panel chevron.
    centralLayout_->setContentsMargins(0, 0, CENTRAL_SIDE_MARGIN, 14);
    centralLayout_->setSpacing(10);
    auto* scrollRow = new QHBoxLayout();
    scrollRow->setContentsMargins(6, 0, 0, 0);
    scrollRow->addWidget(scroll_);
    centralLayout_->addLayout(scrollRow, 1);

    // Drop hint (browser .drop-hint, mainContent.js); the icon is its own label so applyTheme can
    // re-tint it.
    dropHint_ = new QWidget(central);
    dropHint_->setObjectName("dropHintBar");
    dropHint_->setAttribute(Qt::WA_StyledBackground, true);
    auto* dropHintLay = new QHBoxLayout(dropHint_);
    dropHintLay->setContentsMargins(10, 6, 10, 6);
    dropHintLay->setSpacing(6);
    dropHintIcon_ = new QLabel(dropHint_);
    dropHintText_ = new QLabel(dropHint_);
    dropHintText_->setObjectName("dropHintLabel");
    dropHintText_->setTextFormat(Qt::RichText);
    dropHintText_->setText(QString("Drag &amp; drop an <b>image</b> or <b>.json</b> anywhere "
                                   "on the window — or paste an image with <b>%1</b>")
                                .arg(hotkey("paste", "Ctrl+V").toHtmlEscaped()));
    dropHintLay->addWidget(dropHintIcon_, 0, Qt::AlignVCenter);
    dropHintLay->addWidget(dropHintText_, 1);
    centralLayout_->addWidget(dropHint_);

    setCentralWidget(central);
    // The viewport margin around a zoomed-out image must still take Ctrl+wheel / pinch zoom.
    scroll_->viewport()->installEventFilter(this);
    // Pan persistence and the scrollbar reveal ride the scrollbar value (browser: storage.js
    // scroll listener).
    connect(scroll_->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { revealCanvasScrollbars(); scheduleViewSave(); });
    connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { revealCanvasScrollbars(); scheduleViewSave(); });
    // The effects sit on the floating overlay bars, not the hidden model bars.
    QScrollBar* vOverlay = canvasScrollBar(Qt::Vertical);
    QScrollBar* hOverlay = canvasScrollBar(Qt::Horizontal);
    vScrollOpacity_ = new QGraphicsOpacityEffect(vOverlay);
    vScrollOpacity_->setOpacity(0.0);
    vOverlay->setGraphicsEffect(vScrollOpacity_);
    hScrollOpacity_ = new QGraphicsOpacityEffect(hOverlay);
    hScrollOpacity_->setOpacity(0.0);
    hOverlay->setGraphicsEffect(hScrollOpacity_);
    // A faded-out bar must not swallow clicks or drag-pans on the edge strip.
    vOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    hOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    scrollbarHideTimer_ = new QTimer(this);
    scrollbarHideTimer_->setSingleShot(true);
    connect(scrollbarHideTimer_, &QTimer::timeout, this, [this] {
      if (scrollbarHovered_) return;   // the pointer is still on one — stay up
      if (vScrollOpacity_) vScrollOpacity_->setOpacity(0.0);
      if (hScrollOpacity_) hScrollOpacity_->setOpacity(0.0);
      canvasScrollBar(Qt::Vertical)->setAttribute(Qt::WA_TransparentForMouseEvents, true);
      canvasScrollBar(Qt::Horizontal)->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    });
    // A hovered bar never fades out from under the cursor (eventFilter's Enter/Leave branch).
    hOverlay->installEventFilter(this);
    vOverlay->installEventFilter(this);
    // Diagonal keyboard panning combines the held arrows per tick (browser: controlsBinder.js
    // arrowPanTick).
    arrowPanTimer_ = new QTimer(this);
    arrowPanTimer_->setInterval(16);
    connect(arrowPanTimer_, &QTimer::timeout, this, [this] {
      if (!panLeftHeld_ && !panRightHeld_ && !panUpHeld_ && !panDownHeld_) {
        arrowPanTimer_->stop();
        return;
      }
      const int speed = panShiftHeld_ ? 22 : 7;
      int dx = 0, dy = 0;
      if (panLeftHeld_) dx -= 1;
      if (panRightHeld_) dx += 1;
      if (panUpHeld_) dy -= 1;
      if (panDownHeld_) dy += 1;
      if (dx || dy)
        scrollTo(scroll_->horizontalScrollBar()->value() + dx * speed,
                 scroll_->verticalScrollBar()->value() + dy * speed);
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
    selPanel_ = new SelectionPanel(this);
    selPanel_->setMinimumWidth(PANEL_MIN_WIDTH);   // the dock's drag handle stops here
    // Named so QMainWindow::saveState() persists the dock layout.
    selPanel_->setObjectName("selectionPanelDock");
    addDockWidget(Qt::RightDockWidgetArea, selPanel_);
    // Nesting: Qt offers no drop slot in an area whose sole occupant has a fixed width, so the
    // chat could not dock right.
    setDockNestingEnabled(true);

    // A top dock spans the full window width, unlike a central child; the empty title widget
    // suppresses Qt's.
    selectedLineBar_ = new SelectedLineBar(this);
    selectedLineDock_ = new QDockWidget(this);
    selectedLineDock_->setObjectName("selectedLineDock");
    selectedLineDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    selectedLineDock_->setTitleBarWidget(new QWidget(selectedLineDock_));
    selectedLineDock_->setWidget(selectedLineBar_);
    addDockWidget(Qt::TopDockWidgetArea, selectedLineDock_);
    selectedLineDock_->setVisible(false);   // shown only once a line is actually selected
  }

}  // namespace stencil::gui
