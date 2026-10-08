#include "MainWindow.hpp"
#include "DockChrome.hpp"
#include "DockZonesOverlay.hpp"
#include "ChatDock.hpp"
#include "modalReveal.hpp"   // topLeftAt, cursorOrigin
#include "theme.hpp"

#include <QGuiApplication>
#include <QMenuBar>
#include <QScreen>
#include <QStatusBar>

// Where a drag puts the chat: the edge bands it drops on, a dock on the band's side, or a float
// with its top-left on the drop, grown out of the cursor (browser chat/panel/api.js openAt).

namespace stencil::gui {

  void DockChrome::showChatDockZones(std::function<bool()> stillDragging) {
    if (!w.overlays.dockZones) w.overlays.dockZones = new DockZonesOverlay(&w);
    // The bands span the window's dock region (between the menu bar and the status bar): the
    // toolbars are the editor's, inside it, so a top band runs above them.
    QRect target = w.rect();
    int top = 0;
    if (w.menuBar() && w.menuBar()->isVisible())
      top = qMax(top, w.menuBar()->geometry().bottom() + 1);
    int bottom = w.height() - 1;
    // findChild, not statusBar(): the accessor lazily creates a status bar, and this window
    // keeps none.
    if (auto* sb = w.findChild<QStatusBar*>(); sb && sb->isVisible())
      bottom = qMin(bottom, sb->geometry().top() - 1);
    if (bottom > top) {
      target.setTop(top);
      target.setBottom(bottom);
    }
    static_cast<DockZonesOverlay*>(w.overlays.dockZones)->beginDrag(
        themePalette(resolveDark(w.settings.themeMode), w.settings.accentColor).accent,
        target, std::move(stillDragging));
  }

  namespace {
    // A close still sliding counts as closed: it finishes leaving at once, so the drop reopens it.
    bool settleClosing(DockChrome& chrome, QDockWidget* dock) {
      if (chrome.chatClosing) {
        chrome.stopChatAnim();
        dock->hide();
      }
      return dock->isVisible();
    }
  }  // namespace

  void DockChrome::openChatDocked(Qt::DockWidgetArea area) {
    if (!w.chatDock || w.tearingDown) return;
    if (settleClosing(*this, w.chatDock)) {
      dockChatTo(area);
      return;
    }
    setChatCompactPopover(false);
    dockChatTo(area);       // hidden, it is placed at once and the open below slides in there
    w.pop.anchor.clear();   // an anchored open is the compact popover
    if (w.acts.chat) w.acts.chat->setChecked(true);
    else setChatShown(true, /*animate=*/true);
  }

  void DockChrome::openChatFloatingAt(const QPoint& global) {
    if (!w.chatDock || w.tearingDown) return;
    const QSize size = chatFloatRect.isValid() ? chatFloatRect.size() : w.chatDock->floatingDefaultSize();
    const QScreen* screen = w.screen() ? w.screen() : QGuiApplication::primaryScreen();
    chatFloatRect = QRect(support::topLeftAt(global, size, screen->availableGeometry()), size);
    if (!settleClosing(*this, w.chatDock)) {
      setChatCompactPopover(false);
      w.chatDock->setFloating(true);
      w.chatDock->setGeometry(chatFloatRect);
      w.pop.anchor.clear();
      chatRevealFrom = support::cursorOrigin(global);   // the open below grows out of the drop
      if (w.acts.chat) w.acts.chat->setChecked(true);
      else setChatShown(true, /*animate=*/true);
      chatRevealFrom = QRect();
      return;
    }
    if (!w.chatDock->isFloating()) {
      toggleChatFloat(support::cursorOrigin(global));   // out of its edge, onto chatFloatRect
      return;
    }
    setChatCompactPopover(false);   // the popover shape becomes the float it was dropped as
    w.chatDock->setGeometry(chatFloatRect);
    w.chatDock->raise();
  }

}  // namespace stencil::gui
