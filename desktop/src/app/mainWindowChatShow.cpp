#include "mainWindow.hpp"
#include "mainWindowShared.hpp"
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "infoDialog.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "dataExportController.hpp"
#include "serverClient.hpp"
#include "selectionPanel.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/modalChrome.hpp"

#include <QEasingCurve>

// The chat panel's show/hide slide and the splitter it shares with the side panel.

namespace stencil::gui {

  // Hand the points panel back from behind its own dust: the veil must never outlive the
  // flight it belongs to, or an interrupted slide leaves an invisible panel on screen.
  void MainWindow::releasePanelVeil() {
    if (!panelVeil_) return;
    if (selPanel_ && selPanel_->graphicsEffect() == panelVeil_) selPanel_->setGraphicsEffect(nullptr);
    panelVeil_ = nullptr;
  }

  // Pins selPanel_ at `panelRestoreWidth_` (its last settled solo width) for the
  // length of the chat dock's width slide; stopChatAnim releases it.
  void MainWindow::pinPanelWhileSharing(Qt::DockWidgetArea chatArea) {
    // Split first: the slide must trade space with the CANVAS, never fight a panel
    // it isn't even split against.
    ensurePanelChatSplit();
    if (!selPanel_ || selPanel_->isHidden()) return;
    if (chatArea != Qt::LeftDockWidgetArea && chatArea != Qt::RightDockWidgetArea) return;
    if (dockWidgetArea(selPanel_) != chatArea) return;
    if (selPanel_->width() > 120) panelRestoreWidth_ = selPanel_->width();
    selPanel_->setFixedWidth(panelRestoreWidth_ > 120 ? panelRestoreWidth_ : kPanelDefaultWidth);
  }

  // Re-split the panel and chat side-by-side whenever they share an L/R area — plain
  // addDockWidget stacks two docks VERTICALLY unless explicitly split. Idempotent.
  void MainWindow::ensurePanelChatSplit() {
    // Only with the panel ON SCREEN: splitting against a hidden dock doesn't cleanly
    // register and can park it off-screen. setPanelShown's reveal path calls this again.
    if (!selPanel_ || selPanel_->isHidden() || !chatDock_ || chatDock_->isFloating()) return;
    const Qt::DockWidgetArea pArea = dockWidgetArea(selPanel_);
    if (pArea != Qt::LeftDockWidgetArea && pArea != Qt::RightDockWidgetArea) return;
    if (dockWidgetArea(chatDock_) != pArea) return;
    splitDockWidget(selPanel_, chatDock_, Qt::Horizontal);
  }

  // chat dock slide (browser chat-panel parity)
  // The browser panel slides in from its dock edge on open (~0.34 s) and back
  // out on close (~0.26 s), ease-out. Same technique as setPanelShown: pin
  // min==max (setFixedWidth/Height) on every frame so QMainWindow's own layout
  // passes can't override the extent, then release the constraint at the end so
  // the dock stays user-resizable. The axis follows the dock area — width for
  // left/right, height for top/bottom.
  void MainWindow::stopChatAnim() {
    // An INTERRUPTED slide never runs its completion, so the "leaving" state has
    // to be released here too — left set, it silently refused every row menu.
    chatClosing_ = false;
    if (chatDock_) chatDock_->setClosing(false);
    // Always released, whether or not pinPanelWhileSharing actually pinned it —
    // harmless when it didn't, and it must never outlive an interrupted flight.
    if (selPanel_) { selPanel_->setMinimumWidth(kPanelMinWidth); selPanel_->setMaximumWidth(QWIDGETSIZE_MAX); }
    // The dust veil (setChatShown) must never outlive its own flight — an interrupted
    // one (a second toggle mid-slide) hands the dock straight back instead of leaving
    // it invisible behind a cloud that has already stopped moving.
    if (chatVeil_) {
      if (chatDock_ && chatDock_->graphicsEffect() == chatVeil_) chatDock_->setGraphicsEffect(nullptr);
      chatVeil_ = nullptr;
    }
    if (!chatAnim_) return;
    chatAnim_->stop();
    chatAnim_->deleteLater();
    chatAnim_ = nullptr;
    if (!chatDock_) return;
    // Never leave the dock pinned: a stopped slide must hand back the natural
    // constraints, or the dock stays stuck at its mid-animation extent.
    chatDock_->setMinimumWidth(chatNaturalMin_.width());
    chatDock_->setMaximumWidth(QWIDGETSIZE_MAX);
    chatDock_->setMinimumHeight(chatNaturalMin_.height());
    chatDock_->setMaximumHeight(QWIDGETSIZE_MAX);
  }

