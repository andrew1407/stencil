// MainWindow construction, phase 4 of 4: the session / live-co-edit / project-transfer
// controllers, then the persisted state — projects, settings, the saved dock layout and the
// live OS-scheme follow. Order is pinned; see mainWindowSetupCanvas.cpp.
#include "mainWindow.hpp"
#include "canvasWidget.hpp"
#include "chatDock.hpp"
#include "selectionPanel.hpp"
#include "guiHelpers.hpp"
#include "mainWindowShared.hpp"   // fetchUrlBytesAsync, kToolbarLayoutVersion
#include "notifications.hpp"
#include "remoteSession.hpp"
#include "remoteSyncController.hpp"
#include "projectTransferController.hpp"
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

    // Live co-edit push/pull engine (remoteSyncController.hpp): owns the debounce/poll/reload
    // timers + the LiveFeed. It composes remoteSession_ directly for the link state + connections;
    // only the reentrancy flags (two &-flags, now async-in-flight state) and the
    // syncToServer/incognito predicates plus saveToServer / openServerProject (both async) stay as
    // hooks here.
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
    // Local↔server project transfer service (projectTransferController.hpp): operates on the
    // project list + store, reaching the session/UI it can't own through these hooks.
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
    // The image filter/tint (and the compare split view, transient and never
    // persisted at all) must not carry over into a freshly reopened desktop app
    // — every OTHER setting here (theme, accent, drawing defaults…)
    // still does. Reset right after load, before applySettings pushes it onto the
    // toolbar's filter combo and canvas: an explicit Save Project As…/Open Project
    // (.stencil) still round-trips the filter/tint the user actually chose to save;
    // only this general app-relaunch persistence skips it.
    settings_.imageFilter = Settings{}.imageFilter;
    settings_.filterColor = Settings{}.filterColor;
    applySettings(settings_, false);
    if (restoreLast) restoreSession();  // skipped for a blank incognito editor
    // Restore the dock layout saved by closeEvent. Toolbars are forced visible
    // afterwards — their collapse is session-transient (the Controls pill), not
    // persisted — and the panel toggle is re-synced to the restored visibility.
    // NOTE: isHidden(), not isVisible() — the window isn't shown yet, so
    // isVisible() is false for every child and would desync the toggle (the
    // "Hide panel" chevron then no-ops because the action is already unchecked).
    // The panel ALWAYS reopens at the browser's own default width: a width dragged in
    // one session is not carried into the next — the saved layout still
    // brings back which docks are where — and with no saved layout at all it also beats
    // whatever Qt derives from the size hints, which left the six coordinate columns
    // narrow enough to elide their digits. Deferred, because QMainWindow only honours
    // resizeDocks once its layout has run.
    {
      const QPointer<QDockWidget> panel(selPanel_);
      QTimer::singleShot(0, this, [this, panel] {
        if (panel && !panel->isHidden())
          resizeDocks({panel.data()}, {kPanelDefaultWidth}, Qt::Horizontal);
      });
    }
    if (!settings_.windowState.isEmpty()) {
      // Versioned: a state saved against a DIFFERENT set of toolbars restores their old
      // row breaks and re-splits the rows. Bump on every toolbar restructure.
      restoreState(QByteArray::fromBase64(settings_.windowState.toLatin1()), kToolbarLayoutVersion);
      for (QToolBar* tb : findChildren<QToolBar*>()) tb->setVisible(true);
      if (actPanel_) {
        QSignalBlocker b(actPanel_);
        actPanel_->setChecked(!selPanel_->isHidden());
      }
      updatePanelReopenButton();
      // The chat dock is deliberately session-transient (browser parity: full
      // reset on reload): whatever an older saved layout says, it boots hidden
      // at its default left placement.
      if (chatDock_->isFloating()) chatDock_->setFloating(false);
      addDockWidget(Qt::LeftDockWidgetArea, chatDock_);
      chatDock_->hide();
      if (actChat_) {
        QSignalBlocker b(actChat_);
        actChat_->setChecked(false);
      }
    }
    // Auto-connect saved servers (if the preference is on) only for the primary restored
    // window; deferred so the window paints before the synchronous REST handshakes run.
    if (restoreLast)
      QTimer::singleShot(0, this, &MainWindow::autoConnectServers);
    refreshActions();
    onSelectionChanged();
    updateStatusIdle();
    refreshDockMenu();  // macOS Dock menu (no-op elsewhere)

    // Live OS-scheme follow: re-tint when the system scheme flips, but only while
    // we're in "system" mode (an explicit light/dark choice wins). The
    // colorSchemeChanged signal / Qt::ColorScheme arrived in Qt 6.5; on older Qt
    // the system theme is still applied at startup, just not followed live.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) {
              if (settings_.themeMode == "system") applyTheme();
            });
#endif
  }

}  // namespace stencil::gui
