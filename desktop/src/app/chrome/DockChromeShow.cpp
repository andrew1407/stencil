// DockChrome's show half: the chat panel's show and hide slide, Sliding's pour and Transition's edge picture
// over the window's ground, and the hover resync once the slide is done. Browser twin: ui/chat/panel/openState.js
// and chat/touch.css for the clocks.
#include "MainWindow.hpp"
#include "DockChrome.hpp"
#include "../../support/modal/modalReveal.hpp"
#include "mainWindowShared.hpp"
#include "chatSlideClocks.hpp"
#include "windowMotion.hpp"
#include "SelectionPanel.hpp"
#include "ChatDock.hpp"
#include "theme.hpp"   // CHAT_PAGE_GAP_PX
#include "../../support/genie/GenieGhost.hpp"   // support::genie::flightEase
#include "../../support/genie/SurfaceCover.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/modal/hoverResync.hpp"
#include <QGraphicsOpacityEffect>

namespace stencil::gui {

  namespace {
    // Sliding pours the docked chat on the window flight's own lengths, as the browser's (chat/touch.css).
    int chatPourMs(bool shouldShow) { return shouldShow ? support::genie::OPEN_MS : support::genie::CLOSE_MS; }
    // Transition opens on the browser's reveal, 0.51 s (animations/overlay/overlays.css chatReveal*), not the dust clock.
    constexpr int CHAT_REVEAL_IN_MS = 510;
    int chatOpenMs() {
      return support::motionMode() == support::MotionMode::TRANSITION ? CHAT_REVEAL_IN_MS : chatSlideClocks().inMs;
    }
  }  // namespace

  bool DockChrome::revealDockedChat(bool shouldShow, const std::function<void(int)>& pin, int full) {
    if (!support::isTransitionMotionOk() || !w.chatDock) return false;
    ChatDock* dock = w.chatDock;
    const Qt::DockWidgetArea area = w.dockWidgetArea(dock);
    if (shouldShow) {
      pin(full);   // photographed at the extent it opens to; the caller pins the start before a paint
      settleFoldLayout();
    }
    const QPixmap shot = w.grab(QRect(dock->mapTo(&w, QPoint(0, 0)), dock->size()));
    if (shot.isNull()) return false;
    freezeChatBody(area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea);
    chatCover = groundCover(dock);   // the slide's end (stopChatAnim) hands it back
    // Transition's docked chat as it stands open, held to its edge over the ground while the space slides
    // (browser chatReveal*: a clip over the panel in place, which never re-lays its cards).
    support::genie::edgeCover(chatCover, shot, area);
    return true;
  }

  // The toolbar slid with the chat's space under a still pointer: the icon that was pressed lost the pointer unheard.
  void DockChrome::settleChatHover() {
    support::dropStaleHover(w.buttonForAction(w.acts.chat));
    support::resyncHover(&w);
  }

  // The veil must never outlive the flight, or an interrupted slide leaves an invisible panel.
  void DockChrome::releasePanelVeil() {
    delete panelCover.data();
    if (w.selPanel)   // the parts a pour froze under its cover paint again
      for (QWidget* part : {w.selPanel->titleBarWidget(), w.selPanel->widget()}) if (part) part->setUpdatesEnabled(true);
    if (!panelVeil) return;
    if (w.selPanel && w.selPanel->graphicsEffect() == panelVeil) w.selPanel->setGraphicsEffect(nullptr);
    panelVeil = nullptr;
  }

  // Hands the dock back its own paint the instant the motes are gone: the flight's clock starts before the
  // slide's, and waiting for the slide left a beat drawing neither. Idempotent: stopChatAnim repeats it.
  void DockChrome::dropChatVeil() {
    delete chatCover.data();
    thawChatBody();
    if (!chatVeil) return;
    if (w.chatDock && w.chatDock->graphicsEffect() == chatVeil) w.chatDock->setGraphicsEffect(nullptr);
    chatVeil = nullptr;
  }

