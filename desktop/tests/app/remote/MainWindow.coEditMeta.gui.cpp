// MainWindow GUI e2e — a rename or colour change of the linked server project against an in-process
// REST stand-in: each adopts only its own version bump, so a peer's edit between our fetch and our rename still
// reloads, and a rename whose PUT lands after the editor moved to another project leaves that
// project's link alone.
// Shared ground is in coEditGui.hpp, over MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../support/connectNow.hpp"
#include "coEditGui.hpp"
#include "../../../src/app/project/ProjectTitleController.hpp"
#include "../../../src/app/remote/RemoteSession.hpp"
#include "../../../src/app/remote/RemoteSyncController.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // The friend reaches the window's private hub, so this lives on the test class.
  static QPointer<MainWindow> openLinked(stencil::test::MockRest& mock, const QString& id) {
    QPointer<MainWindow> win = newShownWindow();
    win->settings.syncToServer = true;
    QString err;
    if (!stencil::test::connectNow(*win->ensureConnections(), mock.url(), QString(), err)) return {};
    win->remoteSync->setResultTiming(50, 600000);
    win->parts.projects.openServerProject(mock.url(), id);
    return win;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void renameAdoptsOnlyItsOwnVersionBump() {
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    seedProject(mock, QStringLiteral("p1"), QStringLiteral("Shared"));
    stencil::test::MockProject& shared = mock.projects[QStringLiteral("p1")];
    QPointer<MainWindow> win = openLinked(mock, QStringLiteral("p1"));
    QVERIFY(win);
    QTRY_VERIFY(win->canvas->hasImage());
    QTRY_VERIFY(!win->remote.reloading);
    const stencil::gui::RemoteLink& link = win->remote.session->getLink();
    QCOMPARE(link.version, 1);

    // Our own rename is our own bump: adopted.
    win->nameBar.field->setText(QStringLiteral("First"));
    win->projectTitle->commitProjectName();
    QTRY_COMPARE(link.name, QStringLiteral("First"));
    QCOMPARE(shared.version, 2);
    QCOMPARE(link.version, 2);

    // A peer's layout edit lands before our rename's own re-read: the rename's PUT covers it, so
    // the link keeps the version it knew, and the next event still reloads the peer's line.
    shared.layout = layoutOf({lineAt(60)});
    ++shared.version;
    win->nameBar.field->setText(QStringLiteral("Second"));
    win->projectTitle->commitProjectName();
    QTRY_COMPARE(link.name, QStringLiteral("Second"));
    QCOMPARE(shared.version, 4);
    QVERIFY2(link.version == 2, qPrintable(QStringLiteral("the rename adopted a version that covers the peer's edit: %1")
                                              .arg(link.version)));
    win->remoteSync->onRemoteProjectEvent(QStringLiteral("p1"), shared.version, false);
    QTRY_COMPARE(static_cast<int>(win->canvas->getLines().size()), 1);
    QTRY_COMPARE(link.version, 4);
    QTRY_VERIFY(!win->remote.reloading);
    win->close();
    QTRY_VERIFY(win.isNull());
  }

  void colourAdoptsOnlyItsOwnVersionBump() {
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    seedProject(mock, QStringLiteral("p1"), QStringLiteral("Shared"));
    stencil::test::MockProject& shared = mock.projects[QStringLiteral("p1")];
    QPointer<MainWindow> win = openLinked(mock, QStringLiteral("p1"));
    QVERIFY(win);
    QTRY_VERIFY(win->canvas->hasImage());
    QTRY_VERIFY(!win->remote.reloading);
    const stencil::gui::RemoteLink& link = win->remote.session->getLink();

    bool done = false;
    win->parts.projects.setProjectColorById(QStringLiteral("p1"), mock.url(), QStringLiteral("#ff0000"),
                                            [&done](bool ok) { done = ok; });
    QTRY_VERIFY(done);
    QCOMPARE(shared.version, 2);
    QCOMPARE(link.version, 2);

    // A peer's layout edit before the colour PUT's re-read: the link keeps the version it knew.
    shared.layout = layoutOf({lineAt(60)});
    ++shared.version;
    done = false;
    win->parts.projects.setProjectColorById(QStringLiteral("p1"), mock.url(), QStringLiteral("#00ff00"),
                                            [&done](bool ok) { done = ok; });
    QTRY_VERIFY(done);
    QCOMPARE(shared.version, 4);
    QVERIFY2(link.version == 2, qPrintable(QStringLiteral("the colour adopted a version that covers the peer's edit: %1")
                                              .arg(link.version)));
    win->remoteSync->onRemoteProjectEvent(QStringLiteral("p1"), shared.version, false);
    QTRY_COMPARE(static_cast<int>(win->canvas->getLines().size()), 1);
    QTRY_COMPARE(link.version, 4);
    win->close();
    QTRY_VERIFY(win.isNull());
  }

  void aLateRenameLeavesTheNextProjectAlone() {
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    seedProject(mock, QStringLiteral("p1"), QStringLiteral("Shared"));
    seedProject(mock, QStringLiteral("p2"), QStringLiteral("Other"));
    QPointer<MainWindow> win = openLinked(mock, QStringLiteral("p1"));
    QVERIFY(win);
    QTRY_VERIFY(win->canvas->hasImage());
    QTRY_VERIFY(!win->remote.reloading);
    const stencil::gui::RemoteLink& link = win->remote.session->getLink();

    // The rename's PUT is held on the server while the editor moves to p2.
    mock.holdPuts = true;
    win->nameBar.field->setText(QStringLiteral("Moved"));
    win->projectTitle->commitProjectName();
    QTRY_COMPARE(mock.heldOriginals.size(), 1);
    win->parts.projects.openServerProject(mock.url(), QStringLiteral("p2"));
    QTRY_COMPARE(link.id, QStringLiteral("p2"));
    QTRY_VERIFY(!win->remote.reloading);
    const qint64 otherVersion = link.version;
    mock.releasePuts();
    QTRY_COMPARE(mock.projects[QStringLiteral("p1")].name, QStringLiteral("Moved"));
    QTRY_COMPARE(mock.puts, 1);
    QTest::qWait(100);   // the reply's completion has run by now
    QCOMPARE(link.id, QStringLiteral("p2"));
    QCOMPARE(link.name, QStringLiteral("Other"));
    QCOMPARE(link.version, otherVersion);
    QCOMPARE(mock.projects[QStringLiteral("p2")].name, QStringLiteral("Other"));
    win->close();
    QTRY_VERIFY(win.isNull());
  }

  // A layout push still on the wire when the editor moves to p2 lands on p1 and leaves p2's link
  // and record alone.
  void aLatePushLeavesTheNextProjectAlone() {
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    seedProject(mock, QStringLiteral("p1"), QStringLiteral("Shared"));
    seedProject(mock, QStringLiteral("p2"), QStringLiteral("Other"));
    QPointer<MainWindow> win = openLinked(mock, QStringLiteral("p1"));
    QVERIFY(win);
    QTRY_VERIFY(win->canvas->hasImage());
    QTRY_VERIFY(!win->remote.reloading);
    const stencil::gui::RemoteLink& link = win->remote.session->getLink();

    mock.holdPuts = true;
    win->canvas->commitLines({lineAt(20)});
    QTRY_COMPARE(mock.heldOriginals.size(), 1);
    win->parts.projects.openServerProject(mock.url(), QStringLiteral("p2"));
    QTRY_COMPARE(link.id, QStringLiteral("p2"));
    QTRY_VERIFY(!win->remote.reloading);
    const qint64 otherVersion = link.version;
    mock.releasePuts();
    QTRY_COMPARE(mock.puts, 1);
    QTRY_VERIFY(!win->remote.pushing);
    int w = 0, h = 0;
    QCOMPARE(int(stencil::gui::fileStore::parseLayoutJson(mock.projects[QStringLiteral("p1")].layout, w, h).size()), 1);
    QCOMPARE(link.id, QStringLiteral("p2"));
    QCOMPARE(link.version, otherVersion);
    QCOMPARE(mock.projects[QStringLiteral("p2")].version, otherVersion);
    win->close();
    QTRY_VERIFY(win.isNull());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.coEditMeta.gui.moc"
