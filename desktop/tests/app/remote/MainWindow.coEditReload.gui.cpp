// MainWindow GUI e2e — a co-edit reload whose picture download is held open by the REST
// stand-in: a stroke drawn meanwhile survives the landing and reaches the server, and the poll
// starts no second reload while the first is in flight.
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
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.coEditReload.gui.moc"
