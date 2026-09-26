// MainWindow GUI e2e — The tool rows' fold: under dust the real rows stay veiled for the whole
// flight, so the motes ARE the rows (browser dust.css surfaceForm / surfaceLeave), and without a
// flight nothing veils them. Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../MainWindowFlight.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static QList<QToolBar*> toolRows(MainWindow& win) {
    QList<QToolBar*> rows;
    for (QToolBar* b : win.findChildren<QToolBar*>())
      if (b != win.headerToolbar) rows.append(b);
    return rows;
  }
  static bool veiled(const QToolBar* b) {
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(b->graphicsEffect());
    return fx && fx->opacity() == 0.0;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Both ways: veiled while the motes fly, handed back whole once they have landed.
  void theRowsAreTheirDustWhileTheyFold() {
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
      QSKIP("the fold's dust needs a real platform (motionPrefs isDustMotionOk)");
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    const QList<QToolBar*> rows = toolRows(win);
    QVERIFY(!rows.isEmpty());
    for (bool show : {false, true}) {
      win.setToolbarsShown(show, /*animate=*/true);
      QVERIFY2(stencil::guitest::surfaceFlight(win.editor), "no dust flew for the fold");
      for (QToolBar* b : rows)
        QVERIFY2(veiled(b), "a row showed through its own dust");
      QTRY_VERIFY_WITH_TIMEOUT(!stencil::guitest::surfaceFlight(win.editor), 3000);
      QTRY_VERIFY(!win.barsAnim);
      for (QToolBar* b : rows) {
        QVERIFY2(!b->graphicsEffect(), "a row kept its veil after the flight");
        QCOMPARE(b->isVisible(), show);
      }
    }
  }

  // No flight (offscreen, a still interface): the rows slide as they are, never hidden.
  void withoutDustTheRowsAreNeverVeiled() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    const auto motion = withMotion();
    if (stencil::support::isDustMotionOk()) QSKIP("dust flies here; the case above covers it");
    const QList<QToolBar*> rows = toolRows(win);
    win.setToolbarsShown(false, /*animate=*/true);
    for (QToolBar* b : rows) QVERIFY(!veiled(b));
    win.setToolbarsShown(true, /*animate=*/true);
    for (QToolBar* b : rows) QVERIFY(!veiled(b));
    QTRY_VERIFY(!win.barsAnim);
    for (QToolBar* b : rows) QVERIFY(b->isVisible() && !b->graphicsEffect());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarFold.gui.moc"
