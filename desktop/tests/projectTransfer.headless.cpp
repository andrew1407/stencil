// Server-connected headless check for ProjectTransferController (src/app/projectTransferController).
// Drives the REAL extracted controller against a running Stencil collaboration server: it builds a
// local project (a tiny on-disk PNG), then exercises copyLocalProjectToServer + moveLocalProjectToServer
// and asserts, over the server's REST list, that the project actually landed on the server — and that
// COPY leaves the local project in place while MOVE removes it. Cleans up the server projects + temp file.
//
// SELF-SKIPS (exit 0) when no server is reachable, like the gated store/bus tests: point it at one with
// STENCIL_TEST_SERVER (default http://localhost:8090). Built only when Qt is present (Qt-coupled, like the
// other *.headless tests); not part of the Qt-free core stencil_tests.
#include "projectTransferController.hpp"
#include "serverClient.hpp"
#include "canvasWidget.hpp"
#include "notifications.hpp"
#include "fileStore.hpp"
#include "projectsStore.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>
#include <cstdio>
#include <string>
#include <vector>

using namespace stencil::gui;

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  int failures = 0;
  auto check = [&](bool cond, const char* msg) {
    std::printf("%s: %s\n", cond ? "ok" : "FAIL", msg);
    if (!cond) ++failures;
  };

  const QString serverUrl = qEnvironmentVariableIsSet("STENCIL_TEST_SERVER")
                                ? qEnvironmentVariable("STENCIL_TEST_SERVER")
                                : QStringLiteral("http://localhost:8090");

  stencil::net::ConnectionManager mgr;
  QString err;
  if (!mgr.connectTo(serverUrl, QString(), err)) {
    std::printf("SKIP: no reachable stencil server at %s (%s)\n",
                serverUrl.toUtf8().constData(), err.toUtf8().constData());
    return 0;  // self-skip, mirroring the gated go store/bus integration tests
  }
  stencil::net::ServerClient* c = mgr.find(serverUrl);
  check(c != nullptr, "connected client present");
  if (!c) { std::printf("FAILED (%d failures)\n", failures); return 1; }

  QWidget host;
  Notifications notify(&host);
  CanvasWidget canvas(&host);
  stencil::core::ProjectsStore store;
  std::vector<Project> list;
  Settings settings;

  const qint64 ts = QDateTime::currentMSecsSinceEpoch();
  // A tiny on-disk PNG so localProjectOriginal reads the stored file (the project is never the
  // "active" one here, so the canvas branch is skipped).
  const QString imgPath = QDir::tempPath() + QString("/stencil_xfer_%1.png").arg(ts);
  {
    QImage img(20, 10, QImage::Format_ARGB32);
    img.fill(Qt::red);
    check(img.save(imgPath, "PNG"), "wrote a temp source PNG");
  }

  auto addLocal = [&](const QString& name) -> QString {
    Project pr;
    pr.meta.id = store.createId(ts + static_cast<long long>(list.size()), "salt");
    pr.meta.name = name.toStdString();
    pr.meta.hasImage = true;
    pr.imagePath = imgPath;
    list.push_back(pr);
    return QString::fromStdString(pr.meta.id);
  };

  ProjectTransferController::Hooks hooks{
      [&] { return &mgr; },
      [&](const std::string& id) -> Project* {
        for (auto& p : list)
          if (p.meta.id == id) return &p;
        return nullptr;
      },
      [] { return fileStore::LayoutMeta{}; },
      [](const QString&) { return QByteArray{}; },
      [] { return QString(); },  // activeProjectId — none, so localProjectOriginal reads the file
      [] { return QString(); },  // remoteAddress
      [] { return QString(); },  // remoteId
      [](const QString&, const QString&, const QString&, const QString&, qint64) {},  // relink
      [](const QString&) {},                                                          // load into canvas
      [] {},                                                                          // afterChange
  };
  ProjectTransferController xfer(&notify, &canvas, &settings, &store, &list, hooks);

  QVector<QString> createdServerIds;  // for cleanup
  auto serverHasNamed = [&](const QString& name, QString* idOut) -> bool {
    QVector<stencil::net::ServerProject> out;
    if (!c->listProjects(out)) return false;
    for (const auto& p : out)
      if (p.name == name) {
        if (idOut) *idOut = p.id;
        return true;
      }
    return false;
  };

  // ── COPY: leaves the local project in place, creates it on the server ──
  const QString copyName = QString("e2e-copy-%1").arg(ts);
  const QString copyId = addLocal(QString("e2e-src-%1").arg(ts));
  xfer.copyLocalProjectToServer(serverUrl, copyId, copyName);
  QString serverCopyId;
  check(serverHasNamed(copyName, &serverCopyId), "copy: the project appeared on the server");
  if (!serverCopyId.isEmpty()) createdServerIds.push_back(serverCopyId);
  check(list.size() == 1, "copy: the local project stays (copy, not move)");

  // ── MOVE: removes the local project, creates it on the server ──
  const QString moveName = QString("e2e-move-%1").arg(ts);
  const QString moveId = addLocal(moveName);  // move keeps the project's own name
  check(list.size() == 2, "move: a second local project was added");
  xfer.moveLocalProjectToServer(serverUrl, moveId);
  QString serverMoveId;
  check(serverHasNamed(moveName, &serverMoveId), "move: the project appeared on the server");
  if (!serverMoveId.isEmpty()) createdServerIds.push_back(serverMoveId);
  bool localGone = true;
  for (const auto& p : list)
    if (QString::fromStdString(p.meta.id) == moveId) localGone = false;
  check(localGone, "move: the local project was removed after the move");

  // ── cleanup: delete the server projects we created + the temp file ──
  for (const auto& id : createdServerIds) c->deleteProject(id);
  QFile::remove(imgPath);

  std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
