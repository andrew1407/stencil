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
    chatDock = new ChatDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, chatDock);
    chatDock->hide();
    chatDock->setChatSwapSides(settings.chatSwapSides);   // the saved "Swap message sides" preference
    // Captured before the show/hide slide pins min==max; setChatShown restores it (a 0 would drop
    // the 260 px floor).
    chatNaturalMin = QSize(chatDock->minimumWidth(), chatDock->minimumHeight());
    // Toasts and the resize-edge tint follow the dock; the rect alone does not say which side it
    // is on.
    connect(chatDock, &QDockWidget::dockLocationChanged, this,
            [this] { syncToastInset(); positionChatEdge(); });
    connect(chatDock, &QDockWidget::topLevelChanged, this,
            [this] { syncToastInset(); positionChatEdge(); });
    connect(chatDock, &QDockWidget::visibilityChanged, this,
            [this] { syncToastInset(); positionChatEdge(); });
    chatDock->installEventFilter(this);
    connect(chatDock, &ChatDock::sendRequested, this, &MainWindow::onChatSend);
    connect(chatDock, &ChatDock::retryRequested, this, &MainWindow::chatRetryTurn);
    connect(chatDock, &ChatDock::stopRequested, this, &MainWindow::onChatStop);
    connect(chatDock, &ChatDock::reconnectRequested, this,
            [this](const QString&) { openConnections(); });
    // Every close on the dock leaves like the toolbar toggle: a docked chat slides into its edge,
    // a float flies to the icon.
    connect(chatDock, &ChatDock::closeRequested, this, [this] {
      if (!chatDock || tearingDown || !chatDock->isVisible()) return;
      pop.anchor.clear();   // a plain close is never a popover gesture
      if (actChat && actChat->isChecked()) {
        actChat->setChecked(false);   // its handler runs setChatShown(false, animate)
        return;
      }
      setChatShown(false, /*animate=*/true);
    });
    connect(chatDock, &ChatDock::clearRequested, this, &MainWindow::onChatClear);
    // Every note the dock displays is mirrored onto the menu panel, late notes included.
    connect(chatDock, &ChatDock::notePosted, this, [this](const QString& text) {
      chatMirror(QStringLiteral("Note"), text, true);
    });
    connect(chatDock, &ChatDock::toastRequested, this, [this](const QString& text) {
      if (notify) notify->info(text);
    });
    connect(chatDock, &ChatDock::lateNotePosted, this, &MainWindow::chatMirrorLateNote);
    // Edge drop bands over the central dock region for the whole title-bar drag; the release
    // position decides (browser parity). QPointer-guarded: these also fire during teardown.
    const QPointer<QDockWidget> panelGuard(selPanel);
    const QPointer<QDockWidget> chatGuard(chatDock);
    const auto restorePanelWidth = [this, panelGuard, chatGuard] {
      if (tearingDown || !panelGuard || !chatGuard || panelGuard->isHidden()) return;
      if (!chatGuard->isHidden() && !chatGuard->isFloating() &&
          dockWidgetArea(chatGuard) == dockWidgetArea(panelGuard))
        return;   // still side by side — leave the split alone
      const int w = panelRestoreWidth > 120 ? panelRestoreWidth : PANEL_DEFAULT_WIDTH;
      QTimer::singleShot(0, this, [this, panelGuard, w] {
        if (panelGuard && !panelGuard->isHidden())
          resizeDocks({panelGuard.data()}, {w}, Qt::Horizontal);
      });
    };
    connect(chatDock, &QDockWidget::topLevelChanged, this,
            [this, restorePanelWidth](bool) {
              // A tear-off mid-slide would carry the pinned min==max extent into the floating
              // window.
              stopChatAnim();
              restorePanelWidth();
            });
    connect(chatDock, &QDockWidget::visibilityChanged, this,
            [restorePanelWidth](bool) { restorePanelWidth(); });
    connect(chatDock, &QDockWidget::dockLocationChanged, this,
            [restorePanelWidth](Qt::DockWidgetArea) { restorePanelWidth(); });
    // A deliberate layout choice adopts the current shape (browser chatPanel adoptLayout parity).
    connect(chatDock, &ChatDock::dockRequested, this,
            [this] { setChatCompactPopover(false); });
    connect(chatDock, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea) { setChatCompactPopover(false); });
    connect(chatDock, &ChatDock::titleDragStarted, this,
            [this] { setChatCompactPopover(false); });
    connect(chatDock, &ChatDock::dockRequested, this, &MainWindow::dockChatTo);
    connect(chatDock, &ChatDock::floatToggleRequested, this, &MainWindow::toggleChatFloat);
    connect(chatDock, &ChatDock::titleDragStarted, this, [this] {
      if (!dockZones) dockZones = new DockZonesOverlay(this);
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
      static_cast<DockZonesOverlay*>(dockZones)->beginDrag(
          themePalette(resolveDark(settings.themeMode), settings.accentColor).accent,
          target, [this] { return chatDock && chatDock->getDragActive(); });
    });
    connect(chatDock, &ChatDock::titleDragMoved, this, [this](const QPoint& g) {
      if (dockZones && dockZones->isVisible())
        static_cast<DockZonesOverlay*>(dockZones)->dragTo(g);
    });
    connect(chatDock, &ChatDock::titleDragFinished, this,
            [this](const QPoint& g) {
      if (!dockZones || !dockZones->isVisible()) return;
      auto* zones = static_cast<DockZonesOverlay*>(dockZones);
      const int z = zones->zoneAt(g);
      dockZones->hide();
      if (z < 0) return;  // released outside every band → stay floating
      dockChatTo(DockZonesOverlay::area(z));
    });
    connect(chatDock, &ChatDock::titleDragCanceled, this, [this] {
      if (dockZones) dockZones->hide();
    });
    connect(chatDock, &ChatDock::videoAttached, this, &MainWindow::onChatVideoAttached);
    connect(chatDock, &ChatDock::videoDetached, this, [this] {
      chatVideoPath.clear();
      chatVideoFrames = 0;
    });
    connect(chatDock, &ChatDock::openVariantRequested, this,
            [](const QString& id) { openProjectWindowById(id); });
    // The gear opens the assistant-only dialog (browser llmSettingsModal parity).
    connect(chatDock, &ChatDock::settingsRequested, this,
            &MainWindow::openAssistantSettings);
    // A lambda, not a member pointer: the overload's defaulted QRect would not survive the
    // signal's QWidget*-only arity.
    connect(chatDock, &ChatDock::configureProviderRequested, this,
            [this](QWidget* anchor) { openAssistantSettingsFrom(anchor); });
    // The dock re-skinned itself already; persist and mirror to the menu panel, which has no
    // toggle of its own.
    connect(chatDock, &ChatDock::chatSwapSidesChanged, this, [this](bool on) {
      settings.chatSwapSides = on;
      fileStore::saveSettings(settings);
      if (chatMenuPanel) asChatMenu(chatMenuPanel)->setChatSwapSides(on);
    });
  }

}  // namespace stencil::gui
