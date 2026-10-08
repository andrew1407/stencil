// MainWindow GUI e2e — In fullscreen, a window opened from a revealed tool row flies out of its icon,
// never down from above the screen while that icon is in plain sight.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void aRevealedFullscreenIconIsTheFlightOrigin() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(60, 40, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());
    settleLayout(&win, 150);
    win.parts.view.toggleFullscreen();
    settle([&] { return !win.fs.zoomAnim; }, 1500);
    QTest::qWait(qApp->platformName() == QLatin1String("offscreen") ? 0 : 1500);   // the native space switch
    win.fs.barsShown = true;
    win.parts.view.animateBarsHeight(win.fs.bars, true);
    settle([&] { return !win.parts.view.barsAnim; }, 2000);
    settleLayout(&win, 150);

    QAction* opener = nullptr;
    QWidget* icon = nullptr;
    for (QAction* a : {win.acts.projects, win.acts.crop, win.acts.info, win.acts.settings}) {
      QWidget* b = win.buttonForAction(a);
      if (b && b->isVisible() && b->height() > 4) { opener = a; icon = b; break; }
    }
    QVERIFY2(opener && icon, "no dialog icon on the revealed fullscreen rows");

    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    RevealOriginWatcher watcher;
    qApp->installEventFilter(&watcher);
    const auto removeWatcher = qScopeGuard([&] { qApp->removeEventFilter(&watcher); });
    QString seen = "none";
    // The click may open the window a beat later (a press can still become a hold), so poll for it.
    QTimer closer;
    int upTicks = 0;
    connect(&closer, &QTimer::timeout, &win, [&] {
      QWidget* modal = QApplication::activeModalWidget();
      if (!modal || ++upTicks < 8) return;
      seen = modal->metaObject()->className();
      modal->close();
    });
    closer.start(20);
    const QPoint want = flightPointOf(icon, &win);
    QTest::mouseClick(icon, Qt::LeftButton);
    settle([&] { return watcher.captured && seen != "none"; }, 3000);
    closer.stop();
    QVERIFY2(watcher.captured, qPrintable("no flight played; the modal seen: " + seen));
    QVERIFY2((watcher.origin - want).manhattanLength() <= 2,
             qPrintable(QString("fullscreen: flight starts at %1, the revealed icon is at %2")
                            .arg(QDebug::toString(watcher.origin), QDebug::toString(want))));
    win.parts.view.toggleFullscreen();
  }

  // …and once the rows have folded away under the open window, the close pours into the canvas, never
  // up past the screen's top edge where the hidden icon sits.
  void aCloseAfterTheRowsFoldPoursIntoTheCanvas() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(60, 40, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());
    settleLayout(&win, 150);
    win.parts.view.toggleFullscreen();
    settle([&] { return !win.fs.zoomAnim; }, 1500);
    QTest::qWait(qApp->platformName() == QLatin1String("offscreen") ? 0 : 1500);
    win.fs.barsShown = true;
    win.parts.view.animateBarsHeight(win.fs.bars, true);
    settle([&] { return !win.parts.view.barsAnim; }, 2000);
    settleLayout(&win, 150);
    QWidget* icon = win.buttonForAction(win.acts.projects);
    QVERIFY2(icon && icon->isVisible(), "the Projects icon is not on the revealed rows");

    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    RevealOriginWatcher watcher;
    qApp->installEventFilter(&watcher);
    const auto removeWatcher = qScopeGuard([&] { qApp->removeEventFilter(&watcher); });
    QString seen = "none";
    QTimer closer;
    int upTicks = 0;
    connect(&closer, &QTimer::timeout, &win, [&] {
      QWidget* modal = QApplication::activeModalWidget();
      if (!modal || ++upTicks < 8) return;
      if (upTicks == 8) {   // the window is up: the rows fold away under it, as the pointer leaving them does
        win.fs.barsShown = false;
        win.parts.view.animateBarsHeight(win.fs.bars, false);
        return;
      }
      if (win.parts.view.barsAnim || upTicks < 40) return;
      seen = modal->metaObject()->className();
      watcher.reset();
      QTest::keyClick(modal, Qt::Key_Escape);   // the user's way out; fullscreen must stay
      closer.stop();
    });
    closer.start(20);
    QTest::mouseClick(icon, Qt::LeftButton);
    settle([&] { return watcher.captured && seen != "none"; }, 4000);
    QVERIFY2(watcher.captured, qPrintable("no close flight played; the modal seen: " + seen));
    QVERIFY2(win.fs.active, "Escape on the window also left fullscreen");
    QVERIFY2(!icon->isVisible() || icon->height() < 4, "the rows never folded away");
    QWidget* vp = win.scroll->viewport();
    const QPoint centre = vp->mapTo(&win, vp->rect().center());
    QVERIFY2((watcher.origin - centre).manhattanLength() <= 4,
             qPrintable(QString("fullscreen close pours to %1, the canvas centre is %2")
                            .arg(QDebug::toString(watcher.origin), QDebug::toString(centre))));
    settle([&] { return !QApplication::activeModalWidget(); }, 1000);
    QTest::keyClick(&win, Qt::Key_Escape);   // with no window up, Escape is the mode's again
    QVERIFY2(!win.fs.active, "Escape on the editor no longer leaves fullscreen");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.fullscreenFlight.gui.moc"
