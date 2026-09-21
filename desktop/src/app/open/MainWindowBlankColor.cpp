#include "MainWindow.hpp"
#include <QToolButton>
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"
#include "modalReveal.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "ProjectTransferController.hpp"
#include "ServerClient.hpp"
#include "SelectionPanel.hpp"

#include <QImage>

// Writing a project's colour through, and the blank page's own.

namespace stencil::gui {

  void MainWindow::setActiveProjectColor(const QString& color) {
    const auto norm = normalizeProjectColor(color);
    if (!norm) {
      notify->error("Invalid color");
      return;
    }
    // A server-linked session has no local id: push straight to the server.
    if (!remoteSession->getLink().id.isEmpty()) {
      const QString n = *norm;
      QPointer<MainWindow> self(this);
      setProjectColorById(remoteSession->getLink().id, remoteSession->getLink().address, n,
                          [this, self, n](bool ok) {
                            if (!self || !ok) return;
                            remoteSession->getLink().color = n;
                            updateProjectTitle();
                          });
      return;
    }
    if (activeProjectId.isEmpty()) {
      notify->info("Open or save a project first");
      return;
    }
    QPointer<MainWindow> self(this);
    setProjectColorById(activeProjectId, QString(), *norm,
                        [this, self](bool ok) { if (self && ok) updateProjectTitle(); });
  }

  void MainWindow::setActiveBlankColor() {
    if (blankColor.isEmpty() || !canvas->hasImage()) return;  // blanks only
    QColor init(blankColor);
    if (!init.isValid()) init = QColor("#ffffff");
    // Qt's own dialog, anchored on the Blank swatch button.
    const QColor c =
        support::pickColorAnimated(init, this, "Blank background color", nameBar.blankColorBtn);
    if (!c.isValid()) return;
    applyBlankColor(c);
  }

  // Dialog-free; shared with the assistant's §10 blankColor op.
  void MainWindow::applyBlankColor(const QColor& c) {
    if (blankColor.isEmpty() || !canvas->hasImage() || !c.isValid()) return;  // blanks only
    // KEEPING the drawn lines (a separate overlay).
    const core::Lines keep = canvas->getLines();
    QImage img(canvas->imageWidth(), canvas->imageHeight(), QImage::Format_RGB32);
    img.fill(c);
    canvas->loadFromImage(img, /*keepZoom=*/true);   // same dimensions — nothing to refit
    setSourceBytes({}, {});  // recoloured blank is synthetic → re-encode on bundle
    if (!keep.empty()) canvas->setLines(keep);
    blankColor = c.name();
    canvas->setBlankPage(true);  // loadFromImage reset the flag; still a blank
    // Persist into the local project's meta + raster; a server-linked session pushes on the next Save.
    if (Project* pr = findProject(activeProjectId.toStdString())) {
      pr->meta.blankColor = blankColor.toStdString();
      pr->meta.blank = true;
      if (!pr->imagePath.isEmpty()) canvas->getOriginalImage().save(pr->imagePath, "PNG");
      fileStore::saveProjects(projectList);
    }
    refreshActions();
  }

  void MainWindow::setProjectColorById(const QString& id, const QString& serverUrl,
                                       const QString& color, std::function<void(bool)> done) {
    const auto norm = normalizeProjectColor(color);
    if (!norm) {
      notify->error("Invalid color");
      if (done) done(false);
      return;
    }
    // Version-guarded PUT; refresh our linked version so a later save doesn't 409.
    if (!serverUrl.isEmpty()) {
      stencil::net::ServerClient* c = remoteSession->requireClient(serverUrl);
      if (!c) { if (done) done(false); return; }
      const QString n = *norm;
      QPointer<MainWindow> self(this);
      remoteSession->putVersionGuardedAsync(
          c, id,
          [c, id, n](qint64 version, std::function<void(bool, qint64, bool)> cb) {
            c->updateProjectColorAsync(id, n, version, cb);
          },
          [this, self, c, id, serverUrl, n, done](bool ok, qint64 newVersion) {
            if (!self) return;
            if (!ok) {
              notify->error(QString("Color update failed: %1").arg(c->lastError()));
              if (done) done(false);
              return;
            }
            if (remoteSession->getLink().id == id && remoteSession->getLink().address == serverUrl)
              remoteSession->getLink().version = newVersion;
            notify->success(n.isEmpty() ? QStringLiteral("Color reset to theme default")
                                         : QString("Color set to %1").arg(n));
            if (done) done(true);
          });
      return;
    }
    Project* pr = findProject(id.toStdString());
    if (!pr) { if (done) done(false); return; }
    pr->meta.color = norm->toStdString();
    fileStore::saveProjects(projectList);
    refreshDockMenu();
    notify->success(norm->isEmpty() ? QStringLiteral("Color reset to theme default")
                                     : QString("Color set to %1").arg(*norm));
    if (done) done(true);
  }

}  // namespace stencil::gui
