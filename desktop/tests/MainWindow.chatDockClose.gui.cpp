// MainWindow GUI e2e — The close button's flight from every area, the mouse drag, and the placement state.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The title-bar X leaves the SAME way the toolbar toggle does — a docked chat slides into its
  // edge, a float flies into the icon — from all four areas plus floating, transcript kept.
  void chatCloseButtonAnimatesFromEveryDockArea() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QToolButton* closeBtn = dock->findChild<QToolButton*>();
    // The X is the last ghost in the title bar; find it by its tooltip.
    closeBtn = nullptr;
    for (QToolButton* b : dock->findChildren<QToolButton*>())
      if (b->toolTip() == QLatin1String("Close assistant")) closeBtn = b;
    QVERIFY2(closeBtn, "no X in the chat title bar");
    win.chatDock_->appendUser(QStringLiteral("kept across the close"));

    // The float's exit is a cloud of its own pixels flown inside the main window, every
    // mote pouring back into the icon.
    const auto flight = [&win] { return surfaceFlight(&win); };
    const auto settle = [&win] { awaitAnim(win.chatAnim_); awaitFlights(&win); };

    struct Area { Qt::DockWidgetArea area; const char* name; };
    const QVector<Area> areas{{Qt::LeftDockWidgetArea, "left"},
                              {Qt::RightDockWidgetArea, "right"},
                              {Qt::TopDockWidgetArea, "top"},
                              {Qt::BottomDockWidgetArea, "bottom"}};
    for (const Area& a : areas) {
      win.addDockWidget(a.area, dock);
      dock->setFloating(false);
      win.actChat_->setChecked(true);
      win.setChatShown(true, false);
      QTRY_VERIFY(dock->isVisible());
      settle();
      QCOMPARE(win.dockWidgetArea(dock), a.area);

      closeBtn->click();
      // Mid-slide: the extent animation is running and it has NOT blinked out.
      QVERIFY2(win.chatAnim_ != nullptr,
               qPrintable(QString("%1: the X closed with no animation").arg(a.name)));
      QVERIFY2(dock->isVisible(),
               qPrintable(QString("%1: the dock vanished before the slide").arg(a.name)));
      // …the slide runs toward that edge: width for left/right, height for top/bottom.
      const bool horiz = a.area == Qt::LeftDockWidgetArea || a.area == Qt::RightDockWidgetArea;
      const auto extent = [&] { return horiz ? dock->width() : dock->height(); };
      const int before = extent();
      // Caught while it is STILL on screen: a blink-out leaves nothing to shrink.
      QTRY_VERIFY2_WITH_TIMEOUT(dock->isVisible() && extent() < before,
                                qPrintable(QString("%1: the %2 never shrank from %3")
                                               .arg(a.name, horiz ? "width" : "height")
                                               .arg(before)), 1500);
      QTRY_VERIFY2_WITH_TIMEOUT(!dock->isVisible(),
                                qPrintable(QString("%1: it never finished closing").arg(a.name)), 3000);
      QVERIFY2(!win.actChat_->isChecked(),
               qPrintable(QString("%1: the toolbar toggle stayed lit").arg(a.name)));
      settle();

      // …and it reopens cleanly, transcript intact.
      win.actChat_->setChecked(true);
      QTRY_VERIFY2(dock->isVisible(), qPrintable(QString("%1: it would not reopen").arg(a.name)));
      settle();
      bool kept = false;
      for (QLabel* l : dock->findChildren<QLabel*>())
        if (l->property("chatBody").toString() == QLatin1String("kept across the close")) kept = true;
      QVERIFY2(kept, qPrintable(QString("%1: the close lost the transcript").arg(a.name)));
    }

    // Floating: the X flies the window into the icon.
    dock->setFloating(true);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(dock->isVisible() && dock->isFloating());
    settle();
    const QRect windowBox(dock->mapToGlobal(QPoint(0, 0)), dock->size());
    closeBtn->click();
    stencil::gui::DisintegrateOverlay* from = nullptr;
    QTRY_VERIFY2((from = flight()) != nullptr, "floating: the X closed with no flight");
    QWidget* icon = win.buttonForAction(win.actChat_);
    QVERIFY(icon);
    QVERIFY2(!from->gathering(), "floating: the X must scatter the window INTO the icon");
    QCOMPARE(from->surfaceTarget(), flightPointOf(icon, &win));
    QTRY_VERIFY2(!dock->isVisible(), "floating: it never finished closing");
    settle();
    win.actChat_->setChecked(true);
    QTRY_VERIFY2(dock->isVisible(), "floating: it would not reopen");
    Q_UNUSED(windowBox);
    beat();
  }

  // The REAL input path (no probes): the floating dock's title bar consumes press/move/release
  // itself, so a drag works where the window server never delivers the release (macOS).
  void chatDockDragViaMouseEvents() {
    MainWindow win;
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);
    settleLayout(&win, 60);

    const QRect central(win.centralWidget()->mapTo(&win, QPoint(0, 0)),
                        win.centralWidget()->size());
    const auto sendMouse = [&](QEvent::Type t, const QPoint& global) {
      QMouseEvent e(t, title->mapFromGlobal(global), QPointF(global),
                    t == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                    t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                    Qt::NoModifier);
      QApplication::sendEvent(title, &e);
    };
    const QPoint start = title->mapToGlobal(QPoint(30, 8));
    sendMouse(QEvent::MouseButtonPress, start);
    sendMouse(QEvent::MouseMove, start + QPoint(40, 40));   // past the threshold
    QTRY_VERIFY2(win.findChild<QWidget*>("chatDockZones"), "a real drag never showed the zones");
    QWidget* zones = win.findChild<QWidget*>("chatDockZones");
    QVERIFY2(zones && zones->isVisible(), "zones show for a real (event-driven) drag");

    // Release inside the RIGHT band → docked right, zones gone.
    const QRect zr = zones->geometry();   // bands span the dock region, not `central`
    const QPoint rightBand = win.mapToGlobal(QPoint(zr.right() - 20, zr.center().y()));
    sendMouse(QEvent::MouseMove, rightBand);
    sendMouse(QEvent::MouseButtonRelease, rightBand);
    QTRY_VERIFY(!dock->isFloating());
    QCOMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QTRY_VERIFY(!zones->isVisible());
  }

  // The placement button matching the current state is accent-marked and inert.
  void chatDockPlacementState() {
    MainWindow win;
    win.resize(1000, 720);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);
    const QList<QToolButton*> btns = title->findChildren<QToolButton*>();
    QVERIFY(btns.size() >= 6);   // 4 placements + float + close
    // Docked LEFT by default: exactly one placement button wears the active chip, marked by its
    // stylesheet, not by being disabled (QToolButton:disabled paints a dead bordered square).
    const auto activeCount = [&] {
      int n = 0;
      for (QToolButton* b : btns)
        if (b->styleSheet().contains("background:")) ++n;
      return n;
    };
    QTRY_COMPARE(activeCount(), 1);
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QTRY_COMPARE(activeCount(), 1);   // now it's the float button
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatDockClose.gui.moc"
