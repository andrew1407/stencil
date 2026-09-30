// POST /projects carries the optional CreateProjectRequest fields a caller hands it (a copy's
// blank colour and expiry), and nothing more when it hands none; GET /projects/{id} reads a record's
// keywords and expiry back, which a copy of that row carries — against the in-process MockRest.
#include "ServerClient.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonObject>
#include <QTimer>
#include <cstdio>

#include "../support/check.hpp"
#include "../support/mockRest.hpp"

using stencil::net::ServerClient;
using stencil::test::MockRest;

namespace {
  bool createOnce(MockRest& mock, const QJsonObject& extra) {
    ServerClient client(mock.url());
    bool created = false;
    QEventLoop loop;
    client.createProjectAsync(QStringLiteral("photo-copy"), QString(), QString(), true, 20, 10,
                              [&](bool ok, QString id, qint64) {
                                created = ok && !id.isEmpty();
                                loop.quit();
                              }, extra);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    return created;
  }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  MockRest mock;
  check(mock.listen(), "the mock server listens");

  std::printf("a plain create:\n");
  check(createOnce(mock, QJsonObject()), "the project is created");
  check(mock.lastCreate.value("name").toString() == QLatin1String("photo-copy"), "under its name");
  check(!mock.lastCreate.contains("blankColor") && !mock.lastCreate.contains("expiresAt"),
        "with no optional field the caller did not hand over");

  std::printf("a copy's create:\n");
  const QJsonObject extra{{"blankColor", "#112233"}, {"expiresAt", 1893456000000.0}};
  check(createOnce(mock, extra), "the project is created");
  check(mock.lastCreate.value("blankColor").toString() == QLatin1String("#112233"), "the blank colour rides along");
  check(static_cast<qint64>(mock.lastCreate.value("expiresAt").toDouble()) == 1893456000000LL,
        "and the expiry, in epoch ms");
  check(mock.lastCreate.value("hasImage").toBool() && mock.lastCreate.value("imageW").toInt() == 20,
        "beside the fields every create sends");

  std::printf("a fetched record:\n");
  stencil::test::MockProject kept;
  kept.name = QStringLiteral("roof");
  kept.keywords = {QStringLiteral("roof"), QStringLiteral("north side")};
  kept.expiresAt = 1893456000000LL;
  mock.projects.insert(QStringLiteral("p1"), kept);
  ServerClient client(mock.url());
  stencil::net::ServerProject got;
  QEventLoop loop;
  client.getProjectAsync(QStringLiteral("p1"), [&](bool, stencil::net::ServerProject p, QJsonObject) { got = p; loop.quit(); });
  QTimer::singleShot(10000, &loop, &QEventLoop::quit);
  loop.exec();
  check(got.keywords == kept.keywords, "its keywords come back, one entry each");
  check(got.expiresAt == kept.expiresAt, "and its expiry");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
