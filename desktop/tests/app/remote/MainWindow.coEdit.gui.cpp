// MainWindow GUI e2e — live co-edit against an in-process REST stand-in: a peer's layout edit
// keeps the undo history and the decoded picture, and the baked result follows its throttle.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../support/connectNow.hpp"
#include "../../support/mockRest.hpp"
#include "../../../src/app/remote/RemoteSyncController.hpp"

namespace {

  QByteArray pngOf(const QColor& fill) {
    QImage img(120, 80, QImage::Format_RGB32);
    img.fill(fill);
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return png;
  }

  stencil::core::Line lineAt(double y) {
    stencil::core::Line l;
    l.points = {{10, y}, {100, y}};
    return l;
  }

  QJsonObject layoutOf(const stencil::core::Lines& lines) {
    return stencil::gui::fileStore::buildLayoutJson(120, 80, lines, "none", stencil::gui::DEFAULT_ACCENT_HEX,
                                                   stencil::core::CropRect{0, 0, 120, 80});
  }

  // Every notice the window raises, whatever its lifetime on screen.
  struct RecordingSink : stencil::gui::NotificationSink {
    QStringList shown;
    bool show(const stencil::gui::Notice& n) override { shown << n.text; return true; }
    bool isAvailable() const override { return true; }
    void setActive(bool) override {}
    int saved() const { return int(shown.filter(QStringLiteral("Saved \"Shared\"")).size()); }
  };

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void peerEditKeepsHistoryAndResultIsThrottled() {
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    stencil::test::MockProject& shared = mock.projects[QStringLiteral("p1")];
    shared.name = QStringLiteral("Shared");
    shared.original = pngOf(Qt::white);
    shared.layout = layoutOf({});
    QPointer<MainWindow> win = new MainWindow(nullptr, false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->resize(1000, 760);
    win->show();
    QVERIFY(QTest::qWaitForWindowExposed(win.data()));
    win->settings.syncToServer = true;
    auto owned = std::make_unique<RecordingSink>();
    RecordingSink* notices = owned.get();
    win->notify->setSystemSink(std::move(owned));
    win->notify->setChannel(stencil::gui::NotifyChannel::SYSTEM);
    QString err;
    QVERIFY2(stencil::test::connectNow(*win->ensureConnections(), mock.url(), QString(), err),
             qPrintable(err));
    // One result at once, then none for the rest of the case: only the close may send another.
    win->remoteSync->setResultTiming(50, 600000);
    win->parts.projects.openServerProject(mock.url(), QStringLiteral("p1"));
    QTRY_VERIFY(win->canvas->hasImage());
    QCOMPARE(mock.originalGets, 1);
    const qint64 pictureKey = win->canvas->getOriginalImage().cacheKey();

    // Our own edits: each settles into one push, and the result bakes once.
    win->canvas->commitLines({lineAt(20)});
    QTRY_COMPARE(mock.puts, 1);
    QTRY_COMPARE(mock.resultPosts, 1);
    win->canvas->commitLines({lineAt(20), lineAt(30)});
    QTRY_COMPARE(mock.puts, 2);
    win->canvas->commitLines({lineAt(20), lineAt(30), lineAt(40)});
    QTRY_COMPARE(mock.puts, 3);
    QTest::qWait(300);
    QCOMPARE(mock.resultPosts, 1);
    QCOMPARE(notices->saved(), 1);

    // A peer adds a line: the layout lands as one undo step over ours, the picture untouched.
    stencil::core::Lines peer{lineAt(20), lineAt(30), lineAt(40), lineAt(60)};
    shared.layout = layoutOf(peer);
    ++shared.version;
    win->remoteSync->onRemoteProjectEvent(QStringLiteral("p1"), shared.version, false);
    QTRY_COMPARE(int(win->canvas->getLines().size()), 4);
    QCOMPARE(mock.originalGets, 1);
    QCOMPARE(win->canvas->getOriginalImage().cacheKey(), pictureKey);
    QVERIFY2(win->canvas->canUndo(), "a peer's layout edit must not reset the undo history");
    win->canvas->undo();
    QCOMPARE(int(win->canvas->getLines().size()), 3);
    win->canvas->undo();
    QCOMPARE(int(win->canvas->getLines().size()), 2);

    // A peer changes only the filter: a step of its own, so undoing a later stroke keeps it.
    QTRY_VERIFY(!win->remote.reloading && !win->remote.pushing);
    QTest::qWait(500);
    const stencil::core::Lines two = win->canvas->getLines();
    shared.layout = stencil::gui::fileStore::buildLayoutJson(
        120, 80, two, "sepia", stencil::gui::DEFAULT_ACCENT_HEX, stencil::core::CropRect{0, 0, 120, 80});
    ++shared.version;
    win->remoteSync->onRemoteProjectEvent(QStringLiteral("p1"), shared.version, false);
    QTRY_COMPARE(win->canvas->getImageFilter(), QStringLiteral("sepia"));
    win->canvas->commitLines({two[0], two[1], lineAt(50)});
    win->canvas->undo();
    QCOMPARE(win->canvas->getImageFilter(), QStringLiteral("sepia"));
    win->canvas->undo();
    QCOMPARE(win->settings.imageFilter, QStringLiteral("none"));

    // A peer replaces the picture: that one is a full reload.
    QTRY_VERIFY(!win->remote.reloading && !win->remote.pushing);
    QTest::qWait(500);
    shared.original = pngOf(Qt::black);
    ++shared.version;
    win->remoteSync->onRemoteProjectEvent(QStringLiteral("p1"), shared.version, false);
    QTRY_VERIFY(win->canvas->getOriginalImage().cacheKey() != pictureKey);
    QCOMPARE(QColor(win->canvas->getOriginalImage().pixel(5, 5)), QColor(Qt::black));

    // An older server sends no originalHash: every peer edit is the full reload it always was.
    const qint64 afterReplace = win->canvas->getOriginalImage().cacheKey();
    const int getsBefore = mock.originalGets;
    mock.withHash = false;
    shared.layout = layoutOf({lineAt(20)});
    ++shared.version;
    win->remoteSync->onRemoteProjectEvent(QStringLiteral("p1"), shared.version, false);
    QTRY_VERIFY(win->canvas->getOriginalImage().cacheKey() != afterReplace);
    QCOMPARE(mock.originalGets, getsBefore + 1);
    QCOMPARE(int(win->canvas->getLines().size()), 1);
    // A full reload starts the history at the loaded layout: one step back is the empty page.
    win->canvas->undo();
    QVERIFY2(win->canvas->getLines().empty() && !win->canvas->canUndo(),
             "without a hash to compare, the reload is the full one");
    mock.withHash = true;
    QTRY_VERIFY(!win->remote.reloading && !win->remote.pushing);

    // The close sends the result the throttle was still holding, then lets the window go.
    win->canvas->commitLines({lineAt(70)});
    const int putsBefore = mock.puts;
    QTRY_VERIFY(mock.puts > putsBefore);
    QTRY_VERIFY(win->remoteSync->resultBusy());
    const int resultsBefore = mock.resultPosts;
    win->close();
    QTRY_VERIFY(mock.resultPosts > resultsBefore);
    QTRY_VERIFY(win.isNull());
  }

  // A peer crops and turns the picture this editor holds: one step in place, nothing re-downloaded.
  void peerCropAndTurnLandInPlace() {
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    stencil::test::MockProject& shared = mock.projects[QStringLiteral("p2")];
    shared.name = QStringLiteral("Turned");
    shared.original = pngOf(Qt::white);
    shared.layout = layoutOf({});
    QPointer<MainWindow> win = new MainWindow(nullptr, false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->resize(1000, 760);
    win->show();
    QVERIFY(QTest::qWaitForWindowExposed(win.data()));
    win->settings.syncToServer = true;
    QString err;
    QVERIFY2(stencil::test::connectNow(*win->ensureConnections(), mock.url(), QString(), err),
             qPrintable(err));
    win->remoteSync->setResultTiming(50, 600000);
    win->parts.projects.openServerProject(mock.url(), QStringLiteral("p2"));
    QTRY_VERIFY(win->canvas->hasImage());
    const qint64 pictureKey = win->canvas->getOriginalImage().cacheKey();
    win->canvas->commitLines({lineAt(20)});
    QTRY_COMPARE(mock.puts, 1);
    QTRY_VERIFY(!win->remote.reloading && !win->remote.pushing);
    QTest::qWait(300);

    // A quarter turn makes the picture 80x120; the peer's crop sits inside that.
    const stencil::core::CropRect cut{10, 20, 60, 90};
    shared.layout = stencil::gui::fileStore::buildLayoutJson(
        60, 90, {lineAt(20), lineAt(40)}, "none", stencil::gui::DEFAULT_ACCENT_HEX, cut, 1);
    ++shared.version;
    win->remoteSync->onRemoteProjectEvent(QStringLiteral("p2"), shared.version, false);
    QTRY_COMPARE(win->canvas->getRotationQuarters(), 1);
    QCOMPARE(mock.originalGets, 1);
    QCOMPARE(win->canvas->getOriginalImage().cacheKey(), pictureKey);
    QCOMPARE(QSize(win->canvas->imageWidth(), win->canvas->imageHeight()), QSize(60, 90));
    QCOMPARE(win->canvas->getCropRect().y, 20.0);
    QCOMPARE(int(win->canvas->getLines().size()), 2);
    QVERIFY2(win->canvas->canUndo(), "a peer's crop and turn must not reset the undo history");
    QTRY_VERIFY(!win->remote.reloading && !win->remote.pushing);

    // One undo is the geometry and the lines from before the peer, the picture rebuilt in memory.
    win->canvas->undo();
    QCOMPARE(win->canvas->getRotationQuarters(), 0);
    QCOMPARE(QSize(win->canvas->imageWidth(), win->canvas->imageHeight()), QSize(120, 80));
    QCOMPARE(win->canvas->getCropRect().width, 120.0);
    QCOMPARE(int(win->canvas->getLines().size()), 1);
    QVERIFY2(win->canvas->canUndo(), "the user's own steps are still below the peer's");
    win->canvas->redo();
    QCOMPARE(win->canvas->getRotationQuarters(), 1);
    QCOMPARE(int(win->canvas->getLines().size()), 2);
    QCOMPARE(mock.originalGets, 1);
    QCOMPARE(win->canvas->getOriginalImage().cacheKey(), pictureKey);

    QTRY_VERIFY(!win->remote.reloading && !win->remote.pushing);
    win->close();
    QTRY_VERIFY(win.isNull());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.coEdit.gui.moc"
