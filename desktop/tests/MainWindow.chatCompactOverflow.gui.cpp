// MainWindow GUI e2e — The overflow menu and its window flying to the dots trigger, and the floating panel's flight.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The chat composer's "…" was the last popup in the app that still hard-cut on both
  // edges, and the Assistant window it raises grew out of nothing — its Settings item is
  // gone by the time the window opens, so the anchor measured 0x0. Both now belong to the
  // "…" TRIGGER, which is also what makes the window fall from above when the dock is
  // shut: a hidden anchor is no anchor (modalReveal originRect).
  void chatOverflowAndItsWindowFlyToTheDotsTrigger() {
    MainWindow win;
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.chatDock_->setVisible(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    settleLayout(&win, 150);
    // ctest runs with STENCIL_NO_ANIM=1 and every flight is a no-op under it; this test
    // is about the flight itself, so turn it back on for the duration.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });

    auto* moreBtn = win.chatDock_->moreButton();
    QVERIFY(moreBtn && moreBtn->isVisible());
    QMenu* menu = moreBtn->menu();
    QVERIFY(menu);
    const QPoint want = flightPointOf(moreBtn, &win);

    // The menu's own dust is skipped offscreen by design (menuReveal revealPopup — the
    // gui suite picks items the instant the popup lands), so what is checkable here is
    // that BOTH edges are wired, and wired to the trigger. It is a repeat-show menu, so
    // the one-shot MenuReveal would have been wrong; revealMenuFrom is what it gets.
    auto* flight = menu->findChild<QObject*>(QStringLiteral("stencilMenuFlight"),
                                             Qt::FindDirectChildrenOnly);
    QVERIFY2(flight, "the … menu has no flight wired at all");

    // It is four short labelled icons, NOT a menu-bar menu: the theme's gutters (24px
    // left check reserve + 26px right shortcut slack) plus the shortcut column Qt
    // reserves anyway left a visible gap after each glyph and a band of dead space down
    // the right edge. compactIconMenu hugs the longest label instead.
    menu->popup(moreBtn->mapToGlobal(moreBtn->rect().bottomLeft()));
    QVERIFY(QTest::qWaitForWindowExposed(menu));
    settleLayout(menu, 30);
    int widest = 0;
    for (QAction* a : menu->actions())
      widest = std::max(widest, menu->fontMetrics().horizontalAdvance(a->text()));
    QVERIFY2(widest > 0, "no labels to measure");
    const int slack = menu->width() - widest;
    // Icon + paddings only. The untamed hint ran ~95px past the label on this font.
    QVERIFY2(slack > 0 && slack <= 64,
             qPrintable(QString("menu is %1 wide for a %2 label — %3px of slack")
                            .arg(menu->width()).arg(widest).arg(slack)));
    menu->hide();
    QTRY_VERIFY(!menu->isVisible());
    // Popping it twice must not stack a second filter — nor go quiet on the second show.
    menu->popup(moreBtn->mapToGlobal(moreBtn->rect().bottomLeft()));
    QVERIFY(QTest::qWaitForWindowExposed(menu));
    menu->hide();
    QTRY_VERIFY(!menu->isVisible());
    menu->popup(moreBtn->mapToGlobal(moreBtn->rect().bottomLeft()));
    QTRY_VERIFY(menu->isVisible());
    menu->hide();
    QTRY_VERIFY(!menu->isVisible());
    QCOMPARE(menu->findChildren<QObject*>(QStringLiteral("stencilMenuFlight"),
                                          Qt::FindDirectChildrenOnly).size(), 1);

    // …and the window that Settings raises rides the trigger's point — this half DOES
    // fly offscreen, so it is asserted for real.
    QDialog dlg(&win);
    dlg.resize(320, 240);
    stencil::support::revealDialog(dlg, moreBtn);
    dlg.show();
    QTRY_COMPARE(surfaceFlightTarget(&win), want);
    dlg.close();
    awaitFlights(&win);

    // A shut dock leaves nothing on screen to own the window: it falls from above
    // instead of out of the trigger's stale last position.
    win.chatDock_->setVisible(false);
    QTRY_VERIFY(!moreBtn->isVisible());
    QDialog orphan(&win);
    orphan.resize(320, 240);
    stencil::support::revealDialog(orphan, moreBtn);
    orphan.show();
    settle([&] { return surfaceFlightTarget(&win) != QPoint(-1, -1); }, 50);
    const QPoint above = surfaceFlightTarget(&win);
    if (above != QPoint(-1, -1))
      QVERIFY2(above != want, "a hidden trigger must not keep claiming the flight");
    orphan.close();
  }

  // A FLOATING chat opens out of the toolbar icon and shrinks back into it, like every
  // dialog. It used to blink in and out — the floating branch skipped animation entirely.
  void floatingChatFliesFromItsIcon() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(dock);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    awaitAnim(win.chatAnim_);
    QWidget* icon = win.buttonForAction(win.actChat_);
    QVERIFY2(icon && icon->isVisible(), "no chat icon to fly from");
    const QPoint iconPoint = flightPointOf(icon, &win);
    // The flight is a cloud of the dock's own pixels inside the MAIN window, aimed at
    // that icon: gathering out of it on the way in, scattering back into it on the way
    // out. Both are the icon — the DIRECTION is what tells the two apart.
    win.actChat_->setChecked(false);          // close: the window comes apart into the icon
    QTRY_VERIFY(surfaceFlight(&win));
    auto* closing = surfaceFlight(&win);
    QVERIFY2(closing, "closing a floating chat did not animate");
    QCOMPARE(closing->surfaceTarget(), iconPoint);
    QVERIFY2(!closing->gathering(), "the close flight scatters INTO the icon, it does not gather");
    awaitAnim(win.chatAnim_);
    win.actChat_->setChecked(true);           // open: it forms out of the icon
    QTRY_VERIFY(surfaceFlight(&win));
    auto* opening = surfaceFlight(&win);
    QVERIFY2(opening, "opening a floating chat did not animate");
    QCOMPARE(opening->surfaceTarget(), iconPoint);
    QVERIFY2(opening->gathering(), "the open flight gathers OUT of the icon");
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatCompactOverflow.gui.moc"
