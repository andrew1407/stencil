// MainWindow GUI e2e — Dragging the canvas and zoom icons (app/drag): clear-all dropped on the
// canvas clears at once as one undo step, rotate and flip apply there as their click does, and
// nothing happens elsewhere; zoom −/+ follow the pointer's distance and fit steps over them, each
// restoring the starting zoom when released back on itself. Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "iconDragGui.hpp"
#include "iconDrag.hpp"
#include "zoomFollow.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static QPoint canvasMid(const MainWindow& win) { return win.scroll->mapToGlobal(win.scroll->rect().center()); }
  // On the header row's empty end: off the canvas and off every icon.
  static QPoint offCanvas(const MainWindow& win) {
    return win.tools.headerToolbar->mapToGlobal(QPoint(win.tools.headerToolbar->width() - 30, 12));
  }
  static int glows(const MainWindow& win) { return dropGlows(win, stencil::support::DROP_GLOW_NAME); }
  static bool near(double a, double b) { return std::abs(a - b) <= 0.01 * b; }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void clearAllDroppedOnTheCanvasClearsWithoutAsking() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(openLoaded(win) && win.canvas->hasImage());
    stencil::core::Line a, b;
    a.points = {{20, 20}, {80, 60}};
    b.points = {{40, 90}, {120, 30}};
    win.canvas->commitLines({a, b});
    settleLayout(&win, 150);
    QWidget* icon = win.buttonForAction(win.acts.clearAll);
    QVERIFY(icon && icon->isEnabled());
    ModalSeen asked;
    catchModal(asked, 1500);
    dragIcon(icon, offCanvas(win), offCanvas(win));
    QCOMPARE(int(win.canvas->getLines().size()), 2);
    liftIcon(icon);
    QCOMPARE(glows(win), 1);
    dropIcon(icon, canvasMid(win));
    QTRY_VERIFY(win.canvas->getLines().empty());
    QVERIFY2(!asked.seen, "the drop asked for a confirmation");
    QCOMPARE(glows(win), 0);
    QLabel* toast = win.notify->toasts()->lastToast();
    QVERIFY2(toast && toast->text().contains(QStringLiteral("All lines cleared")), "…and said it, as a confirmed clear does");
    win.acts.undo->trigger();
    QCOMPARE(int(win.canvas->getLines().size()), 2);   // one undo step brings both back
  }

  void rotateAndFlipApplyOnlyOnTheCanvas() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(openLoaded(win) && win.canvas->hasImage());
    settleLayout(&win, 150);
    const int q0 = win.canvas->getRotationQuarters();
    dragIcon(win.buttonForAction(win.acts.rotateRight), offCanvas(win), offCanvas(win));
    QCOMPARE(win.canvas->getRotationQuarters(), q0);
    dragIcon(win.buttonForAction(win.acts.rotateRight), canvasMid(win), canvasMid(win));
    QTRY_COMPARE(win.canvas->getRotationQuarters(), (q0 + 1) % 4);
    dragIcon(win.buttonForAction(win.acts.rotateLeft), canvasMid(win), canvasMid(win));
    QTRY_COMPARE(win.canvas->getRotationQuarters(), q0);
    const bool mirrored = win.canvas->getMirrored();
    QWidget* flip = win.buttonForAction(win.acts.flipImage);
    dragIcon(flip, canvasMid(win), iconCentre(flip));
    QCOMPARE(win.canvas->getMirrored(), mirrored);   // released back on its icon
    dragIcon(flip, canvasMid(win), canvasMid(win));
    QTRY_COMPARE(win.canvas->getMirrored(), !mirrored);
    QVERIFY(win.canvas->canUndo());
  }

  void zoomIconsFollowThePointerAndComeBackOnTheIcon() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(openLoaded(win) && win.canvas->hasImage());
    settleLayout(&win, 150);
    win.setZoom(1.0);
    QWidget* in = win.buttonForAction(win.acts.zoomIn);
    QWidget* out = win.buttonForAction(win.acts.zoomOut);
    const QPoint from = in->mapToGlobal(QRectF(in->rect()).center().toPoint());
    liftIcon(in);
    iconMouse(in, QEvent::MouseMove, from + QPoint(0, 150));
    const double far = win.canvas->getScale();
    QVERIFY2(near(far, std::exp(stencil::gui::ZOOM_DRAG_K * 150)), "150 px out, + zooms by e^(k·150)");
    QCOMPARE(win.zoom->currentText(), QString::number(qRound(far * 100)) + "%");
    iconMouse(in, QEvent::MouseMove, from + QPoint(0, 60));
    QVERIFY2(win.canvas->getScale() < far, "moving back toward + brings the zoom back down");
    dropIcon(in, from + QPoint(0, 60));
    const double kept = win.canvas->getScale();
    QVERIFY(near(kept, std::exp(stencil::gui::ZOOM_DRAG_K * 60)));
    dragIcon(out, iconCentre(out) + QPoint(260, 120), iconCentre(out));
    QCOMPARE(win.canvas->getScale(), kept);   // released on −: the zoom from before the press
    dragIcon(out, iconCentre(out) + QPoint(0, 300), iconCentre(out) + QPoint(0, 300));
    QVERIFY2(near(win.canvas->getScale(), kept / 8), "300 px out, − zooms ÷8");
    const double before = win.canvas->getScale();
    QTest::mouseClick(in, Qt::LeftButton);
    QVERIFY2(near(win.canvas->getScale(), before * 1.25), "a plain click on + still steps the zoom");
  }

  void theFitDragStepsOnMinusAndPlusAndRestoresOnItself() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(openLoaded(win) && win.canvas->hasImage());
    settleLayout(&win, 150);
    win.setZoom(1.0);
    QWidget* fit = win.tools.zoomFitBtn;
    QWidget* in = win.buttonForAction(win.acts.zoomIn);
    QWidget* out = win.buttonForAction(win.acts.zoomOut);
    liftIcon(fit);
    QCOMPARE(glows(win), 2);   // − and + start shining
    iconMouse(fit, QEvent::MouseMove, iconCentre(in));
    QTest::qWait(stencil::gui::HOLD_ZOOM_TICK_MS * 3 + 60);
    const double stepped = win.canvas->getScale();
    QVERIFY2(stepped >= 1.0 + 2 * stencil::gui::HOLD_ZOOM_STEP - 1e-9, "resting on + steps the zoom in");
    iconMouse(fit, QEvent::MouseMove, iconCentre(out));
    QTest::qWait(stencil::gui::HOLD_ZOOM_TICK_MS * 2 + 60);
    QVERIFY2(win.canvas->getScale() < stepped, "resting on − steps it out");
    dropIcon(fit, iconCentre(fit));
    QCOMPARE(win.canvas->getScale(), 1.0);   // released on fit: the zoom from the drag's start
    QCOMPARE(glows(win), 0);
    liftIcon(fit);
    iconMouse(fit, QEvent::MouseMove, iconCentre(in));
    QTest::qWait(stencil::gui::HOLD_ZOOM_TICK_MS * 2 + 60);
    dropIcon(fit, canvasMid(win));
    const double kept = win.canvas->getScale();
    QTest::qWait(stencil::gui::HOLD_ZOOM_TICK_MS * 2);
    QVERIFY2(kept > 1.0 && win.canvas->getScale() == kept, "released elsewhere, the zoom stays and stops");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarDragsCanvas.gui.moc"
