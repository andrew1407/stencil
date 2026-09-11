#include "mainWindow.hpp"
#include <QToolButton>
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "canvasWidget.hpp"
#include "guiHelpers.hpp"
#include "modalReveal.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "remoteSession.hpp"
#include "projectTransferController.hpp"
#include "serverClient.hpp"
#include "selectionPanel.hpp"

#include <QImage>

// Writing a project's colour through, and the blank page's own.

namespace stencil::gui {

  void MainWindow::setActiveProjectColor(const QString& color) {
    const auto norm = normalizeProjectColor(color);
    if (!norm) {
      notify_->error("Invalid color");
      return;
    }
    // A server-linked session has no local id: push the colour straight to the server.
    if (!remoteSession_->link().id.isEmpty()) {
      const QString n = *norm;
      QPointer<MainWindow> self(this);
      setProjectColorById(remoteSession_->link().id, remoteSession_->link().address, n,
                          [this, self, n](bool ok) {
                            if (!self || !ok) return;
                            remoteSession_->link().color = n;
                            updateProjectTitle();
                          });
      return;
    }
    if (activeProjectId_.isEmpty()) {
      notify_->info("Open or save a project first");
      return;
    }
    QPointer<MainWindow> self(this);
    setProjectColorById(activeProjectId_, QString(), *norm,
                        [this, self](bool ok) { if (self && ok) updateProjectTitle(); });
  }

  void MainWindow::setActiveBlankColor() {
    if (blankColor_.isEmpty() || !canvas_->hasImage()) return;  // blanks only
    QColor init(blankColor_);
    if (!init.isValid()) init = QColor("#ffffff");
    // Qt's own dialog (not the OS-native one), anchored on the Blank swatch button.
    const QColor c =
        support::pickColorAnimated(init, this, "Blank background color", blankColorBtn_);
    if (!c.isValid()) return;
    applyBlankColor(c);
  }

  // The recolour itself, dialog-free — shared by the toolbar button above and
  // the assistant's §10 blankColor op (ChatPlanTarget).
  void MainWindow::applyBlankColor(const QColor& c) {
    if (blankColor_.isEmpty() || !canvas_->hasImage() || !c.isValid()) return;  // blanks only
    // Regenerate the solid fill at the current size, KEEPING the drawn lines (a separate overlay).
    const core::Lines keep = canvas_->lines();
    QImage img(canvas_->imageWidth(), canvas_->imageHeight(), QImage::Format_RGB32);
    img.fill(c);
    canvas_->loadFromImage(img, /*keepZoom=*/true);   // same dimensions — nothing to refit
    setSourceBytes({}, {});  // recoloured blank is synthetic → re-encode on bundle
    if (!keep.empty()) canvas_->setLines(keep);
    blankColor_ = c.name();
    canvas_->setBlankPage(true);  // loadFromImage reset the flag; still a blank
    // Persist the new fill into the active local project's meta + raster so a reopen shows it.
    // (A server-linked session pushes the recoloured original on the next Save.)
    if (Project* pr = findProject(activeProjectId_.toStdString())) {
      pr->meta.blankColor = blankColor_.toStdString();
      pr->meta.blank = true;
      if (!pr->imagePath.isEmpty()) canvas_->originalImage().save(pr->imagePath, "PNG");
      fileStore::saveProjects(projectList_);
    }
    refreshActions();
  }

  void MainWindow::setProjectColorById(const QString& id, const QString& serverUrl,
                                       const QString& color, std::function<void(bool)> done) {
    const auto norm = normalizeProjectColor(color);
    if (!norm) {
      notify_->error("Invalid color");
      if (done) done(false);
      return;
    }
    // Server project: version-guarded PUT UpdateProject{color} (async). Refresh our linked
    // version when it's the open session so a later save doesn't 409.
    if (!serverUrl.isEmpty()) {
      stencil::net::ServerClient* c = remoteSession_->requireClient(serverUrl);
      if (!c) { if (done) done(false); return; }
      const QString n = *norm;
      QPointer<MainWindow> self(this);
      remoteSession_->putVersionGuardedAsync(
          c, id,
          [c, id, n](qint64 version, std::function<void(bool, qint64, bool)> cb) {
            c->updateProjectColorAsync(id, n, version, cb);
          },
          [this, self, c, id, serverUrl, n, done](bool ok, qint64 newVersion) {
            if (!self) return;
            if (!ok) {
              notify_->error(QString("Color update failed: %1").arg(c->lastError()));
              if (done) done(false);
              return;
            }
            if (remoteSession_->link().id == id && remoteSession_->link().address == serverUrl)
              remoteSession_->link().version = newVersion;
            notify_->success(n.isEmpty() ? QStringLiteral("Color reset to theme default")
                                         : QString("Color set to %1").arg(n));
            if (done) done(true);
          });
      return;
    }
    // Local project: update the meta + persist (synchronous).
    Project* pr = findProject(id.toStdString());
    if (!pr) { if (done) done(false); return; }
    pr->meta.color = norm->toStdString();
    fileStore::saveProjects(projectList_);
    refreshDockMenu();
    notify_->success(norm->isEmpty() ? QStringLiteral("Color reset to theme default")
                                     : QString("Color set to %1").arg(*norm));
    if (done) done(true);
  }

}  // namespace stencil::gui
