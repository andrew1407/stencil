// MainWindow construction, phase 4 of 4: controllers, then persisted state. Order is pinned; see
// WindowAssembly.cpp.
#include "MainWindow.hpp"
#include "ProjectTitleController.hpp"
#include "WindowAssembly.hpp"
#include "ChatSessionController.hpp"
#include "SelectionPanel.hpp"
#include "mainWindowShared.hpp"   // fetchUrlBytesAsync, TOOLBAR_LAYOUT_VERSION
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "ProjectTransferController.hpp"
#include "StencilFileSync.hpp"
#include "SiblingWindows.hpp"
#include "SharedState.hpp"
#include "ProjectsDialog.hpp"
#include <QStyleHints>
#include <QToolBar>

namespace stencil::gui {

  void WindowAssembly::setupSyncControllers() {
    w.projectTitle = std::make_unique<ProjectTitleController>(
        &w, w.canvas, w.scroll, w.incognito, w.activeProjectId, w.nameBar, w.acts, w.remote, w.notify,
        w.settings,
        ProjectTitleController::Hooks{
            [this] { return w.activeProjectName(); },
            [this](const QString& name, const QString& exceptId) {
              const auto check = w.checkProjectName(name, exceptId);
              return ProjectTitleController::NameCheck{check.ok, QString::fromStdString(check.reason)};
            },
            [this](const QString& id, const QString& name) { return w.parts.projects.renameProjectById(id, name); },
            [this] { return w.parts.projects.currentProjectColor(); },
            [this] { return w.docSource.blankColor; },
            [this](QToolButton* btn, const QColor& color) { w.updateColorSwatch(btn, color); },
            [this] { return w.painted.dark; },
            [this] { w.updateImageSizeInfo(); },
        });
    w.session.attach(&w, [this] { w.saveSessionNow(); }, [this] { w.parts.persistence.saveActiveProjectView(); });

    // Owns the debounce/poll/reload timers + the LiveFeed and composes remoteSession; the
    // reentrancy flags and the async save/open stay here as hooks.
    w.remoteSync = std::make_unique<RemoteSyncController>(
        &w, w.remote.session, &w.remote.reloading, &w.remote.pushing, &w.chatSession->planRunning,
        RemoteSyncController::Hooks{
            [this] { return w.settings.syncToServer; },
            [this] { return w.incognito; },
            [this] { w.parts.projects.saveToServer(); },
            [this](const QString& a, const QString& i, bool s) { w.parts.projects.openServerProject(a, i, s); },
            [this] {
              w.parts.projects.resetToBlankEditor();
              w.projectTitle->updateProjectTitle();
              w.notify->info(QStringLiteral("This server project was deleted"));
            },
            [this](std::function<void()> done) { w.parts.persistence.uploadServerResult(std::move(done)); },
        });
    w.projectTransfer = std::make_unique<ProjectTransferController>(
        w.notify, w.canvas, &w.settings, &w.projectsStore, &w.projectList,
        ProjectTransferController::Hooks{
            [this] { return w.remote.connections; },
            [this](const std::string& id) { return w.findProject(id); },
            [this] { return w.currentLayoutMeta(); },
            [this](const QString& url, std::function<void(QByteArray)> done) {
              fetchUrlBytesAsync(&w, url, std::move(done));
            },
            [this] { return w.activeProjectId; },
            [this] { return w.remote.session->getLink().address; },
            [this] { return w.remote.session->getLink().id; },
            [this](const QString& serverUrl, const QString& newId, const QString& name,
                   const QString& color, qint64 version) {
              w.activeProjectId.clear();
              w.remote.session->getLink().bind(serverUrl, newId, name, color, version);
              w.remoteSync->startRemotePoll();
              w.projectTitle->updateProjectTitle();
            },
            [this](const QString& id, bool animate, std::function<void()> then) {
              if (!w.loadProjectIntoCanvas(id, animate, [then](bool) { then(); })) then();
            },
            [this] { w.refreshActions(); SiblingWindows::refreshDockMenu(w.projectList); },
        });
    w.stencilSync = std::make_unique<StencilFileSync>(
        &w, w.notify,
        StencilFileSync::Hooks{
            [this] { return w.buildStencilBytes(); },
            [this](const QByteArray& text, bool merge) { w.parts.persistence.applyStencilExternal(text, merge); },
            [this](bool linked) {
              if (w.acts.stencilLiveSync) w.acts.stencilLiveSync->setEnabled(linked);
              if (w.acts.deleteProjectFile) w.acts.deleteProjectFile->setEnabled(linked);
            },
        });
  }

