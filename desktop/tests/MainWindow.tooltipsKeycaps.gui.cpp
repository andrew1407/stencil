// MainWindow GUI e2e — The keycaps a tooltip wears, and the shake they play on arrival.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowTip.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The KEYCAPS shake as their tooltip appears, a brief non-repeating flick (browser .tip-key.key-shake):
  // showing the tip is the trigger, the PANEL never moves, and the caps settle back where they started.
  void showingATooltipShakesItsKeycaps() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    win.raise();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* btn = capCarrier(win, true);
    QVERIFY2(btn, "no shown toolbar button whose tooltip carries keycaps");
    stencil::gui::AppTooltip* tip = stencil::gui::appTooltip();
    QVERIFY(tip);

    // Park the pointer ON the control so the anti-stranding heartbeat leaves it up: the offscreen screen is
    // smaller than this window, so walk the WINDOW until the cursor's landing spot and the control agree.
    const auto onControl = [&] {
      return btn->rect().contains(btn->mapFromGlobal(QCursor::pos()));
    };
    for (int i = 0; i < 4 && !onControl(); ++i) {
      QCursor::setPos(btn->mapToGlobal(btn->rect().center()));
      if (onControl()) break;
      win.move(win.pos() + (QCursor::pos() - btn->mapToGlobal(btn->rect().center())));
      QTest::qWait(40);
    }
    sendToolTipTo(btn);
    QVERIFY2(tip->isVisible(), "the tooltip did not appear");
    // The caps HOLD STILL while the tip is still assembling out of its own motes, and are queued for the
    // moment it lands; without dust there is nothing to wait for and the shake is immediate instead.
    QVERIFY2(tip->shaking() || tip->shakePending(),
             "the keycaps were neither shaken nor queued to shake");
    if (tip->shakePending())
      QVERIFY2(!tip->shaking(), "the caps moved while the tip was still forming");
    QTRY_VERIFY_WITH_TIMEOUT(tip->shaking(), stencil::gui::AppTooltip::DUST_IN_MS + 2000);
    QVERIFY2(tip->keycapsShown() > 0, "the shake found no caps to move");
    QLabel* body = tip->findChild<QLabel*>();
    QVERIFY(body);
    const QPoint home = tip->pos();
    // The CAPS really move — pixels, not a counter — while the panel around them holds still, and it is
    // one pass: they settle back on the picture they started from.
    QImage midShake;
    for (int i = 0; i < 40 && midShake.isNull(); ++i) {
      QTest::qWait(8);
      QCOMPARE(tip->pos(), home);                     // the panel itself must not swing
      if (tip->shakeOffset() != 0) midShake = body->grab().toImage();
    }
    QVERIFY2(!midShake.isNull(), "the caps never left their resting slot");
    QTRY_VERIFY_WITH_TIMEOUT(!tip->shaking(), stencil::gui::AppTooltip::SHAKE_MS + 2000);
    QCOMPARE(tip->pos(), home);
    QCOMPARE(tip->shakeOffset(), 0);
    const QImage settled = body->grab().toImage();
    QVERIFY2(midShake != settled, "the shake redrew the tooltip exactly as it sits at rest");
    QVERIFY2(tip->isVisible(), "the shake must not retire the tooltip");
    // A re-sent ToolTip for the SAME control is not a new appearance (Qt keeps re-arming
    // its wake-up while the pointer wanders inside one control): no second shake.
    sendToolTipTo(btn);
    QVERIFY2(!tip->shaking() && !tip->shakePending(),
             "the same tooltip shook again under a wandering pointer");

    // ANY key retires it — Escape included. The shake announces the shortcut; it is not a
    // way to pin the tooltip open.
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(win.canvas_, &esc);
    QTRY_VERIFY_WITH_TIMEOUT(!tip->isVisible(), 1500);

    // A tooltip with NO shortcut draws no caps, so it has nothing to announce.
    QToolButton* plain = capCarrier(win, false);
    if (!plain) {  // every live control happens to carry a chord — give one a bare tooltip
      for (QToolButton* b : win.findChildren<QToolButton*>())
        if (b->isVisible() && b->isEnabled() && b != btn) { plain = b; break; }
      QVERIFY(plain);
      plain->setToolTip(QStringLiteral("Bare hover text, nothing bound"));
    }
    QCursor::setPos(plain->mapToGlobal(plain->rect().center()));
    sendToolTipTo(plain);
    QVERIFY(tip->isVisible());
    QVERIFY2(!stencil::gui::hasKeycaps(body->text()), "the control drew keycaps after all");
    QVERIFY2(!tip->shaking() && !tip->shakePending(), "a tooltip with no keycaps still shook");
    QCOMPARE(tip->shakeOffset(), 0);
    QCOMPARE(tip->keycapsShown(), 0);
    const QPoint bareHome = tip->pos();
    const QImage bare = body->grab().toImage();
    QTest::qWait(120);
    QCOMPARE(tip->pos(), bareHome);                    // …and nothing moved while we watched
    QCOMPARE(body->grab().toImage(), bare);

    // A fast sweep re-points the tooltip mid-shake, over and over: the shakes must not
    // stack, and every cap must end up back on its own slot.
    for (int i = 0; i < 8; ++i) {
      sendToolTipTo(i % 2 ? plain : btn);
      QTest::qWait(20);
    }
    QCursor::setPos(btn->mapToGlobal(btn->rect().center()));
    sendToolTipTo(btn);
    QTRY_VERIFY_WITH_TIMEOUT(!tip->shaking(), stencil::gui::AppTooltip::SHAKE_MS + 2000);
    QCOMPARE(tip->shakeOffset(), 0);
    // Settled back EXACTLY: the same picture as the first appearance left behind.
    QCOMPARE(body->grab().toImage(), settled);
    // …and nothing is stranded: the pointer is on neither control any more.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
    QTRY_VERIFY_WITH_TIMEOUT(!tip->isVisible(), 3000);
    QCOMPARE(tip->shakeOffset(), 0);

    // Reduced motion: shown, correct, and not a pixel of shake — panel or caps.
    qputenv("STENCIL_NO_ANIM", "1");
    QCursor::setPos(btn->mapToGlobal(btn->rect().center()));
    sendToolTipTo(btn);
    QVERIFY(tip->isVisible());
    QVERIFY(stencil::gui::hasKeycaps(body->text()));
    const QPoint restingPos = tip->pos();
    QVERIFY2(!tip->shaking(), "reduced motion still shook the keycaps");
    QCOMPARE(tip->shakeOffset(), 0);
    QTest::qWait(120);
    QCOMPARE(tip->pos(), restingPos);
    QCOMPARE(tip->shakeOffset(), 0);
    QCOMPARE(body->grab().toImage(), settled);         // the caps drawn exactly where they live
    qunsetenv("STENCIL_NO_ANIM");
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.tooltipsKeycaps.gui.moc"
