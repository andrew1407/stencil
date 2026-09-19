// MainWindow GUI e2e — The unread mark across every closed state, and the compact swap from every route.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A result that lands with no chat surface to show it must still reach the
  // user: a toast, plus an unread mark on the chat icon. The three gaps this
  // pins — a turn finishing DURING the close slide (the dock stays isVisible()
  // for 260ms), a turn owned by the context-menu panel, and the §3.2/§3.1 chain
  // finishing after the reply was announced — were all silent.
  void chatToastAndUnreadCoverEveryClosedState() {
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    const auto toast = [&]() -> QWidget* {
      return win.chatToast_ && win.chatToast_->isVisible() ? win.chatToast_ : nullptr;
    };
    // Nothing may mark the icon at all now; the lambda stays to prove it.
    const auto unreadShown = [&] {
      for (QLabel* d : win.findChildren<QLabel*>(QStringLiteral("chatUnreadDot")))
        if (d->isVisible()) return true;
      return false;
    };

    // Closed chat: the predicate says "nobody can see this".
    QVERIFY(!win.chatDock_->isVisible());
    QVERIFY2(win.chatSurfaceHidden(), "a closed chat should count as hidden");
    win.showChatToast(QStringLiteral("Assistant finished — done"), true);
    QVERIFY2(toast(), "no toast with the chat closed");
    QVERIFY2(!unreadShown(), "the toast is the whole notice — nothing is left on the icon");

    // Opening clears the mark, and nothing toasts while the chat is up.
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    QVERIFY2(!unreadShown(), "opening must leave the icon unmarked too");
    QVERIFY2(!win.chatSurfaceHidden(), "an open chat must not count as hidden");

    // ITEM A — mid-close: the dock is still isVisible() during its slide, but a
    // result landing then has nowhere to go, so it counts as hidden.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    win.actChat_->setChecked(false);
    QVERIFY2(win.chatDock_->isVisible(), "the close should still be animating");
    QVERIFY2(win.chatSurfaceHidden(), "a chat mid-close must count as hidden");
    if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim);
    QTRY_VERIFY(!win.chatDock_->isVisible());

    // ITEM C — the context-menu panel is a chat surface too.
    win.ensureChatMenuPanel();
    win.chatMenuPanel_->setGeometry(20, 20, 340, 620);
    win.chatMenuPanel_->show();
    QTRY_VERIFY2(!win.chatSurfaceHidden(), "a visible menu panel must count as a surface");
    win.chatMenuPanel_->hide();
    QTRY_VERIFY2(win.chatSurfaceHidden(), "a dismissed menu panel leaves nothing to look at");

    // ITEM B — §3.0: settling a turn is not itself an event. Nothing runs after
    // the reply, so the terminal has no news of its own to toast.
    if (win.chatToast_) win.chatToast_->hide();
    win.chatTurnSettled();
    QVERIFY2(!toast(), "the turn terminal must be silent — nothing runs after the reply");
    QVERIFY2(!unreadShown(), "…and it must not mark the icon either");
    beat();
  }

  // Opening the COMPACT chat while one is already on screen is a popover swap: the
  // outgoing shape animates out FIRST — a docked panel slides back into its edge, a
  // floating one flies into the icon — and only then does the compact float reveal.
  // Every route has to do it (right-click on the icon, Alt-hover peek, the toolbar
  // action), from BOTH shapes: the float route used to skip it entirely.
  void compactChatSwapAnimatesFromEveryRoute() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(dock);
    auto* icon = qobject_cast<QToolButton*>(win.buttonForAction(win.actChat_));

    // The outgoing FLOAT's exit is a cloud of its own pixels flown inside the main
    // window: it SCATTERS, every mote pouring back into the icon.
    const auto flight = [&win] { return surfaceFlight(&win); };
    // Past the whole surface flight, so a cloud from the LAST swap can never be mistaken
    // for the next one's (the gather is the longer of the two clocks).
    const auto flushGhosts = [&win] { awaitAnim(win.chatAnim_); awaitFlights(&win); };

    // Put the chat in `floating` shape with a message in it, ready to be swapped.
    const auto arm = [&](bool floating, const QString& mark) {
      win.setChatShown(false, false);
      win.chatCompactPopover_ = false;
      dock->setFloating(floating);
      win.actChat_->setChecked(true);
      win.setChatShown(true, false);
      QTRY_VERIFY(dock->isVisible());
      QCOMPARE(dock->isFloating(), floating);
      win.chatDock_->appendUser(mark);
      flushGhosts();
    };
    // Assert the swap: outgoing animates (slide for a dock, flight for a float),
    // the compact float lands, and the transcript came along.
    const auto expectSwap = [&](bool wasFloating, const QString& mark, const char* route) {
      if (wasFloating) {
        auto* from = flight();
        QVERIFY2(from, qPrintable(QString("%1: the outgoing FLOAT did not fly out").arg(route)));
        QVERIFY2(!from->gathering(),
                 qPrintable(QString("%1: the outgoing float must come APART, not form").arg(route)));
        QCOMPARE(from->surfaceTarget(), flightPointOf(icon, &win));
      } else {
        QVERIFY2(win.chatAnim_ != nullptr,
                 qPrintable(QString("%1: the docked panel did not slide out").arg(route)));
        QVERIFY2(!dock->isFloating(),
                 qPrintable(QString("%1: it tore off before the slide played").arg(route)));
      }
      QTRY_VERIFY_WITH_TIMEOUT(win.chatCompactShowing(), 4000);
      bool kept = false;
      for (QLabel* l : dock->findChildren<QLabel*>())
        if (l->property("chatBody").toString() == mark) kept = true;
      QVERIFY2(kept, qPrintable(QString("%1: the swap lost the conversation").arg(route)));
      flushGhosts();
    };

    // ── the four routes ──
    for (const bool floating : {false, true}) {
      const QString shape = floating ? QStringLiteral("float") : QStringLiteral("dock");
      // Right-click on the toolbar icon (the popover gesture).
      {
        const QString mark = shape + " ctx";
        arm(floating, mark);
        QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(4, 4),
                             icon->mapToGlobal(QPoint(4, 4)));
        QApplication::sendEvent(icon, &ev);
        expectSwap(floating, mark, qPrintable(shape + " + right-click"));
      }
      // Alt-hover peek onto the same icon.
      {
        const QString mark = shape + " peek";
        arm(floating, mark);
        win.altPeekOpen(icon, win.actChat_);
        expectSwap(floating, mark, qPrintable(shape + " + alt-peek"));
      }
    }
    // …and the shape the user actually had: a float that IS the compact popover,
    // MOVED away from its anchor. Re-opening it used to teleport the window with
    // no motion at either end — the reported "the chat just vanished".
    {
      QVERIFY(win.chatCompactShowing());
      win.chatDock_->appendUser(QStringLiteral("moved compact"));
      dock->move(dock->pos() + QPoint(160, 120));   // as if dragged
      flushGhosts();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(4, 4),
                           icon->mapToGlobal(QPoint(4, 4)));
      QApplication::sendEvent(icon, &ev);
      expectSwap(/*wasFloating=*/true, QStringLiteral("moved compact"),
                 "moved compact float + right-click");
      // Back at the anchor, the same gesture is idempotent: no flight, no move.
      const QRect settled = dock->geometry();
      QApplication::sendEvent(icon, &ev);
      QCOMPARE(dock->geometry(), settled);
      QVERIFY2(!flight(), "a re-pin that moves nothing must not animate");
      QVERIFY(win.chatCompactShowing());
    }
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatCompactSwap.gui.moc"
