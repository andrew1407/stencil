// MainWindow GUI e2e — A picture decodes off the GUI thread and lands in a continuation: the loop
// keeps turning, a load a newer one overtook is dropped, a window closed mid-decode is safe, and the
// boot restore holds the first show until it lands. Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

#include <QThreadPool>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // Large enough that its decode spans many turns of a 1 ms timer.
  static QString bigPicture() {
    static const QString path = QDir::temp().filePath(QStringLiteral("stencil_e2e_big.png"));
    if (!QFileInfo::exists(path)) {
      QImage img(4000, 3000, QImage::Format_RGB32);
      for (int y = 0; y < img.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) row[x] = qRgb(x & 255, y & 255, (x ^ y) & 255);
      }
      img.save(path, "PNG");
    }
    return path;
  }

  static stencil::gui::Project projectOn(const QString& id, const QString& imagePath) {
    stencil::gui::Project pr;
    pr.meta.id = id.toStdString();
    pr.meta.name = id.toStdString();
    pr.meta.hasImage = true;
    pr.imagePath = imagePath;
    return pr;
  }

  // Every pool decode has run, and every answer it posted has had its turn on the GUI thread.
  static void drainDecodes() {
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
  }

 private slots:
  void initTestCase() {
    prepareGuiTestCase();
    QVERIFY(QFileInfo::exists(bigPicture()));
  }

  // openPathFromOS returns before the picture is in, and a 1 ms GUI timer keeps ticking until it is.
  void aPictureDecodesOffTheGuiThread() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    int ticks = 0;
    QTimer beat;
    beat.setInterval(1);
    connect(&beat, &QTimer::timeout, this, [&ticks] { ++ticks; });
    beat.start();
    win.openPathFromOS(bigPicture());
    QVERIFY2(!win.canvas->hasImage(), "the picture was decoded inside the call that asked for it");
    QTRY_VERIFY_WITH_TIMEOUT(win.canvas->hasImage(), 10000);
    beat.stop();
    QVERIFY2(ticks >= 5, qPrintable(QStringLiteral("the GUI loop stalled: %1 ticks").arg(ticks)));
    QCOMPARE(win.canvas->getOriginalImage().size(), QSize(4000, 3000));
  }

  // Two project opens in a row: the second wins even though the first finishes decoding later.
  void aLoadANewerOneOvertookIsDropped() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.projectList.push_back(projectOn(QStringLiteral("big"), bigPicture()));
    win.projectList.push_back(projectOn(QStringLiteral("small"), guiTestImage()));
    int bigAnswers = 0, smallAnswers = 0;
    bool bigLanded = true, smallLanded = false;
    QVERIFY(win.loadProjectIntoCanvas(QStringLiteral("big"), false, [&](bool ok) {
      ++bigAnswers;
      bigLanded = ok;
    }));
    QVERIFY(win.loadProjectIntoCanvas(QStringLiteral("small"), false, [&](bool ok) {
      ++smallAnswers;
      smallLanded = ok;
    }));
    QTRY_COMPARE_WITH_TIMEOUT(smallAnswers, 1, 10000);
    drainDecodes();
    QVERIFY(smallLanded);
    QCOMPARE(bigAnswers, 1);
    QVERIFY2(!bigLanded, "the overtaken project still answered as opened");
    QCOMPARE(win.activeProjectId, QStringLiteral("small"));
    QCOMPARE(win.canvas->getImagePath(), guiTestImage());
    QCOMPARE(win.canvas->getOriginalImage().size(), QSize(240, 160));
  }

  // A window closed and deleted while its picture decodes takes the answer with it.
  void closingDuringAPendingLoadIsSafe() {
    auto* win = new MainWindow(nullptr, false);
    win->resize(1000, 760);
    win->show();
    QVERIFY(QTest::qWaitForWindowExposed(win));
    win->projectList.push_back(projectOn(QStringLiteral("big"), bigPicture()));
    bool answered = false;
    QVERIFY(win->loadProjectIntoCanvas(QStringLiteral("big"), true, [&answered](bool) { answered = true; }));
    win->openPathFromOS(bigPicture());
    win->close();
    delete win;
    drainDecodes();
    QVERIFY2(!answered, "a decode landed on a window that was gone");
  }

  // The boot restore decodes after the window shows: until it lands the window stays unseen and
  // the canvas empty, then the picture is in as it was left and the window fades in.
  void bootRestoreHoldsTheFirstShowUntilItLands() {
    stencil::gui::Session s;
    s.imagePath = bigPicture();
    s.scale = 0.25;
    s.pageSize = QStringLiteral("A4");
    stencil::gui::fileStore::saveSession(s);
    auto cleanup = qScopeGuard([] { stencil::gui::fileStore::saveSession(stencil::gui::Session{}); });
    MainWindow win(nullptr, /*restoreLast=*/true);
    win.resize(1000, 760);
    win.show();
    QVERIFY2(!win.canvas->hasImage(), "the restore decoded inside the constructor");
    QCOMPARE(win.windowOpacity(), 0.0);
    QTRY_VERIFY_WITH_TIMEOUT(win.canvas->hasImage(), 10000);
    QCOMPARE(win.canvas->getImagePath(), bigPicture());
    QCOMPARE(win.canvas->getScale(), 0.25);
    QTRY_VERIFY_WITH_TIMEOUT(win.windowOpacity() > 0.99, 2000);
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.openAsync.gui.moc"
