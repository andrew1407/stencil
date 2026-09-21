#include "MainWindow.hpp"
#include "mainWindowShared.hpp"
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "CropDialog.hpp"
#include "guiHelpers.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSession.hpp"
#include "ProjectTransferController.hpp"
#include "ServerClient.hpp"
#include "SelectionPanel.hpp"
#include "AssistantSettingsDialog.hpp"
#include "SettingsDialog.hpp"
#include "ShortcutsDialog.hpp"

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
    if (!settings.syncToServer) return;  // sync off — fetched project stays edit-in-memory only
    stencil::net::ServerClient* c = remoteSession->requireClient(
        remoteSession->getLink().address, QString("Not connected to %1 — reconnect it first").arg(remoteSession->getLink().address));
    if (!c) return;
    // remotePushing guards the poll for the whole async push; the shared clearer drops it on
    // every exit path.
    remotePushing = true;
    auto pushGuard = std::shared_ptr<void>(nullptr, [self = QPointer<MainWindow>(this)](void*) {
      if (self) self->remotePushing = false;
    });
    QPointer<MainWindow> self(this);
    const int w = canvas->imageWidth();
    const int h = canvas->imageHeight();
    // On a version conflict, union-merge the server's lines with ours and retry (up to 6) so a
    // tight race still converges.
    typedef stencil::net::ServerClient::GuardOutcome GO;
    stencil::net::ServerClient::runGuardedWriteAsync(
        /*attempts=*/6, /*startVersion=*/remoteSession->getLink().version,
        [this, self, c, w, h, pushGuard](qint64 version, std::function<void(GO)> cb) {
          if (!self) { cb(GO::FAILED); return; }
          const QJsonObject layout =
              fileStore::buildLayoutJson(w, h, canvas->allLines(),
                                         settings.imageFilter, settings.filterColor,
                                         canvas->getCropRect(), canvas->getRotationQuarters(),
                                         currentLayoutMeta());
          c->updateProjectAsync(
              remoteSession->getLink().id, remoteSession->getLink().name, layout, version,
              [this, self, c, cb](bool ok, qint64 newVersion, bool conflict) {
                if (!self) { cb(GO::FAILED); return; }
                if (ok) {
                  remoteSession->getLink().version = newVersion;
                  cb(GO::COMMITTED);
                  return;
                }
                if (!conflict) {
                  notify->error(QString("Server save failed — %1").arg(c->lastError()));
                  cb(GO::FAILED);
                  return;
                }
                cb(GO::CONFLICT);
              });
        },
        [this, self, c, pushGuard](qint64 /*version*/, std::function<void(bool, qint64)> cb) {
          if (!self) { cb(false, 0); return; }
          c->getProjectAsync(
              remoteSession->getLink().id,
              [this, self, cb](bool ok, stencil::net::ServerProject meta, QJsonObject srvLayout) {
                if (!self || !ok) { cb(false, 0); return; }  // give up (re-read failed)
                int sw = 0, sh = 0;
                core::Lines mlines = fileStore::parseLayoutJson(srvLayout, sw, sh);
                QSet<QString> seen;
                for (const auto& l : mlines) seen.insert(lineKey(l));
                for (const auto& l : canvas->allLines()) {
                  const QString k = lineKey(l);
                  if (!seen.contains(k)) { mlines.push_back(l); seen.insert(k); }
                }
                {  // apply merged lines (+ peer filter) locally without re-triggering a push.
                  // The reload flag brackets the synchronous block (onCanvasChanged reads it).
                  remoteReloading = true;
                  canvas->setLines(mlines);
                  // Adopt the peer's filter unless this user changed their own (the scalar cannot
                  // merge).
                  if (!filterDirty) {
                    QString sf, st;
                    parseLayoutFilter(srvLayout, settings.filterColor, sf, st);
                    applyTintColor(QColor(st));
                    applyImageFilter(sf);
                  }
                  remoteReloading = false;
                }
                remoteSession->getLink().version = meta.version;
                cb(true, meta.version);
              });
        },
        [this, self, c, w, h, pushGuard](GO outcome) {
          if (!self) return;
          // A hard failure already notified; a lingering Conflict means the attempts were
          // exhausted.
          if (outcome == GO::FAILED) return;
          if (outcome != GO::COMMITTED) {
            notify->error(
                "This project was edited elsewhere — reload it from the server before "
                "saving again");
            return;
          }
          filterDirty = false;   // our filter (if any) is now the server's
          // Fired after the result upload + version refresh, matching the previous synchronous
          // order.
          auto announce = [this, self, pushGuard]() {
            if (self)
              notify->success(QString("Saved \"%1\" to %2")
                                   .arg(remoteSession->getLink().name, remoteSession->getLink().address));
          };
          // The file write bumps the version, so re-read it for the next save's guard.
          if (canvas->hasImage()) {
            const QByteArray bytes = pngBytes(canvas->renderToImage(true));
            c->uploadFileAsync(
                remoteSession->getLink().id, "result", bytes, "png", w, h,
                [this, self, c, announce, pushGuard](bool uok) {
                  if (!self) return;
                  if (!uok) { announce(); return; }
                  c->getProjectAsync(remoteSession->getLink().id,
                                     [this, self, announce](bool gok, stencil::net::ServerProject meta,
                                                            QJsonObject) {
                                       if (!self) return;
                                       if (gok) remoteSession->getLink().version = meta.version;
                                       announce();
                                     });
                });
          } else {
            announce();
          }
        });
  }

}  // namespace stencil::gui
