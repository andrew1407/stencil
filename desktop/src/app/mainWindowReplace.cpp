#include "mainWindow.hpp"
#include "notifications.hpp"
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "incognitoOverlay.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "projectsDialog.hpp"
#include "remoteSession.hpp"
#include "remoteSyncController.hpp"
#include "projectTransferController.hpp"
#include "serverClient.hpp"
#include "selectionPanel.hpp"
#include "assistantSettingsDialog.hpp"
#include "settingsDialog.hpp"

#include <QFileInfo>
#include <QJsonObject>

// Replacing an open project's picture, and promoting/publishing an incognito canvas.

namespace stencil::gui {

  // True when the current editor holds a saved/linked project whose image can be swapped in
  // place (not a blank or incognito session — there's nothing to keep the same).
  bool MainWindow::canReplaceActive() const {
    return canvas_->hasImage() && !incognito_
        && (!activeProjectId_.isEmpty() || !remoteSession_->link().address.isEmpty());
  }

  // Replace the CURRENT project's image in place (same local id / server link), instead of
  // making a new project. `rename` adopts the new file's name; `keepAnnotations` keeps the
  // existing lines over the new image. Server sessions also re-upload the `original`.
  void MainWindow::replaceProjectImage(const QString& path, bool rename, bool keepAnnotations) {
    const core::Lines kept = keepAnnotations ? canvas_->allLines() : core::Lines{};
    if (!loadLocalImageReset(path)) return;   // loadImage clears lines + provenance, keeps binding
    if (keepAnnotations && !kept.empty()) canvas_->setLines(kept);
    if (rename) {
      const QString newName = QFileInfo(path).completeBaseName();
      if (!remoteSession_->link().address.isEmpty()) {
        remoteSession_->link().name = newName;
      } else if (Project* pr = findProject(activeProjectId_.toStdString())) {
        pr->meta.name = newName.toStdString();
      }
      updateProjectTitle();
    }
    // Server-linked: re-upload the new original (saveToServer only pushes the result), THEN
    // saveToActiveProject pushes the layout + rendered result. Ordering preserved: the original
    // upload + version refresh must finish before saveToActiveProject (whose guard reads it).
    QPointer<MainWindow> self(this);
    auto save = [this, self]() { if (self) saveToActiveProject(); };
    if (!remoteSession_->link().address.isEmpty())
      replaceServerOriginal(save);
    else
      save();
  }

  // Re-upload the linked server project's `original` with the current canvas image, refreshing
  // the version guard, then invoke `done`. No-op (but `done` still fires) when not server-linked
  // or sync is off (matches edit-in-memory).
  void MainWindow::replaceServerOriginal(std::function<void()> done) {
    if (remoteSession_->link().address.isEmpty() || !settings_.syncToServer) {
      if (done) done();
      return;
    }
    stencil::net::ServerClient* c = connections_ ? connections_->find(remoteSession_->link().address) : nullptr;
    if (!c || !canvas_->hasImage()) {
      if (done) done();
      return;
    }
    const int w = canvas_->imageWidth();
    const int h = canvas_->imageHeight();
    QPointer<MainWindow> self(this);
    c->uploadFileAsync(remoteSession_->link().id, "original", pngBytes(canvas_->image()), "png", w, h,
                       [this, self, c, done](bool uok) {
                         if (!self) return;
                         if (!uok) { if (done) done(); return; }
                         c->getProjectAsync(remoteSession_->link().id,
                                            [this, self, done](bool gok, stencil::net::ServerProject meta,
                                                               QJsonObject) {
                                              if (!self) return;
                                              if (gok) remoteSession_->link().version = meta.version;
                                              if (done) done();
                                            });
                       });
  }

  // Leave incognito and keep what is on screen as a LOCAL project (the local twin of
  // publishIncognitoToServer). Incognito's promise is that the app writes nothing on its own —
  // an explicit "save this" from the user is not the app deciding, so it is honoured here
  // instead of being refused.
  QString MainWindow::promoteIncognitoToLocal(const QString& name) {
    if (!canvas_->hasImage()) return QString();
    if (incognito_) {
      incognito_ = false;
      incognitoOverlay_->setActive(false);
      actIncognito_->blockSignals(true);
      actIncognito_->setChecked(false);
      actIncognito_->blockSignals(false);
      updateProjectTitle();
    }
    QString seed = name.trimmed();
    if (seed.isEmpty()) seed = canvas_->imageBaseName();
    if (seed.isEmpty()) seed = QStringLiteral("Untitled");
    const QString unique = uniqueLocalProjectName(seed);
    createLocalProject(unique, /*announce=*/false);
    return unique;
  }

  // Publish the current incognito session to a server: create the project there, upload the
  // original, link the session, leave incognito, then push the annotated layout + result.
  // Mirrors the browser's publishIncognitoToServer (a server-backed project is not incognito).
  void MainWindow::publishIncognitoToServer(const QString& serverUrl) {
    if (!canvas_->hasImage()) {
      notify_->error("Open an image first");
      return;
    }
    // Leave incognito first so the create/save paths persist normally.
    if (incognito_) {
      incognito_ = false;
      incognitoOverlay_->setActive(false);
      actIncognito_->blockSignals(true);
      actIncognito_->setChecked(false);
      actIncognito_->blockSignals(false);
    }
    QString name = canvas_->imageBaseName();
    if (name.isEmpty()) name = QStringLiteral("Untitled");
    QPointer<MainWindow> self(this);
    // create + upload original + link the session; the tail runs once linked (creation failure
    // notifies and never fires onLinked, so nothing is pushed).
    createServerProject(serverUrl, name, [this, self]() {
      if (!self) return;
      // Push the annotated layout + result now, regardless of the sync toggle (explicit publish).
      // saveToServer reads settings_.syncToServer only at entry (synchronously), so restoring it
      // right after the async save is kicked off is safe.
      const bool savedSync = settings_.syncToServer;
      settings_.syncToServer = true;
      saveToServer();
      settings_.syncToServer = savedSync;
      remoteSync_->startRemotePoll();   // live co-edit: watch for peers changing this project
      refreshActions();
      updateProjectTitle();
    });
  }

}  // namespace stencil::gui
