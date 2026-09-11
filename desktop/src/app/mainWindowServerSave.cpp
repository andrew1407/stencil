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
    // A stable value key for a line, for dedup when merging two editors' layouts on a
    // save conflict (mirrors the browser's JSON-stringify dedup in mergeLines).
    QString lineKey(const core::Line& l) {
      // pointColor rides second, matching the browser's lineDedupeKey field order: two
      // lines alike but for their point colour are different lines, and an UNSET one keys
      // as "" so a server round-trip (which omits the field) still dedupes against the
      // local original.
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

  // Save a server-linked session back: version-guarded name/layout PUT, then upload
  // the rendered result. A 409 surfaces a clear "edited elsewhere" message and
  // leaves the link untouched. Mirrors the browser's saveToServer/saveRemoteProject.
  void MainWindow::saveToServer() {
    if (!settings_.syncToServer) return;  // sync off — fetched project stays edit-in-memory only
    stencil::net::ServerClient* c = remoteSession_->requireClient(
        remoteSession_->link().address, QString("Not connected to %1 — reconnect it first").arg(remoteSession_->link().address));
    if (!c) return;
    // Guard the poll for the whole push (async in flight) so we don't reload our own change. The
    // shared clearer sets remotePushing_ false once the last pending continuation is gone — every
    // exit path (commit, conflict, hard error, or the client/window destroyed mid-flight).
    remotePushing_ = true;
    auto pushGuard = std::shared_ptr<void>(nullptr, [self = QPointer<MainWindow>(this)](void*) {
      if (self) self->remotePushing_ = false;
    });
    QPointer<MainWindow> self(this);
    const int w = canvas_->imageWidth();
    const int h = canvas_->imageHeight();
    // Concurrent co-edit: on a version-guard conflict, merge the server's latest lines with
    // ours and retry — looping (up to 6 attempts) so a tight race (incl. the result upload's
    // extra version bump) still converges with both editors' annotations intact. The
    // read→PUT→retry loop is the shared primitive; the line-union merge below is this save's
    // conflict-resolution policy.
    using GO = stencil::net::ServerClient::GuardOutcome;
    stencil::net::ServerClient::runGuardedWriteAsync(
        /*attempts=*/6, /*startVersion=*/remoteSession_->link().version,
        [this, self, c, w, h, pushGuard](qint64 version, std::function<void(GO)> cb) {
          if (!self) { cb(GO::Failed); return; }
          const QJsonObject layout =
              fileStore::buildLayoutJson(w, h, canvas_->allLines(),
                                         settings_.imageFilter, settings_.filterColor,
                                         canvas_->cropRect(), canvas_->rotationQuarters(),
                                         currentLayoutMeta());
          c->updateProjectAsync(
              remoteSession_->link().id, remoteSession_->link().name, layout, version,
              [this, self, c, cb](bool ok, qint64 newVersion, bool conflict) {
                if (!self) { cb(GO::Failed); return; }
                if (ok) {
                  remoteSession_->link().version = newVersion;
                  cb(GO::Committed);
                  return;
                }
                if (!conflict) {
                  notify_->error(QString("Server save failed — %1").arg(c->lastError()));
                  cb(GO::Failed);
                  return;
                }
                cb(GO::Conflict);
              });
        },
        [this, self, c, pushGuard](qint64 /*version*/, std::function<void(bool, qint64)> cb) {
          if (!self) { cb(false, 0); return; }
          // Pull the peer's latest, union-merge their lines into ours (deduped), adopt the
          // server version, and retry.
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
                  // Synchronous block: the reload flag brackets it (onCanvasChanged reads it).
                  remoteReloading_ = true;
                  canvas_->setLines(mlines);
                  // Adopt the peer's filter UNLESS this user changed their own, so a line-only
                  // edit doesn't clobber the peer's filter change (the scalar can't merge).
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
          // A hard (non-409) failure already notified inside the attempt and stops here; a
          // lingering Conflict means the attempts were exhausted (or a re-read failed).
          if (outcome == GO::Failed) return;
          if (outcome != GO::Committed) {
            notify_->error(
                "This project was edited elsewhere — reload it from the server before "
                "saving again");
            return;
          }
          filterDirty_ = false;   // our filter (if any) is now the server's
          // Confirm our own save (the union-merge kept both editors' annotations intact). Fired
          // after the result upload + version refresh, matching the previous synchronous order.
          auto announce = [this, self, pushGuard]() {
            if (self)
              notify_->success(QString("Saved \"%1\" to %2")
                                   .arg(remoteSession_->link().name, remoteSession_->link().address));
          };
          // Upload the annotated render as the 'result'. The file write bumps the version, so
          // re-read it to keep the guard accurate for the next save.
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
