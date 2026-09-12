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
#include "../support/DisintegrateOverlay.hpp"
#include "../support/modalChrome.hpp"

#include <QEasingCurve>

// The chat panel's show/hide slide and the splitter it shares with the side panel.

namespace stencil::gui {

  // The veil must never outlive the flight, or an interrupted slide leaves an invisible panel.
  void MainWindow::releasePanelVeil() {
    if (!panelVeil_) return;
    if (selPanel_ && selPanel_->graphicsEffect() == panelVeil_) selPanel_->setGraphicsEffect(nullptr);
    panelVeil_ = nullptr;
  }

  // Pins selPanel_ at its last settled solo width for the chat slide; stopChatAnim releases it.
  void MainWindow::pinPanelWhileSharing(Qt::DockWidgetArea chatArea) {
    // Split first: the slide must trade space with the CANVAS.
    ensurePanelChatSplit();
    if (!selPanel_ || selPanel_->isHidden()) return;
    if (chatArea != Qt::LeftDockWidgetArea && chatArea != Qt::RightDockWidgetArea) return;
    if (dockWidgetArea(selPanel_) != chatArea) return;
    if (selPanel_->width() > 120) panelRestoreWidth_ = selPanel_->width();
    selPanel_->setFixedWidth(panelRestoreWidth_ > 120 ? panelRestoreWidth_ : PANEL_DEFAULT_WIDTH);
  }

  // Plain addDockWidget stacks two docks VERTICALLY unless explicitly split. Idempotent.
  void MainWindow::ensurePanelChatSplit() {
    // Splitting against a hidden dock can park it off-screen; setPanelShown's reveal calls this again.
    if (!selPanel_ || selPanel_->isHidden() || !chatDock_ || chatDock_->isFloating()) return;
    const Qt::DockWidgetArea pArea = dockWidgetArea(selPanel_);
    if (pArea != Qt::LeftDockWidgetArea && pArea != Qt::RightDockWidgetArea) return;
    if (dockWidgetArea(chatDock_) != pArea) return;
    splitDockWidget(selPanel_, chatDock_, Qt::Horizontal);
  }

  // Browser chat-panel slide: ~0.34 s in, ~0.26 s out, ease-out. Pin min==max on every frame so QMainWindow's own
  // layout passes can't override the extent; release at the end so the dock stays user-resizable.
  void MainWindow::stopChatAnim() {
    // An INTERRUPTED slide never runs its completion, so "leaving" is released here too.
    chatClosing_ = false;
    if (chatDock_) chatDock_->setClosing(false);
    // Always released — it must never outlive an interrupted flight.
    if (selPanel_) { selPanel_->setMinimumWidth(PANEL_MIN_WIDTH); selPanel_->setMaximumWidth(QWIDGETSIZE_MAX); }
    if (chatVeil_) {
      if (chatDock_ && chatDock_->graphicsEffect() == chatVeil_) chatDock_->setGraphicsEffect(nullptr);
      chatVeil_ = nullptr;
    }
    if (!chatAnim_) return;
    chatAnim_->stop();
    chatAnim_->deleteLater();
    chatAnim_ = nullptr;
    if (!chatDock_) return;
    // A stopped slide must hand back the natural constraints.
    chatDock_->setMinimumWidth(chatNaturalMin_.width());
    chatDock_->setMaximumWidth(QWIDGETSIZE_MAX);
    chatDock_->setMinimumHeight(chatNaturalMin_.height());
    chatDock_->setMaximumHeight(QWIDGETSIZE_MAX);
  }

  void MainWindow::setChatShown(bool show, bool animate) {
    if (!chatDock_) return;
    const bool wasVisible = chatDock_->isVisible();
    stopChatAnim();  // re-entrancy: a second toggle mid-slide wins outright
    if (show) {
      chatClosing_ = false;
      chatDock_->setClosing(false);
    }
    // A full open re-docks to the area the popover displaced (browser restoreFromCompact parity).
    if (show && chatDock_->isFloating() && chatCompactPopover_) {
      chatCompactPopover_ = false;
      // Same orientation rule as dockChatTo's place(): top/bottom claim a full-width row.
      if (chatCompactPrevArea_ == Qt::TopDockWidgetArea
          || chatCompactPrevArea_ == Qt::BottomDockWidgetArea)
        addDockWidget(chatCompactPrevArea_, chatDock_, Qt::Vertical);
      else
        addDockWidget(chatCompactPrevArea_, chatDock_);
      chatDock_->setFloating(false);
    }
    // Floating = its own window: no dock edge to slide from; it flies out of the toolbar icon like a dialog.
    if (!tearingDown_ && animate && chatDock_->isFloating() && show != wasVisible) {
      QWidget* icon = buttonForAction(actChat_);
      if (show) {
        chatDock_->show();
        support::revealWindow(*chatDock_, icon);
      } else {
        chatClosing_ = true;
        chatDock_->setClosing(true);
        support::dismissWindow(*chatDock_, icon);   // hides at once; the ghost flies
        chatClosing_ = false;
        chatDock_->setClosing(false);
      }
      return;
    }
    if (tearingDown_ || chatDock_->isFloating() || !animate) {
      // An instant show/hide still reflows the dock layout — pin around it too.
      if (!tearingDown_ && !chatDock_->isFloating()) pinPanelWhileSharing(dockWidgetArea(chatDock_));
      chatDock_->setVisible(show);
      stopChatAnim();   // releases the pin immediately — no animation follows it
      return;
    }
    const Qt::DockWidgetArea area = dockWidgetArea(chatDock_);
    pinPanelWhileSharing(area);   // released by the slide's own stopChatAnim() below
    const bool horiz = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
    const auto extent = [this, horiz] {
      return horiz ? chatDock_->width() : chatDock_->height();
    };
    const auto pin = chatExtentPin(horiz);
    if (!show && wasVisible && extent() > 80) chatRestoreExtent_ = extent();
    const int full = chatRestoreExtent_ > 80 ? chatRestoreExtent_ : (horiz ? 345 : 320);
    // Interrupting a hide: grow from where it actually is.
    const int from = show ? (wasVisible && extent() < full ? extent() : 0) : extent();
    const int to = show ? full : 0;
    // Snapshot at FULL extent, veil built in chatSurfaceFlight. Same CHAT_SLIDE_OUT_MS both ways (chatPanel.js closeMs).
    // Shown BEFORE it is measured: a HIDDEN dock contributes no space to the dock layout.
    if (show) chatDock_->show();
    QPointer<gui::DisintegrateOverlay> dustFx =
        chatSurfaceFlight(area, /*gather=*/show, CHAT_SLIDE_OUT_MS, pin, show ? full : from);
    if (show) pin(from);
    // A dock mid-slide is already "away" for results: it stays isVisible() for the whole slide.
    if (!show) { chatClosing_ = true; chatDock_->setClosing(true); }
    chatAnim_ = startExtentSlide(this, from, to,
                                 CHAT_SLIDE_OUT_MS,  // browser: 0.34s both ways, matching the dust flight above
                                 pinAndRaiseDust(pin, dustFx), [this, show] {
                                   stopChatAnim();  // releases the pinned constraints
                                   if (!show) chatDock_->hide();
                                   chatClosing_ = false;
                                   chatDock_->setClosing(false);
                                   // Caret after the slide, so the animation's layout work does not steal focus.
                                   if (show) chatDock_->focusInput();
                                 },
                                 // Close eases OUT (InOutQuad): OutCubic front-loads the collapse and reads as a slam.
                                 show ? QEasingCurve::OutCubic : QEasingCurve::InOutQuad);
  }

}  // namespace stencil::gui