  void WindowAssembly::restorePersistedState(bool restoreLast) {
    // The first window reads the files, a sibling shares them; a sibling's save re-applies its
    // settings here (this window keeps its own picture filter and layout) and repaints Projects.
    if (SiblingWindows::openCount() == 0) SharedState::instance().load();
    SiblingWindows::noteOpened(&w);
    w.settings = SharedState::instance().getSettings();
    QObject::connect(&SharedState::instance(), &SharedState::settingsChanged, &w,
                     [this](const Settings& s, QObject* source) {
                       if (source == &w) return;
                       Settings next = s;
                       next.imageFilter = w.settings.imageFilter;
                       next.filterColor = w.settings.filterColor;
                       next.windowState = w.settings.windowState;
                       w.applySettings(next, false);
                     });
    QObject::connect(&SharedState::instance(), &SharedState::projectsChanged, &w, [this](QObject* source) {
      if (source == &w) return;
      w.refreshActions();
      for (ProjectsDialog* dlg : w.findChildren<ProjectsDialog*>()) dlg->setProjects(w.projectList);
    });
    // The filter/tint and the compare view never carry over an app relaunch (a .stencil round-trip
    // still keeps them); reset before applySettings.
    w.settings.imageFilter = Settings{}.imageFilter;
    w.settings.filterColor = Settings{}.filterColor;
    w.applySettings(w.settings, false);
    if (restoreLast) w.parts.persistence.restoreSession();  // skipped for a blank incognito editor
    // isHidden(), not isVisible(): the window is not shown yet, so isVisible() is false for every
    // child. The panel reopens at the browser's default width, deferred - resizeDocks needs layout.
    {
      const QPointer<QDockWidget> panel(w.selPanel);
      QTimer::singleShot(0, &w, [this, panel] {
        if (panel && !panel->isHidden())
          w.editor->resizeDocks({panel.data()}, {w.panelSlide.DEFAULT_WIDTH}, Qt::Horizontal);
      });
    }
    if (!w.settings.windowState.isEmpty()) {
      // Versioned: a state saved against a different toolbar set restores stale row breaks. Bump
      // on every restructure.
      w.editor->restoreState(QByteArray::fromBase64(w.settings.windowState.toLatin1()), TOOLBAR_LAYOUT_VERSION);
      for (QToolBar* tb : w.findChildren<QToolBar*>()) tb->setVisible(true);
      if (w.acts.panel) {
        QSignalBlocker b(w.acts.panel);
        w.acts.panel->setChecked(!w.selPanel->isHidden());
      }
      w.parts.dockChrome.updatePanelReopenButton();
      // The chat dock is session-transient (browser parity): boots hidden at its default
      // placement.
      if (w.chatDock->isFloating()) w.chatDock->setFloating(false);
      w.addDockWidget(Qt::LeftDockWidgetArea, w.chatDock);
      w.chatDock->hide();
      if (w.acts.chat) {
        QSignalBlocker b(w.acts.chat);
        w.acts.chat->setChecked(false);
      }
    }
    // Deferred so the window paints before the synchronous REST handshakes run.
    if (restoreLast)
      QTimer::singleShot(0, &w, [this] { w.parts.persistence.autoConnectServers(); });
    w.refreshActions();
    w.onSelectionChanged();
    w.updateStatusIdle();
    SiblingWindows::refreshDockMenu(w.projectList);  // macOS Dock menu (no-op elsewhere)

    // Follow the OS scheme only in "system" mode; colorSchemeChanged arrived in Qt 6.5, older Qt
    // applies it at startup only.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // macOS re-announces the scheme as the app comes back to the front; only a real flip re-themes.
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &w,
                     [this](Qt::ColorScheme) {
                       if (w.settings.themeMode == "system" &&
                           support::forcedDark().value_or(resolveDark(w.settings.themeMode)) != w.painted.dark)
                         w.applyTheme();
                     });
#endif
  }

}  // namespace stencil::gui
