// MainWindow GUI e2e — The assistant in its COMPACT shape: the floating popover the toolbar
// icon opens, the flights in and out of that icon, and what stands in for a chat surface when
// none is up (the toast and the icon's unread mark).
// Shared ground (helpers, the loaded window, the motion pins) is in mainWindow.gui.hpp.
#include "mainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The assistant's settings (the chat's … ▸ Settings) have a chord of their own, from the
  // shared registry: it opens the assistant-only dialog with no chat surface up at all, and
  // pressed again inside that dialog it closes it — the toolbar windows' toggle rule.
  void assistantSettingsShortcutOpensAndClosesTheDialog() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    QVERIFY2(!win.actAssistantSettings_->shortcut().isEmpty(), "the dialog has a chord");
    QCOMPARE(win.hotkeyLabels_.value(QStringLiteral("openAssistantSettings")),
             QStringLiteral("AI Assistant Settings"));   // the Shortcuts window lists it
    QVERIFY(!win.chatDock_->isVisible());

    QString dialogName;
    bool sawOwn = false, closedByOwn = false;
    QTimer::singleShot(0, &win, [&] {
      QDialog* dlg = nullptr;
      for (int i = 0; i < 200 && !dlg; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) QTest::qWait(10);
      }
      if (!dlg) return;
      dialogName = dlg->objectName();
      for (QShortcut* sc : dlg->findChildren<QShortcut*>()) {
        if (sc->key() == win.actAssistantSettings_->shortcut()) { sawOwn = true; emit sc->activated(); }
      }
      closedByOwn = !dlg->isVisible();
      if (!closedByOwn) dlg->reject();
    });
    win.actAssistantSettings_->trigger();   // blocks in exec() until the timer closes it

    QCOMPARE(dialogName, QString("assistantSettingsDialog"));
    QVERIFY2(sawOwn, "the dialog carried its own opener's chord");
    QVERIFY2(closedByOwn, "its own shortcut closed the window");
    beat();
  }

  // The chat toolbar icon answers the same popover gestures as every dialog icon
  // (browser chatPanel.js openCompact): a double-click opens a COMPACT FLOATING chat
  // pinned by the icon, and re-pins it when already open. A plain click still toggles,
  // deferred one double-click interval (whose trailing-release swallow this exercises).
  void chatIconPopoverGesture() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    win.raise();
    win.activateWindow();   // the typing-guard check below needs a focus owner
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    QToolButton* btn = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->defaultAction() == chat) { btn = b; break; }
    QVERIFY(btn);

    // A real double-click is press, release, dblclick, release — all four must go
    // through the gesture filter (QTest::mouseDClick waits internally, which the
    // deferred-click timer turns into a plain click first).
    const QPoint hit = btn->rect().center();
    const auto send = [btn, hit](QEvent::Type t) {
      QMouseEvent e(t, QPointF(hit), btn->mapToGlobal(hit), Qt::LeftButton,
                    t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                    Qt::NoModifier);
      QApplication::sendEvent(btn, &e);
    };
    const auto doubleClick = [&send] {
      send(QEvent::MouseButtonPress);
      send(QEvent::MouseButtonRelease);
      send(QEvent::MouseButtonDblClick);
      send(QEvent::MouseButtonRelease);
    };

    // While a text control has FOCUS, Alt belongs to the typing: with the cursor
    // resting on the icon, pressing Alt must NOT open the peek. Checked FIRST —
    // before any floating-dock activation, which the offscreen platform cannot
    // hand back (no raise()), leaves setFocus without an active window. The zoom
    // combo's inner line edit is the focusable text control used for it — which
    // needs an image, since the ZOOM cluster is dead without one.
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.zoom_->isEnabled());
    QCursor::setPos(btn->mapToGlobal(hit));
    QLineEdit* zoomEdit = win.zoom_->lineEdit();
    QVERIFY(zoomEdit);
    zoomEdit->setFocus();
    // The guard reads QApplication::focusWidget() — the editable combo is its
    // line edit's FOCUS PROXY, so that is what focus lands on.
    QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(win.zoom_));
    QTest::keyPress(zoomEdit, Qt::Key_Alt);
    QTest::keyRelease(zoomEdit, Qt::Key_Alt);
    QTest::qWait(50);
    QVERIFY2(!dock->isVisible(), "Alt while typing must not open the peek");
    win.zoom_->clearFocus();
    QTRY_VERIFY(QApplication::focusWidget() != win.zoom_);

    doubleClick();
    QTRY_VERIFY(dock->isVisible());
    QVERIFY2(dock->isFloating(), "the popover gesture must float the dock, not slide it in docked");
    QVERIFY(chat->isChecked());
    // Pinned next to the icon at the compact size — the shared placement rule.
    const QRect btnRect(btn->mapToGlobal(QPoint(0, 0)), btn->size());
    const QRect expect = stencil::support::popoverRect(
        btnRect, win.chatDock_->floatingDefaultSize(), btn->screen()->availableGeometry());
    QTRY_COMPARE(dock->geometry().topLeft(), expect.topLeft());
    QCOMPARE(dock->size(), expect.size());
    // The deferred single-click must NOT fire off the dblclick's trailing release
    // and yank the chat back shut (the swallow-release regression).
    QTest::qWait(QApplication::doubleClickInterval() + 300);
    QVERIFY2(dock->isVisible(), "the dblclick's trailing release must not re-arm the deferred toggle");
    QVERIFY(chat->isChecked());

    // The gesture while ALREADY open re-pins compact — it never hides.
    doubleClick();
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(dock->isFloating());
    QVERIFY(chat->isChecked());

    // A plain click still toggles it off — after the double-click interval.
    send(QEvent::MouseButtonPress);
    send(QEvent::MouseButtonRelease);
    QTRY_VERIFY_WITH_TIMEOUT(!dock->isVisible(), QApplication::doubleClickInterval() + 2000);
    QVERIFY(!chat->isChecked());

    // Alt while hovering (the peek route, browser altHover parity): rest the
    // cursor on the icon and press Alt — the compact float opens with no click —
    // and it is HOLD-to-peek: releasing Alt closes it again.
    QCursor::setPos(btn->mapToGlobal(hit));
    QTest::qWait(20);
    QTest::keyPress(&win, Qt::Key_Alt);
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(dock->isFloating());
    QVERIFY(chat->isChecked());
    QTest::keyRelease(&win, Qt::Key_Alt);
    QTRY_VERIFY2(!dock->isVisible(), "releasing Alt must close what the peek opened");
    QVERIFY(!chat->isChecked());

    // ENGAGED peek: move the cursor INTO the peeked window before releasing Alt —
    // it stays open (sticky), instead of being yanked out from under the user.
    QCursor::setPos(btn->mapToGlobal(hit));
    QTest::qWait(20);
    QTest::keyPress(&win, Qt::Key_Alt);
    QTRY_VERIFY(dock->isVisible());
    QCursor::setPos(dock->frameGeometry().center());
    QTest::qWait(20);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QTest::qWait(50);
    QVERIFY2(dock->isVisible(), "releasing Alt with the cursor inside must keep the peek open");
    QVERIFY(chat->isChecked());
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());

    // A DELIBERATE (dblclick) open is sticky: an Alt press+release leaves it be.
    doubleClick();
    QTRY_VERIFY(dock->isVisible());
    QTest::keyPress(&win, Qt::Key_Alt);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QTest::qWait(50);
    QVERIFY2(dock->isVisible(), "Alt release must never close a dblclick-opened popover");
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());

    // A DISABLED icon opens nothing — mini window included — and must not arm a
    // stale popover anchor that would pin the NEXT dialog to it.
    QCursor::setPos(btn->mapToGlobal(hit));
    QTest::qWait(20);
    chat->setEnabled(false);
    doubleClick();
    QTest::qWait(80);
    QVERIFY2(!dock->isVisible(), "dblclick on a disabled icon must not open the popover");
    QTest::keyPress(&win, Qt::Key_Alt);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QTest::qWait(50);
    QVERIFY2(!dock->isVisible(), "Alt-peek on a disabled icon must not open the popover");
    chat->setEnabled(true);

    dock->setFloating(false);  // leave the shared default placement for later slots
    beat();
  }

  // Assistant completions landing while the chat dock is HIDDEN surface as a
  // clickable bottom-left toast (browser closedToast parity): success/failure
  // text truncated to ~90 chars, auto-anchored bottom-left, click = open the
  // dock + dismiss; an open dock shows no toast.
  void chatToastWhenDockHidden() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());

    // Success (chat-only plan) while hidden → success toast with the reply.
    stencil::llm::LlmReply ok;
    ok.ok = true;
    ok.text = "{\"version\":1,\"reply\":\"All done, boss\",\"actions\":[]}";
    win.onChatReply(ok);
    auto* toast = win.findChild<QWidget*>("chatToast");
    QVERIFY(toast);
    QTRY_VERIFY(toast->isVisible());
    auto* label = toast->findChild<QLabel*>();
    QVERIFY(label);
    QVERIFY(label->text().contains("Assistant finished"));
    QVERIFY(label->text().contains("All done, boss"));
    // Anchored bottom-left (18 px inset).
    QCOMPARE(toast->x(), 18);
    QVERIFY(toast->geometry().bottom() > win.height() - 40);

    // Click → the dock opens through the normal path and the toast dismisses.
    QTest::mouseClick(toast, Qt::LeftButton);
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(chat->isChecked());
    QTRY_VERIFY(!toast->isVisible());

    // Failure while hidden → error toast, truncated to the ~90-char bound.
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    stencil::llm::LlmReply bad;
    bad.ok = false;
    bad.failure = stencil::llm::LlmFailure::Http;
    bad.error = QString(200, QChar('x'));
    win.onChatReply(bad);
    QTRY_VERIFY(toast->isVisible());
    QVERIFY(label->text().startsWith("Assistant failed"));
    QVERIFY(label->text().size() <= 90);
    QVERIFY(label->text().endsWith(QChar(0x2026)));
    QTest::mouseClick(toast, Qt::LeftButton);  // dismiss + reopen for the next check
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(!toast->isVisible());

    // Open dock: completions must NOT raise a toast.
    win.onChatReply(ok);
    QVERIFY(!toast->isVisible());
    beat();
  }

  // The chat dock's gear opens the DEDICATED assistant dialog (browser
  // llmSettingsModal parity) — provider / base URL / model / API key / server
  // only, none of the full Settings sheet's unrelated controls — and saving it
  // round-trips the provider through the normal persistence path.
  void chatGearOpensAssistantOnlySettings() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    auto* gear = dock->findChild<QToolButton*>("chatGear");
    QVERIFY(gear);

    const stencil::gui::Settings before = stencil::gui::fileStore::loadSettings();
    // Pick a provider that differs from whatever is configured now.
    const QString target =
        win.settings_.llmProvider == QLatin1String("openai-compat") ? "ollama"
                                                                   : "openai-compat";
    // The dialog is modal (exec blocks the click), so inspect + drive it from a
    // timer, the way dismissModal does for the confirmations.
    bool inspected = false, wrongControls = false, sawProvider = false;
    QString dialogName;
    QTimer::singleShot(0, [&] {
      QDialog* dlg = nullptr;
      for (int i = 0; i < 200 && !dlg; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) QTest::qWait(10);
      }
      if (!dlg) return;
      dialogName = dlg->objectName();
      auto* provider = dlg->findChild<QComboBox*>("llmProvider");
      sawProvider = provider != nullptr;
      // Assistant-only: none of the full Settings dialog's controls (autosave /
      // visibility checkboxes, thickness & page-size spin boxes, the page-size
      // combo) may be here. The one checkbox that DOES belong is the §12
      // save-chats opt-in (llmSaveChats) — an assistant setting.
      wrongControls = !dlg->findChildren<QDoubleSpinBox*>().isEmpty() ||
                      !dlg->findChildren<QSpinBox*>().isEmpty();
      for (QCheckBox* cb : dlg->findChildren<QCheckBox*>())
        if (cb->objectName() != QLatin1String("llmSaveChats")) wrongControls = true;
      for (QComboBox* c : dlg->findChildren<QComboBox*>())
        if (c->findData(QString("A4")) >= 0 || c->findText("A4") >= 0)
          wrongControls = true;
      // The LLM rows the browser modal has, all present.
      for (const char* name : {"llmBaseUrl", "llmModel", "llmApiKey", "llmServer"})
        if (!dlg->findChild<QWidget*>(name)) wrongControls = true;
      if (provider) {
        const int idx = provider->findData(target);
        if (idx >= 0) provider->setCurrentIndex(idx);
      }
      inspected = true;
      dlg->accept();  // Save
    });
    gear->click();  // blocks in exec() until the timer accepts

    QVERIFY2(inspected, "the gear did not open a modal dialog");
    QCOMPARE(dialogName, QString("assistantSettingsDialog"));
    QVERIFY(sawProvider);
    QVERIFY2(!wrongControls, "the assistant dialog carries unrelated settings");
    // Saved through the same path as the full dialog: live settings + the file.
    QCOMPARE(win.settings_.llmProvider, target);
    QCOMPARE(stencil::gui::fileStore::loadSettings().llmProvider, target);

    stencil::gui::fileStore::saveSettings(before);  // leave the user's config alone
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

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

  // The chat's icon controls shimmer on hover like every other button in the app
  // (browser layout.css shimmers every <button>, chat ones included): the sweep
  // starts on Enter, stops on Leave, and never runs on a disabled control.
  // Checked on the dock's composer + title bar, the per-message "…", and the
  // context-menu panel's composer.
  void chatIconButtonsShimmerOnHover() {
    const auto motion = withMotion();   // the sweep honours motionReduced(), which is on here
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.chatDock_->appendUser(QStringLiteral("shimmer me"));
    win.ensureChatMenuPanel();
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();
    win.chatMirror(QStringLiteral("You"), QStringLiteral("shimmer me too"), false);
    settleLayout(win.chatMenuPanel_, 200);

    const auto overlayOf = [](QWidget* w) {
      return w ? w->findChild<QWidget*>("shimmerOverlay") : nullptr;
    };
    const auto hoverEnter = [](QWidget* w) {
      QEnterEvent e(QPointF(4, 4), QPointF(4, 4), w->mapToGlobal(QPoint(4, 4)));
      QApplication::sendEvent(w, &e);
    };
    // Every chat icon control carries the overlay, mouse-through and exactly the
    // size of the button, and a hover starts a sweep that a leave cancels.
    // The overlay is there, sized to the button and mouse-through.
    const auto wired = [&](QWidget* b, const char* what) {
      QWidget* fx = overlayOf(b);
      QVERIFY2(fx, qPrintable(QString("%1: no shimmer overlay").arg(what)));
      QCOMPARE(fx->geometry(), b->rect());
      QVERIFY2(fx->testAttribute(Qt::WA_TransparentForMouseEvents),
               qPrintable(QString("%1: the overlay would eat clicks").arg(what)));
    };
    // …and a hover sweeps it, a leave cancels at once.
    const auto sweeps = [&](QWidget* b, const char* what) {
      wired(b, what);
      QWidget* fx = overlayOf(b);
      QVERIFY(fx);
      hoverEnter(b);
      QTRY_VERIFY2(fx->property("sweepProgress").toReal() >= 0.0,
                   qPrintable(QString("%1: hover started no sweep").arg(what)));
      QEvent leave(QEvent::Leave);
      QApplication::sendEvent(b, &leave);
      QCOMPARE(fx->property("sweepProgress").toReal(), -1.0);   // cancelled at once
    };

    // An ENABLED dock control (the composer's send is disabled on an empty box —
    // it is the disabled case below).
    QToolButton* live = nullptr;
    for (QToolButton* b : win.chatDock_->findChildren<QToolButton*>())
      if (b->isVisible() && b->isEnabled() && overlayOf(b)) { live = b; break; }
    QVERIFY2(live, "no shimmered enabled button in the chat dock");
    sweeps(live, "dock composer/header button");

    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardUser")) card = f;
    QVERIFY(card);
    auto* more = qobject_cast<QToolButton*>(card->property("chatMoreBtn").value<QObject*>());
    QVERIFY2(more, "the card has no \"…\"");
    more->show();   // normally revealed by the card's own hover
    // Presence only: the "…" LIFTS itself 1px on hover, and offscreen QPA (which
    // has no real cursor to keep inside the moved button) answers that move with
    // a synthetic Leave that cancels the sweep. A real pointer stays inside it.
    wired(more, "row-menu \"…\"");

    QToolButton* panelBtn = nullptr;
    for (QToolButton* b : win.chatMenuPanel_->findChildren<QToolButton*>())
      if (b->isEnabled() && overlayOf(b)) { panelBtn = b; break; }
    QVERIFY2(panelBtn, "no shimmered button in the menu panel");
    sweeps(panelBtn, "menu panel composer button");

    // A DISABLED control stays quiet: send, with nothing typed.
    QToolButton* send = win.chatDock_->findChild<QToolButton*>("chatSend");
    QVERIFY(send);
    QVERIFY2(!send->isEnabled(), "the empty composer's send should be disabled");
    QWidget* sendFx = overlayOf(send);
    QVERIFY2(sendFx, "the send button lost its shimmer overlay");
    hoverEnter(send);
    QCOMPARE(sendFx->property("sweepProgress").toReal(), -1.0);
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "mainWindow.chatCompact.gui.moc"
