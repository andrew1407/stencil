#include "mainWindow.hpp"
#include "mainWindowShared.hpp"
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "cropDialog.hpp"
#include "guiHelpers.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "remoteSession.hpp"
#include "projectTransferController.hpp"
#include "serverClient.hpp"
#include "selectionPanel.hpp"
#include "assistantSettingsDialog.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"

#include <QJsonObject>

// Saving to a server project, including the two-editor layout merge.

namespace stencil::gui {

  namespace {
    // Dedup key for merging two editors' layouts (mirrors the browser's mergeLines).
    QString lineKey(const core::Line& l) {
      // pointColor rides second (browser lineDedupeKey order); unset keys as "" so a server round-
      // trip still dedupes.
      QString k = QString("%1|%2|%3|%4|%5|%6|%7")
                      .arg(QString::fromStdString(l.color))
                      .arg(QString::fromStdString(l.pointColor))
                      .arg(l.thickness).arg(l.pointSize)
                      .arg(QString::fromStdString(l.style))
                      .arg(l.locked ? 1 : 0)
                      .arg(QString::fromStdString(l.fillColor));
      for (const auto& p : l.points) k += QString(";%1,%2").arg(p.x).arg(p.y);
      return k;
    }
  }  // namespace

  // Version-guarded name/layout PUT, then the render upload; a 409 leaves the link untouched.
  // Mirrors the browser's saveToServer.
  void MainWindow::saveToServer() {
    if (!settings_.syncToServer) return;  // sync off — fetched project stays edit-in-memory only
    stencil::net::ServerClient* c = remoteSession_->requireClient(
        remoteSession_->link().address, QString("Not connected to %1 — reconnect it first").arg(remoteSession_->link().address));
    if (!c) return;
    // remotePushing_ guards the poll for the whole async push; the shared clearer drops it on
    // every exit path.
    remotePushing_ = true;
    auto pushGuard = std::shared_ptr<void>(nullptr, [self = QPointer<MainWindow>(this)](void*) {
      if (self) self->remotePushing_ = false;
    });
    QPointer<MainWindow> self(this);
    const int w = canvas_->imageWidth();
    const int h = canvas_->imageHeight();
    // On a version conflict, union-merge the server's lines with ours and retry (up to 6) so a
    // tight race still converges.
    typedef stencil::net::ServerClient::GuardOutcome GO;
    stencil::net::ServerClient::runGuardedWriteAsync(
        /*attempts=*/6, /*startVersion=*/remoteSession_->link().version,
        [this, self, c, w, h, pushGuard](qint64 version, std::function<void(GO)> cb) {
          if (!self) { cb(GO::FAILED); return; }
          const QJsonObject layout =
              fileStore::buildLayoutJson(w, h, canvas_->allLines(),
                                         settings_.imageFilter, settings_.filterColor,
                                         canvas_->cropRect(), canvas_->rotationQuarters(),
                                         currentLayoutMeta());
          c->updateProjectAsync(
              remoteSession_->link().id, remoteSession_->link().name, layout, version,
              [this, self, c, cb](bool ok, qint64 newVersion, bool conflict) {
                if (!self) { cb(GO::FAILED); return; }
                if (ok) {
                  remoteSession_->link().version = newVersion;
                  cb(GO::COMMITTED);
                  return;
                }
                if (!conflict) {
                  notify_->error(QString("Server save failed — %1").arg(c->lastError()));
                  cb(GO::FAILED);
                  return;
                }
                cb(GO::CONFLICT);
              });
        },
        [this, self, c, pushGuard](qint64 /*version*/, std::function<void(bool, qint64)> cb) {
          if (!self) { cb(false, 0); return; }
          c->getProjectAsync(
              remoteSession_->link().id,
              [this, self, cb](bool ok, stencil::net::ServerProject meta, QJsonObject srvLayout) {
                if (!self || !ok) { cb(false, 0); return; }  // give up (re-read failed)
                int sw = 0, sh = 0;
                core::Lines mlines = fileStore::parseLayoutJson(srvLayout, sw, sh);
                QSet<QString> seen;
                for (const auto& l : mlines) seen.insert(lineKey(l));
                for (const auto& l : canvas_->allLines()) {
                  const QString k = lineKey(l);
                  if (!seen.contains(k)) { mlines.push_back(l); seen.insert(k); }
                }
                {  // apply merged lines (+ peer filter) locally without re-triggering a push.
                  // The reload flag brackets the synchronous block (onCanvasChanged reads it).
                  remoteReloading_ = true;
                  canvas_->setLines(mlines);
                  // Adopt the peer's filter unless this user changed their own (the scalar cannot
                  // merge).
                  if (!filterDirty_) {
                    QString sf, st;
                    parseLayoutFilter(srvLayout, settings_.filterColor, sf, st);
                    applyTintColor(QColor(st));
                    applyImageFilter(sf);
                  }
                  remoteReloading_ = false;
                }
                remoteSession_->link().version = meta.version;
                cb(true, meta.version);
              });
        },
        [this, self, c, w, h, pushGuard](GO outcome) {
          if (!self) return;
          // A hard failure already notified; a lingering Conflict means the attempts were
          // exhausted.
          if (outcome == GO::FAILED) return;
          if (outcome != GO::COMMITTED) {
            notify_->error(
                "This project was edited elsewhere — reload it from the server before "
                "saving again");
            return;
          }
          filterDirty_ = false;   // our filter (if any) is now the server's
          // Fired after the result upload + version refresh, matching the previous synchronous
          // order.
          auto announce = [this, self, pushGuard]() {
            if (self)
              notify_->success(QString("Saved \"%1\" to %2")
                                   .arg(remoteSession_->link().name, remoteSession_->link().address));
          };
          // The file write bumps the version, so re-read it for the next save's guard.
          if (canvas_->hasImage()) {
            const QByteArray bytes = pngBytes(canvas_->renderToImage(true));
            c->uploadFileAsync(
                remoteSession_->link().id, "result", bytes, "png", w, h,
                [this, self, c, announce, pushGuard](bool uok) {
                  if (!self) return;
                  if (!uok) { announce(); return; }
                  c->getProjectAsync(remoteSession_->link().id,
                                     [this, self, announce](bool gok, stencil::net::ServerProject meta,
                                                            QJsonObject) {
                                       if (!self) return;
                                       if (gok) remoteSession_->link().version = meta.version;
                                       announce();
                                     });
                });
          } else {
            announce();
          }
        });
  }

}  // namespace stencil::gui
