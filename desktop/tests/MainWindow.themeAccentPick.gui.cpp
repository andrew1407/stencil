// MainWindow GUI e2e — Picking an accent directly off the popover.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Logo accent-preset picker as a first-class popover: right-click opens it sticky, hold-Alt peeks,
  // Alt-glide swaps one at a time, a mid-peek right-press promotes to sticky, plain click cycles.
  void logoAccentPopoverPicksDirectly() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 30);   // let the toolbar's own deferred layout pass settle first
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo);
    const QString original = win.settings_.accentColor;   // persisted — restored below
    const auto& presets = stencil::gui::accentPresets();
    QVERIFY(!presets.empty());
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();   // typingFocus gate off
    const QPoint c = logo->rect().center();
    int pick = -1;
    for (size_t i = 0; i < presets.size(); ++i)
      if (presets[i].key != original) { pick = int(i); break; }
    QVERIFY(pick >= 0);

    // ── STICKY right-click route ──
    bool stickyOpened = false, rowsOk = false, stickySurvivedAlt = false;
    bool picksOk = true, escapeClosedAfterPicks = false;
    QString lastPick;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.pop_.active.data();
      stickyOpened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
      if (pop) {
        int currentCount = 0;
        rowsOk = true;
        for (const auto& a : presets) {
          auto* row = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + a.key);
          rowsOk = rowsOk && row && row->text() == a.label && !row->icon().isNull();
          if (row && row->property("currentAccent").toBool()) ++currentCount;
        }
        rowsOk = rowsOk && currentCount == 1;
      }
      // Sticky: Alt press/release must NOT close it (only a peek dies with the key).
      QTest::keyPress(&win, Qt::Key_Alt);
      QTest::keyRelease(&win, Qt::Key_Alt);
      stickySurvivedAlt = win.pop_.active && !win.pop_.active->isHidden();
      // Picking a colour APPLIES it and CLOSES the popover (user decision — hovering already previews
      // live, so a click is a commit). Browser twin: the logo menu closes on a pick.
      if (pop) {
        const QString key = presets[size_t(pick)].key;
        auto* row = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + key);
        if (!row) { picksOk = false; }
        else {
          row->click();
          settle([&] { return win.settings_.accentColor == key; }, 50);
          picksOk = win.settings_.accentColor == key;                     // applied
          escapeClosedAfterPicks = !win.pop_.active || win.pop_.active->isHidden();  // closed
          lastPick = key;
        }
      }
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
    });
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's exec until the timer acts
    QVERIFY2(stickyOpened, "right-click did not open the accent popover");
    QVERIFY2(rowsOk, "popover rows must be the preset list with one ✓-marked current row");
    QVERIFY2(stickySurvivedAlt, "the sticky popover must survive an Alt press/release");
    QVERIFY2(picksOk, "a colour pick must apply the accent");
    QVERIFY2(escapeClosedAfterPicks, "a colour pick must close the popover");
    QCOMPARE(win.settings_.accentColor, lastPick);
    QVERIFY2(!win.logoClickTimer_->isActive(), "the popover routes must not arm the click-cycle");
    QVERIFY(!win.pop_.active);

    // ── Alt+click stays inert (no popover, no accent cycle armed) ──
    QTest::mouseClick(logo, Qt::LeftButton, Qt::AltModifier, c);
    QVERIFY2(!win.pop_.active, "Alt+click must not open a popover");
    QVERIFY2(!win.logoClickTimer_->isActive(), "Alt+click must not arm the click-cycle timer");

    // PEEK: plain Alt hold over the logo opens promptly and Alt release closes. Hover is simulated via
    // WA_UnderMouse, the state a real Enter leaves behind.
    logo->setAttribute(Qt::WA_UnderMouse, true);
    bool peekOpened = false, releaseClosed = false;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.pop_.active.data();
      peekOpened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
      QTest::keyRelease(&win, Qt::Key_Alt);   // ends the peek on the spot
      releaseClosed = !win.pop_.active || win.pop_.active->isHidden();
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
    });
    QTest::keyPress(&win, Qt::Key_Alt);   // blocks in the peek's exec
    QVERIFY2(peekOpened, "holding Alt over the logo did not peek the accent popover");
    QVERIFY2(releaseClosed, "releasing Alt must close the peeked popover at once");
    QVERIFY(!win.pop_.active);

    // ── GLIDE logo → Connections: one popover at a time, swapped by the system ──
    QToolButton* connBtn = nullptr;
    for (auto it = win.pop_.buttons.cbegin(); it != win.pop_.buttons.cend(); ++it)
      if (it.value() == win.actConnect_) connBtn = static_cast<QToolButton*>(it.key());
    QVERIFY2(connBtn, "no popover button registered for the Connections action");
    win.altHeldForTest_ = true;   // the glide poll's stand-in for a physically held Alt
    bool accentFirst = false;
    QTimer::singleShot(120, &win, [&] {
      accentFirst = win.pop_.active &&
                    win.pop_.active->objectName() == QLatin1String("accentPopover");
      // The pointer glides off the logo onto the Connections icon…
      logo->setAttribute(Qt::WA_UnderMouse, false);
      connBtn->setAttribute(Qt::WA_UnderMouse, true);
      // …and the glide poll (80ms) rejects this popover, then opens the next one.
    });
    QTest::keyPress(&win, Qt::Key_Alt);   // returns once the glide rejects the accent popover
    QVERIFY2(accentFirst, "the glide phase did not start from the accent popover");
    bool swapped = false, glideClosed = false;
    QTimer::singleShot(150, &win, [&] {   // fires inside the Connections popover's exec
      QDialog* pop = win.pop_.active.data();
      swapped = pop && qobject_cast<stencil::gui::ConnectDialog*>(pop) && pop->isVisible() &&
                win.findChildren<QDialog*>("accentPopover").isEmpty();   // single instance
      connBtn->setAttribute(Qt::WA_UnderMouse, false);
      win.altHeldForTest_ = false;
      QTest::keyRelease(&win, Qt::Key_Alt);   // closes the glided-to popover too
      glideClosed = !win.pop_.active || win.pop_.active->isHidden();
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
    });
    settle([&] { return glideClosed; }, 700);   // the deferred altPeekOpen ran and closed
    QVERIFY2(swapped, "the glide did not swap to the Connections popover (single instance)");
    QVERIFY2(glideClosed, "Alt release did not close the glided-to popover");
    QVERIFY(!win.pop_.active);

    // ── Mid-peek gestures: left-click on the logo is a NO-OP; right-press PROMOTES ──
    logo->setAttribute(Qt::WA_UnderMouse, true);
    bool noopKept = false, cycleNotArmed = false, promoted = false;
    QTimer::singleShot(120, &win, [&] {
      if (win.pop_.active) {
        QTest::mousePress(logo, Qt::LeftButton, Qt::NoModifier, c);
        QTest::mouseRelease(logo, Qt::LeftButton, Qt::NoModifier, c);
        noopKept = win.pop_.active && !win.pop_.active->isHidden();
        cycleNotArmed = !win.logoClickTimer_->isActive();
        QTest::mousePress(logo, Qt::RightButton, Qt::NoModifier, c);   // promote to sticky
        QTest::mouseRelease(logo, Qt::RightButton, Qt::NoModifier, c);
        QTest::keyRelease(&win, Qt::Key_Alt);   // promoted → the release must NOT close it
        promoted = win.pop_.active && !win.pop_.active->isHidden();
      }
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
    });
    QTest::keyPress(&win, Qt::Key_Alt);
    QVERIFY2(noopKept, "a left-click on the logo must not dismiss its peeked popover");
    QVERIFY2(cycleNotArmed, "a left-click during the peek armed the accent cycle");
    QVERIFY2(promoted, "a right-press during the peek must promote it past the Alt release");
    QVERIFY(!win.pop_.active);

    // ── Losing the app's focus ends a peek (Cmd-Tab eats the keyup). The popover is a
    // child widget of this window, so it is THIS window's deactivation that says so ──
    bool deactClosed = false;
    QTimer::singleShot(120, &win, [&] {
      if (win.pop_.active) {
        QEvent deact(QEvent::WindowDeactivate);
        QApplication::sendEvent(&win, &deact);
        deactClosed = !win.pop_.active || win.pop_.active->isHidden();
      }
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
      QTest::keyRelease(&win, Qt::Key_Alt);
    });
    QTest::keyPress(&win, Qt::Key_Alt);
    QVERIFY2(deactClosed, "the peeked popover must close when the app loses focus");
    QVERIFY(!win.pop_.active);
    logo->setAttribute(Qt::WA_UnderMouse, false);

    // ── Alt with the cursor NOT over any popover icon opens nothing ──
    win.move(400, 300);   // the offscreen cursor's resting point goes cold too
    QTest::qWait(30);
    QTest::keyPress(&win, Qt::Key_Alt);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QVERIFY2(!win.pop_.active, "Alt away from the icons must not open a popover");

    // ── A PLAIN click still cycles: it arms the deferred timer ──
    QTest::mouseClick(logo, Qt::LeftButton, Qt::NoModifier, c);
    QVERIFY2(win.logoClickTimer_->isActive(), "plain click no longer arms the accent cycle");
    win.logoClickTimer_->stop();

    // Leave the persisted accent as we found it — the settings are shared across tests.
    auto restore = win.settings_;
    restore.accentColor = original;
    win.applySettings(restore, true);
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.themeAccentPick.gui.moc"
