// MainWindow GUI e2e — Selection: delete in the lists, dragging a whole line, alt-delete and hover cross.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A bare Delete/Backspace inside the selection panel's lists removes the current row, scoped
  // by widget focus, so the global Alt+Delete on the canvas selection is untouched.
  void deleteKeyRemovesRowInSelectionLists() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    // Commit two lines through the real drawing path.
    QAction* start = actionByText(&win, "Start Drawing");
    QAction* newLine = actionByText(&win, "New Line");
    QVERIFY(start && newLine);
    const int W = canvas->width(), H = canvas->height();
    const QList<QList<QPoint>> shapes{
        {QPoint(W * 0.25, H * 0.25), QPoint(W * 0.45, H * 0.35)},
        {QPoint(W * 0.55, H * 0.55), QPoint(W * 0.75, H * 0.65), QPoint(W * 0.65, H * 0.8)},
    };
    for (const QList<QPoint>& shape : shapes) {
      start->trigger();
      for (const QPoint& p : shape) { QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, p); beat(); }
      newLine->trigger();
    }
    QCOMPARE(static_cast<int>(canvas->getLines().size()), 2);

    // --- Lines tab: Delete on the current row removes that line ---
    auto* linesList = win.findChild<QTableWidget*>("linesList");
    QVERIFY(linesList);
    QTRY_COMPARE(linesList->rowCount(), 2);
    QVERIFY2(linesList->focusPolicy() != Qt::NoFocus, "the list must accept focus for a scoped Delete");
    linesList->setCurrentCell(0, 0);
    QTest::keyClick(linesList, Qt::Key_Delete);
    QTRY_COMPARE(static_cast<int>(canvas->getLines().size()), 1);
    // The current row survives the repopulate, so a second press deletes again.
    QCOMPARE(linesList->currentRow(), 0);
    QTest::keyClick(linesList, Qt::Key_Backspace);
    QTRY_COMPARE(static_cast<int>(canvas->getLines().size()), 0);

    // --- Points table: same key, unchanged behaviour ---
    start->trigger();
    for (const QPoint& p : { QPoint(W * 0.3, H * 0.3), QPoint(W * 0.5, H * 0.4), QPoint(W * 0.4, H * 0.6) }) {
      QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, p);
      beat();
    }
    QCOMPARE(totalPoints(canvas), 3);
    auto* points = win.findChild<QTableWidget*>("pointsTable");
    QVERIFY(points);
    QTRY_COMPARE(points->rowCount(), 3);
    points->setCurrentCell(1, 0);
    QTest::keyClick(points, Qt::Key_Delete);
    QTRY_COMPARE(totalPoints(canvas), 2);
    beat();
  }

  // Alt+Shift dragging a line must move EVERY point — including when Shift lifts a beat before
  // the mouse button, which is how the gesture naturally ends.
  void wholeLineDragMovesEveryPoint() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    const double s = canvas->getScale();
    stencil::core::Line line;
    line.points = {{20, 20}, {60, 20}, {100, 40}, {140, 60}};
    canvas->setLines({line});
    const auto orig = canvas->getLines()[0].points;

    auto sendMouse = [&](QEvent::Type t, const QPointF& pos, Qt::MouseButton btn,
                         Qt::MouseButtons btns, Qt::KeyboardModifiers mods) {
      QMouseEvent ev(t, pos, canvas->mapToGlobal(pos.toPoint()), btn, btns, mods);
      QCoreApplication::sendEvent(canvas, &ev);
    };

    // Grab the FIRST segment's midpoint (widget space = image * scale), drag by a known
    // delta with Shift ALREADY RELEASED on the move and the release, then let go.
    const QPointF grab(40 * s, 20 * s);
    const QPointF drop((40 + 30) * s, (20 + 25) * s);
    sendMouse(QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton,
              Qt::AltModifier | Qt::ShiftModifier);
    sendMouse(QEvent::MouseMove, drop, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, drop, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    beat();

    const auto& moved = canvas->getLines()[0].points;
    QCOMPARE(static_cast<int>(moved.size()), 4);
    const double px = std::max(0.5, 1.0 / s);   // the canvas reads whole widget pixels
    for (std::size_t i = 0; i < moved.size(); ++i) {
      QVERIFY2(std::abs(moved[i].x - (orig[i].x + 30)) < px &&
                   std::abs(moved[i].y - (orig[i].y + 25)) < px,
               qPrintable(QString("point %1 carries the full drag delta").arg(i)));
    }
  }

  // Alt+Delete routes by selection: a focused POINT narrows it to that point and the line
  // survives; with no focused point it deletes the selected line, as the hotkeys label says.
  void altDeleteRoutesBySelection() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    stencil::core::Line line;
    line.points = {{30, 30}, {80, 30}, {80, 80}};
    canvas->setLines({line});

    QAction* del = actionByText(&win, "Delete Selected Line (Point if focused)");
    QVERIFY2(del, "the routed delete action is discoverable under its new label");

    // Click-select a POINT → the chord deletes just that point.
    canvas->selectLineAt(30, 30);
    QCOMPARE(canvas->getSelectedPoint(), 0);
    del->trigger();
    QTRY_COMPARE(static_cast<int>(canvas->getLines().size()), 1);
    QCOMPARE(static_cast<int>(canvas->getLines()[0].points.size()), 2);

    // Select the LINE via a segment (no focused point) → the chord deletes the line.
    canvas->selectLineAt(80, 55);
    QVERIFY(canvas->getSelectedLineIdx() == 0 && canvas->getSelectedPoint() == -1);
    del->trigger();
    QTRY_COMPARE(static_cast<int>(canvas->getLines().size()), 0);
  }

  // Hover cross-highlight plumbing: moving over a point emits canvasHoverChanged (panel rows
  // tint), and the panel-driven setListHover* calls drive the reverse direction.
  void hoverCrossHighlightSignals() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    const double s = canvas->getScale();
    stencil::core::Line line;
    line.points = {{40, 40}, {90, 40}};
    canvas->setLines({line});

    QSignalSpy spy(canvas, &CanvasWidget::canvasHoverChanged);
    QMouseEvent over(QEvent::MouseMove, QPointF(40 * s, 40 * s),
                     canvas->mapToGlobal(QPoint(40 * s, 40 * s)), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &over);
    QTRY_VERIFY(spy.count() >= 1);
    const QList<QVariant> args = spy.last();
    QCOMPARE(args.at(0).toInt(), 0);   // line 0
    QCOMPARE(args.at(1).toInt(), 0);   // point 0
    QCOMPARE(args.at(2).toInt(), 0);   // over line 0 (tints the Lines-list row)

    // Leaving the canvas clears the hover (all -1) so panel tints drop too.
    spy.clear();
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(canvas, &leave);
    QTRY_VERIFY(spy.count() >= 1);
    QCOMPARE(spy.last().at(0).toInt(), -1);

    // Reverse direction: the panel rows drive the canvas ring/glow without incident.
    canvas->setListHoverLine(0);
    canvas->setListHoverPoint(1);
    canvas->setListHoverLine(-1);
    canvas->setListHoverPoint(-1);
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.canvasSelect.gui.moc"
