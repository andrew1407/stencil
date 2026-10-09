// MainWindow construction, phase 1 of 4 — the order is pinned by the composition GUI test; reorder
// nothing.
#include "MainWindow.hpp"
#include "WindowAssembly.hpp"
#include "ArrowPanner.hpp"
#include "../../support/control/dblReset.hpp"
#include "CanvasWidget.hpp"
#include "mainWindowHelpers.hpp"   // CENTRAL_SIDE_MARGIN
#include "OverlayScrollArea.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "../../support/control/swap/controlSwap.hpp"
#include "../../support/icon/iconMotion.hpp"
#include "../../support/modal/hoverResync.hpp"
#include "../../support/menu/comboAltPeek.hpp"
#include "../../support/tip/altPeek.hpp"
#include <QCursor>
#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QGraphicsOpacityEffect>

namespace stencil::gui {

  namespace {
    // A buttonless move at the cursor, only while it rests over the canvas: a held button is a
    // drag gesture in flight, which a synthetic move must never feed.
    void rehoverAtCursor(QWidget* canvas) {
      if (!canvas || QGuiApplication::mouseButtons() != Qt::NoButton) return;
      const QPoint global = QCursor::pos();
      const QPoint local = canvas->mapFromGlobal(global);
      if (!canvas->rect().contains(local)) return;
      QMouseEvent move(QEvent::MouseMove, QPointF(local), QPointF(global), Qt::NoButton, Qt::NoButton,
                       QGuiApplication::keyboardModifiers());
      QCoreApplication::sendEvent(canvas, &move);
    }
  }  // namespace

  void WindowAssembly::setupCanvasArea() {
    w.canvas = new CanvasWidget(&w);
    // OverlayScrollArea: theme.cpp disables Qt's transient bars app-wide, so mirror bars float
    // over the viewport (browser parity).
    w.scroll = new OverlayScrollArea(&w);
    w.scroll->setWidget(w.canvas);
    w.scroll->setAlignment(Qt::AlignCenter);
    w.scroll->setFrameShape(QFrame::NoFrame);
    // Named for theme.cpp's boxed canvas-viewport rule.
    w.scroll->setObjectName("canvasViewport");
    auto* central = new QWidget(&w);
    w.centralLayout = new QVBoxLayout(central);
    // The Image Size dock owns the top gap; only the canvas gets a left inset (wrapper below);
    // right clears the panel chevron.
    w.centralLayout->setContentsMargins(0, 0, CENTRAL_SIDE_MARGIN, 14);
    w.centralLayout->setSpacing(10);
    auto* scrollRow = new QHBoxLayout();
    scrollRow->setContentsMargins(6, 0, 0, 0);
    scrollRow->addWidget(w.scroll);
    w.centralLayout->addLayout(scrollRow, 1);

    // Drop hint (browser .drop-hint, mainContent.js); the icon is its own label so applyTheme can
    // re-tint it.
    w.tools.dropHint = new QWidget(central);
    w.tools.dropHint->setObjectName("dropHintBar");
    w.tools.dropHint->setAttribute(Qt::WA_StyledBackground, true);
    auto* dropHintLay = new QHBoxLayout(w.tools.dropHint);
    dropHintLay->setContentsMargins(10, 6, 10, 6);
    dropHintLay->setSpacing(6);
    w.tools.dropHintIcon = new QLabel(w.tools.dropHint);
    w.tools.dropHintText = new QLabel(w.tools.dropHint);
    w.tools.dropHintText->setObjectName("dropHintLabel");
    w.tools.dropHintText->setTextFormat(Qt::RichText);
    w.tools.dropHintText->setWordWrap(true);   // browser .drop-hint wraps: its one line must not floor the canvas's width
    w.parts.theme.refreshDropHint();
    dropHintLay->addWidget(w.tools.dropHintIcon, 0, Qt::AlignVCenter);
    dropHintLay->addWidget(w.tools.dropHintText, 1);
    w.centralLayout->addWidget(w.tools.dropHint);

    w.editor = new QMainWindow(&w);
    w.editor->setWindowFlags(Qt::Widget);   // the ctor ORs Qt::Window in; as a child it is a plain widget
    w.editor->setCentralWidget(central);
    w.setCentralWidget(w.editor);
    // The viewport margin around a zoomed-out image must still take Ctrl+wheel / pinch zoom.
    w.scroll->viewport()->installEventFilter(&w);
    // Pan persistence and the scrollbar reveal ride the scrollbar value (browser: storage.js
    // scroll listener).
    // A pan moves the picture under a still cursor: re-hover where it rests (browser canvasPointer.js),
    // so the ring and tooltip never linger over what used to be there.
    const auto scrolled = [this](int) {
      w.parts.view.revealCanvasScrollbars();
      w.parts.persistence.scheduleViewSave();
      rehoverAtCursor(w.canvas);
    };
    QObject::connect(w.scroll->horizontalScrollBar(), &QScrollBar::valueChanged, &w, scrolled);
    QObject::connect(w.scroll->verticalScrollBar(), &QScrollBar::valueChanged, &w, scrolled);
    // The effects sit on the floating overlay bars, not the hidden model bars.
    QScrollBar* vOverlay = w.parts.view.canvasScrollBar(Qt::Vertical);
    QScrollBar* hOverlay = w.parts.view.canvasScrollBar(Qt::Horizontal);
    w.parts.view.vScrollOpacity = new QGraphicsOpacityEffect(vOverlay);
    w.parts.view.vScrollOpacity->setOpacity(0.0);
    vOverlay->setGraphicsEffect(w.parts.view.vScrollOpacity);
    w.parts.view.hScrollOpacity = new QGraphicsOpacityEffect(hOverlay);
    w.parts.view.hScrollOpacity->setOpacity(0.0);
    hOverlay->setGraphicsEffect(w.parts.view.hScrollOpacity);
    // A faded-out bar must not swallow clicks or drag-pans on the edge strip.
    vOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    hOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    w.parts.view.scrollbarHideTimer = new QTimer(&w);
    w.parts.view.scrollbarHideTimer->setSingleShot(true);
    QObject::connect(w.parts.view.scrollbarHideTimer, &QTimer::timeout, &w, [this] {
      if (w.parts.view.scrollbarHovered) return;   // the pointer is still on one — stay up
      if (w.parts.view.vScrollOpacity) w.parts.view.vScrollOpacity->setOpacity(0.0);
      if (w.parts.view.hScrollOpacity) w.parts.view.hScrollOpacity->setOpacity(0.0);
      w.parts.view.canvasScrollBar(Qt::Vertical)->setAttribute(Qt::WA_TransparentForMouseEvents, true);
      w.parts.view.canvasScrollBar(Qt::Horizontal)->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    });
    // A hovered bar never fades out from under the cursor (eventFilter's Enter/Leave branch).
    hOverlay->installEventFilter(&w);
    vOverlay->installEventFilter(&w);
    // Diagonal keyboard panning combines the held arrows per tick (browser: controlsBinder.js
    // arrowPanTick).
    w.arrowPan = std::make_unique<ArrowPanner>(&w, [this](int dx, int dy) {
      w.parts.view.scrollTo(w.scroll->horizontalScrollBar()->value() + dx,
                            w.scroll->verticalScrollBar()->value() + dy);
    });
  }

