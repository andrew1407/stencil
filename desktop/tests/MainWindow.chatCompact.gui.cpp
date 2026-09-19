// MainWindow GUI e2e — The compact popover the toolbar icon opens, and the settings shortcut beside it.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The assistant's settings have a chord of their own from the shared registry: it opens the
  // assistant-only dialog with no chat surface up, and pressed inside it closes it.
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

  // The chat toolbar icon answers every dialog icon's popover gestures (browser chatPanel.js
  // openCompact): a double-click opens the compact float and re-pins it; a click toggles.
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

    // A real double-click is press, release, dblclick, release — all four must go through the
    // gesture filter (QTest::mouseDClick's internal wait becomes a plain click first).
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

    // While a text control has FOCUS, Alt belongs to the typing: over the icon it must NOT peek.
    // Checked FIRST — floating-dock activation, which offscreen QPA cannot give, unsets focus.
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

    // Alt while hovering (the peek route, browser altHover parity): the compact float opens with
    // no click, and it is HOLD-to-peek — releasing Alt closes it again.
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

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatCompact.gui.moc"
