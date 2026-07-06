// Server-connected headless SMOKE test for the async co-edit round-trip that
// MainWindow::openServerProject + saveToServer implement. Those two methods are
// private and canvas/UI-entangled, so this test drives the SAME async ServerClient
// primitives they are built on, with TWO independent connections standing in for two
// editors sharing one project:
//
//   • the open/load path  — getProjectAsync + downloadFileAsync (what openServerProject
//     does to pull a peer's project onto the canvas), incl. decoding the original bytes;
//   • the save path        — runGuardedWriteAsync with a line-union merge resolve (exactly
//     what saveToServer uses: PUT the layout guarded by the version, and on a 409 re-read
//     the peer's latest, union the lines, and retry until it converges);
//   • co-edit convergence  — an edit saved by editor A becomes visible to editor B, and a
//     concurrent save from a STALE editor B merges (both editors' lines survive) instead
//     of clobbering A's — the property live co-editing depends on.
//
// It does NOT exercise MainWindow's canvas adoption itself (that stays UI-coupled); it
// closes the gap that the async open/save chains + the guarded-write merge had no
// server-connected coverage (only ProjectTransferController did).
//
// SELF-SKIPS (exit 0) when no server is reachable, like the transfer/store integration
// tests: point it at one with STENCIL_TEST_SERVER (default http://localhost:8090). Built
// only when Qt is present; not part of the Qt-free core stencil_tests.
#include "serverClient.hpp"

#include <QBuffer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <cstdio>
#include <functional>
#include <memory>

