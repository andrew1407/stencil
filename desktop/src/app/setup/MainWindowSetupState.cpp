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
    session.attach(this, [this] { saveSessionNow(); }, [this] { saveActiveProjectView(); });

    // Owns the debounce/poll/reload timers + the LiveFeed and composes remoteSession; the
    // reentrancy flags and the async save/open stay here as hooks.
    remoteSync = std::make_unique<RemoteSyncController>(
        this, remoteSession, &remoteReloading, &remotePushing, &planRunning,
        RemoteSyncController::Hooks{
            [this] { return settings.syncToServer; },
            [this] { return incognito; },
            [this] { saveToServer(); },
            [this](const QString& a, const QString& i, bool s) { openServerProject(a, i, s); },
            [this] {
              resetToBlankEditor();
              updateProjectTitle();
              notify->info(QStringLiteral("This server project was deleted"));
            },
        });
    projectTransfer = std::make_unique<ProjectTransferController>(
        notify, canvas, &settings, &projectsStore, &projectList,
        ProjectTransferController::Hooks{
            [this] { return connections; },
            [this](const std::string& id) { return findProject(id); },
            [this] { return currentLayoutMeta(); },
            [this](const QString& url, std::function<void(QByteArray)> done) {
              fetchUrlBytesAsync(this, url, std::move(done));
            },
            [this] { return activeProjectId; },
            [this] { return remoteSession->getLink().address; },
            [this] { return remoteSession->getLink().id; },
            [this](const QString& serverUrl, const QString& newId, const QString& name,
                   const QString& color, qint64 version) {
              activeProjectId.clear();
              remoteSession->getLink().bind(serverUrl, newId, name, color, version);
              remoteSync->startRemotePoll();
              updateProjectTitle();
            },
            [this](const QString& id, bool animate) { loadProjectIntoCanvas(id, animate); },
            [this] { refreshActions(); refreshDockMenu(); },
        });
  }

  void MainWindow::restorePersistedState(bool restoreLast) {
    projectList = fileStore::loadProjects();
    settings = fileStore::loadSettings();
    // The filter/tint and the compare view never carry over an app relaunch (a .stencil round-trip
    // still keeps them); reset before applySettings.
    settings.imageFilter = Settings{}.imageFilter;
    settings.filterColor = Settings{}.filterColor;
    applySettings(settings, false);
    if (restoreLast) restoreSession();  // skipped for a blank incognito editor
    // isHidden(), not isVisible(): the window is not shown yet, so isVisible() is false for every
    // child. The panel reopens at the browser's default width, deferred - resizeDocks needs layout.
    {
      const QPointer<QDockWidget> panel(selPanel);
      QTimer::singleShot(0, this, [this, panel] {
        if (panel && !panel->isHidden())
          editor->resizeDocks({panel.data()}, {PANEL_DEFAULT_WIDTH}, Qt::Horizontal);
      });
    }
    if (!settings.windowState.isEmpty()) {
      // Versioned: a state saved against a different toolbar set restores stale row breaks. Bump
      // on every restructure.
      editor->restoreState(QByteArray::fromBase64(settings.windowState.toLatin1()), TOOLBAR_LAYOUT_VERSION);
      for (QToolBar* tb : findChildren<QToolBar*>()) tb->setVisible(true);
      if (actPanel) {
        QSignalBlocker b(actPanel);
        actPanel->setChecked(!selPanel->isHidden());
      }
      updatePanelReopenButton();
      // The chat dock is session-transient (browser parity): boots hidden at its default
      // placement.
      if (chatDock->isFloating()) chatDock->setFloating(false);
      addDockWidget(Qt::LeftDockWidgetArea, chatDock);
      chatDock->hide();
      if (actChat) {
        QSignalBlocker b(actChat);
        actChat->setChecked(false);
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
              if (settings.themeMode == "system") applyTheme();
            });
#endif
  }

}  // namespace stencil::gui