  void WindowAssembly::installWindowFilters() {
    // App-wide filter so Escape can leave fullscreen from any focus (see eventFilter).
    qApp->installEventFilter(&w);
    // The canvas↔panel separator is QMainWindow chrome: its hover only reaches the window's own
    // HoverMove.
    w.setAttribute(Qt::WA_Hover, true);
    installIconMotion();
    installControlSwap();
    support::installDblReset();   // a double-click puts a selector or a check back to its default
    support::installDialogReveal();
    support::installDialogCentring();   // exec()'s frame guess leaves a frameless modal off-centre
    support::installModalDismiss();   // a press outside a modal closes it (browser parity)
    support::installHoverResync();
    support::installComboAltPeek();
    // A selector's peek opening closes the popover or compact chat it does not sit inside; a
    // glide from a selector's list onto an icon (the list's grab kept its Enter away) opens it.
    support::addGlideHandle(&w, [this](QWidget* opener) {
      QPointer<QToolButton> btn = qobject_cast<QToolButton*>(opener);
      QPointer<QAction> next = w.pop.buttons.value(opener, nullptr);
      if (w.pop.active) {
        if (opener && w.pop.overlay && w.pop.overlay->isAncestorOf(opener)) return;
        w.pop.peekNextButton = btn;   // execMaybePopover opens it once this one is down
        w.pop.peekNextAction = next;
        w.pop.peekAction.clear();
        w.dismissPopover();
        return;
      }
      w.parts.popoverGestures.closeCompactChatFor(opener);
      if (btn && next)
        QTimer::singleShot(0, &w, [this, btn, next] { if (btn && next) w.parts.popoverGestures.altPeekOpen(btn, next); });
    });
  }

  void WindowAssembly::setupDocks() {
    w.selPanel = new SelectionPanel(&w);
    w.selPanel->readOnly = [this] { return w.canvas->compareReadOnly(); };
    w.selPanel->setMinimumWidth(w.panelSlide.MIN_WIDTH);   // the dock's drag handle stops here
    // The dock lays its title and body out in its contents rect, so the gap is page, not card.
    w.selPanel->setContentsMargins(0, 0, w.panelSlide.EDGE_GAP, 0);
    // Named so QMainWindow::saveState() persists the dock layout.
    w.selPanel->setObjectName("selectionPanelDock");
    w.editor->addDockWidget(Qt::RightDockWidgetArea, w.selPanel);
    // Nesting: the Selected Line bar stacks OVER the Image Size dock in the top area.
    w.editor->setDockNestingEnabled(true);

    // A top dock spans the full window width, unlike a central child; the empty title widget
    // suppresses Qt's.
    w.selectedLineBar = new SelectedLineBar(&w);
    w.selectedLineBar->setLineColorDefault([this] { return QColor(w.settings.defaultColor); });
    w.selectedLineBar->setPointColorDefault([this] { return w.parts.styleControls.effectiveDefaultPointColor(); });
    w.selectedLineDock = new QDockWidget(&w);
    w.selectedLineDock->setObjectName("selectedLineDock");
    w.selectedLineDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    w.selectedLineDock->setTitleBarWidget(new QWidget(w.selectedLineDock));
    w.selectedLineDock->setWidget(w.selectedLineBar);
    w.editor->addDockWidget(Qt::TopDockWidgetArea, w.selectedLineDock);
    w.selectedLineDock->setVisible(false);   // shown only once a line is actually selected
  }

}  // namespace stencil::gui
