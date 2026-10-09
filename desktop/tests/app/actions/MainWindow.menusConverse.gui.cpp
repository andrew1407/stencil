// MainWindow GUI e2e — A conversation driven from the Assistant flyout, which never dismisses the menu.
// Shared ground is in menusAssistant.gui.hpp, over MainWindow.gui.hpp.
#include "menusAssistant.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Typing, sending, stopping and the canceled turn all leave the menu standing; the turn lands
  // in the SHARED history and the dock, survives reopening the menu, and the trash clears both.
  void assistantConversationLeavesTheMenuOpen() {
    MainWindow win(nullptr, false);
    QVERIFY(showWithWorkingImage(win));
    MockChatTransport mock;
    armSepiaAssistant(win.settings, win.parts.chatAppliers.llmClient, mock);
    // The menu's attach button feeds the DOCK's attachment state; stage one
    // there and the menu-driven turn must carry it (working image + this one).
    QImage att(12, 8, QImage::Format_RGB32);
    att.fill(Qt::green);
    win.chatDock->addAttachmentImage(att);

    bool typedThrough = false, subAliveAfterSend = false, rootAliveAfterSend = false;
    bool sendWasStop = false, stopSeen = false, stoppedRowSeen = false;
    bool escClosedSub = false, chipsHiddenAfterSend = true;
    int rowsAfterSend = 0, postedImages = 0;
    QString posted;
    bool assistantOpened = false;
    QTimer::singleShot(0, [&] {
      const AssistantFlyout f = openAssistantFlyout();
      assistantOpened = f.sub != nullptr;
      if (!f.complete()) return;
      QMenu* menu = f.menu;
      QMenu* sub = f.sub;
      QWidget* panel = f.panel;
      QPlainTextEdit* input = f.input;
      QToolButton* sendBtn = f.send;
      // Click into the input THROUGH the menu (the real popup path): focus
      // lands there and the submenu does not close.
      QTest::mouseClick(sub, Qt::LeftButton, {},
                        input->mapTo(sub, input->rect().center()));
      // Typing goes to the INPUT, not the menu's key navigation.
      QTest::keyClicks(sub, "make it sepia");
      typedThrough = input->toPlainText() == QString("make it sepia");
      // Enter sends instead of activating the highlighted menu item.
      QTest::keyClick(sub, Qt::Key_Return);
      posted = QString::fromUtf8(QJsonDocument(mock.body).toJson(QJsonDocument::Compact));
      const QJsonArray msgs = mock.body.value("messages").toArray();
      if (!msgs.isEmpty())
        postedImages = msgs.last().toObject().value("images").toArray().size();
      subAliveAfterSend = sub->isVisible();
      rootAliveAfterSend = menu->isVisible();
      rowsAfterSend = panel->findChildren<QLabel*>().size();
      if (auto* chipBox = panel->findChild<QWidget*>("chatSuggest"))
        chipsHiddenAfterSend = !chipBox->isVisible();

      // Busy → the send button becomes STOP; clicking it THROUGH the menu
      // aborts without closing anything.
      win.chatDock->setBusy(true);
      win.chatSession->chatMirrorBusy(true);
      win.chatSession->chatMirrorPending(true);
      sendWasStop = sendBtn->toolTip() == QString("Stop the response");
      QTest::mouseClick(sub, Qt::LeftButton, {},
                        sendBtn->mapTo(sub, sendBtn->rect().center()));
      stopSeen = win.chatSession->chatStopRequested && sub->isVisible() && menu->isVisible();
      // The canceled reply turns the in-flight row into a muted "Stopped.".
      win.chatDock->setBusy(false);
      win.chatSession->chatMirrorBusy(false);
      stencil::llm::LlmReply canceled;
      canceled.ok = false;
      canceled.failure = stencil::llm::LlmFailure::TRANSPORT;
      canceled.error = "Operation canceled";
      win.chatSession->onChatReply(canceled);
      for (QLabel* l : panel->findChildren<QLabel*>())
        if (l->text().contains("Stopped.")) stoppedRowSeen = true;

      // Escape belongs to the menu even with the input focused.
      QTest::keyClick(sub, Qt::Key_Escape);
      escClosedSub = !sub->isVisible();
      menu->close();
    });
    win.parts.canvasMenu.showContextMenu(win.mapToGlobal(QPoint(400, 300)));

    QVERIFY2(assistantOpened, "the Assistant submenu did not open");
    QVERIFY2(chipsHiddenAfterSend, "the chips survived the first message");
    QVERIFY2(typedThrough, "keys typed at the menu never reached the chat input");
    QVERIFY2(!posted.isEmpty() && posted.contains("make it sepia"),
             "Enter in the menu did not send through the shared LLM client");
    QVERIFY2(subAliveAfterSend && rootAliveAfterSend, "the menu closed on send");
    QVERIFY2(rowsAfterSend >= 2, "the menu transcript did not record the exchange");
    QVERIFY2(sendWasStop, "the menu send button did not become STOP while busy");
    QVERIFY2(stopSeen, "clicking STOP in the menu closed it or did not abort");
    QVERIFY2(stoppedRowSeen, "the canceled turn did not render as Stopped.");
    QVERIFY2(escClosedSub, "Escape did not close the assistant submenu");
    // Working image + its §7 edge map + the attachment staged on the dock: the
    // menu send goes through the same attachment state the attach button feeds.
    QCOMPARE(postedImages, 3);

    // ONE conversation: the turn typed in the menu is in the shared history AND
    // rendered in the dock.
    QCOMPARE(win.chatSession->chatHistory.size(), 2);  // user + assistant
    QCOMPARE(win.chatSession->chatHistory.first().text, QString("make it sepia"));
    bool dockSawIt = false;
    for (QLabel* l : win.chatDock->findChildren<QLabel*>())
      if (l->text().contains("make it sepia")) dockSawIt = true;
    QVERIFY2(dockSawIt, "the menu turn never reached the dock transcript");

    // The panel is a QWidgetAction owned by the WINDOW, so reopening the menu
    // (rebuilt from scratch on every right-click) keeps the transcript.
    bool survived = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = findMenu();
      if (!menu) return;
      QMenu* sub = openSubByKey(menu, "Assistant");
      if (sub)
        if (auto* panel = sub->findChild<QWidget*>("chatMenuPanel"))
          for (QLabel* l : panel->findChildren<QLabel*>())
            if (l->text().contains("make it sepia")) survived = true;
      menu->close();
    });
    win.parts.canvasMenu.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    QVERIFY2(survived, "reopening the menu lost the chat transcript");

    // The dock's trash clears both surfaces.
    win.chatSession->onChatClear();
    QVERIFY(win.chatSession->chatHistory.isEmpty());
    QVERIFY(win.chatSession->chatMenuPanel);
    // The mirrored rows scatter and then go (dock parity), so they leave on the
    // event loop — a hidden row is already on its way out and doesn't count.
    const auto menuRowsLeft = [&win] {
      for (QLabel* l : win.chatSession->chatMenuPanel->findChildren<QLabel*>())
        if (!l->isHidden() && l->text().contains("make it sepia")) return true;
      return false;
    };
    QTRY_VERIFY2(!menuRowsLeft(), "clearing the conversation left the menu transcript");

    win.parts.chatAppliers.llmClient.reset();  // drop the mock before it goes out of scope
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menusConverse.gui.moc"
