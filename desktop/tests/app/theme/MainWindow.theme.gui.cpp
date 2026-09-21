// MainWindow GUI e2e — The logo's hover pulse, and how it holds while the accent popover is up.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The logo's hover fx (browser logoPulse / logoRaysSpin parity): a window-level overlay owns the
  // pixels, the loop genuinely ADVANCES, and leaving stops every animation and hands the icon back.
  void logoHoverFxPulsesWhileHoveredOnly() {
    const auto motion = withMotion();   // the loop honours motionReduced(), which is on here
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* logo = win.logoBtn;
    QVERIFY(logo);
    QWidget* fx = win.findChild<QWidget*>("logoHoverFx");
    QVERIFY2(fx, "logo hover fx overlay not installed");
    QTest::qWait(20);   // the overlay shows its resting mark on a deferred tick after show
    auto iconBlank = [logo] {
      const QImage im = logo->icon().pixmap(logo->iconSize()).toImage();
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x)
          if (qAlpha(im.pixel(x, y)) != 0) return false;
      return true;
    };
    // The overlay paints the mark at ALL times (a QToolButton draws its icon half size on Retina), so at
    // rest it is VISIBLE but not animating and the button icon is blank. Only pulse/rays are hover-gated.
    QVERIFY2(fx->isVisible(), "the fx paints the resting mark");
    QVERIFY(!fx->property("fxActive").toBool());
    QVERIFY2(iconBlank(), "the overlay owns the mark at rest (button icon blanked)");

    const QPointF c(logo->rect().center());
    QEnterEvent enter(c, c, logo->mapToGlobal(logo->rect().center()));
    QApplication::sendEvent(logo, &enter);
    QVERIFY2(fx->isVisible(), "hover-enter must show the fx overlay");
    QVERIFY(fx->property("fxActive").toBool());
    QVERIFY2(iconBlank(), "the overlay owns the mark while animating (icon blanked)");
    QVERIFY2(fx->width() > logo->width() && fx->height() > logo->height(),
             "fx overlay must give the glow/rays room AROUND the button");
    const qreal b0 = fx->property("pulseBeat").toReal();
    const qreal a0 = fx->property("raysAngle").toReal();
    QTRY_VERIFY2(fx->property("pulseBeat").toReal() != b0, "pulse beat did not advance");
    QTRY_VERIFY2(fx->property("raysAngle").toReal() != a0, "ray rotation did not advance");
    QVERIFY(!fx->grab().isNull());   // painting the fx offscreen must not crash

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(logo, &leave);
    QVERIFY2(fx->isVisible(), "leave keeps the resting mark shown (only the pulse stops)");
    QVERIFY(!fx->property("fxActive").toBool());
    for (QVariantAnimation* a : fx->findChildren<QVariantAnimation*>())
      QVERIFY2(a->state() != QAbstractAnimation::Running,
               "an fx animation kept running after hover-leave");
    QVERIFY2(iconBlank(), "the overlay keeps the mark after leave (button icon stays blanked)");
  }

  // Browser parity: the accent popover the logo opens is part of the logo's hover — the shine holds
  // across the anchor gap and while the cursor rests there, and stops only once it has left both.
  void logoHoverFxHoldsOverAccentPopover() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* logo = win.logoBtn;
    QWidget* fx = win.findChild<QWidget*>("logoHoverFx");
    QVERIFY(logo && fx);
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();   // typingFocus gate off
    const QPoint c = logo->rect().center();
    const QPoint logoGlobal = logo->mapToGlobal(c);
    const QPoint awayGlobal = win.mapToGlobal(QPoint(win.width() / 2, win.height() - 40));
    const auto enter = [](QWidget* w, const QPoint& global) {
      const QPointF local(w->mapFromGlobal(global));
      QEnterEvent e(local, local, QPointF(global));
      QApplication::sendEvent(w, &e);
    };
    const auto leave = [](QWidget* w) {
      QEvent e(QEvent::Leave);
      QApplication::sendEvent(w, &e);
    };
    QCursor::setPos(logoGlobal);
    enter(logo, logoGlobal);
    QVERIFY(fx->property("fxActive").toBool());

    bool opened = false, heldOnCrossing = false, heldOnBox = false;
    bool stoppedOffBoth = false, startedOnBox = false;
    QTimer::singleShot(600, &win, [&] {   // past the popover's open flight
      QWidget* box = win.pop.overlay.data();
      opened = box && box->isVisible() && win.pop.active &&
               win.pop.active->objectName() == QLatin1String("accentPopover");
      if (!opened) { if (win.pop.active) win.pop.active->reject(); return; }
      // The cursor crosses the anchor gap onto the box: the logo's Leave alone must not
      // stop the loop (the browser's hover bridge), and resting on the box holds it.
      const QPoint boxGlobal = box->mapToGlobal(box->rect().center());
      QCursor::setPos(boxGlobal);
      leave(logo);
      QTest::qWait(60);   // inside the grace
      heldOnCrossing = fx->property("fxActive").toBool();
      enter(box, boxGlobal);
      QTest::qWait(300);  // well past the grace
      heldOnBox = fx->property("fxActive").toBool() && fx->isVisible();
      // Off both (onto the canvas): the loop stops once the grace runs out.
      QCursor::setPos(awayGlobal);
      leave(box);
      QTest::qWait(300);
      stoppedOffBoth = !fx->property("fxActive").toBool();
      // A hover that BEGINS on the popover lights the logo too.
      QCursor::setPos(boxGlobal);
      enter(box, boxGlobal);
      startedOnBox = fx->property("fxActive").toBool();
      QCursor::setPos(awayGlobal);
      leave(box);
      win.pop.active->reject();
    });
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logoGlobal);
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's loop until the timer acts
    QTRY_VERIFY(!win.pop.active);
    QVERIFY2(opened, "right-click did not open the accent popover");
    QVERIFY2(heldOnCrossing, "leaving the logo for the open popover stopped the shine");
    QVERIFY2(heldOnBox, "hovering the open popover did not hold the shine");
    QVERIFY2(stoppedOffBoth, "the shine kept running with the cursor off logo and popover");
    QVERIFY2(startedOnBox, "hovering the popover did not start the shine");
    // The popover has gone and the cursor is on neither: the loop is down and idle.
    QTRY_VERIFY(!fx->property("fxActive").toBool());
    for (QVariantAnimation* a : fx->findChildren<QVariantAnimation*>())
      QVERIFY(a->state() != QAbstractAnimation::Running);
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.theme.gui.moc"
