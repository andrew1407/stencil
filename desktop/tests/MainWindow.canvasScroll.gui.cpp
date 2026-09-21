// MainWindow GUI e2e — The canvas scrollbars staying hidden until a pan or a zoom asks for them.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Canvas scrollbars are invisible at rest, revealed only by a pan or zoom — never by hover —
  // and fade out once the view settles; the opacity is plain state under offscreen QPA too.
  void canvasScrollbarsHideUntilPanOrZoom() {
    MainWindow win;
    win.resize(600, 500);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // TALL, not square: the last leg needs a zoom where the width fits and the height
    // still does not, and a square image left that to a viewport size the layout picks.
    QImage img(800, 4000, QImage::Format_RGB32);   // bigger than the viewport at 100%
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    win.refreshActions();
    QVERIFY(win.vScrollOpacity && win.hScrollOpacity);

    win.setZoom(1.0);
    QCOMPARE(win.vScrollOpacity->opacity(), 1.0);
    QCOMPARE(win.hScrollOpacity->opacity(), 1.0);
    // Overlay bars, browser-style: they float INSIDE the viewport (which spans the whole
    // area — no gutter reserved beside/below it) and are only there while there's overflow.
    QScrollBar* vbar = win.canvasScrollBar(Qt::Vertical);
    QScrollBar* hbar = win.canvasScrollBar(Qt::Horizontal);
    QVERIFY(vbar->isVisible() && hbar->isVisible());
    QVERIFY(win.scroll->viewport()->geometry().contains(vbar->geometry()));
    QVERIFY(win.scroll->viewport()->geometry().contains(hbar->geometry()));
    QCOMPARE(win.scroll->viewport()->geometry(), win.scroll->contentsRect());
    QVERIFY(!vbar->testAttribute(Qt::WA_TransparentForMouseEvents));
    // The thumb is a painted pill in the browser's thumb grey (support/PillScrollBars.hpp; QSS
    // cannot round a handle on macOS): its top-edge midpoint carries it, the slot corner not.
    {
      QStyleOptionSlider opt;
      opt.initFrom(vbar);
      opt.orientation = Qt::Vertical;
      opt.minimum = vbar->minimum(); opt.maximum = vbar->maximum();
      opt.sliderPosition = vbar->sliderPosition(); opt.sliderValue = vbar->value();
      opt.pageStep = vbar->pageStep(); opt.singleStep = vbar->singleStep();
      opt.upsideDown = vbar->invertedAppearance();
      const QRect slider = vbar->style()->subControlRect(QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarSlider, vbar);
      QVERIFY(slider.isValid());
      const QImage shot = vbar->grab().toImage();
      const qreal dpr = shot.devicePixelRatio();
      const QColor thumb = stencil::gui::canvasScrollThumb(stencil::gui::resolveDark(win.settings.themeMode));
      const auto near = [](const QColor& a, const QColor& b) {
        return qAbs(a.red() - b.red()) < 24 && qAbs(a.green() - b.green()) < 24 && qAbs(a.blue() - b.blue()) < 24;
      };
      const QColor mid = shot.pixelColor(QPoint(slider.center().x(), slider.top() + 1) * dpr);
      const QColor corner = shot.pixelColor(QPoint(slider.left(), slider.top()) * dpr);
      QVERIFY2(near(mid, thumb), qPrintable("the thumb's top-edge midpoint is not the thumb grey: " + mid.name()));
      QVERIFY2(!near(corner, thumb), "the thumb's corner is filled — the thumb is not rounded");
      // Thin at rest, a little thicker under the pointer (browser parity). FOUR px off the centre
      // line: the pill is centred on the slot's half-pixel centre, so centre−3 is antialiased.
      const QPoint side(slider.center().x() - 4, slider.top() + 6);
      const QColor slot = corner;
      QVERIFY2(near(shot.pixelColor(side * dpr), slot), "the resting thumb is already wide");
      QEnterEvent enter(QPointF(slider.center()), QPointF(vbar->mapTo(&win, slider.center())),
                        QPointF(vbar->mapToGlobal(slider.center())));
      QCoreApplication::sendEvent(vbar, &enter);
      const auto swollen = [&] { return !near(vbar->grab().toImage().pixelColor(side * dpr), slot); };
      settle(swollen, 300);   // the 150ms swell
      QVERIFY2(swollen(), "the thumb did not swell under the pointer");
      QEvent leave0(QEvent::Leave);
      QCoreApplication::sendEvent(vbar, &leave0);
      settle([&] { return !swollen(); }, 300);
      QVERIFY2(near(vbar->grab().toImage().pixelColor(side * dpr), slot), "the thumb did not settle back after the pointer left");
      win.scrollbarHovered = false;
      win.revealCanvasScrollbars();   // re-arm the reveal our synthetic Leave just cancelled
    }
    // Out on the idle timer's own timeout(), not on an opacity that may never have been
    // 1: a QTRY on the value alone goes green on a fade that never ran.
    const auto hidesOut = [&win] {
      QSignalSpy fired(win.scrollbarHideTimer, &QTimer::timeout);
      return win.scrollbarHideTimer->isActive() && fired.wait(1500)
             && win.vScrollOpacity->opacity() == 0.0 && win.hScrollOpacity->opacity() == 0.0;
    };
    QVERIFY(hidesOut());

    // A pan (here: the vertical scrollbar's own value, exactly what a drag-pan/wheel-scroll
    // drives — see MainWindow::scrollTo) reveals it again, and it fades back out the same way.
    win.scroll->verticalScrollBar()->setValue(50);
    QCOMPARE(win.vScrollOpacity->opacity(), 1.0);
    QVERIFY(hidesOut());

    // Hovering the bar itself (to grab it) must never let it fade out from under the cursor.
    QEvent enter(QEvent::Enter);
    QCoreApplication::sendEvent(win.canvasScrollBar(Qt::Vertical), &enter);
    QVERIFY(win.scrollbarHovered);
    QCOMPARE(win.vScrollOpacity->opacity(), 1.0);
    QTest::qWait(1200);   // would have hidden by now if hovering didn't suppress it
    QCOMPARE(win.vScrollOpacity->opacity(), 1.0);
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(win.canvasScrollBar(Qt::Vertical), &leave);
    QVERIFY(!win.scrollbarHovered);
    QVERIFY(hidesOut());
    // Dragging the floating bar drives the real scroll model, and vice versa.
    vbar->setValue(120);
    QCOMPARE(win.scroll->verticalScrollBar()->value(), 120);
    win.scroll->verticalScrollBar()->setValue(60);
    QCOMPARE(vbar->value(), 60);
    // Once an axis fits, its bar goes away entirely rather than lingering as a gutter, and the
    // survivor runs the viewport's full length. The zoom is read off the LIVE viewport.
    win.setZoom(double(win.scroll->viewport()->width() - 40) / 800.0);
    QTRY_VERIFY(!hbar->isVisible());
    QVERIFY(vbar->isVisible());
    QCOMPARE(vbar->height(), win.scroll->viewport()->height());
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.canvasScroll.gui.moc"
