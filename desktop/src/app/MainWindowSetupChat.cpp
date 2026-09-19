// MainWindow construction, phase 2 of 4: the chat dock and its signals. Order is pinned; see
// MainWindowSetupCanvas.cpp.
#include "MainWindow.hpp"
#include "ChatDock.hpp"
#include "ChatMenuPanel.hpp"
#include "DockZonesOverlay.hpp"
#include "SelectionPanel.hpp"
#include "fileStore.hpp"
#include "Notifications.hpp"
#include "theme.hpp"
#include <QDockWidget>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

namespace stencil::gui {

  void MainWindow::setupChatDock() {
    // Dockable on all four sides + floating; hidden and docked left on every launch (browser
    // parity).
    chatDock_ = new ChatDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, chatDock_);
    chatDock_->hide();
    chatDock_->setChatSwapSides(settings_.chatSwapSides);   // the saved "Swap message sides" preference
    // Captured before the show/hide slide pins min==max; setChatShown restores it (a 0 would drop
    // the 260 px floor).
    chatNaturalMin_ = QSize(chatDock_->minimumWidth(), chatDock_->minimumHeight());
    // Toasts and the resize-edge tint follow the dock; the rect alone does not say which side it
    // is on.
    connect(chatDock_, &QDockWidget::dockLocationChanged, this,
            [this] { syncToastInset(); positionChatEdge(); });
    connect(chatDock_, &QDockWidget::topLevelChanged, this,
            [this] { syncToastInset(); positionChatEdge(); });
    connect(chatDock_, &QDockWidget::visibilityChanged, this,
            [this] { syncToastInset(); positionChatEdge(); });
    chatDock_->installEventFilter(this);
    connect(chatDock_, &ChatDock::sendRequested, this, &MainWindow::onChatSend);
    connect(chatDock_, &ChatDock::retryRequested, this, &MainWindow::chatRetryTurn);
    connect(chatDock_, &ChatDock::stopRequested, this, &MainWindow::onChatStop);
    connect(chatDock_, &ChatDock::reconnectRequested, this,
            [this](const QString&) { openConnections(); });
    // Every close on the dock leaves like the toolbar toggle: a docked chat slides into its edge,
    // a float flies to the icon.
    connect(chatDock_, &ChatDock::closeRequested, this, [this] {
      if (!chatDock_ || tearingDown_ || !chatDock_->isVisible()) return;
      pop_.anchor.clear();   // a plain close is never a popover gesture
      if (actChat_ && actChat_->isChecked()) {
        actChat_->setChecked(false);   // its handler runs setChatShown(false, animate)
        return;
      }
      setChatShown(false, /*animate=*/true);
    });
    connect(chatDock_, &ChatDock::clearRequested, this, &MainWindow::onChatClear);
    // Every note the dock displays is mirrored onto the menu panel, late notes included.
    connect(chatDock_, &ChatDock::notePosted, this, [this](const QString& text) {
      chatMirror(QStringLiteral("Note"), text, true);
    });
    connect(chatDock_, &ChatDock::toastRequested, this, [this](const QString& text) {
      if (notify_) notify_->info(text);
    });
    connect(chatDock_, &ChatDock::lateNotePosted, this, &MainWindow::chatMirrorLateNote);
    // Edge drop bands over the central dock region for the whole title-bar drag; the release position
    // decides (browser parity). Without this the panel absorbs the whole freed column when the chat
    // leaves. QPointer-guarded: these also fire during teardown, when the docks may be gone.
    const QPointer<QDockWidget> panelGuard(selPanel_);
    const QPointer<QDockWidget> chatGuard(chatDock_);
    const auto restorePanelWidth = [this, panelGuard, chatGuard] {
      if (tearingDown_ || !panelGuard || !chatGuard || panelGuard->isHidden()) return;
      if (!chatGuard->isHidden() && !chatGuard->isFloating() &&
          dockWidgetArea(chatGuard) == dockWidgetArea(panelGuard))
        return;   // still side by side — leave the split alone
      const int w = panelRestoreWidth_ > 120 ? panelRestoreWidth_ : PANEL_DEFAULT_WIDTH;
      QTimer::singleShot(0, this, [this, panelGuard, w] {
        if (panelGuard && !panelGuard->isHidden())
          resizeDocks({panelGuard.data()}, {w}, Qt::Horizontal);
      });
    };
    connect(chatDock_, &QDockWidget::topLevelChanged, this,
            [this, restorePanelWidth](bool) {
              // A tear-off mid-slide would carry the pinned min==max extent into the floating
              // window.
              stopChatAnim();
              restorePanelWidth();
            });
    connect(chatDock_, &QDockWidget::visibilityChanged, this,
            [restorePanelWidth](bool) { restorePanelWidth(); });
    connect(chatDock_, &QDockWidget::dockLocationChanged, this,
            [restorePanelWidth](Qt::DockWidgetArea) { restorePanelWidth(); });
    // A deliberate layout choice adopts the current shape (browser chatPanel adoptLayout parity).
    connect(chatDock_, &ChatDock::dockRequested, this,
            [this] { setChatCompactPopover(false); });
    connect(chatDock_, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea) { setChatCompactPopover(false); });
    connect(chatDock_, &ChatDock::titleDragStarted, this,
            [this] { setChatCompactPopover(false); });
    connect(chatDock_, &ChatDock::dockRequested, this, &MainWindow::dockChatTo);
    connect(chatDock_, &ChatDock::floatToggleRequested, this, &MainWindow::toggleChatFloat);
    connect(chatDock_, &ChatDock::titleDragStarted, this, [this] {
      if (!dockZones_) dockZones_ = new DockZonesOverlay(this);
      // The bands span the dock region (window minus toolbars and status bar), not the central
      // widget, which shifts with what is docked.
      QRect target = rect();
      int top = 0;
      if (menuBar() && menuBar()->isVisible())
        top = qMax(top, menuBar()->geometry().bottom() + 1);
      for (QToolBar* tb : findChildren<QToolBar*>())
        if (tb->isVisible() && !tb->isFloating() && toolBarArea(tb) == Qt::TopToolBarArea)
          top = qMax(top, tb->geometry().bottom() + 1);
      int bottom = height() - 1;
      // findChild, not statusBar(): the accessor lazily creates a status bar, and this window
      // keeps none.
      if (auto* sb = findChild<QStatusBar*>(); sb && sb->isVisible())
        bottom = qMin(bottom, sb->geometry().top() - 1);
      if (bottom > top) {
        target.setTop(top);
        target.setBottom(bottom);
      }
      static_cast<DockZonesOverlay*>(dockZones_)->beginDrag(
          themePalette(resolveDark(settings_.themeMode), settings_.accentColor).accent,
          target, [this] { return chatDock_ && chatDock_->dragActive(); });
    });
    connect(chatDock_, &ChatDock::titleDragMoved, this, [this](const QPoint& g) {
      if (dockZones_ && dockZones_->isVisible())
        static_cast<DockZonesOverlay*>(dockZones_)->dragTo(g);
    });
    connect(chatDock_, &ChatDock::titleDragFinished, this,
            [this](const QPoint& g) {
      if (!dockZones_ || !dockZones_->isVisible()) return;
      auto* zones = static_cast<DockZonesOverlay*>(dockZones_);
      const int z = zones->zoneAt(g);
      dockZones_->hide();
      if (z < 0) return;  // released outside every band → stay floating
      dockChatTo(DockZonesOverlay::area(z));
    });
    connect(chatDock_, &ChatDock::titleDragCanceled, this, [this] {
      if (dockZones_) dockZones_->hide();
    });
    connect(chatDock_, &ChatDock::videoAttached, this, &MainWindow::onChatVideoAttached);
    connect(chatDock_, &ChatDock::videoDetached, this, [this] {
      chatVideoPath_.clear();
      chatVideoFrames_ = 0;
    });
    connect(chatDock_, &ChatDock::openVariantRequested, this,
            [](const QString& id) { openProjectWindowById(id); });
    // The gear opens the assistant-only dialog (browser llmSettingsModal parity).
    connect(chatDock_, &ChatDock::settingsRequested, this,
            &MainWindow::openAssistantSettings);
    // A lambda, not a member pointer: the overload's defaulted QRect would not survive the
    // signal's QWidget*-only arity.
    connect(chatDock_, &ChatDock::configureProviderRequested, this,
            [this](QWidget* anchor) { openAssistantSettingsFrom(anchor); });
    // The dock re-skinned itself already; persist and mirror to the menu panel, which has no
    // toggle of its own.
    connect(chatDock_, &ChatDock::chatSwapSidesChanged, this, [this](bool on) {
      settings_.chatSwapSides = on;
      fileStore::saveSettings(settings_);
      if (chatMenuPanel_) asChatMenu(chatMenuPanel_)->setChatSwapSides(on);
    });
  }

}  // namespace stencil::gui
