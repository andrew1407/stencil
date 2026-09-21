#include "MainWindow.hpp"
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

// The chat panel's show/hide slide and the splitter it shares with the side panel.

namespace stencil::gui {

  // The veil must never outlive the flight, or an interrupted slide leaves an invisible panel.
  void MainWindow::releasePanelVeil() {
    if (!panelVeil) return;
    if (selPanel && selPanel->graphicsEffect() == panelVeil) selPanel->setGraphicsEffect(nullptr);
    panelVeil = nullptr;
  }

  // Pins selPanel at its last settled solo width for the chat slide; stopChatAnim releases it.
  void MainWindow::pinPanelWhileSharing(Qt::DockWidgetArea chatArea) {
    // Split first: the slide must trade space with the CANVAS.
    ensurePanelChatSplit();
    if (!selPanel || selPanel->isHidden()) return;
    if (chatArea != Qt::LeftDockWidgetArea && chatArea != Qt::RightDockWidgetArea) return;
    if (dockWidgetArea(selPanel) != chatArea) return;
    if (!panelAnim && selPanel->width() > 120) panelRestoreWidth = selPanel->width();
    selPanel->setFixedWidth(panelRestoreWidth > 120 ? panelRestoreWidth : PANEL_DEFAULT_WIDTH);
  }

  // Plain addDockWidget stacks two docks VERTICALLY unless explicitly split. Idempotent.
  void MainWindow::ensurePanelChatSplit() {
    // Splitting against a hidden dock can park it off-screen; setPanelShown's reveal calls this again.
    if (!selPanel || selPanel->isHidden() || !chatDock || chatDock->isFloating()) return;
    const Qt::DockWidgetArea pArea = dockWidgetArea(selPanel);
    if (pArea != Qt::LeftDockWidgetArea && pArea != Qt::RightDockWidgetArea) return;
    if (dockWidgetArea(chatDock) != pArea) return;
    splitDockWidget(selPanel, chatDock, Qt::Horizontal);
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
    // Always released — it must never outlive an interrupted flight.
    if (selPanel) { selPanel->setMinimumWidth(PANEL_MIN_WIDTH); selPanel->setMaximumWidth(QWIDGETSIZE_MAX); }
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
    const bool wasVisible = chatDock->isVisible();
    stopChatAnim();  // re-entrancy: a second toggle mid-slide wins outright
    if (show) {
      chatClosing = false;
      chatDock->setClosing(false);
    }
    // A full open re-docks to the area the popover displaced (browser restoreFromCompact parity).
    if (show && chatDock->isFloating() && chatCompactPopover) {
      setChatCompactPopover(false);
      // Same orientation rule as dockChatTo's place(): top/bottom claim a full-width row.
      if (chatCompactPrevArea == Qt::TopDockWidgetArea
          || chatCompactPrevArea == Qt::BottomDockWidgetArea)
        addDockWidget(chatCompactPrevArea, chatDock, Qt::Vertical);
      else
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
      // An instant show/hide still reflows the dock layout — pin around it too.
      if (!tearingDown && !chatDock->isFloating()) pinPanelWhileSharing(dockWidgetArea(chatDock));
      chatDock->setVisible(show);
      stopChatAnim();   // releases the pin immediately — no animation follows it
      return;
    }
    const Qt::DockWidgetArea area = dockWidgetArea(chatDock);
    pinPanelWhileSharing(area);   // released by the slide's own stopChatAnim() below
    const bool horiz = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
    const auto extent = [this, horiz] {
      return horiz ? chatDock->width() : chatDock->height();
    };
    const auto pin = chatExtentPin(horiz);
    if (!show && wasVisible && extent() > 80) chatRestoreExtent = extent();
    const int full = chatRestoreExtent > 80 ? chatRestoreExtent : (horiz ? 345 : 320);
    // Interrupting a hide: grow from where it actually is.
    const int from = show ? (wasVisible && extent() < full ? extent() : 0) : extent();
    const int to = show ? full : 0;
    // The dust flight and the slide run one clock (chatPanel.js), and leaving is the quicker
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
