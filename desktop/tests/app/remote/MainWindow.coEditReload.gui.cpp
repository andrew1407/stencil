// MainWindow GUI e2e — a co-edit reload whose picture download is held open by the REST
// stand-in: a stroke drawn meanwhile survives the landing and reaches the server, and the poll
// starts no second reload while the first is in flight.
#include "../../MainWindow.gui.hpp"
#include "../../support/connectNow.hpp"
#include "coEditGui.hpp"
#include "../../../src/app/remote/RemoteSession.hpp"
#include "../../../src/app/remote/RemoteSyncController.hpp"

namespace {

  bool hasLineAt(const stencil::core::Lines& lines, double y) {
    for (const auto& l : lines)
      if (!l.points.empty() && l.points.front().y == y) return true;
    return false;
  }

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void strokeDuringReloadSurvivesAndPushes() {
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    stencil::test::MockProject& shared = seedProject(mock, QStringLiteral("p1"), QStringLiteral("Shared"));
    QPointer<MainWindow> win = newShownWindow();
    QVERIFY(QTest::qWaitForWindowExposed(win.data()));
    win->settings.syncToServer = true;
    QString err;
    QVERIFY2(stencil::test::connectNow(*win->ensureConnections(), mock.url(), QString(), err),
             qPrintable(err));
    win->remoteSync->setResultTiming(50, 600000);
    win->parts.projects.openServerProject(mock.url(), QStringLiteral("p1"));
    QTRY_VERIFY(win->canvas->hasImage());
    win->canvas->commitLines({lineAt(20)});
    QTRY_COMPARE(mock.puts, 1);
    QTRY_VERIFY(!win->remote.reloading && !win->remote.pushing);
    QTest::qWait(300);

    // A peer replaces the picture and adds a line; its download is held open.
    shared.original = pngOf(Qt::black);
    shared.layout = layoutOf({lineAt(20), lineAt(60)});
    ++shared.version;
    mock.holdOriginals = true;
    win->remoteSync->onRemoteProjectEvent(QStringLiteral("p1"), shared.version, false);
    QTRY_COMPARE(int(mock.heldOriginals.size()), 1);
    QVERIFY(win->remote.reloading);

    // The poll ticks through the slow download: no second reload starts.
    QTest::qWait(stencil::gui::RemoteSyncController::POLL_MS * 2 + 500);
    QCOMPARE(int(mock.heldOriginals.size()), 1);

    // A stroke drawn while the download is still out.
    win->canvas->commitLines({lineAt(20), lineAt(40)});
    const int putsBefore = mock.puts;
    mock.releaseOriginals();
    QTRY_COMPARE(QColor(win->canvas->getOriginalImage().pixel(5, 5)), QColor(Qt::black));
    QTRY_VERIFY(!win->remote.reloading);
    const stencil::core::Lines landed = win->canvas->getLines();
    QVERIFY2(hasLineAt(landed, 40), "the stroke drawn during the reload is kept");
    QVERIFY2(hasLineAt(landed, 60), "…beside the peer's line");
    QTRY_VERIFY(mock.puts > putsBefore);
    int sw = 0, sh = 0;
    QVERIFY2(hasLineAt(stencil::gui::fileStore::parseLayoutJson(shared.layout, sw, sh), 40),
             "…and reaches the server");

    QTRY_VERIFY(!win->remote.reloading && !win->remote.pushing);
    win->close();
    QTRY_VERIFY(win.isNull());
  }

  // An open the user overtook never lands: p1's picture answering after p2's has landed leaves
  // the editor on p2.
  void anOvertakenOpenNeverLands() {
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    seedProject(mock, QStringLiteral("p1"), QStringLiteral("p1"), Qt::white);
    seedProject(mock, QStringLiteral("p2"), QStringLiteral("p2"), Qt::black);
    QPointer<MainWindow> win = newShownWindow();
    QVERIFY(QTest::qWaitForWindowExposed(win.data()));
    QString err;
    QVERIFY2(stencil::test::connectNow(*win->ensureConnections(), mock.url(), QString(), err),
             qPrintable(err));
    mock.holdOriginals = true;
    win->parts.projects.openServerProject(mock.url(), QStringLiteral("p1"));
    QTRY_COMPARE(int(mock.heldOriginals.size()), 1);
    win->parts.projects.openServerProject(mock.url(), QStringLiteral("p2"));
    QTRY_COMPARE(int(mock.heldOriginals.size()), 2);
    const auto answer = [&mock](int i) {
      const auto held = mock.heldOriginals.at(i);
      if (held.socket) stencil::test::MockRest::reply(held.socket, mock.route(held.line, held.body));
    };
    answer(1);
    QTRY_COMPARE(win->remote.session->getLink().id, QStringLiteral("p2"));
    QTRY_COMPARE(QColor(win->canvas->getOriginalImage().pixel(5, 5)), QColor(Qt::black));
    answer(0);
    QTest::qWait(400);
    QCOMPARE(win->remote.session->getLink().id, QStringLiteral("p2"));
    QCOMPARE(QColor(win->canvas->getOriginalImage().pixel(5, 5)), QColor(Qt::black));
    mock.heldOriginals.clear();
    mock.holdOriginals = false;
    win->close();
    QTRY_VERIFY(win.isNull());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.coEditReload.gui.moc"
