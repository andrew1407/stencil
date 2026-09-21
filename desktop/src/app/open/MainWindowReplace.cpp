#include "MainWindow.hpp"
#include "Notifications.hpp"
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "IncognitoOverlay.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "ProjectTransferController.hpp"
#include "ServerClient.hpp"
#include "SelectionPanel.hpp"
#include "AssistantSettingsDialog.hpp"
#include "SettingsDialog.hpp"

#include <QFileInfo>
#include <QJsonObject>

// Replacing an open project's picture, and promoting/publishing an incognito canvas.

namespace stencil::gui {

  // Not a blank or incognito session — there is nothing to keep the same.
  bool MainWindow::canReplaceActive() const {
    return canvas->hasImage() && !incognito
        && (!activeProjectId.isEmpty() || !remoteSession->getLink().address.isEmpty());
  }

  // Same local id / server link; `rename` adopts the file's name, `keepAnnotations` keeps the
  // lines. Server sessions re-upload the `original`.
  void MainWindow::replaceProjectImage(const QString& path, bool rename, bool keepAnnotations) {
    const core::Lines kept = keepAnnotations ? canvas->allLines() : core::Lines{};
    if (!loadLocalImageReset(path)) return;   // loadImage clears lines + provenance, keeps binding
    if (keepAnnotations && !kept.empty()) canvas->setLines(kept);
    if (rename) {
      const QString newName = QFileInfo(path).completeBaseName();
      if (!remoteSession->getLink().address.isEmpty()) {
        remoteSession->getLink().name = newName;
      } else if (Project* pr = findProject(activeProjectId.toStdString())) {
        pr->meta.name = newName.toStdString();
      }
      updateProjectTitle();
    }
    // The original upload + version refresh must finish before saveToActiveProject, whose guard
    // reads it.
    QPointer<MainWindow> self(this);
    auto save = [this, self]() { if (self) saveToActiveProject(); };
    if (!remoteSession->getLink().address.isEmpty())
      replaceServerOriginal(save);
    else
      save();
  }

  // `done` still fires when not server-linked or sync is off.
  void MainWindow::replaceServerOriginal(std::function<void()> done) {
    if (remoteSession->getLink().address.isEmpty() || !settings.syncToServer) {
      if (done) done();
      return;
    }
    stencil::net::ServerClient* c = connections ? connections->find(remoteSession->getLink().address) : nullptr;
    if (!c || !canvas->hasImage()) {
      if (done) done();
      return;
    }
    const int w = canvas->imageWidth();
    const int h = canvas->imageHeight();
    QPointer<MainWindow> self(this);
    c->uploadFileAsync(remoteSession->getLink().id, "original", pngBytes(canvas->getImage()), "png", w, h,
                       [this, self, c, done](bool uok) {
                         if (!self) return;
                         if (!uok) { if (done) done(); return; }
                         c->getProjectAsync(remoteSession->getLink().id,
                                            [this, self, done](bool gok, stencil::net::ServerProject meta,
                                                               QJsonObject) {
                                              if (!self) return;
                                              if (gok) remoteSession->getLink().version = meta.version;
                                              if (done) done();
                                            });
                       });
  }

  // The local twin of publishIncognitoToServer; an explicit user save is not the app writing on
  // its own.
  QString MainWindow::promoteIncognitoToLocal(const QString& name) {
    if (!canvas->hasImage()) return QString();
    if (incognito) {
      incognito = false;
      incognitoOverlay->setActive(false);
      actIncognito->blockSignals(true);
      actIncognito->setChecked(false);
      actIncognito->blockSignals(false);
      updateProjectTitle();
    }
    QString seed = name.trimmed();
    if (seed.isEmpty()) seed = canvas->imageBaseName();
    if (seed.isEmpty()) seed = QStringLiteral("Untitled");
    const QString unique = uniqueLocalProjectName(seed);
    createLocalProject(unique, /*announce=*/false);
    return unique;
  }

  // Mirrors the browser's publishIncognitoToServer (a server-backed project is not incognito).
  void MainWindow::publishIncognitoToServer(const QString& serverUrl) {
    if (!canvas->hasImage()) {
      notify->error("Open an image first");
      return;
    }
    if (incognito) {
      incognito = false;
      incognitoOverlay->setActive(false);
      actIncognito->blockSignals(true);
      actIncognito->setChecked(false);
      actIncognito->blockSignals(false);
    }
    QString name = canvas->imageBaseName();
    if (name.isEmpty()) name = QStringLiteral("Untitled");
    QPointer<MainWindow> self(this);
    // Creation failure notifies and never fires onLinked, so nothing is pushed.
    createServerProject(serverUrl, name, [this, self]() {
      if (!self) return;
      // Pushed regardless of the sync toggle; saveToServer reads settings.syncToServer only at
      // entry, so restoring it right after is safe.
      const bool savedSync = settings.syncToServer;
      settings.syncToServer = true;
      saveToServer();
      settings.syncToServer = savedSync;
      remoteSync->startRemotePoll();   // live co-edit: watch for peers changing this project
      refreshActions();
      updateProjectTitle();
    });
  }

}  // namespace stencil::gui
