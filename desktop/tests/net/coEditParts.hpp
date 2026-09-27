#pragma once
// The co-edit smoke suite's sections, one TU each behind this header, called in this order from
// main(): the helpers the two editors share (the loop pump, a layout line and its identity, the
// guarded save mirroring saveToServer) and the sections' declarations.
#include "ServerClient.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <cstdio>
#include <functional>
#include <memory>

namespace coedit {

  using stencil::net::ServerClient;
  using stencil::net::ServerProject;
  typedef ServerClient::GuardOutcome GO;

  inline int failures = 0;
  inline void check(bool cond, const char* msg) {
    std::printf("%s: %s\n", cond ? "ok" : "FAIL", msg);
    if (!cond) ++failures;
  }

  // Pump the event loop until `ready` flips or we time out (the async API completes on
  // the GUI thread; a headless test has to drive the loop itself).
  inline void pump(const std::shared_ptr<bool>& ready, int ms = 8000) {
    QElapsedTimer t;
    t.start();
    while (!*ready && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }

  // Compact-JSON identity of a layout line (the test's analogue of MainWindow::lineKey,
  // used to union two editors' lines without duplicating a shared one).
  inline QString lineKey(const QJsonValue& v) {
    return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
  }
  inline QJsonValue line(const QString& tag) {
    QJsonObject o;
    o.insert("tag", tag);  // distinguishes each editor's line across the round-trip
    QJsonArray pts;
    pts.append(1);
    pts.append(2);
    o.insert("points", pts);
    return QJsonValue(o);
  }
  inline bool layoutHas(const QJsonObject& layout, const QString& tag) {
    for (const QJsonValue& v : layout.value("lines").toArray())
      if (v.toObject().value("tag").toString() == tag) return true;
    return false;
  }

  // A guarded save mirroring saveToServer: PUT {lines: *myLines} guarded by `startVer`; on a 409 the
  // resolve re-reads the peer's latest, unions their lines in and retries. Reports (committed, version).
  inline void guardedSave(ServerClient* cli, const QString& id, qint64 startVer,
                          const std::shared_ptr<QJsonArray>& myLines,
                          const std::function<void(bool committed, qint64 newVer)>& done) {
    auto winner = std::make_shared<qint64>(startVer);
    ServerClient::runGuardedWriteAsync(
        /*attempts=*/6, startVer,
        [cli, id, myLines, winner](qint64 version, std::function<void(GO)> cb) {
          QJsonObject layout;
          layout.insert("lines", *myLines);
          cli->updateProjectAsync(id, QString(), layout, version,
                                  [cb, winner](bool ok, qint64 nv, bool conflict) {
                                    if (ok) {
                                      *winner = nv;
                                      cb(GO::COMMITTED);
                                      return;
                                    }
                                    cb(conflict ? GO::CONFLICT : GO::FAILED);
                                  });
        },
        [cli, id, myLines](qint64 /*version*/,
                           std::function<void(bool, qint64)> cb) {
          cli->getProjectAsync(id, [myLines, cb](bool ok, ServerProject meta,
                                                 QJsonObject layout) {
            if (!ok) {
              cb(false, 0);
              return;
            }
            // Union: start from the peer's server lines, append ours not already present.
            QJsonArray merged = layout.value("lines").toArray();
            QSet<QString> seen;
            for (const QJsonValue& v : merged) seen.insert(lineKey(v));
            for (const QJsonValue& v : *myLines)
              if (!seen.contains(lineKey(v))) {
                merged.append(v);
                seen.insert(lineKey(v));
              }
            *myLines = merged;
            cb(true, meta.version);  // adopt the server version, then retry the PUT
          });
        },
        [done, winner](GO o) { done(o == GO::COMMITTED, *winner); });
  }

  // A creates the shared project and uploads its original; B opens and loads it. False when there
  // is no project to go on with.
  bool openShared(ServerClient* A, ServerClient* B, QString& id, qint64& v0, qint64& bVersion);
  // A saves, B reloads, B's stale save merges, both converge; then the project is deleted.
  void saveAndConverge(ServerClient* A, ServerClient* B, const QString& id, qint64 v0, qint64 bVersion);

}  // namespace coedit
