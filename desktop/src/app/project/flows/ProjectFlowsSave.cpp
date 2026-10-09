#include "MainWindow.hpp"
#include "ProjectFlows.hpp"
#include "ConnectDialog.hpp"
#include "mainWindowShared.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "lineUnion.hpp"
#include "ServerClient.hpp"

// Saving to a server project, including the two-editor layout merge.

namespace stencil::gui {

  // Version-guarded name/layout PUT; the baked result follows on its own throttle. A 409 leaves
  // the link untouched. Mirrors the browser's saveToServer.
  void ProjectFlows::saveToServer() {
    // One push at a time: a second waits for the first on the push timer.
    if (w.remote.pushing) { w.remoteSync->scheduleRemotePush(); return; }
    if (!w.settings.syncToServer) {  // sync off — fetched project stays edit-in-memory only
      w.remoteSync->pushFinished();
      return;
    }
    stencil::net::ServerClient* c = w.remote.session->requireClient(
        w.remote.session->getLink().address, QString("Not connected to %1 — reconnect it first").arg(w.remote.session->getLink().address));
    if (!c) {
      w.remoteSync->pushFinished();
      return;
    }
    // remotePushing guards the poll for the whole async push; the shared clearer drops it on
    // every exit path, unless a newer push owns it.
    w.remote.pushing = true;
    const int seq = ++w.remote.pushSeq;
    auto pushGuard = std::shared_ptr<void>(nullptr, [self = QPointer<MainWindow>(&w), seq](void*) {
      if (!self || self->remote.pushSeq != seq) return;
      self->remote.pushing = false;
      if (self->remoteSync) self->remoteSync->pushFinished();
    });
    QPointer<MainWindow> self(&w);
    const int width = w.canvas->imageWidth();
    const int h = w.canvas->imageHeight();
    // The push carries the project it started on, and touches the link only while it still holds it.
    const QString id = w.remote.session->getLink().id;
    const QString name = w.remote.session->getLink().name;
    const QString addr = w.remote.session->getLink().address;
    const auto stillLinked = [this, id, addr] {
      return w.remote.session->getLink().id == id && w.remote.session->getLink().address == addr;
    };
    // On a version conflict, union-merge the server's lines with ours and retry (up to 6) so a
    // tight race still converges.
    typedef stencil::net::ServerClient::GuardOutcome GO;
    stencil::net::ServerClient::runGuardedWriteAsync(
        /*attempts=*/6, /*startVersion=*/w.remote.session->getLink().version,
        [this, self, c, width, h, id, name, stillLinked, pushGuard](qint64 version, std::function<void(GO)> cb) {
          if (!self || !stillLinked()) { cb(GO::FAILED); return; }
          const QJsonObject layout =
              fileStore::buildLayoutJson(width, h, w.canvas->allLines(),
                                         w.settings.imageFilter, w.settings.filterColor,
                                         w.canvas->getCropRect(), w.canvas->getRotationQuarters(),
                                         w.currentLayoutMeta(), w.canvas->getMirrored());
          c->updateProjectAsync(
              id, name, layout, version,
              [this, self, cb, stillLinked](bool ok, qint64 newVersion, bool conflict) {
                if (!self) { cb(GO::FAILED); return; }
                if (ok) {
                  if (stillLinked()) w.remote.session->getLink().version = newVersion;
                  cb(GO::COMMITTED);
                  return;
                }
                if (!conflict) { cb(GO::FAILED); return; }
                cb(GO::CONFLICT);
              });
        },
        [this, self, c, id, stillLinked, pushGuard](qint64 /*version*/, std::function<void(bool, qint64)> cb) {
          if (!self || !stillLinked()) { cb(false, 0); return; }
          c->getProjectAsync(
              id,
              [this, self, cb, stillLinked](bool ok, stencil::net::ServerProject meta, QJsonObject srvLayout) {
                // The re-read merges into the canvas, which must still hold this project.
                if (!self || !ok || !stillLinked()) { cb(false, 0); return; }
                int sw = 0, sh = 0;
                const model::LineUnion merged =
                    model::unionLines(fileStore::parseLayoutJson(srvLayout, sw, sh), w.canvas->allLines());
                {  // apply merged lines (+ peer filter) locally without re-triggering a push.
                  // The reload flag brackets the synchronous block (onCanvasChanged reads it).
                  w.remote.reloading = true;
                  // Adopt the peer's filter unless this user changed their own (the scalar cannot
                  // merge); the merged lines' one step carries it, else it is a step of its own.
                  if (!w.filterDirty) {
                    QString sf, st;
                    parseLayoutFilter(srvLayout, w.settings.filterColor, sf, st);
                    w.applyTintColor(QColor(st), /*asUndoStep=*/false);
                    w.applyImageFilter(sf, false);
                  }
                  if (merged.peerAdded) w.canvas->commitLines(merged.lines);   // one undo step; history kept
                  else w.canvas->commitFilter(w.settings.imageFilter, w.tools.filterColorValue);
                  w.remote.reloading = false;
                }
                w.remote.session->getLink().version = meta.version;
                cb(true, meta.version);
              });
        },
        [this, self, c, name, addr, stillLinked, pushGuard](GO outcome) {
          if (!self || !stillLinked()) return;   // the editor left this project meanwhile
          // A lingering Conflict means the attempts were exhausted; toasts follow the outcome
          // changing, so a steady stream of saves stays quiet.
          if (outcome != GO::COMMITTED) {
            if (!w.remote.session->saveOutcomeChanged(false)) return;
            if (outcome == GO::FAILED)
              w.notify->error(QString("Server save failed — %1").arg(c->lastError()));
            else
              w.notify->error("This project was edited elsewhere — reload it from the server before "
                            "saving again");
            return;
          }
          w.filterDirty = false;   // our filter (if any) is now the server's
          if (w.remote.session->saveOutcomeChanged(true))
            w.notify->success(QString("Saved \"%1\" to %2").arg(name, addr));
          w.remoteSync->scheduleResultUpload();
        });
  }

  void ProjectFlows::openConnections() {
    ConnectDialog dlg(w.ensureConnections(), &w);
    // Sits beside Auto-connect there (browser parity).
    dlg.setSyncToServer(w.settings.syncToServer);
    QObject::connect(&dlg, &ConnectDialog::syncToServerToggled, &w, [this](bool on) {
      Settings s = w.settings;
      s.syncToServer = on;
      w.applySettings(s, true);
    });
    // Reports on the toast stack, never a native alert (browser parity).
    QObject::connect(&dlg, &ConnectDialog::toast, &w, [this](const QString& text, bool failed) {
      if (!w.notify) return;
      if (failed) w.notify->error(text); else w.notify->success(text);
    });
    w.execMaybePopover(dlg, w.acts.connect);
    w.warnInsecureConnections();  // the dialog may have added a plaintext-remote connection
  }
}  // namespace stencil::gui
