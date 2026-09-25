// MainWindow GUI e2e — Changing placement: the animation, and the chat standing outside the editor shell.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "theme.hpp"   // DOCK_SEPARATOR_PX

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
    awaitAnim(win.chatAnim);
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
    awaitAnim(win.chatAnim);

    // …and coming back from FLOAT slides in at the side you picked, rather than
    // appearing at full width (the floating branch used to skip the animation).
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    awaitAnim(win.chatAnim);
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

  // The chat docks on the WINDOW, outside the editor shell (browser: a fixed-position panel the
  // page is inset by): docked right it stands beyond the points panel, running the shell's full
  // height, toolbars included, and the panel's width never moves through the flight.
  void chatDockedRightStandsOutsideThePanel() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.selPanel && win.chatDock && win.editor);
    QVERIFY(win.editor->dockWidgetArea(win.selPanel) == Qt::RightDockWidgetArea);
    QTRY_VERIFY(!win.selPanel->isHidden());

    win.actChat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible() && !win.chatDock->isFloating());
    awaitAnim(win.chatAnim);
    const int panelBefore = win.selPanel->width();
    QVERIFY2(panelBefore > 120, "the points panel never reached its natural width");

    emit static_cast<stencil::gui::ChatDock*>(win.chatDock)->dockRequested(Qt::RightDockWidgetArea);
    int maxSeen = 0, minSeen = win.width();
    QVERIFY2(win.chatAnim, "the placement change did not animate");
    QElapsedTimer flightClock;
    flightClock.start();
    while (win.chatAnim && flightClock.elapsed() < 1500) {   // the flight's own length
      QTest::qWait(15);
      if (win.selPanel->isHidden()) continue;
      const int w = win.selPanel->width();
      maxSeen = std::max(maxSeen, w);
      minSeen = std::min(minSeen, w);
    }
    QTRY_COMPARE(win.dockWidgetArea(win.chatDock), Qt::RightDockWidgetArea);
    awaitAnim(win.chatAnim);
    const QRect chat(win.chatDock->mapTo(&win, QPoint(0, 0)), win.chatDock->size());
    const QRect panel(win.selPanel->mapTo(&win, QPoint(0, 0)), win.selPanel->size());
    const QRect shell(win.editor->mapTo(&win, QPoint(0, 0)), win.editor->size());
    QVERIFY2(chat.left() >= panel.right(), "chat did not land beyond the points panel");
    QCOMPARE(chat.top(), shell.top());
    QCOMPARE(chat.height(), shell.height());
    QVERIFY2(chat.top() < win.headerToolbar->mapTo(&win, QPoint(0, 0)).y() + win.headerToolbar->height(),
             "the chat starts under the toolbars instead of beside them");
    QVERIFY2(maxSeen <= panelBefore + 8,
             qPrintable(QString("points panel widened to %1 (was %2)").arg(maxSeen).arg(panelBefore)));
    QVERIFY2(minSeen >= panelBefore - 8,
             qPrintable(QString("points panel squeezed to %1 mid-slide (was %2)").arg(minSeen).arg(panelBefore)));
    // The header's placement chips follow: the right chevron is the lit one.
    auto* dock = static_cast<stencil::gui::ChatDock*>(win.chatDock);
    QCOMPARE(dock->chrome.dockBtns.size(), 4);
    QCOMPARE(dock->chrome.dockBtns[3]->property(stencil::gui::ICON_STATE_PROPERTY).toString(), QStringLiteral("active"));
    QCOMPARE(dock->chrome.dockBtns[0]->property(stencil::gui::ICON_STATE_PROPERTY).toString(), QString());

    // Close the chat: the panel keeps its width...
    win.actChat->setChecked(false);
    QTRY_VERIFY(!win.chatDock->isVisible());
    awaitAnim(win.chatAnim);
    QVERIFY2(win.selPanel->width() >= panelBefore - 8,
             qPrintable(QString("panel stayed narrow after chat closed: %1 (was %2)")
                            .arg(win.selPanel->width()).arg(panelBefore)));
    // ...and reopening the chat leaves it alone too.
    win.actChat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    awaitAnim(win.chatAnim);
    QVERIFY2(win.selPanel->width() <= panelBefore + 8,
             qPrintable(QString("panel reopened very wide: %1 (was %2)")
                            .arg(win.selPanel->width()).arg(panelBefore)));
  }

  // Docked top, the chat is a full-width row ABOVE the toolbars and the Image Size dock (browser
  // .chat-dock-top: top 0, left 0, right 0), and the up chevron is the lit chip.
  void chatDockedTopSpansTheWindowAboveTheToolbars() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.chatDock && win.editor && win.headerToolbar && win.imageInfoDock);
    win.actChat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible() && !win.chatDock->isFloating());
    emit static_cast<stencil::gui::ChatDock*>(win.chatDock)->dockRequested(Qt::TopDockWidgetArea);
    QTRY_COMPARE(win.dockWidgetArea(win.chatDock), Qt::TopDockWidgetArea);
    awaitAnim(win.chatAnim);
    const QRect chat(win.chatDock->mapTo(&win, QPoint(0, 0)), win.chatDock->size());
    QCOMPARE(chat.left(), 0);
    QCOMPARE(chat.width(), win.width());
    QVERIFY2(chat.bottom() < win.headerToolbar->mapTo(&win, QPoint(0, 0)).y(),
             "the toolbars sit above the top-docked chat");
    QVERIFY2(chat.bottom() < win.imageInfoDock->mapTo(&win, QPoint(0, 0)).y(),
             "the Image Size dock sits above the top-docked chat");
    // The browser's 10px of page lies between the chat and the shell (chat/touch.css body padding).
    const int shellTop = win.editor->mapTo(&win, QPoint(0, 0)).y();
    QCOMPARE(shellTop - chat.bottom() - 1, stencil::gui::CHAT_PAGE_GAP_PX);
    auto* dock = static_cast<stencil::gui::ChatDock*>(win.chatDock);
    QCOMPARE(dock->chrome.dockBtns.size(), 4);
    QCOMPARE(dock->chrome.dockBtns[1]->property(stencil::gui::ICON_STATE_PROPERTY).toString(), QStringLiteral("active"));
    QCOMPARE(dock->chrome.dockBtns[0]->property(stencil::gui::ICON_STATE_PROPERTY).toString(), QString());
  }

  // A points panel hidden while the chat takes its side comes back inside the editor shell, at
  // the shell's central height, beside the chat and never under it.
  void panelReshownBesideARightDockedChat() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.selPanel && win.chatDock && win.actPanel && win.actChat && win.editor);
    QTRY_VERIFY(!win.selPanel->isHidden());
    win.actPanel->setChecked(false);
    QTRY_VERIFY(win.selPanel->isHidden());
    win.actChat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible() && !win.chatDock->isFloating());
    emit static_cast<stencil::gui::ChatDock*>(win.chatDock)->dockRequested(Qt::RightDockWidgetArea);
    QTRY_COMPARE(win.dockWidgetArea(win.chatDock), Qt::RightDockWidgetArea);
    awaitAnim(win.chatAnim);
    win.actPanel->setChecked(true);
    QTRY_VERIFY(!win.selPanel->isHidden());
    QTRY_COMPARE_WITH_TIMEOUT(win.selPanel->height(), win.editor->centralWidget()->height(), 1000);
    QVERIFY2(win.chatDock->mapTo(&win, QPoint(0, 0)).x() >= win.selPanel->mapTo(&win, QPoint(0, 0)).x() + win.selPanel->width(),
             "the panel reappeared under the chat instead of beside it");
  }

  // The lit placement chip follows a re-dock made straight on the window while the dock is
  // still hidden (the docs capture, a restored layout), not only one the header buttons asked for.
  void placementChipFollowsADirectRedock() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = static_cast<stencil::gui::ChatDock*>(win.chatDock);
    QVERIFY(dock && dock->isHidden());
    win.addDockWidget(Qt::RightDockWidgetArea, dock, Qt::Horizontal);
    dock->setFloating(false);
    dock->show();
    QTRY_VERIFY(dock->isVisible());
    QCOMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QCOMPARE(dock->chrome.dockBtns.size(), 4);
    QTRY_COMPARE(dock->chrome.dockBtns[3]->property(stencil::gui::ICON_STATE_PROPERTY).toString(), QStringLiteral("active"));
    QCOMPARE(dock->chrome.dockBtns[0]->property(stencil::gui::ICON_STATE_PROPERTY).toString(), QString());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatDockPlace.gui.moc"
