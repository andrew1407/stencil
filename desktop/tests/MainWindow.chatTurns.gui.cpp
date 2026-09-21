// MainWindow GUI e2e — Stopping a busy turn, and the conversation persisting with its project.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // While a turn is in flight the send button becomes STOP and Enter is a no-op (single-turn
  // guard); STOP turns the pending "…" into a muted "Stopped." with no history push and no toast.
  void chatStopWhileBusy() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    auto* input = dock->findChild<QPlainTextEdit*>("chatInput");
    auto* send = dock->findChild<QToolButton*>("chatSend");
    QVERIFY(input && send);

    // Simulate an in-flight turn: exactly the state onChatSend sets up.
    win.chatDock->showPending();
    win.chatDock->setBusy(true);
    QVERIFY(send->isEnabled());  // STOP mode is always clickable
    QCOMPARE(send->toolTip(), QString("Stop the response"));

    // Enter while busy is ignored (single-turn): the input keeps its text and
    // no send fires (a send would clear it).
    input->setPlainText("second question");
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(input->toPlainText(), QString("second question"));
    input->clear();

    // Click STOP → the abort flag is set; then the canceled reply lands.
    const int histBefore = win.chatHistory.size();
    QTest::mouseClick(send, Qt::LeftButton);
    QVERIFY(win.chatStopRequested);
    win.chatDock->setBusy(false);  // what the chat completion wrapper does
    stencil::llm::LlmReply canceled;
    canceled.ok = false;
    canceled.failure = stencil::llm::LlmFailure::TRANSPORT;
    canceled.error = "Operation canceled";
    win.onChatReply(canceled);

    // The pending card became "Stopped."; nothing was pushed or toasted.
    bool stoppedShown = false;
    for (QLabel* l : dock->findChildren<QLabel*>())
      if (l->text() == QString("Stopped.")) stoppedShown = true;
    QVERIFY(stoppedShown);
    QCOMPARE(win.chatHistory.size(), histBefore);
    auto* toast = win.findChild<QWidget*>("chatToast");
    QVERIFY(!toast || !toast->isVisible());

    // Composer back to normal: send glyph/tooltip restored, guard cleared.
    QVERIFY(!win.chatStopRequested);
    QCOMPARE(send->toolTip(), QString());
    QVERIFY(!send->isEnabled());  // idle + empty input gates send again
    input->setPlainText("hello");
    QVERIFY(send->isEnabled());
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

  // Chat persistence (llm-contract §12): with the opt-in ON a settled conversation is filed on the
  // active LOCAL project and replays on reopen, the trash deletes it, and with it OFF nothing is.
  void chatPersistsWithProject() {
    using stencil::gui::fileStore::parseChatDoc;
    using stencil::gui::Project;
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    win.settings.saveChatsWithProject = true;

    // A local project to file the chat under.
    win.adoptCanvasAsLocalProject();
    QVERIFY(!win.activeProjectId.isEmpty());
    const QString projectId = win.activeProjectId;

    // A settled turn: history push + the persist that onChatReply's tail runs.
    stencil::llm::ChatMessage u;
    u.role = "user";
    u.text = "make it sepia";
    stencil::llm::ChatMessage a;
    a.role = "assistant";
    a.text = "Sepia applied.";
    win.pushChatHistory(u);
    win.pushChatHistory(a);
    // The doc is built from what was DISPLAYED (§12.1), so mirror the two rows
    // the send/reply paths would have posted.
    win.chatMirror("You", u.text, false);
    win.chatMirror("Assistant", a.text, false);
    win.persistActiveChat();
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY(pr);
      QCOMPARE(parseChatDoc(pr->chat).size(), 2);   // saved, text-only, in order
    }

    // Reopening the project replays the saved conversation: replay history AND
    // dock transcript cards (restoreChatFromDoc via loadProjectIntoCanvas).
    win.resetChatState();
    QVERIFY(win.chatHistory.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(projectId));
    QCOMPARE(win.chatHistory.size(), 2);
    QCOMPARE(win.chatHistory.last().text, QString("Sepia applied."));
    {
      auto* dock = qobject_cast<stencil::gui::ChatDock*>(
          win.findChild<QDockWidget*>("llmChatDock"));
      QVERIFY(dock);
      auto* scrollArea = dock->findChild<QScrollArea*>();
      QVERIFY(scrollArea && scrollArea->widget());
      const auto cards = scrollArea->widget()->findChildren<QFrame*>(
          QString(), Qt::FindDirectChildrenOnly);
      QCOMPARE(cards.size(), 2);   // one card per restored turn
    }

    // The trash clears the persisted copy too (§12.2).
    win.onChatClear();
    QVERIFY(win.chatHistory.isEmpty());
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY(pr && pr->chat.isEmpty());
    }

    // Opt-in OFF (the default): a turn leaves the record untouched.
    win.settings.saveChatsWithProject = false;
    win.pushChatHistory(u);
    win.persistActiveChat();
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY(pr && pr->chat.isEmpty());
    }

    // Tidy the dev state dir: drop the project this test created.
    dismissModal("OK");
    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurns.gui.moc"