using stencil::net::ConnectionManager;
using stencil::net::ServerClient;
using stencil::net::ServerProject;
using GO = ServerClient::GuardOutcome;

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  int failures = 0;
  auto check = [&](bool cond, const char* msg) {
    std::printf("%s: %s\n", cond ? "ok" : "FAIL", msg);
    if (!cond) ++failures;
  };

  const QString serverUrl = qEnvironmentVariableIsSet("STENCIL_TEST_SERVER")
                                ? qEnvironmentVariable("STENCIL_TEST_SERVER")
                                : QStringLiteral("http://localhost:8090");

  // Two independent connections = two editors (each gets its own token).
  ConnectionManager mgrA, mgrB;
  QString errA, errB;
  if (!mgrA.connectTo(serverUrl, QString(), errA) ||
      !mgrB.connectTo(serverUrl, QString(), errB)) {
    std::printf("SKIP: no reachable stencil server at %s (%s / %s)\n",
                serverUrl.toUtf8().constData(), errA.toUtf8().constData(),
                errB.toUtf8().constData());
    return 0;  // self-skip, like the gated transfer / go store integration tests
  }
  ServerClient* A = mgrA.find(serverUrl);
  ServerClient* B = mgrB.find(serverUrl);
  check(A && B, "two editor connections established");
  if (!A || !B) { std::printf("FAILED (%d failures)\n", failures); return 1; }

  // Pump the event loop until `ready` flips or we time out (the async API completes on
  // the GUI thread; a headless test has to drive the loop itself).
  auto pump = [&](const std::shared_ptr<bool>& ready, int ms = 8000) {
    QElapsedTimer t;
    t.start();
    while (!*ready && t.elapsed() < ms) app.processEvents(QEventLoop::AllEvents, 50);
  };

  // Compact-JSON identity of a layout line (the test's analogue of MainWindow::lineKey,
  // used to union two editors' lines without duplicating a shared one).
  auto lineKey = [](const QJsonValue& v) {
    return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
  };
  auto line = [](const QString& tag) {
    QJsonObject o;
    o.insert("tag", tag);  // distinguishes each editor's line across the round-trip
    QJsonArray pts;
    pts.append(1);
    pts.append(2);
    o.insert("points", pts);
    return QJsonValue(o);
  };
  auto layoutHas = [&](const QJsonObject& layout, const QString& tag) {
    for (const QJsonValue& v : layout.value("lines").toArray())
      if (v.toObject().value("tag").toString() == tag) return true;
    return false;
  };

  // A guarded save mirroring saveToServer: PUT {lines: *myLines} guarded by `startVer`; on a
  // 409 the resolve re-reads the peer's latest, unions their lines into *myLines, and retries.
  // Reports (committed, winningVersion).
  auto guardedSave = [&](ServerClient* cli, const QString& id, qint64 startVer,
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
                                      cb(GO::Committed);
                                      return;
                                    }
                                    cb(conflict ? GO::Conflict : GO::Failed);
                                  });
        },
        [cli, id, myLines, lineKey](qint64 /*version*/,
                                    std::function<void(bool, qint64)> cb) {
          cli->getProjectAsync(id, [myLines, cb, lineKey](bool ok, ServerProject meta,
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
        [done, winner](GO o) { done(o == GO::Committed, *winner); });
  };

  // A tiny PNG original so the open/load path has bytes to download + decode.
  QByteArray png;
  {
    QImage img(20, 10, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    check(img.save(&buf, "PNG"), "encoded a source PNG");
  }

  const QString name = QStringLiteral("e2e-coedit-%1")
                           .arg(QString::number(qHash(png) ^ 0x5715));  // clock-free unique-ish
  QString id;
  qint64 v0 = 0;

  // ── Editor A: create the shared project + upload its original ──
  {
    auto ready = std::make_shared<bool>(false);
    A->createProjectAsync(name, QString(), QString(), /*hasImage=*/true, 20, 10,
                          [&](bool ok, QString newId, qint64 ver) {
                            check(ok && !newId.isEmpty(), "A: created the shared project");
                            id = newId;
                            v0 = ver;
                            *ready = true;
                          });
    pump(ready);
  }
  if (id.isEmpty()) { std::printf("FAILED (%d failures)\n", failures); return 1; }
  {
    auto ready = std::make_shared<bool>(false);
    A->uploadFileAsync(id, "original", png, "png", 20, 10, [&](bool ok) {
      check(ok, "A: uploaded the original image");
      *ready = true;
    });
    pump(ready);
  }

  // ── Editor B: open/load the project (openServerProject's data path) ──
  qint64 bVersion = 0;
  {
    auto ready = std::make_shared<bool>(false);
    B->getProjectAsync(id, [&](bool ok, ServerProject meta, QJsonObject /*layout*/) {
      check(ok && meta.name == name, "B: opened the project (getProject sees A's project)");
      bVersion = meta.version;
      *ready = true;
    });
    pump(ready);
  }
  {
    auto ready = std::make_shared<bool>(false);
    B->downloadFileAsync(id, "original", [&](bool ok, QByteArray bytes) {
      QImage img;
      check(ok && !bytes.isEmpty() && img.loadFromData(bytes),
            "B: downloaded + decoded the original (open/load path)");
      *ready = true;
    });
    pump(ready);
  }

  // ── Editor A: save an annotation [A1] (saveToServer's guarded-write path) ──
  qint64 vA = 0;
  {
    auto ready = std::make_shared<bool>(false);
    auto linesA = std::make_shared<QJsonArray>();
    linesA->append(line("A1"));
    guardedSave(A, id, v0, linesA, [&](bool committed, qint64 nv) {
      check(committed, "A: guarded save committed");
      vA = nv;
      *ready = true;
    });
    pump(ready);
  }
  check(vA > v0, "A: server version advanced after the save");

  // ── Editor B: poll/reload (openServerProject silent-reload path) sees A's change ──
  {
    auto ready = std::make_shared<bool>(false);
    B->getProjectAsync(id, [&](bool ok, ServerProject meta, QJsonObject layout) {
      check(ok && meta.version == vA, "B: poll sees the new version (peer edit visible)");
      check(layoutHas(layout, "A1"), "B: peer's annotation [A1] is present after reload");
      *ready = true;
    });
    pump(ready);
  }

  // ── Editor B: concurrent save from a STALE version [B1] → 409 → merge → converge ──
  // B still holds v0 (bVersion), so its first PUT conflicts; the guarded-write resolve must
  // union A's [A1] with B's [B1] and retry, so BOTH survive (co-edit, not clobber).
  qint64 vB = 0;
  {
    auto ready = std::make_shared<bool>(false);
    auto linesB = std::make_shared<QJsonArray>();
    linesB->append(line("B1"));
    guardedSave(B, id, bVersion, linesB, [&](bool committed, qint64 nv) {
      check(committed, "B: stale save resolved via merge + retry (no clobber)");
      vB = nv;
      *ready = true;
    });
    pump(ready);
  }
  check(vB > vA, "B: merged save advanced the version past A's");

  // ── Convergence: the server layout now holds BOTH editors' annotations ──
  {
    auto ready = std::make_shared<bool>(false);
    A->getProjectAsync(id, [&](bool ok, ServerProject meta, QJsonObject layout) {
      check(ok, "final: re-read the converged project");
      check(layoutHas(layout, "A1") && layoutHas(layout, "B1"),
            "co-edit converged: both [A1] and [B1] survive the concurrent save");
      check(meta.version == vB, "final version matches the last committed save");
      *ready = true;
    });
    pump(ready);
  }

  // ── cleanup ──
  {
    auto ready = std::make_shared<bool>(false);
    A->deleteProjectAsync(id, [&](bool) { *ready = true; });
    pump(ready, 5000);
  }

  std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
