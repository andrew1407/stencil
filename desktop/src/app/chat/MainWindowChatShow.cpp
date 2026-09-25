#include "MainWindow.hpp"
#include "../../support/modal/modalReveal.hpp"   // support::motionReduced()
#include "mainWindowShared.hpp"
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "InfoDialog.hpp"
#include "LinksDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "DataExportController.hpp"
#include "ServerClient.hpp"
#include "SelectionPanel.hpp"
#include "SettingsDialog.hpp"
#include "ShortcutsDialog.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/modal/modalChrome.hpp"

#include <QEasingCurve>

// The chat panel's show/hide slide.

namespace stencil::gui {

  // The veil must never outlive the flight, or an interrupted slide leaves an invisible panel.
  void MainWindow::releasePanelVeil() {
    if (!panelVeil) return;
    if (selPanel && selPanel->graphicsEffect() == panelVeil) selPanel->setGraphicsEffect(nullptr);
    panelVeil = nullptr;
  }

  // Browser chat-panel slide: ~0.34 s in, ~0.26 s out, ease-out. Pin min==max on every frame so QMainWindow's own
  // layout passes can't override the extent; release at the end so the dock stays user-resizable.
  /* Hands the dock back its own paint. Called the instant the motes are gone — the flight's
   * clock starts before the slide's, so waiting for the slide left a beat with the overlay
   * destroyed and the dock still veiled, drawing neither. Idempotent: stopChatAnim repeats it. */
  void MainWindow::dropChatVeil() {
    if (!chatVeil) return;
    if (chatDock && chatDock->graphicsEffect() == chatVeil) chatDock->setGraphicsEffect(nullptr);
    chatVeil = nullptr;
  }

  void MainWindow::stopChatAnim() {
    // An INTERRUPTED slide never runs its completion, so "leaving" is released here too.
    chatClosing = false;
    if (chatDock) chatDock->setClosing(false);
    dropChatVeil();
    if (!chatAnim) return;
    chatAnim->stop();
    chatAnim->deleteLater();
    chatAnim = nullptr;
    if (!chatDock) return;
    // A stopped slide must hand back the natural constraints.
    chatDock->setMinimumWidth(chatNaturalMin.width());
    chatDock->setMaximumWidth(QWIDGETSIZE_MAX);
    chatDock->setMinimumHeight(chatNaturalMin.height());
    chatDock->setMaximumHeight(QWIDGETSIZE_MAX);
  }

  void MainWindow::setChatShown(bool show, bool animate) {
    if (!chatDock) return;
    if (support::motionReduced()) animate = false;   // `none`: a plain show/hide, as the browser's
    const bool wasVisible = chatDock->isVisible();
    stopChatAnim();  // re-entrancy: a second toggle mid-slide wins outright
    if (show) {
      chatClosing = false;
      chatDock->setClosing(false);
    }
    // A full open re-docks to the area the popover displaced (browser restoreFromCompact parity).
    if (show && chatDock->isFloating() && chatCompactPopover) {
      setChatCompactPopover(false);
      addDockWidget(chatCompactPrevArea, chatDock);
      chatDock->setFloating(false);
    }
    // Floating = its own window: no dock edge to slide from; it flies out of the toolbar icon like a dialog.
    if (!tearingDown && animate && chatDock->isFloating() && show != wasVisible) {
      QWidget* icon = buttonForAction(actChat);
      if (show) {
        chatDock->show();
        support::revealWindow(*chatDock, icon);
      } else {
        chatClosing = true;
        chatDock->setClosing(true);
        support::dismissWindow(*chatDock, icon);   // hides at once; the ghost flies
        chatClosing = false;
        chatDock->setClosing(false);
      }
      return;
    }
    if (tearingDown || chatDock->isFloating() || !animate) {
      chatDock->setVisible(show);
      stopChatAnim();   // no animation follows to release the pinned extent — do it now
      if (show && !tearingDown && !chatDock->isFloating()) {
        // What the slide's end would have done: the remembered extent, and the caret.
        const Qt::DockWidgetArea at = dockWidgetArea(chatDock);
        const bool horiz = at != Qt::TopDockWidgetArea && at != Qt::BottomDockWidgetArea;
        const int full = chatOpenExtent(chatRestoreExtent, horiz);
        resizeDocks({chatDock}, {full}, horiz ? Qt::Horizontal : Qt::Vertical);
      }
      if (show && !tearingDown) chatDock->focusInput();
      return;
    }
    const Qt::DockWidgetArea area = dockWidgetArea(chatDock);
    const bool horiz = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
    const auto extent = [this, horiz] {
      return horiz ? chatDock->width() : chatDock->height();
    };
    const auto pin = chatExtentPin(horiz);
    if (!show && wasVisible && extent() > 80) chatRestoreExtent = extent();
    const int full = chatOpenExtent(chatRestoreExtent, horiz);
    // Interrupting a hide: grow from where it actually is.
    const int from = show ? (wasVisible && extent() < full ? extent() : 0) : extent();
    const int to = show ? full : 0;
    // The dust flight and the slide run one clock (chat/panel.js), and leaving is the quicker
    // of the two. Shown BEFORE it is measured: a HIDDEN dock contributes no space to the layout.
    const int slideMs = show ? CHAT_SLIDE_IN_MS : CHAT_SLIDE_OUT_MS;
    if (show) chatDock->show();
    QPointer<gui::DisintegrateOverlay> dustFx =
        chatSurfaceFlight(area, /*gather=*/show, slideMs, pin, show ? full : from);
    if (show) pin(from);
    // A dock mid-slide is already "away" for results: it stays isVisible() for the whole slide.
    if (!show) { chatClosing = true; chatDock->setClosing(true); }
    chatAnim = startExtentSlide(this, from, to,
                                 slideMs,   // the dust flight above runs the same clock
                                 pinAndRaiseDust(pin, dustFx), [this, show] {
                                   stopChatAnim();  // releases the pinned constraints
                                   if (!show) chatDock->hide();
                                   chatClosing = false;
                                   chatDock->setClosing(false);
                                   // Caret after the slide, so the animation's layout work does not steal focus.
                                   if (show) chatDock->focusInput();
                                 },
                                 // Close eases OUT (InOutQuad): OutCubic front-loads the collapse and reads as a slam.
                                 show ? QEasingCurve::OutCubic : QEasingCurve::InOutQuad);
  }

}  // namespace stencil::gui
