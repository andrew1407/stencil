// MainWindow GUI e2e — The compact popover adopting the layout it is dragged into, and the drag zones.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Browser parity (ui/chatDock.js): compact, the dock sits beside its icon but its title bar
  // still DRAGS — and the drag adopts the layout, so what moves is a float the user chose,
  // never a popover still pinned to an icon. Only the bar's double-click toggle stays dead:
  // the browser's header has none.
  void chatCompactPopoverDragsAndAdoptsTheLayout() {
    MainWindow win;
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.chatDock_;   // the concrete dock: the drag state is ChatDock's own
    QVERIFY(dock);
    win.openChatCompact(&win);   // any anchor: the popover only needs a rect to sit beside
    QTRY_VERIFY(win.chatCompactShowing());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);
    settleLayout(&win, 60);

    const QRect before = dock->geometry();
    const auto pressTitle = [&](QEvent::Type type) {
      QMouseEvent ev(type, QPointF(8, 8), QPointF(8, 8), QPointF(title->mapToGlobal(QPoint(8, 8))),
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(title, &ev);
    };
    // A press ARMS the drag; only a move past the start distance makes it live, so the
    // gesture has to be driven all the way through to prove anything either way.
    const auto moveTitle = [&](const QPoint& to) {
      QMouseEvent mv(QEvent::MouseMove, QPointF(to), QPointF(to), QPointF(title->mapToGlobal(to)),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(title, &mv);
    };
    const auto releaseTitle = [&] {
      QMouseEvent up(QEvent::MouseButtonRelease, QPointF(8, 8), QPointF(8, 8),
                     QPointF(title->mapToGlobal(QPoint(8, 8))), Qt::LeftButton,
                     Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(title, &up);
    };

    // The double-click toggle first, while the shape is still compact: it moves nothing.
    pressTitle(QEvent::MouseButtonDblClick);
    QTest::qWait(40);
    QCOMPARE(dock->geometry(), before);
    QVERIFY2(win.chatCompactShowing(), "a dblclick on the bar neither floats nor docks it");

    // The drag: it goes live from the compact shape, and starting it adopts the layout.
    pressTitle(QEvent::MouseButtonPress);
    moveTitle(QPoint(240, 180));
    QTest::qWait(40);
    QVERIFY2(dock->dragActive(), "the compact title bar drags, like the browser's header");
    QVERIFY2(!win.chatCompactShowing(),
             "and the drag adopts: what moves is a float the user chose, not a pinned popover");
    releaseTitle();
  }

  void chatDockDragZones() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);

    settleLayout(&win, 60);   // let the layout reclaim the floated dock's slot
    const QRect central(win.centralWidget()->mapTo(&win, QPoint(0, 0)),
                        win.centralWidget()->size());
    // Offscreen has no movable cursor / synthetic global button state, so the
    // poll reads STUBBED probes — exactly the delivery-free situation of a
    // native macOS drag, where moves/releases never reach the widget.
    QPoint stubCursor = win.mapToGlobal(central.topLeft());  // ≠ the first drag target
    bool stubDown = false;
    win.chatDock_->setDragProbesForTest([&stubCursor] { return stubCursor; },
                                        [&stubDown] { return stubDown; });
    const auto dragTo = [&](const QPoint& globalPos) {
      stubDown = true;
      // Synthesized press on the title bar starts the poll (delivery of the
      // PRESS is all the real flow needs — moves/releases are polled).
      QMouseEvent press(QEvent::MouseButtonPress, QPointF(8, 8), QPointF(8, 8),
                        QPointF(title->mapToGlobal(QPoint(8, 8))), Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(title, &press);
      stubCursor = globalPos;  // observed by the poll loop
    };
    const auto releaseAt = [&](const QPoint& globalPos) {
      stubCursor = globalPos;
      QTest::qWait(40);  // a couple of poll ticks at the drop spot
      stubDown = false;  // "button up" → the poll finishes at stubCursor
      QTest::qWait(40);
    };

    // The overlay appears for the whole drag and spans the DOCK REGION: full
    // window width, below the toolbars, above the status bar — NOT the central
    // widget (which shrinks by whatever is docked, drifting the bands inward).
    dragTo(win.mapToGlobal(central.center()));
    QTRY_VERIFY(win.findChild<QWidget*>("chatDockZones"));
    auto* zones = win.findChild<QWidget*>("chatDockZones");
    QVERIFY(zones);
    QTRY_VERIFY(zones->isVisible());
    const QRect zr = zones->geometry();
    QCOMPARE(zr.left(), 0);
    QCOMPARE(zr.width(), win.width());
    QVERIFY2(zr.top() > 0 && zr.top() <= central.top(), "starts below the toolbars");
    QVERIFY2(zr.bottom() >= central.bottom(), "reaches past the central area's bottom");
    // Native docking is locked out for the whole drag: the zones are the ONLY
    // docking mechanism (Qt can't show its placeholder or hover-dock).
    QCOMPARE(dock->allowedAreas(), Qt::NoDockWidgetArea);

    // LEFT band → docks left; areas restored on release.
    releaseAt(win.mapToGlobal(QPoint(zr.left() + 30, zr.center().y())));
    QTRY_VERIFY(!zones->isVisible());
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);

    // Tear-off-from-DOCKED: the drag starts docked, the poll forces the float
    // past the drag threshold (native docking suppressed throughout), the
    // zones appear, and the RIGHT release band decides.
    QVERIFY(!dock->isFloating());
    dragTo(win.mapToGlobal(central.center()));  // press on the DOCKED title
    QTRY_VERIFY(dock->isFloating());            // forced into the zone flow
    QTRY_VERIFY(zones->isVisible());
    QCOMPARE(dock->allowedAreas(), Qt::NoDockWidgetArea);
    releaseAt(win.mapToGlobal(
        QPoint(zr.right() - 30, zr.center().y())));
    QTRY_VERIFY(!zones->isVisible());
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);

    // BOTTOM band → docks bottom.
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    dragTo(win.mapToGlobal(central.center()));
    QTRY_VERIFY(zones->isVisible());
    releaseAt(win.mapToGlobal(
        QPoint(zr.center().x(), zr.bottom() - 30)));
    QTRY_VERIFY(!zones->isVisible());
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::BottomDockWidgetArea);

    // Mid-area release → stays floating; the overlay is gone either way.
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    dragTo(win.mapToGlobal(central.center() + QPoint(40, 0)));
    QTRY_VERIFY(zones->isVisible());
    releaseAt(win.mapToGlobal(central.center()));
    QTRY_VERIFY(!zones->isVisible());
    QVERIFY(dock->isFloating());

    // Restore the default placement for later slots.
    win.addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->setFloating(false);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatDockDrag.gui.moc"
