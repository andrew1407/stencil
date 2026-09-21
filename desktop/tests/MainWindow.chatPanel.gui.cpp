// MainWindow GUI e2e — The panel's input focus, its word-delete, and the retroactive side swap.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Opening the assistant puts the caret in its box — otherwise the first thing you type
  // goes to the canvas shortcuts instead of the prompt you meant to write.
  void openingTheChatFocusesItsInput() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    win.actChat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(win.chatDock->input->hasFocus(), 3000);
    beat();
  }

  // A focused text box owns the standard editing chords: ⌥⌫ in the chat box deletes the word
  // behind the cursor, not the selected line, while the canvas action works everywhere else.
  void wordDeleteInTheChatBoxDeletesAWordNotALine() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    stencil::core::Lines seeded;                 // something for deleteLine to bite on
    seeded.push_back(stencil::core::Line{{{10, 10}, {40, 40}}});
    canvas->setLines(seeded);
    canvas->selectLineByIndex(0);
    const int lines = static_cast<int>(canvas->getLines().size());

    win.actChat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    QPlainTextEdit* input = win.chatDock->input;
    input->setFocus();
    QTRY_VERIFY(input->hasFocus());
    input->setPlainText(QStringLiteral("crop the portrait"));
    input->moveCursor(QTextCursor::End);

    QTest::keyClick(input, Qt::Key_Backspace, Qt::AltModifier);

    QCOMPARE(input->toPlainText(), QStringLiteral("crop the "));
    QCOMPARE(static_cast<int>(canvas->getLines().size()), lines);   // the drawing is untouched
    beat();
  }

  // Browser .chat-msg-user::before/::after parity: a settled bubble grows a tail at the corner
  // facing the panel centre, and "Swap message sides" flips every card already on screen.
  void chatSwapSidesReskinsRetroactively() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // Whatever this machine's settings.json already holds — restored at the end,
    // like every other test here that touches real persisted settings.
    const bool wasSwapped = win.settings.chatSwapSides;
    if (wasSwapped) win.chatDock->setChatSwapSides(false);   // start from a known state
    win.actChat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    win.chatDock->appendUser(QStringLiteral("hi"), {});
    win.chatDock->appendAssistant(QStringLiteral("hello"));
    // The entrance holds each card's opacity effect and drops the claim when it lands
    // (ENTERING_PROPERTY), so that IS the slide's own completion flag.
    QTRY_VERIFY(noneEntering(win.chatDock));
    // A short bubble's width can settle over a couple more layout passes (viewport/scrollbar
    // interplay in applyChatBubbleWidths), so one explicit re-sync makes the geometry checks firm.
    win.chatDock->applyBubbleWidths();

    QFrame* userCard = nullptr;
    QFrame* asstCard = nullptr;
    for (QFrame* f : win.chatDock->findChildren<QFrame*>("chatCardUser")) userCard = f;
    for (QFrame* f : win.chatDock->findChildren<QFrame*>("chatCardAssistant")) asstCard = f;
    QVERIFY(userCard && asstCard);
    auto* transcriptLayout = qobject_cast<QVBoxLayout*>(userCard->parentWidget()->layout());
    QVERIFY(transcriptLayout);
    // QBoxLayout carries a widget's alignment on the LayoutItem, not as a queryable property, so
    // find it by index — setAlignment(widget,…) is the only setter Qt offers.
    const auto alignmentOf = [](QVBoxLayout* lay, QWidget* w) {
      return lay->itemAt(lay->indexOf(w))->alignment();
    };
    QCOMPARE(alignmentOf(transcriptLayout, userCard), Qt::AlignRight);
    QCOMPARE(alignmentOf(transcriptLayout, asstCard), Qt::AlignLeft);

    // Every role bubble carries a tail — a real widget, positioned OUTSIDE the
    // card's own box on the side its alignment implies.
    const auto tailOf = [](QFrame* card) {
      return qobject_cast<QWidget*>(card->property("chatTail").value<QObject*>());
    };
    auto* userTail = tailOf(userCard);
    auto* asstTail = tailOf(asstCard);
    QVERIFY2(userTail && userTail->isVisible(), "the user bubble has no tail");
    QVERIFY2(asstTail && asstTail->isVisible(), "the assistant bubble has no tail");
    // The tail hangs flush from the bubble's BOTTOM edge and pokes past the corner facing the panel
    // centre — pinned as that invariant, not as the placement formula's exact pixel offsets.
    QVERIFY2(userTail->geometry().right() > userCard->geometry().right(),
             "the user bubble's tail must poke out past its RIGHT edge");
    QVERIFY2(qAbs(userTail->geometry().bottom() - userCard->geometry().bottom()) <= 2,
             "the tail must hang flush with the bubble's bottom edge");
    QVERIFY2(asstTail->geometry().left() < asstCard->geometry().left(),
             "the assistant bubble's tail must poke out past its LEFT edge");
    if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
      QTest::qWait(50);
      win.chatDock->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) + "/dock-tails.png");
    }

    // Flip it — through the REAL menu action, not the setter directly, so the
    // persistence signal is exercised too.
    bool signaled = false;
    bool signaledValue = false;
    connect(win.chatDock, &stencil::gui::ChatDock::chatSwapSidesChanged, &win,
            [&](bool on) { signaled = true; signaledValue = on; });
    auto* moreBtn = win.chatDock->findChild<QToolButton*>("chatMore");
    QVERIFY(moreBtn && moreBtn->menu());
    QAction* swapAction = nullptr;
    for (QAction* a : moreBtn->menu()->actions())
      if (a->text() == QLatin1String("Swap message sides")) swapAction = a;
    QVERIFY2(swapAction, "no \"Swap message sides\" action in the … menu");
    swapAction->trigger();

    QVERIFY2(signaled && signaledValue, "the dock must emit chatSwapSidesChanged(true)");
    QVERIFY2(win.settings.chatSwapSides, "MainWindow must persist the flip into settings");
    QVERIFY(win.chatDock->getChatSwapSides());

    // The EXISTING cards moved — this is the whole point (a browser/extension
    // parity CSS class would do this for free; Qt has to re-skin by hand).
    QCOMPARE(alignmentOf(transcriptLayout, userCard), Qt::AlignLeft);
    QCOMPARE(alignmentOf(transcriptLayout, asstCard), Qt::AlignRight);
    QVERIFY2(userTail->geometry().left() < userCard->geometry().left(),
             "swapped: the user bubble's tail must have moved to poke out past its LEFT edge");
    QVERIFY2(asstTail->geometry().right() > asstCard->geometry().right(),
             "swapped: the assistant bubble's tail must have moved to poke out past its RIGHT edge");

    // A round trip through the SAME file persistence every other setting uses.
    const stencil::gui::Settings loaded = stencil::gui::fileStore::loadSettings();
    QVERIFY2(loaded.chatSwapSides, "the flip must survive a settings.json round trip");

    // …and a NEWLY opened context-menu mirror panel picks up the SAME preference,
    // not the pre-flip default.
    win.ensureChatMenuPanel();
    win.chatMirror(QStringLiteral("Assistant"), QStringLiteral("mirrored"), false);
    QTRY_VERIFY(win.chatMenuPanel->findChild<QFrame*>("chatCardAssistant"));
    QFrame* mirroredAsst = nullptr;
    for (QFrame* f : win.chatMenuPanel->findChildren<QFrame*>("chatCardAssistant"))
      mirroredAsst = f;
    QVERIFY2(mirroredAsst, "the mirror panel never rendered the appended row");
    auto* mirrorLayout = qobject_cast<QVBoxLayout*>(mirroredAsst->parentWidget()->layout());
    QVERIFY(mirrorLayout);
    QCOMPARE(alignmentOf(mirrorLayout, mirroredAsst), Qt::AlignRight);

    // Restore whatever this machine's settings.json held before the test, exactly
    // (an even number of clicks isn't enough if it started true).
    win.chatDock->setChatSwapSides(wasSwapped);
    win.settings.chatSwapSides = wasSwapped;
    stencil::gui::fileStore::saveSettings(win.settings);
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatPanel.gui.moc"
