#include "MainWindow.hpp"
#include "ProjectTitleController.hpp"
#include "SourceOpener.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "RemoteSession.hpp"
#include "ServerClient.hpp"
#include "../../../support/rowWork.hpp"

#include <QFileInfo>

// Replacing an open project's picture, and promoting/publishing an incognito canvas.

namespace stencil::gui {

  // Not a blank or incognito session — there is nothing to keep the same.
  bool SourceOpener::canReplaceActive() const {
    return w.canvas->hasImage() && !w.incognito
        && (!w.activeProjectId.isEmpty() || !w.remote.session->getLink().address.isEmpty());
  }

  // Same local id / server link; `rename` adopts the file's name, `keepAnnotations` keeps the
  // lines. Server sessions re-upload the `original`.
  void SourceOpener::replaceProjectImage(const QString& path, bool rename, bool keepAnnotations) {
    const core::Lines kept = keepAnnotations ? w.canvas->allLines() : core::Lines{};
    // loadImage clears lines + provenance and keeps the binding; the rest follows once it is in.
    loadLocalImageReset(path, [this, path, rename, keepAnnotations, kept](bool ok) {
      if (!ok) return;
      if (keepAnnotations && !kept.empty()) w.canvas->setLines(kept);
      if (rename) {
        const QString newName = QFileInfo(path).completeBaseName();
        if (!w.remote.session->getLink().address.isEmpty()) {
          w.remote.session->getLink().name = newName;
        } else if (Project* pr = w.findProject(w.activeProjectId.toStdString())) {
          pr->meta.name = newName.toStdString();
        }
        w.projectTitle->updateProjectTitle();
      }
      // The original upload + version refresh must finish before saveToActiveProject, whose guard
      // reads it.
      QPointer<MainWindow> self(&w);
      auto save = [this, self]() { if (self) w.saveToActiveProject(); };
      if (!w.remote.session->getLink().address.isEmpty())
        replaceServerOriginal(save);
      else
        save();
    });
  }

  // `done` still fires when not server-linked or sync is off.
  void SourceOpener::replaceServerOriginal(std::function<void()> done) {
    if (w.remote.session->getLink().address.isEmpty() || !w.settings.syncToServer) {
      if (done) done();
      return;
    }
    stencil::net::ServerClient* c = w.remote.connections ? w.remote.connections->find(w.remote.session->getLink().address) : nullptr;
    if (!c || !w.canvas->hasImage()) {
      if (done) done();
      return;
    }
    const int width = w.canvas->imageWidth();
    const int height = w.canvas->imageHeight();
    const QString addr = w.remote.session->getLink().address;
    const QString id = w.remote.session->getLink().id;
    const QImage original = w.canvas->getImage();
    QPointer<MainWindow> self(&w);
    // The PNG encode runs on the pool; the link is re-checked once it lands.
    support::runOnPool<QByteArray>(
        &w, [original] { return pngBytes(original); },
        [this, self, addr, id, width, height, done](QByteArray bytes) {
          stencil::net::ServerClient* c = w.remote.connections ? w.remote.connections->find(addr) : nullptr;
          if (!c || w.remote.session->getLink().id != id) { if (done) done(); return; }
          const qint64 before = w.remote.session->getLink().version;
          c->uploadFileAsync(id, "original", bytes, "png", width, height, [this, self, c, id, before, done](bool uok) {
            if (!self) return;
            if (!uok) { if (done) done(); return; }
            c->getProjectAsync(id, [this, self, id, before, done](bool gok, stencil::net::ServerProject meta,
                                                                  QJsonObject) {
              if (!self) return;
              if (gok && w.remote.session->getLink().id == id)
                adoptOwnFileVersion(w.remote.session->getLink(), before, meta.version);
              if (done) done();
            });
          });
        });
  }
}  // namespace stencil::gui
