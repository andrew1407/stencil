// MainWindow GUI e2e — Changing placement: the animation, a shared panel side, and a hidden panel still splitting.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Moving the chat between sides animates: it slides out of the old edge and back in at
  // the new one (the same extent slide the icon's open/close uses). It used to jump.
  void chatPlacementChangeAnimates() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible() && !dock->isFloating());
    awaitAnim(win.chatAnim_);
    const int settled = dock->width();
    QVERIFY2(settled > 80, "the dock never reached its full width");
    // Ask for the opposite side and watch the extent actually move mid-flight.
    emit static_cast<stencil::gui::ChatDock*>(dock)->dockRequested(Qt::RightDockWidgetArea);
    bool sawCollapse = false;
    for (int i = 0; i < 20 && !sawCollapse; ++i) {
      QTest::qWait(20);
      const int e = dock->isFloating() ? settled
                                       : (win.dockWidgetArea(dock) == Qt::TopDockWidgetArea
                                          || win.dockWidgetArea(dock) == Qt::BottomDockWidgetArea)
                                             ? dock->height() : dock->width();
      if (e < settled / 2) sawCollapse = true;
    }
    QVERIFY2(sawCollapse, "the dock jumped to the new side without sliding out");
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QTRY_VERIFY2(dock->width() > settled / 2, "it never grew back at the new edge");
    awaitAnim(win.chatAnim_);

    // …and coming back from FLOAT slides in at the side you picked, rather than
    // appearing at full width (the floating branch used to skip the animation).
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    awaitAnim(win.chatAnim_);
    emit static_cast<stencil::gui::ChatDock*>(dock)->dockRequested(Qt::LeftDockWidgetArea);
    bool sawNarrow = false;
    for (int i = 0; i < 20 && !sawNarrow; ++i) {
      QTest::qWait(15);
      if (!dock->isFloating() && dock->width() < settled / 2) sawNarrow = true;
    }
    QVERIFY2(sawNarrow, "docking from float snapped straight to full width");
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QTRY_VERIFY2(dock->width() > settled / 2, "it never grew in from the float");
  }

  // Browser parity (.main-content flex row): docked on the SAME side as the points panel, the
  // chat only ever eats into the canvas column; the fixed-width panel beside it never moves.
  void chatSharingPanelSideKeepsPanelWidthStable() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.selPanel_ && win.chatDock_);
    QVERIFY(win.dockWidgetArea(win.selPanel_) == Qt::RightDockWidgetArea);
    QTRY_VERIFY(!win.selPanel_->isHidden());

    // Chat starts docked LEFT by default — opening it in an UNRELATED area settles QMainWindow's
    // dock layout onto the panel's real natural width, not its incidental construction size.
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible() && !win.chatDock_->isFloating());
    awaitAnim(win.chatAnim_);
    const int panelBefore = win.selPanel_->width();
    QVERIFY2(panelBefore > 120, "the points panel never reached its natural width");

    // Now place the chat RIGHT, alongside the points panel, and watch the panel's
    // width through the whole flight.
    emit static_cast<stencil::gui::ChatDock*>(win.chatDock_)->dockRequested(Qt::RightDockWidgetArea);
    int maxSeen = 0, minSeen = win.width();
    QVERIFY2(win.chatAnim_, "the placement change did not animate");
    QElapsedTimer flightClock;
    flightClock.start();
    while (win.chatAnim_ && flightClock.elapsed() < 1500) {   // the flight's own length
      QTest::qWait(15);
      if (win.selPanel_->isHidden()) continue;
      const int w = win.selPanel_->width();
      maxSeen = std::max(maxSeen, w);
      minSeen = std::min(minSeen, w);
    }
    QTRY_COMPARE(win.dockWidgetArea(win.chatDock_), Qt::RightDockWidgetArea);
    awaitAnim(win.chatAnim_);
    // They must land SIDE BY SIDE (same row, chat to the right of the panel) —
    // never stacked vertically (Qt's plain, unsplit addDockWidget default).
    QCOMPARE(win.selPanel_->mapTo(&win, QPoint(0, 0)).y(), win.chatDock_->mapTo(&win, QPoint(0, 0)).y());
    QVERIFY2(win.chatDock_->mapTo(&win, QPoint(0, 0)).x() > win.selPanel_->mapTo(&win, QPoint(0, 0)).x(),
             "chat did not land to the right of the points panel");
    // The panel must never balloon past its pre-share width, nor get squeezed away —
    // the chat's own slide is what should move, not the panel sitting beside it.
    QVERIFY2(maxSeen <= panelBefore + 8,
             qPrintable(QString("points panel widened to %1 (was %2)").arg(maxSeen).arg(panelBefore)));
    QVERIFY2(minSeen >= 100,
             qPrintable(QString("points panel collapsed to %1 mid-slide").arg(minSeen)));

    // Close the chat: the panel should hand its width right back...
    win.actChat_->setChecked(false);
    QTRY_VERIFY(!win.chatDock_->isVisible());
    awaitAnim(win.chatAnim_);
    QVERIFY2(win.selPanel_->width() >= panelBefore - 8,
             qPrintable(QString("panel stayed narrow after chat closed: %1 (was %2)")
                            .arg(win.selPanel_->width()).arg(panelBefore)));

    // …and reopening the chat must not have baked a bad "restore" width into the panel: it settles
    // back near its own size, never "very wide".
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    awaitAnim(win.chatAnim_);
    QVERIFY2(win.selPanel_->width() <= panelBefore + 8,
             qPrintable(QString("panel reopened very wide: %1 (was %2)")
                            .arg(win.selPanel_->width()).arg(panelBefore)));
  }

  // The points panel can be HIDDEN when the chat is placed onto its side, so ensurePanelChatSplit
  // must repair the split wherever either dock's visibility flips, not only when both show.
  void chatPlacedWhilePanelHiddenStillSplitsSideBySide() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.selPanel_ && win.chatDock_ && win.actPanel_ && win.actChat_);
    QVERIFY(win.dockWidgetArea(win.selPanel_) == Qt::RightDockWidgetArea);
    QTRY_VERIFY(!win.selPanel_->isHidden());

    // Hide the points panel FIRST...
    win.actPanel_->setChecked(false);
    QTRY_VERIFY(win.selPanel_->isHidden());

    // ...then place the (unrelated-side) chat onto the panel's side while it's hidden.
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible() && !win.chatDock_->isFloating());
    emit static_cast<stencil::gui::ChatDock*>(win.chatDock_)->dockRequested(Qt::RightDockWidgetArea);
    QTRY_COMPARE(win.dockWidgetArea(win.chatDock_), Qt::RightDockWidgetArea);
    awaitAnim(win.chatAnim_);

    // Now reveal the panel again — it must come back BESIDE the chat, not squashed
    // underneath it.
    win.actPanel_->setChecked(true);
    QTRY_VERIFY(!win.selPanel_->isHidden());
    // The re-laid-out row, not a guess at how long it takes to arrive.
    QTRY_COMPARE_WITH_TIMEOUT(win.selPanel_->mapTo(&win, QPoint(0, 0)).y(),
                              win.chatDock_->mapTo(&win, QPoint(0, 0)).y(), 1000);
    QVERIFY2(win.chatDock_->mapTo(&win, QPoint(0, 0)).x() > win.selPanel_->mapTo(&win, QPoint(0, 0)).x(),
             "the panel reappeared stacked under the chat instead of beside it");
    // Squashed means SHARING a vertical row with the chat, not "shorter than half the window": the
    // stacked toolbars and the top info dock can leave the whole dock row well under half of it.
    QCOMPARE(win.selPanel_->height(), win.chatDock_->height());
    QVERIFY(win.centralWidget());
    QVERIFY2(win.selPanel_->height() == win.centralWidget()->height(),
             "the panel came back with a squashed, shared-row height");
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatDockPlace.gui.moc"
