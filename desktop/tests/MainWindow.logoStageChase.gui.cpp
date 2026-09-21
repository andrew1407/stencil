// MainWindow GUI e2e — the logo stage's ROAMING shows: the mark that chases the pointer and the
// one that flees it, and the hand they wear. Shared ground (helpers, the loaded window) is in
// MainWindow.gui.hpp; the held show, the lock and the pink edit are in MainWindow.logoStage.
#include "MainWindow.gui.hpp"
#include "LogoStage.hpp"

#include <cmath>

using stencil::gui::LogoStage;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static LogoStage* stageOf(MainWindow& win) { return win.findChild<LogoStage*>("logoStage"); }
  // The pointer is placed, and NOTHING is sent: the stage has to read it for itself.
  static void pointAt(MainWindow& win, QPoint at) {
    QCursor::setPos(win.mapToGlobal(at));
    QTest::qWait(900);
  }
  static double gap(QPointF a, QPoint b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }


  // The chase samples the real pointer every frame. Taking it from move events instead left the
  // mark sitting still whenever one failed to reach the stage, and the hand never applied.
  void theChasingMarkFollowsThePointerWithNoMoveEvents() {
    const auto motion = withMotion();   // the suite runs with STENCIL_NO_ANIM on; a chase needs it
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    LogoStage* stage = stageOf(win);
    QVERIFY2(stage->activateByName("chaseMe"), "white opens the chasing show");

    const QPoint first(120, 140), second(760, 600);
    const double opening = gap(stage->markPos(), first);
    pointAt(win, first);
    const QPointF near = stage->markPos();
    QVERIFY2(gap(near, first) < opening / 2, "the mark closed most of the gap to the pointer");

    pointAt(win, second);
    const QPointF far = stage->markPos();
    QVERIFY2(far.x() > near.x() + 150 && far.y() > near.y() + 150, "…and back across after it");
    QCOMPARE(stage->cursor().shape(), Qt::PointingHandCursor);
  }

  // Black flees instead, and only once the pointer is inside escape.radiusPx of it.
  void theFleeingMarkKeepsAwayFromThePointer() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    LogoStage* stage = stageOf(win);
    QVERIFY2(stage->activateByName("runaway"), "black opens the fleeing show");

    const QPoint beside = stage->markPos().toPoint() + QPoint(12, 10);   // well inside its radius
    const double opening = gap(stage->markPos(), beside);
    pointAt(win, beside);
    const QPointF away = stage->markPos();
    QVERIFY2(gap(away, beside) > opening + stage->markSize(), "it pushed clear of the pointer");
    QVERIFY2(win.rect().contains(away.toPoint()), "…without leaving the window");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.logoStageChase.gui.moc"