  void MainWindow::setChatShown(bool show, bool animate) {
    if (!chatDock_) return;
    const bool wasVisible = chatDock_->isVisible();
    stopChatAnim();  // re-entrancy: a second toggle mid-slide wins outright
    // Opening clears the "closing" state — the user is looking at the conversation now.
    if (show) {
      chatClosing_ = false;
      chatDock_->setClosing(false);
    }
    // A full open supersedes the icon-popover shape: re-dock to the area the
    // popover displaced instead of reopening the tiny float at its old spot
    // (browser chatPanel restoreFromCompact parity).
    if (show && chatDock_->isFloating() && chatCompactPopover_) {
      chatCompactPopover_ = false;
      // Same orientation rule as dockChatTo's place(): top/bottom claim their own
      // full-width row, or the hidden selected-line dock squeezes the chat sideways.
      if (chatCompactPrevArea_ == Qt::TopDockWidgetArea
          || chatCompactPrevArea_ == Qt::BottomDockWidgetArea)
        addDockWidget(chatCompactPrevArea_, chatDock_, Qt::Vertical);
      else
        addDockWidget(chatCompactPrevArea_, chatDock_);
      chatDock_->setFloating(false);
    }
    // Floating = its own window: there is no dock edge to slide from, and clamping it
    // would fight the tear-off geometry. It flies out of (and back into) the toolbar
    // icon instead — the same motion every dialog uses.
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
      // An instant show/hide still reflows the dock layout in one shot — pin around it too.
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
    // Reopen at the extent the dock had when it was last dismissed.
    if (!show && wasVisible && extent() > 80) chatRestoreExtent_ = extent();
    const int full = chatRestoreExtent_ > 80 ? chatRestoreExtent_ : (horiz ? 345 : 320);
    // Interrupting a hide part-way: grow from where it actually is, so a fast
    // double-toggle never snaps back to 0 first.
    const int from = show ? (wasVisible && extent() < full ? extent() : 0) : extent();
    const int to = show ? full : 0;
    // The docked shape slides its own size (below); it is ALSO a surface, so it dusts
    // through the shared chatSurfaceFlight — snapshot at FULL extent (a show that
    // starts from a squeezed size still dusts the settled content), veil built there.
    // Same kChatSlideOutMs both ways (chatPanel.js closeMs parity): a 260ms close cut
    // the cloud's flight short and it blinked out instead.
    // Shown BEFORE it is measured: a HIDDEN dock contributes no space to the main
    // window's dock layout, so grabbing it first could catch it at a stale/zero size.
    if (show) chatDock_->show();
    QPointer<gui::DisintegrateOverlay> dustFx =
        chatSurfaceFlight(area, /*gather=*/show, kChatSlideOutMs, pin, show ? full : from);
    if (show) pin(from);
    // A dock mid-slide is already "away" as far as results go: it stays
    // isVisible() for the whole 340ms, and a turn landing in that window used to
    // find a surface that could not actually show it, so it said nothing at all.
    if (!show) { chatClosing_ = true; chatDock_->setClosing(true); }
    chatAnim_ = startExtentSlide(this, from, to,
                                 kChatSlideOutMs,  // browser: 0.34s both ways, matching the dust flight above
                                 pinAndRaiseDust(pin, dustFx), [this, show] {
                                   stopChatAnim();  // releases the pinned constraints
                                   if (!show) chatDock_->hide();
                                   chatClosing_ = false;
                                   chatDock_->setClosing(false);
                                   // Opening the assistant puts the caret where you are about
                                   // to type (browser parity) — after the slide, so the focus
                                   // is not stolen back by the animation's layout work.
                                   if (show) chatDock_->focusInput();
                                 },
                                 // Open keeps the sharp OutCubic throw; close still eases OUT
                                 // (InOutQuad) rather than sharing it — the extent collapsing
                                 // under OutCubic front-loaded the whole thing into the first
                                 // frames and read as a slam, same as it would for the dust.
                                 show ? QEasingCurve::OutCubic : QEasingCurve::InOutQuad);
  }

}  // namespace stencil::gui
