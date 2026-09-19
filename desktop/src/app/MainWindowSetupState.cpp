// MainWindow construction, phase 4 of 4: controllers, then persisted state. Order is pinned; see
// MainWindowSetupCanvas.cpp.
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "ChatDock.hpp"
#include "SelectionPanel.hpp"
#include "guiHelpers.hpp"
#include "mainWindowShared.hpp"   // fetchUrlBytesAsync, TOOLBAR_LAYOUT_VERSION
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "ProjectTransferController.hpp"
#include <QAction>
#include <QDockWidget>
#include <QGuiApplication>
#include <QSignalBlocker>
#include <QStyleHints>
#include <QTimer>
#include <QToolBar>

namespace stencil::gui {

  void MainWindow::setupSyncControllers() {
    session_.attach(this, [this] { saveSessionNow(); }, [this] { saveActiveProjectView(); });

    // Owns the debounce/poll/reload timers + the LiveFeed and composes remoteSession_; the
    // reentrancy flags and the async save/open stay here as hooks.
    remoteSync_ = std::make_unique<RemoteSyncController>(
        this, remoteSession_, &remoteReloading_, &remotePushing_,
        RemoteSyncController::Hooks{
            [this] { return settings_.syncToServer; },
            [this] { return incognito_; },
            [this] { saveToServer(); },
            [this](const QString& a, const QString& i, bool s) { openServerProject(a, i, s); },
            [this] {
              resetToBlankEditor();
              updateProjectTitle();
              notify_->info(QStringLiteral("This server project was deleted"));
            },
        });
    projectTransfer_ = std::make_unique<ProjectTransferController>(
        notify_, canvas_, &settings_, &projectsStore_, &projectList_,
        ProjectTransferController::Hooks{
            [this] { return connections_; },
            [this](const std::string& id) { return findProject(id); },
            [this] { return currentLayoutMeta(); },
            [this](const QString& url, std::function<void(QByteArray)> done) {
              fetchUrlBytesAsync(this, url, std::move(done));
            },
            [this] { return activeProjectId_; },
            [this] { return remoteSession_->link().address; },
            [this] { return remoteSession_->link().id; },
            [this](const QString& serverUrl, const QString& newId, const QString& name,
                   const QString& color, qint64 version) {
              activeProjectId_.clear();
              remoteSession_->link().bind(serverUrl, newId, name, color, version);
              remoteSync_->startRemotePoll();
              updateProjectTitle();
            },
            [this](const QString& id, bool animate) { loadProjectIntoCanvas(id, animate); },
            [this] { refreshActions(); refreshDockMenu(); },
        });
  }

  void MainWindow::restorePersistedState(bool restoreLast) {
    projectList_ = fileStore::loadProjects();
    settings_ = fileStore::loadSettings();
    // The filter/tint and the compare view never carry over an app relaunch (a .stencil round-trip
    // still keeps them); reset before applySettings.
    settings_.imageFilter = Settings{}.imageFilter;
    settings_.filterColor = Settings{}.filterColor;
    applySettings(settings_, false);
    if (restoreLast) restoreSession();  // skipped for a blank incognito editor
    // isHidden(), not isVisible(): the window is not shown yet, so isVisible() is false for every
    // child. The panel reopens at the browser's default width, deferred - resizeDocks needs layout.
    {
      const QPointer<QDockWidget> panel(selPanel_);
      QTimer::singleShot(0, this, [this, panel] {
        if (panel && !panel->isHidden())
          resizeDocks({panel.data()}, {PANEL_DEFAULT_WIDTH}, Qt::Horizontal);
      });
    }
    if (!settings_.windowState.isEmpty()) {
      // Versioned: a state saved against a different toolbar set restores stale row breaks. Bump
      // on every restructure.
      restoreState(QByteArray::fromBase64(settings_.windowState.toLatin1()), TOOLBAR_LAYOUT_VERSION);
      for (QToolBar* tb : findChildren<QToolBar*>()) tb->setVisible(true);
      if (actPanel_) {
        QSignalBlocker b(actPanel_);
        actPanel_->setChecked(!selPanel_->isHidden());
      }
      updatePanelReopenButton();
      // The chat dock is session-transient (browser parity): boots hidden at its default
      // placement.
      if (chatDock_->isFloating()) chatDock_->setFloating(false);
      addDockWidget(Qt::LeftDockWidgetArea, chatDock_);
      chatDock_->hide();
      if (actChat_) {
        QSignalBlocker b(actChat_);
        actChat_->setChecked(false);
      }
    }
    // Deferred so the window paints before the synchronous REST handshakes run.
    if (restoreLast)
      QTimer::singleShot(0, this, &MainWindow::autoConnectServers);
    refreshActions();
    onSelectionChanged();
    updateStatusIdle();
    refreshDockMenu();  // macOS Dock menu (no-op elsewhere)

    // Follow the OS scheme only in "system" mode; colorSchemeChanged arrived in Qt 6.5, older Qt
    // applies it at startup only.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) {
              if (settings_.themeMode == "system") applyTheme();
            });
#endif
  }

}  // namespace stencil::gui
