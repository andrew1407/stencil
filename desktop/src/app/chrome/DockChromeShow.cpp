#include "MainWindow.hpp"
#include "DockChrome.hpp"
#include "../../support/modal/modalReveal.hpp"   // support::motionReduced()
#include "mainWindowShared.hpp"
#include "chatSlideClocks.hpp"
#include "mainWindowHelpers.hpp"
#include "SelectionPanel.hpp"
#include "ChatDock.hpp"
#include <QGraphicsOpacityEffect>

// The chat panel's show/hide slide.

namespace stencil::gui {

  // The veil must never outlive the flight, or an interrupted slide leaves an invisible panel.
  void DockChrome::releasePanelVeil() {
    if (!panelVeil) return;
    if (w.selPanel && w.selPanel->graphicsEffect() == panelVeil) w.selPanel->setGraphicsEffect(nullptr);
    panelVeil = nullptr;
  }

  // Browser chat-panel slide: ~0.34 s in, ~0.26 s out, ease-out. Pin min==max on every frame so QMainWindow's own
  // layout passes can't override the extent; release at the end so the dock stays user-resizable.
  /* Hands the dock back its own paint. Called the instant the motes are gone — the flight's
   * clock starts before the slide's, so waiting for the slide left a beat with the overlay
   * destroyed and the dock still veiled, drawing neither. Idempotent: stopChatAnim repeats it. */
  void DockChrome::dropChatVeil() {
    if (!chatVeil) return;
    if (w.chatDock && w.chatDock->graphicsEffect() == chatVeil) w.chatDock->setGraphicsEffect(nullptr);
    chatVeil = nullptr;
  }

  void DockChrome::stopChatAnim() {
    // An INTERRUPTED slide never runs its completion, so "leaving" is released here too.
    chatClosing = false;
    if (w.chatDock) w.chatDock->setClosing(false);
    dropChatVeil();
    if (!chatAnim) return;
    chatAnim->stop();
    chatAnim->deleteLater();
    chatAnim = nullptr;
    if (!w.chatDock) return;
    // A stopped slide must hand back the natural constraints.
    w.chatDock->setMinimumWidth(chatNaturalMin.width());
    w.chatDock->setMaximumWidth(QWIDGETSIZE_MAX);
    w.chatDock->setMinimumHeight(chatNaturalMin.height());
    w.chatDock->setMaximumHeight(QWIDGETSIZE_MAX);
  }

  void DockChrome::setChatShown(bool show, bool animate) {
    if (!w.chatDock) return;
    if (support::motionReduced()) animate = false;   // `none`: a plain show/hide, as the browser's
    const bool wasVisible = w.chatDock->isVisible();
    stopChatAnim();  // re-entrancy: a second toggle mid-slide wins outright
    if (show) {
      chatClosing = false;
      w.chatDock->setClosing(false);
    }
    // A full open re-docks to the area the popover displaced (browser restoreFromCompact parity).
    if (show && w.chatDock->isFloating() && chatCompactPopover) {
      setChatCompactPopover(false);
      w.addDockWidget(chatCompactPrevArea, w.chatDock);
      w.chatDock->setFloating(false);
    }
    // Floating = its own window: no dock edge to slide from; it flies out of the toolbar icon like a dialog.
    if (!w.tearingDown && animate && w.chatDock->isFloating() && show != wasVisible) {
      QWidget* icon = w.buttonForAction(w.acts.chat);
      if (show) {
        support::veilForReveal(*w.chatDock);
        w.chatDock->show();
        support::revealWindow(*w.chatDock, icon, chatRevealFrom);
      } else {
        chatClosing = true;
        w.chatDock->setClosing(true);
        support::dismissWindow(*w.chatDock, icon);   // hides at once; the ghost flies
        chatClosing = false;
        w.chatDock->setClosing(false);
      }
      return;
    }
    if (w.tearingDown || w.chatDock->isFloating() || !animate) {
      w.chatDock->setVisible(show);
      stopChatAnim();   // no animation follows to release the pinned extent — do it now
      if (show && !w.tearingDown && !w.chatDock->isFloating()) {
        // What the slide's end would have done: the remembered extent, and the caret.
        const Qt::DockWidgetArea at = w.dockWidgetArea(w.chatDock);
        const bool horiz = at != Qt::TopDockWidgetArea && at != Qt::BottomDockWidgetArea;
        const int full = chatOpenExtent(chatRestoreExtent, horiz);
        w.resizeDocks({w.chatDock}, {full}, horiz ? Qt::Horizontal : Qt::Vertical);
      }
      if (show && !w.tearingDown) w.chatDock->focusInput();
      return;
    }
    const Qt::DockWidgetArea area = w.dockWidgetArea(w.chatDock);
    const bool horiz = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
    const auto extent = [this, horiz] {
      return horiz ? w.chatDock->width() : w.chatDock->height();
    };
    const auto pin = chatExtentPin(horiz);
    if (!show && wasVisible && extent() > 80) chatRestoreExtent = extent();
    const int full = chatOpenExtent(chatRestoreExtent, horiz);
    // Interrupting a hide: grow from where it actually is.
    const int from = show ? (wasVisible && extent() < full ? extent() : 0) : extent();
    const int to = show ? full : 0;
    // The dust flight and the slide run one clock (chat/panel.js), and leaving is the quicker
    // of the two. Shown BEFORE it is measured: a HIDDEN dock contributes no space to the layout.
    const int slideMs = show ? chatSlideClocks().inMs : chatCloseMs();
    if (show) w.chatDock->show();
    QPointer<gui::DisintegrateOverlay> dustFx =
        chatSurfaceFlight(area, /*gather=*/show, slideMs, pin, show ? full : from);
    if (show) pin(from);
    // A dock mid-slide is already "away" for results: it stays isVisible() for the whole slide.
    if (!show) { chatClosing = true; w.chatDock->setClosing(true); }
    chatAnim = startExtentSlide(&w, from, to,
                                 slideMs,   // the dust flight above runs the same clock
                                 pinAndRaiseDust(pin, dustFx), [this, show] {
                                   stopChatAnim();  // releases the pinned constraints
                                   if (!show) w.chatDock->hide();
                                   chatClosing = false;
                                   w.chatDock->setClosing(false);
                                   // Caret after the slide, so the animation's layout work does not steal focus.
                                   if (show) w.chatDock->focusInput();
                                 },
                                 // Close eases OUT (InOutQuad): OutCubic front-loads the collapse and reads as a slam.
                                 show ? QEasingCurve::OutCubic : QEasingCurve::InOutQuad);
  }

}  // namespace stencil::gui
