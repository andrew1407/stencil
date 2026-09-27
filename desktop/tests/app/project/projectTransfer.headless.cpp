// Headless check for ProjectTransferController (src/app/projectTransferController): it builds local
// projects, exercises copyLocalProjectToServer + moveLocalProjectToServer, and asserts over REST that
// each landed with its layout and colour — COPY leaving the local one in place, MOVE removing it.
// Runs against STENCIL_TEST_SERVER (default http://localhost:8090) when one answers, else against
// the in-process stand-in (tests/support/mockRest.hpp). Built only when Qt is present.
#include "ProjectTransferController.hpp"
#include "ServerClient.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "fileStore.hpp"
#include "ProjectsStore.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QWidget>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "../../support/connectNow.hpp"
#include "../../support/mockRest.hpp"

using namespace stencil::gui;

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  int failures = 0;
  auto check = [&](bool cond, const char* msg) {
    std::printf("%s: %s\n", cond ? "ok" : "FAIL", msg);
    if (!cond) ++failures;
  };

  QString serverUrl = qEnvironmentVariableIsSet("STENCIL_TEST_SERVER")
                          ? qEnvironmentVariable("STENCIL_TEST_SERVER")
                          : QStringLiteral("http://localhost:8090");

  stencil::net::ConnectionManager mgr;
  QString err;
  stencil::test::MockRest mock;
  const bool live = stencil::test::connectNow(mgr, serverUrl, QString(), err);
  if (!live) {
    std::printf("no stencil server at %s (%s): using the in-process stand-in\n",
                serverUrl.toUtf8().constData(), err.toUtf8().constData());
    check(mock.listen(), "the stand-in listens");
    serverUrl = mock.url();
    check(stencil::test::connectNow(mgr, serverUrl, QString(), err), "connected to the stand-in");
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
      [](const QString&, std::function<void(QByteArray)> done) { done(QByteArray{}); },
      [] { return QString(); },  // activeProjectId — none, so localProjectOriginal reads the file
      [] { return QString(); },  // remoteAddress
      [] { return QString(); },  // remoteId
      [](const QString&, const QString&, const QString&, const QString&, qint64) {},  // relink
      [](const QString&, bool, std::function<void()> then) { then(); },              // load into canvas
      [] {},                                                                          // afterChange
  };
  ProjectTransferController xfer(&notify, &canvas, &settings, &store, &list, hooks);

  QVector<QString> createdServerIds;  // for cleanup

  // The controller is fully async, so the test drives the event loop: `listServer` issues an async list
  // and pumps until it resolves; `waitFor` re-lists each pass until its predicate holds or it times out.
  auto listServer = [&]() {
    auto ready = std::make_shared<bool>(false);
    auto out = std::make_shared<QVector<stencil::net::ServerProject>>();
    c->listProjectsAsync([ready, out](bool ok, QVector<stencil::net::ServerProject> ps) {
      if (ok) *out = ps;
      *ready = true;
    });
    QElapsedTimer t;
    t.start();
    while (!*ready && t.elapsed() < 5000) app.processEvents(QEventLoop::AllEvents, 50);
    return *out;
  };
  auto serverHasNamed = [&](const QString& name, QString* idOut) -> bool {
    for (const auto& p : listServer())
      if (p.name == name) {
        if (idOut) *idOut = p.id;
        return true;
      }
    return false;
  };
  auto waitFor = [&](std::function<bool()> pred) -> bool {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 10000) {
      if (pred()) return true;
      app.processEvents(QEventLoop::AllEvents, 100);
    }
    return pred();
  };

  // ── COPY: leaves the local project in place, creates it on the server ──
  const QString copyName = QString("e2e-copy-%1").arg(ts);
  const QString copyId = addLocal(QString("e2e-src-%1").arg(ts));
  xfer.copyLocalProjectToServer(serverUrl, copyId, copyName);
  QString serverCopyId;
  check(waitFor([&] { return serverHasNamed(copyName, &serverCopyId); }),
        "copy: the project appeared on the server");
  if (!serverCopyId.isEmpty()) createdServerIds.push_back(serverCopyId);
  check(list.size() == 1, "copy: the local project stays (copy, not move)");

  // ── MOVE: removes the local project, creates it on the server with its layout and colour ──
  const QString moveName = QString("e2e-move-%1").arg(ts);
  const QString moveId = addLocal(moveName);  // move keeps the project's own name
  {
    stencil::core::Line line;
    line.points = {{1, 1}, {9, 4}};
    list.back().lines = {line};
    list.back().meta.color = "#ff0000";
  }
  check(list.size() == 2, "move: a second local project was added");
  xfer.moveLocalProjectToServer(serverUrl, moveId);
  QString serverMoveId;
  check(waitFor([&] { return serverHasNamed(moveName, &serverMoveId); }),
        "move: the project appeared on the server");
  if (!serverMoveId.isEmpty()) createdServerIds.push_back(serverMoveId);
  // The move erases the local copy only in its async tail (after the server create lands).
  const bool localGone = waitFor([&] {
    for (const auto& p : list)
      if (QString::fromStdString(p.meta.id) == moveId) return false;
    return true;
  });
  check(localGone, "move: the local project was removed after the move");
  {
    // The original upload bumps the version; the layout and colour PUTs must carry the new one.
    auto ready = std::make_shared<bool>(false);
    auto meta = std::make_shared<stencil::net::ServerProject>();
    auto layout = std::make_shared<QJsonObject>();
    c->getProjectAsync(serverMoveId, [ready, meta, layout](bool, stencil::net::ServerProject m, QJsonObject l) {
      *meta = m;
      *layout = l;
      *ready = true;
    });
    waitFor([&] { return *ready; });
    check(layout->value("lines").toArray().size() == 1, "move: the layout landed on the server");
    check(meta->color == QStringLiteral("#ff0000"), "move: the colour landed on the server");
  }
  if (!live) {
    // A layout PUT the server refuses is said out loud, and the local project is kept.
    const QString keptId = addLocal(QString("e2e-kept-%1").arg(ts));
    mock.putStatus = 500;
    xfer.moveLocalProjectToServer(serverUrl, keptId);
    const int attempts = mock.putAttempts;
    waitFor([&] { return mock.putAttempts > attempts; });
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 300) app.processEvents(QEventLoop::AllEvents, 20);
    bool kept = false;
    for (const auto& p : list) kept = kept || QString::fromStdString(p.meta.id) == keptId;
    check(kept, "move: a refused layout PUT keeps the local project");
    mock.putStatus = 0;
  }

  // ── cleanup: delete the server projects we created + the temp file ──
  for (const auto& id : createdServerIds) {
    auto ready = std::make_shared<bool>(false);
    c->deleteProjectAsync(id, [ready](bool) { *ready = true; });
    QElapsedTimer t;
    t.start();
    while (!*ready && t.elapsed() < 5000) app.processEvents(QEventLoop::AllEvents, 50);
  }
  QFile::remove(imgPath);

  std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
