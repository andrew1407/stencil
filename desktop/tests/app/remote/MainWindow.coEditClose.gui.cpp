// MainWindow GUI e2e — closing a server-linked window against an in-process REST stand-in: a
// layout push still inside its debounce goes out first, and a push in flight holds the close until
// its PUT lands.
// Shared ground is in coEditGui.hpp, over MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../support/connectNow.hpp"
#include "coEditGui.hpp"
#include "../../../src/app/remote/RemoteSyncController.hpp"

namespace {

  int serverLines(stencil::test::MockRest& mock) {
    int w = 0, h = 0;
    return static_cast<int>(
        stencil::gui::fileStore::parseLayoutJson(mock.projects[QStringLiteral("p1")].layout, w, h).size());
  }

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // The friend reaches the window's private hub, so this lives on the test class.
  static QPointer<MainWindow> openLinked(stencil::test::MockRest& mock) {
    seedProject(mock, QStringLiteral("p1"), QStringLiteral("Shared"));
    QPointer<MainWindow> win = newShownWindow();
    win->settings.syncToServer = true;
    QString err;
    if (!stencil::test::connectNow(*win->ensureConnections(), mock.url(), QString(), err)) return {};
    win->remoteSync->setResultTiming(50, 600000);
    win->parts.projects.openServerProject(mock.url(), QStringLiteral("p1"));
    return win;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void closeSendsThePushStillInItsDebounce() {
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    QPointer<MainWindow> win = openLinked(mock);
    QVERIFY(win);
    QTRY_VERIFY(win->canvas->hasImage());
    QTRY_VERIFY(!win->remote.reloading);
    win->canvas->commitLines({lineAt(20)});
    QCOMPARE(mock.puts, 0);
    win->close();
    QTRY_COMPARE(mock.puts, 1);
    QCOMPARE(serverLines(mock), 1);
    QTRY_VERIFY(win.isNull());
  }

  void closeWaitsForThePushInFlight() {
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    QPointer<MainWindow> win = openLinked(mock);
    QVERIFY(win);
    QTRY_VERIFY(win->canvas->hasImage());
    QTRY_VERIFY(!win->remote.reloading);
    mock.holdPuts = true;
    win->canvas->commitLines({lineAt(20)});
    QTRY_COMPARE(mock.heldOriginals.size(), 1);   // the PUT is on the wire, unanswered
    QVERIFY(win->remote.pushing);
    win->close();
    QTest::qWait(300);
    QVERIFY2(!win.isNull(), "the window closed under its in-flight push");
    QVERIFY(win->isHidden());
    mock.releasePuts();
    QTRY_COMPARE(mock.puts, 1);
    QCOMPARE(serverLines(mock), 1);
    QTRY_VERIFY(win.isNull());
  }

  // Forgetting the linked server takes its token with it: the live feed (REST port + 1) hangs up
  // and never dials back.
  void forgettingTheLinkedServerStopsItsFeed() {
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    QTcpServer feed;
    QVERIFY(feed.listen(QHostAddress::LocalHost, mock.server.serverPort() + 1));
    int dials = 0;
    QPointer<QTcpSocket> peer;
    QObject::connect(&feed, &QTcpServer::newConnection, [&] {
      while (QTcpSocket* s = feed.nextPendingConnection()) {
        ++dials;
        peer = s;
      }
    });
    QPointer<MainWindow> win = openLinked(mock);
    QVERIFY(win);
    QTRY_VERIFY(win->canvas->hasImage());
    QTRY_VERIFY(peer && peer->state() == QAbstractSocket::ConnectedState);
    win->remote.connections->disconnectFrom(mock.url());
    QTRY_VERIFY(!peer || peer->state() != QAbstractSocket::ConnectedState);
    const int before = dials;
    QTest::qWait(3500);   // past LiveFeed's 3 s redial
    QCOMPARE(dials, before);
    win->close();
    QTRY_VERIFY(win.isNull());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.coEditClose.gui.moc"
