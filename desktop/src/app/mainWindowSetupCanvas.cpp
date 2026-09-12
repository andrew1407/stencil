// MainWindow construction, phase 1 of 4 (mainWindow.cpp holds the ctor that calls these,
// in this order): the central canvas column, the app-wide event filters and shared motion
// installs, then the two pinned docks. Construction order is observable and pinned by
// tests/mainWindow.composition.gui.cpp — re-cut these phases freely, reorder nothing.
#include "mainWindow.hpp"
#include "canvasWidget.hpp"
#include "mainWindowHelpers.hpp"   // kCentralSideMargin
#include "overlayScrollArea.hpp"
#include "chatDock.hpp"
#include "selectionPanel.hpp"
#include "selectedLineBar.hpp"
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
    // central canvas in a scroll area, with the "Image Size" bar stacked above it — a
    // plain child widget in a QVBoxLayout (see buildImageInfoBar()). The "Selected Line:"
    // bar is NOT in here — see selectedLineDock_ below, right after selPanel_.
    canvas_ = new CanvasWidget(this);
    // OverlayScrollArea, not a plain QScrollArea: theme.cpp's QScrollBar styling turns off
    // Qt's native transient/overlay scrollbar mode app-wide (see overlayScrollArea.hpp), so
    // this floats two mirror bars over the full viewport for browser parity — the base
    // class's own bars stay hidden and keep acting as the scroll model everywhere below.
    scroll_ = new OverlayScrollArea(this);
    scroll_->setWidget(canvas_);
    scroll_->setAlignment(Qt::AlignCenter);
    scroll_->setFrameShape(QFrame::NoFrame);
    // Named so theme.cpp can give it the browser's boxed canvas-viewport look (border: 2px
    // solid --border-canvas) — a plain NoFrame area read as the picture floating loose, with
    // nothing marking where the canvas area ends.
    scroll_->setObjectName("canvasViewport");
    auto* central = new QWidget(this);
    centralLayout_ = new QVBoxLayout(central);
    // Top margin 0: the Image Size dock above (buildImageInfoBar) now owns its own top/bottom
    // gap (browser parity: .info/.canvas-viewport margin-top). Left 0: coord-status/drop-hint
    // want no left inset — only the canvas gets one, via scroll_'s own wrapper below. Right
    // stays kCentralSideMargin (clears the panel chevron); bottom is a real gap so drop-hint
    // doesn't butt the window edge.
    centralLayout_->setContentsMargins(0, 0, kCentralSideMargin, 14);
    centralLayout_->setSpacing(10);
    // scroll_'s own 6px left inset — wrapped rather than given to scroll_ itself, which has
    // no contentsMargins of its own (a QScrollArea's "margin" is its viewport, already spoken
    // for by canvas centring/zoom math).
    auto* scrollRow = new QHBoxLayout();
    scrollRow->setContentsMargins(6, 0, 0, 0);
    scrollRow->addWidget(scroll_);
    centralLayout_->addLayout(scrollRow, 1);

    // Drag & drop hint below the canvas (browser .drop-hint parity, mainContent.js): repeats
    // what dropEvent already does (image/.json dropped anywhere on the window, Cmd/Ctrl+V
    // pastes). Icon kept as its own label (see applyTheme) since a rasterised glyph can't
    // be re-tinted by the stylesheet the way the text beside it is.
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
    // The canvas is sized to the image, so a zoomed-out image leaves margin around it
    // that belongs to the viewport, not the canvas. Filter the viewport so Ctrl+wheel /
    // trackpad pinch there still zoom (otherwise you can't zoom a small image back up).
    scroll_->viewport()->installEventFilter(this);
    // Pan persistence (browser parity: storage.js's debounced scroll listener) — every
    // drag-scroll, wheel-scroll or keyboard pan lands here via Qt's scrollbar value (setZoom
    // is the equivalent single point for zoom). Same signal also reveals the bars — see
    // revealCanvasScrollbars.
    connect(scroll_->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { revealCanvasScrollbars(); scheduleViewSave(); });
    connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { revealCanvasScrollbars(); scheduleViewSave(); });
    // Invisible at rest; revealCanvasScrollbars() (pan or zoom) fades them in, the idle timer
    // below fades them back out (see the QSS comment by their handle rules). The effects sit
    // on the floating overlay bars — the ones actually painted — not the hidden model bars.
    QScrollBar* vOverlay = canvasScrollBar(Qt::Vertical);
    QScrollBar* hOverlay = canvasScrollBar(Qt::Horizontal);
    vScrollOpacity_ = new QGraphicsOpacityEffect(vOverlay);
    vScrollOpacity_->setOpacity(0.0);
    vOverlay->setGraphicsEffect(vScrollOpacity_);
    hScrollOpacity_ = new QGraphicsOpacityEffect(hOverlay);
    hScrollOpacity_->setOpacity(0.0);
    hOverlay->setGraphicsEffect(hScrollOpacity_);
    // A faded-out bar still covers the viewport's edge strip, so it must not swallow clicks
    // or drag-pans that start there (browser parity: a hidden overlay bar isn't there at all).
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
    // Hovering a bar directly (to find/grab it) must never let it fade out from under the
    // cursor — tracked via eventFilter's Enter/Leave branch for these two watched objects.
    hOverlay->installEventFilter(this);
    vOverlay->installEventFilter(this);
    // Diagonal keyboard panning (browser parity: controlsBinder.js arrowPanTick, ~60fps via
    // rAF) — combines whichever arrows are CURRENTLY held every tick, rather than each key's
    // own native auto-repeat stepping the canvas on its own axis (see keyPressEvent).
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
    // Hover events for the window itself: the canvas↔panel separator is QMainWindow
    // chrome, so the grip overlay's hot state can only be driven from the window's
    // own HoverMove (mainWindowEvents.cpp) — never delivered without this.
    setAttribute(Qt::WA_Hover, true);
    // …and the one that gives every icon button its own hover motion (iconMotion.hpp).
    installIconMotion();
    // …and the one that gives every checkbox its particle toggle and every combo its
    // value exchange, in whatever dialog they are built (controlSwap.hpp).
    installControlSwap();
    // …and the one that gives every dialog its flight, including the QMessageBox
    // confirmations built and exec'd in a single expression (modalReveal.hpp).
    support::installDialogReveal();
    support::installModalDismiss();   // a press outside a modal closes it (browser parity)
  }

  void MainWindow::setupDocks() {
    selPanel_ = new SelectionPanel(this);
    selPanel_->setMinimumWidth(kPanelMinWidth);   // the dock's drag handle stops here
    // Named so QMainWindow::saveState() can persist/restore the dock layout
    // (unnamed docks are skipped with a warning).
    selPanel_->setObjectName("selectionPanelDock");
    addDockWidget(Qt::RightDockWidgetArea, selPanel_);
    // The selection panel owns the right area with a pinned width (setPanelShown drives
    // it via setFixedWidth during the slide animation). Without nesting, Qt offers no
    // drop slot in an area whose sole occupant can't resize — which made it impossible
    // to drag the chat dock onto the right side. Nesting restores those drop slots
    // (the chat dock stacks above/below the panel).
    setDockNestingEnabled(true);

    // "Selected Line:" bar (browser #selection-panel parity). Qt::TopDockWidgetArea, by
    // default, spans the FULL window width — above the left/right dock corners, not just
    // beside them — the same way selPanel_'s RIGHT dock spans the full height; a plain
    // central-widget child would be narrowed by selPanel_'s own width instead (user
    // report). No title bar (an empty stand-in widget suppresses Qt's default one): this
    // strip is its own content, not a draggable/closable panel.
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
