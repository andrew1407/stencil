// MainWindow GUI e2e — A tooltip fading in and out, a fast sweep stranding none, and the gates that suppress it.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowTip.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Tooltips FADE in and out (browser #app-tooltip: 90 ms) instead of snapping. Qt's own
  // QTipLabel cannot be animated, so QEvent::ToolTip is taken over — and the wake-up delay,
  // the content and the placement all have to survive that swap.
  void tooltipFadesInAndOut() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    win.raise();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* btn = tipCarrier(win);
    QVERIFY2(btn, "no shown toolbar button with a tooltip");
    stencil::gui::AppTooltip* tip = stencil::gui::appTooltip();
    QVERIFY2(tip, "the app tooltip was never installed");
    QVERIFY(!tip->isVisible());

    // Park the pointer ON the control: the panel's anti-stranding heartbeat retires a
    // tooltip whose control the pointer has left, and here it must not.
    QCursor::setPos(btn->mapToGlobal(btn->rect().center()));
    sendToolTipTo(btn);
    QVERIFY2(tip->isVisible(), "the tooltip did not take over QEvent::ToolTip");
    QCOMPARE(tip->owner(), static_cast<QWidget*>(btn));
    QVERIFY2(tip->windowOpacity() < 0.99, "it snapped in at full opacity");
    QTRY_COMPARE_WITH_TIMEOUT(tip->windowOpacity(), 1.0, 1500);   // …and rose to solid
    QTest::qWait(300);
    QVERIFY2(tip->isVisible(), "it retired while the pointer was still on its control");
    // Placed off the cursor and kept on screen.
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    QVERIFY2(screen.intersects(tip->geometry()), "the tooltip was placed off screen");
    QLabel* body = tip->findChild<QLabel*>();
    QVERIFY(body && !body->text().isEmpty());

    // Leaving the control fades it OUT — still visible while it goes, gone at the end.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(btn, &leave);
    QVERIFY2(tip->fadingOut(), "it vanished instead of fading");
    QTRY_VERIFY_WITH_TIMEOUT(!tip->isVisible(), 1500);

    // Reduced motion: shown solid at once, hidden at once — the same end states.
    qputenv("STENCIL_NO_ANIM", "1");
    sendToolTipTo(btn);
    QVERIFY(tip->isVisible());
    QCOMPARE(tip->windowOpacity(), 1.0);
    QApplication::sendEvent(btn, &leave);
    QVERIFY2(!tip->isVisible(), "reduced motion still played the fade-out");
    qunsetenv("STENCIL_NO_ANIM");
    beat();
  }

  // A fast pointer sweep must never STRAND a tooltip: whatever happened to the control it
  // described — hidden, disabled, or simply left behind without a Leave we saw — the panel
  // goes on its own.
  void fastPointerSweepStrandsNoTooltip() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    win.raise();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* btn = tipCarrier(win);
    QVERIFY(btn);
    stencil::gui::AppTooltip* tip = stencil::gui::appTooltip();
    QVERIFY(tip);

    // Shown for a control the pointer is nowhere near — the sweep already moved on, and no
    // Leave was ever delivered for it.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
    sendToolTipTo(btn);
    QVERIFY(tip->isVisible());
    // The cursor is nowhere near it (offscreen QPA parks it at the origin, and no Leave is
    // synthesised) — the heartbeat is the only thing that can clear this.
    QTRY_VERIFY_WITH_TIMEOUT(!tip->isVisible(), 3000);
    QVERIFY(!tip->owner());
    beat();
  }

  // The tooltip must not show — or stay stuck showing — while the mouse is down doing
  // something else (Alt-dragging a point, drag-creating a rect/zoom box, panning);
  // hoverLeft() retracts one already up when the drag starts.
  void noTooltipWhileTheMouseIsDownDrawingOrDragging() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    settleLayout(&win, 150);
    win.settings_.tooltipEnabled = true;    // independent of the machine's saved settings
    win.settings_.tooltipShowScreen = true;

    stencil::core::Line line;
    line.points = {{40, 40}, {160, 120}};
    canvas->setLines({line});
    const double s = canvas->scale();

    auto sendMouse = [&](QEvent::Type t, const QPointF& pos, Qt::MouseButton btn,
                         Qt::MouseButtons btns, Qt::KeyboardModifiers mods) {
      QMouseEvent ev(t, pos, canvas->mapToGlobal(pos.toPoint()), btn, btns, mods);
      QCoreApplication::sendEvent(canvas, &ev);
    };
    const auto moveTo = [&](double ix, double iy) {
      sendMouse(QEvent::MouseMove, QPointF(ix * s, iy * s), Qt::NoButton, Qt::NoButton,
               Qt::NoModifier);
    };

    // Baseline: hovering the first point (no modifiers, nothing else going on) shows the
    // tooltip after the reveal delay — proves the setup actually can show one at all.
    moveTo(40, 40);
    QTRY_VERIFY_WITH_TIMEOUT(win.tooltip_->isVisible(), 1000);

    // Alt-press ON that same point starts a point drag without moving first — the
    // stranding case: the very next move must retract the tooltip that was already up,
    // not just skip showing a new one. (The drag itself relocates the point to wherever
    // it's released — setLines() below puts it back for the next section.)
    sendMouse(QEvent::MouseButtonPress, QPointF(40 * s, 40 * s), Qt::LeftButton,
             Qt::LeftButton, Qt::AltModifier);
    sendMouse(QEvent::MouseMove, QPointF(70 * s, 60 * s), Qt::NoButton, Qt::LeftButton,
             Qt::AltModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!win.tooltip_->isVisible(), 1000);
    // Dragging further — even back over the SECOND point — never re-shows it either.
    sendMouse(QEvent::MouseMove, QPointF(160 * s, 120 * s), Qt::NoButton, Qt::LeftButton,
             Qt::AltModifier);
    QTest::qWait(260);   // outwait the reveal delay — it must still be hidden
    QVERIFY2(!win.tooltip_->isVisible(), "a point drag popped a tooltip mid-drag");
    sendMouse(QEvent::MouseButtonRelease, QPointF(160 * s, 120 * s), Qt::LeftButton,
             Qt::NoButton, Qt::AltModifier);
    beat();

    // Rect-draw mode, dragging out a box over the first point: no tooltip either.
    canvas->setLines({line});   // undo the point drag above — point 1 back at (40, 40)
    canvas->setDrawMode(CanvasWidget::DrawMode::RECT);
    moveTo(40, 40);
    QTRY_VERIFY_WITH_TIMEOUT(win.tooltip_->isVisible(), 1000);
    sendMouse(QEvent::MouseButtonPress, QPointF(40 * s, 40 * s), Qt::LeftButton,
             Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, QPointF(90 * s, 90 * s), Qt::NoButton, Qt::LeftButton,
             Qt::NoModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!win.tooltip_->isVisible(), 1000);
    QTest::qWait(260);
    QVERIFY2(!win.tooltip_->isVisible(), "a rect-draw drag popped a tooltip mid-drag");
    sendMouse(QEvent::MouseButtonRelease, QPointF(90 * s, 90 * s), Qt::LeftButton,
             Qt::NoButton, Qt::NoModifier);
    canvas->setDrawMode(CanvasWidget::DrawMode::LINE);
    beat();

    // Shift-drag (zoom rect) over a point: still nothing.
    canvas->setLines({line});   // drop the rect-draw commit above, back to the plain line
    moveTo(40, 40);
    QTRY_VERIFY_WITH_TIMEOUT(win.tooltip_->isVisible(), 1000);
    sendMouse(QEvent::MouseButtonPress, QPointF(40 * s, 40 * s), Qt::LeftButton,
             Qt::LeftButton, Qt::ShiftModifier);
    // Kept under the 4-image-px commit threshold (mouseReleaseEvent) so releasing does
    // NOT actually zoom — this section only cares about the tooltip during the drag.
    sendMouse(QEvent::MouseMove, QPointF(42 * s, 41 * s), Qt::NoButton, Qt::LeftButton,
             Qt::ShiftModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!win.tooltip_->isVisible(), 1000);
    QTest::qWait(260);
    QVERIFY2(!win.tooltip_->isVisible(), "a zoom-rect drag popped a tooltip mid-drag");
    sendMouse(QEvent::MouseButtonRelease, QPointF(42 * s, 41 * s), Qt::LeftButton,
             Qt::NoButton, Qt::ShiftModifier);
    beat();

    // Back to a plain hover afterwards: the tooltip is not stuck off either.
    moveTo(40, 40);
    QTRY_VERIFY_WITH_TIMEOUT(win.tooltip_->isVisible(), 1000);
    beat();
  }

  // The idle canvas card says "＋ Blank image" on its face; a hover tooltip repeating that
  // is noise, so neither surface carries one any more.
  void blankImageCardHasNoTooltip() {
    MainWindow win;
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.canvas_->clearImage();
    win.refreshActions();
    // Painting the card is what used to install the tooltip.
    QVERIFY(waitForIdleCard(win));
    QVERIFY2(win.canvas_->toolTip().isEmpty(),
             qPrintable("the empty canvas still has a tooltip: " + win.canvas_->toolTip()));
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.tooltipsFade.gui.moc"