  void DockChrome::stopChatAnim() {
    // An INTERRUPTED slide never runs its completion, so "leaving" is released here too.
    chatClosing = false;
    ++chatPourRun;
    if (w.chatDock) w.chatDock->setClosing(false);
    dropChatVeil();
    if (chatGapless && w.chatDock) {   // the gap back beside the dock, the space it stood at unchanged
      const Qt::DockWidgetArea at = w.dockWidgetArea(w.chatDock);
      const bool isHoriz = at != Qt::TopDockWidgetArea && at != Qt::BottomDockWidgetArea;
      chatExtentPin(isHoriz)(std::max(0, (isHoriz ? w.chatDock->width() : w.chatDock->height()) - CHAT_PAGE_GAP_PX));
      setChatGapless(false);
    }
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

  // Floating = its own window: no dock edge to slide from; it flies out of the toolbar icon like a dialog.
  void DockChrome::flyFloatingChat(bool shouldShow) {
    QWidget* icon = w.buttonForAction(w.acts.chat);
    if (shouldShow) {
      support::veilForReveal(*w.chatDock);
      w.chatDock->show();
      support::revealWindow(*w.chatDock, icon, chatRevealFrom);
      return;
    }
    chatClosing = true;
    w.chatDock->setClosing(true);
    support::dismissWindow(*w.chatDock, icon);   // hides at once; the ghost flies
    chatClosing = false;
    w.chatDock->setClosing(false);
  }

  // The chat slide runs chatOpenMs() in and chatCloseMs() out, pinned min==max on every frame so QMainWindow's
  // own layout passes cannot override the extent; released at the end so the dock stays user-resizable.
  void DockChrome::setChatShown(bool shouldShow, bool shouldAnimate) {
    if (!w.chatDock) return;
    if (support::motionReduced()) shouldAnimate = false;   // `none`: a plain show/hide, as the browser's
    const bool wasVisible = w.chatDock->isVisible();
    stopChatAnim();  // re-entrancy: a second toggle mid-slide wins outright
    if (shouldShow) {
      chatClosing = false;
      w.chatDock->setClosing(false);
    }
    // A full open re-docks to the area the popover displaced (browser restoreFromCompact parity).
    if (shouldShow && w.chatDock->isFloating() && chatCompactPopover) {
      setChatCompactPopover(false);
      w.addDockWidget(chatCompactPrevArea, w.chatDock);
      w.chatDock->setFloating(false);
    }
    if (!w.tearingDown && shouldAnimate && w.chatDock->isFloating() && shouldShow != wasVisible) {
      flyFloatingChat(shouldShow);
      return;
    }
    if (w.tearingDown || w.chatDock->isFloating() || !shouldAnimate) {
      w.chatDock->setVisible(shouldShow);
      stopChatAnim();   // no animation follows to release the pinned extent — do it now
      if (shouldShow && !w.tearingDown && !w.chatDock->isFloating()) {
        // What the slide's end would have done: the remembered extent, and the caret.
        const Qt::DockWidgetArea at = w.dockWidgetArea(w.chatDock);
        const bool isHoriz = at != Qt::TopDockWidgetArea && at != Qt::BottomDockWidgetArea;
        const int full = chatOpenExtent(chatRestoreExtent, isHoriz);
        w.resizeDocks({w.chatDock}, {full}, isHoriz ? Qt::Horizontal : Qt::Vertical);
      }
      if (!w.tearingDown) { if (shouldShow) w.chatDock->focusInput(); settleChatHover(); }
      return;
    }
    const Qt::DockWidgetArea area = w.dockWidgetArea(w.chatDock);
    const bool isHoriz = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
    const auto extent = [this, isHoriz] {
      return isHoriz ? w.chatDock->width() : w.chatDock->height();
    };
    const auto pin = chatSpacePin(area);   // lengths below are the whole space: the dock and its page gap
    if (!shouldShow && wasVisible && extent() > 80) chatRestoreExtent = extent();
    const int full = chatOpenExtent(chatRestoreExtent, isHoriz) + CHAT_PAGE_GAP_PX;
    // Interrupting a hide: grow from where it actually is.
    const int from = shouldShow ? (wasVisible && extent() + CHAT_PAGE_GAP_PX < full ? extent() + CHAT_PAGE_GAP_PX : 0)
                          : extent() + CHAT_PAGE_GAP_PX;
    const int to = shouldShow ? full : 0;
    // The dust flight and the slide run one clock (chat/panel.js), and leaving is the quicker
    // of the two. Shown BEFORE it is measured: a HIDDEN dock contributes no space to the layout.
    const int slideMs = shouldShow ? chatOpenMs() : chatCloseMs();
    if (shouldShow) w.chatDock->show();
    const bool wasPoured = pourDockedChat(shouldShow, chatPourMs(shouldShow), pin, shouldShow ? full : from, shouldShow ? from : to);
    const bool wasRevealed = !wasPoured && revealDockedChat(shouldShow, pin, full);
    QPointer<gui::DisintegrateOverlay> dustFx =
        wasPoured || wasRevealed ? nullptr : chatSurfaceFlight(area, /*shouldGather=*/shouldShow, slideMs, pin, shouldShow ? full : from);
    if (wasPoured || wasRevealed) {
      setChatGapless(true);   // photographed at rest; from here the gap slides with it
      pin(from);
      settleFoldLayout();   // its first paint shows the start, never the open layout the pictures were taken at
    } else if (shouldShow) {
      pin(from);
    }
    // A dock mid-slide is already "away" for results: it stays isVisible() for the whole slide.
    if (!shouldShow) { chatClosing = true; w.chatDock->setClosing(true); }
    const support::genie::SpaceClock space = support::genie::spaceClock(shouldShow || !wasPoured, wasPoured ? chatPourMs(shouldShow) : slideMs);
    chatAnim = startExtentSlide(&w, from, to, space.ms,   // the dust flight above runs the same clock
                                 pinAndRaiseDust(pin, dustFx), [this, shouldShow] {
                                   stopChatAnim();  // releases the pinned constraints
                                   if (!shouldShow) w.chatDock->hide();
                                   // Caret after the slide, so the animation's layout work does not steal focus.
                                   if (shouldShow) w.chatDock->focusInput();
                                   settleChatHover();   // the toolbar slid under a still pointer
                                 },
                                 // Close eases OUT (InOutQuad): OutCubic front-loads the collapse and reads as a slam.
                                 wasPoured ? support::genie::flightEase(shouldShow)
                                        : shouldShow ? QEasingCurve::OutCubic : QEasingCurve::InOutQuad,
                                 space.delayMs);
  }

}  // namespace stencil::gui
