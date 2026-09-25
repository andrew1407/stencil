// MainWindow GUI e2e — the logo stage's ROAMING shows: the mark that chases the pointer and the
// one that flees it, and the hand they wear. Shared ground (helpers, the loaded window) is in
// MainWindow.gui.hpp; the held show, the lock and the pink edit are in MainWindow.logoStage.
#include "../../MainWindow.gui.hpp"
#include "LogoStage.hpp"

#include <cmath>

#include "logoStageRules.hpp"

using stencil::gui::LogoStage;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  struct MoveCount : QObject {
    int* n;
    explicit MoveCount(int* n) : n(n) {}
    bool eventFilter(QObject*, QEvent* e) override { *n += e->type() == QEvent::MouseMove; return false; }
  };
  static LogoStage* stageOf(MainWindow& win) { return win.findChild<LogoStage*>("logoStage"); }
  // The pointer is placed, and NOTHING is sent: the stage has to read it for itself.
  static void pointAt(MainWindow& win, QPoint at) {
    QCursor::setPos(win.mapToGlobal(at));
    QTest::qWait(900);
  }
  static double gap(QPointF a, QPoint b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The pointer rests where it was before the word opened the show and never moves again, so
  // no move reaches the stage: the mark has only the real pointer to go by, from its first frame.
  void aTypedShowRunsAtTheIdlePointer_data() {
    QTest::addColumn<QString>("word");
    QTest::addColumn<bool>("flees");
    QTest::newRow("chaseme") << QStringLiteral("chaseme") << false;
    QTest::newRow("runaway") << QStringLiteral("runaway") << true;
  }
  void aTypedShowRunsAtTheIdlePointer() {
    QFETCH(QString, word);
    QFETCH(bool, flees);
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    LogoStage* stage = stageOf(win);
    const QPoint rest = flees ? QPoint(420, 330) : QPoint(160, 520);   // runaway: inside its radius
    QCursor::setPos(win.mapToGlobal(rest));
    QTest::qWait(50);
    int moves = 0;
    MoveCount counter(&moves);
    stage->installEventFilter(&counter);
    QTest::keyClicks(QApplication::focusWidget() ? QApplication::focusWidget() : &win, word);
    QVERIFY2(stage->isOpen(), "the typed word opened the show");
    const double opening = gap(stage->markPos(), rest);
    QTest::qWait(900);
    QCOMPARE(moves, 0);
    if (flees) QVERIFY2(gap(stage->markPos(), rest) > opening + stage->markSize(), "it fled a pointer it never saw move");
    else QVERIFY2(gap(stage->markPos(), rest) < opening / 2, "it closed on a pointer it never saw move");
  }

  // Through the real window, with the chat docked or not: a press on the flying mark punches it,
  // and a press away from it ends the show.
  void aPressOnTheFlyingMarkPunchesItAndOneAwayEndsIt_data() {
    QTest::addColumn<bool>("chat");
    QTest::addColumn<bool>("held");
    QTest::newRow("bare") << false << false;
    QTest::newRow("chat docked") << true << false;
    QTest::newRow("held open") << false << true;
  }
  void aPressOnTheFlyingMarkPunchesItAndOneAwayEndsIt() {
    QFETCH(bool, chat);
    QFETCH(bool, held);
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 720);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    if (chat && win.actChat) { win.actChat->setChecked(true); QTest::qWait(300); }
    LogoStage* stage = stageOf(win);
    if (held) {
      win.settings.accentColor = QStringLiteral("#123456");   // any other custom hex holds randomWalk open
      const QPoint logoAt = win.logoBtn->mapTo(&win, win.logoBtn->rect().center());
      QTest::mousePress(win.windowHandle(), Qt::LeftButton, Qt::NoModifier, logoAt);
      QTest::qWait(stencil::support::logoStageConfig().holdMs + 200);
      QTest::mouseRelease(win.windowHandle(), Qt::LeftButton, Qt::NoModifier, logoAt);
      QVERIFY2(stage->isOpen() && stage->showName() == QLatin1String("randomWalk"), "the hold opened randomWalk");
    } else {
      QVERIFY(stage->activateByName("randomWalk"));
    }
    QTest::qWait(700);
    const auto speed = [&] {
      const QPointF a = stage->markPos();
      QTest::qWait(60);
      return gap(stage->markPos(), a.toPoint()) / 0.06;
    };
    const double cruise = speed();
    const QPoint on = stage->mapTo(&win, stage->markPos().toPoint());
    QTest::mouseClick(win.windowHandle(), Qt::LeftButton, Qt::NoModifier, on);
    QVERIFY2(stage->isOpen(), "a press on the mark keeps the show");
    QVERIFY2(speed() > cruise * 1.4, qPrintable(QStringLiteral("the press punched it: %1 -> %2").arg(cruise).arg(speed())));
    QTest::qWait(1500);
    QPoint away = stage->markPos().x() < win.width() / 2 ? QPoint(win.width() - 20, win.height() - 20) : QPoint(20, win.height() - 20);
    QTest::mouseClick(win.windowHandle(), Qt::LeftButton, Qt::NoModifier, away);
    QTest::qWait(60);
    QVERIFY2(!stage->isOpen(), "a press away from the mark ends the show");
  }

  // A widget asked for its native view gets a window of its own, and a real press lands there
  // first: the stage must still hear it and close.
  void aPressOnANativeChildWindowStillEndsTheShow() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 720);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QWidget* bar = win.logoBtn->parentWidget();
    bar->winId();
    QVERIFY(bar->windowHandle());
    LogoStage* stage = stageOf(win);
    QVERIFY(stage->activateByName("randomWalk"));
    QTest::qWait(50);
    QTest::mouseClick(bar->windowHandle(), Qt::LeftButton, Qt::NoModifier, QPoint(bar->width() - 8, bar->height() / 2));
    QTest::qWait(60);
    QVERIFY2(!stage->isOpen(), "a press on a native child of the window ends the show");
  }

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
