// MainWindow GUI e2e — a .stc script across the loads it waits on: an @source download and its
// @frame reload park the run with no nested loop under it, and the later edits land in order.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../MainWindow.gui.hpp"
#include "../support/heldImageServer.hpp"

namespace {

  // Every notice the window raises, whatever its lifetime on screen.
  struct RecordingSink : stencil::gui::NotificationSink {
    QStringList shown;
    bool show(const stencil::gui::Notice& n) override { shown << n.text; return true; }
    bool isAvailable() const override { return true; }
    void setActive(bool) override {}
  };

  QString writeScript(const QTemporaryDir& dir, const QString& text) {
    const QString path = QDir(dir.path()).filePath(QStringLiteral("loads.stc"));
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(text.toUtf8());
    return path;
  }

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void scriptRunsAcrossItsLoads() {
    stencil::test::HeldImageServer http;
    QVERIFY(http.listen());
    QTemporaryDir dir;
    const QString path = writeScript(dir, QStringLiteral("@source %1:\n  @frame 3\n  @filter bw\n"
                                                         "  @rect (2,2) (6,6)\n").arg(http.url()));
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto owned = std::make_unique<RecordingSink>();
    RecordingSink* notices = owned.get();
    win.notify->setSystemSink(std::move(owned));
    win.notify->setChannel(stencil::gui::NotifyChannel::SYSTEM);

    win.parts.scriptHost.runScriptFromFile(path);
    QTRY_COMPARE(http.requests, 1);
    QVERIFY2(win.pop.loops.isEmpty(), "the run waits with no nested loop under it");
    QCOMPARE(win.pop.awaits.size(), qsizetype(1));
    QVERIFY(!win.canvas->hasImage());
    QVERIFY(!notices->shown.contains(QStringLiteral("Script executed successfully")));

    http.release();   // this answer and the @frame reload's
    QTRY_VERIFY_WITH_TIMEOUT(notices->shown.contains(QStringLiteral("Script executed successfully")),
                             10000);
    QCOMPARE(http.requests, 2);
    QVERIFY(win.pop.awaits.isEmpty());
    QCOMPARE(win.canvas->effectiveOriginalImage().size(), QSize(20, 14));
    const QImage out = win.canvas->renderToImage(false);
    const QRgb px = out.pixel(out.width() - 2, out.height() - 2);
    QVERIFY2(qRed(px) == qGreen(px) && qGreen(px) == qBlue(px), "@filter landed on the loaded picture");
    QCOMPARE(int(win.canvas->allLines().size()), 1);
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.scriptAwait.gui.moc"
