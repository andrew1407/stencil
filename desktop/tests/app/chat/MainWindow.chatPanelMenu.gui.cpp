// MainWindow GUI e2e — The menu panel: existing history, its bubble sizing, and the dock transcript it mirrors.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A panel created LATE must render the conversation that already happened —
  // chat in the dock first, then open the context menu for the first time.
  void chatMenuPanelRendersExistingHistory() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = "ollama";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"later reply\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient = std::make_unique<stencil::llm::LlmClient>(&mock);
    QVERIFY(!win.chatMenuPanel);  // never built yet

    auto* chat = win.findChild<QAction*>("actChat");
    chat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    auto* dockInput = win.chatDock->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(dockInput);
    dockInput->setPlainText("said before the menu existed");
    QTest::keyClick(dockInput, Qt::Key_Return);
    QCOMPARE(win.chatHistory.size(), 2);

    win.ensureChatMenuPanel();  // first time the menu is needed
    QVERIFY(win.chatMenuPanel);
    QStringList bodies;
    for (QLabel* l : win.chatMenuPanel->findChildren<QLabel*>())
      if (!l->property("chatRole").toString().isEmpty())
        bodies << l->property("chatBody").toString();
    QCOMPARE(bodies.size(), 2);
    QCOMPARE(bodies.at(0), QString("said before the menu existed"));
    QCOMPARE(bodies.at(1), QString("later reply"));

    win.llmClient.reset();
    beat();
  }

  // The panel is built LAZILY inside a hidden QWidgetAction, so it re-measures rows on the way in
  // rather than leaving them at the default 100px viewport; its pending row bounces the dots.
  void chatMenuPanelSizesBubblesAndAnimatesPending() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = "ollama";
    win.settings.llmBaseUrl = "http://localhost:11434";
    win.ensureChatMenuPanel();
    QVERIFY(win.chatMenuPanel);
    // Mirrored while the panel is still HIDDEN — the state that collapsed them.
    const QString longText = QStringLiteral(
        "Loading the image into incognito, converting to black & white and cropping to "
        "portrait 3:4. Once it is done I will report back with the result.");
    for (int i = 0; i < 4; ++i) {
      win.chatMirror(QStringLiteral("You"), longText, false);
      win.chatMirror(QStringLiteral("Assistant"), longText, false);
    }
    win.chatMenuPanel->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel->show();
    settleLayout(win.chatMenuPanel, 300);

    auto* scroll = win.chatMenuPanel->findChild<QScrollArea*>("chatMenuTranscript");
    QVERIFY(scroll);
    const int avail = scroll->viewport()->width();
    QVERIFY2(avail > 100, "the panel transcript never got a real width");
    int checked = 0;
    for (QFrame* card : win.chatMenuPanel->findChildren<QFrame*>()) {
      if (!card->property("chatMoreBtn").isValid()) continue;   // rows only
      ++checked;
      QVERIFY2(card->width() > avail / 2,
               qPrintable(QString("a mirrored bubble collapsed to %1 of %2 px")
                              .arg(card->width())
                              .arg(avail)));
      QVERIFY2(card->width() <= avail, "a bubble overflowed the transcript");
    }
    QVERIFY2(checked >= 4, "no mirrored rows to measure");

    // …and the pending row animates: the shared dots widget, with its own timer.
    win.chatMirrorPending(true);
    QTRY_VERIFY2(win.chatMenuPanel->findChild<QWidget*>(QStringLiteral("chatTypingDots")),
                 "the panel's pending row has no typing dots");
    QWidget* dots = win.chatMenuPanel->findChild<QWidget*>(QStringLiteral("chatTypingDots"));
    QTRY_VERIFY2(dots->isVisible(), "the typing dots are not on screen");
    // It really MOVES: sample the painted frame twice.
    const QImage a = dots->grab().toImage();
    QTest::qWait(160);
    const QImage b = dots->grab().toImage();
    QVERIFY2(a != b, "the typing dots are static");
    // Stopping swaps them for the text, as in the dock.
    win.chatMirrorStopped(QStringLiteral("retry me"));
    QTRY_VERIFY2(!win.chatMenuPanel->findChild<QWidget*>(QStringLiteral("chatTypingDots")),
                 "the dots outlived the turn");
    beat();
  }

  // The context-menu Assistant panel renders the DOCK's transcript, not a second ad-hoc one: the
  // same message texts in FULL, whichever surface sent them, and a clear empties both.
  void chatMenuPanelMirrorsTheDockTranscript() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = "ollama";
    win.settings.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const QString& reply) {
      return QJsonDocument(
                 QJsonObject{{"message",
                              QJsonObject{{"content",
                                           QString("{\"version\":1,\"reply\":\"%1\","
                                                   "\"actions\":[]}")
                                               .arg(reply)}}}})
          .toJson(QJsonDocument::Compact);
    };
    // Long enough that the old one-line elision would have cut it.
    const QString longUser = QStringLiteral(
        "please remove this project and then tell me what happened to the image "
        "I was looking at, in as many words as you can manage");
    const QString longReply = QStringLiteral(
        "Removed the working image and its lines; nothing was saved, so there was "
        "no project file to delete alongside it.");
    mock.response = wrap(longReply);
    win.llmClient = std::make_unique<stencil::llm::LlmClient>(&mock);

    auto* chat = win.findChild<QAction*>("actChat");
    chat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    auto* dockInput = win.chatDock->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(dockInput);
    dockInput->setPlainText(longUser);
    QTest::keyClick(dockInput, Qt::Key_Return);
    QTRY_COMPARE(win.chatHistory.size(), 2);

    win.ensureChatMenuPanel();
    QVERIFY(win.chatMenuPanel);
    // The pending "…" card and cards already handed to deleteLater are excluded
    // the way assistantBubbleTexts does it.
    const auto bodies = [](QWidget* surface) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QStringList out;
      for (QLabel* l : surface->findChildren<QLabel*>()) {
        const QString b = l->property("chatBody").toString();
        if (!b.isEmpty() && b != QStringLiteral("…")) out << b;
      }
      return out;
    };
    QCOMPARE(bodies(win.chatMenuPanel), bodies(win.chatDock));
    QVERIFY2(bodies(win.chatMenuPanel).contains(longReply), "the reply is missing from the panel");

    // Full text on screen, wrapped — not the old "Assistant: Op…" stub.
    bool sawFull = false;
    for (QLabel* l : win.chatMenuPanel->findChildren<QLabel*>()) {
      if (l->property("chatBody").toString() != longReply) continue;
      sawFull = true;
      QCOMPARE(l->text(), longReply);            // never elided
      QVERIFY2(l->wordWrap(), "a panel row must wrap, not elide");
      QVERIFY2(l->parentWidget()->objectName() == QLatin1String("chatCardAssistant"),
               "the panel row is not the dock's assistant card");
    }
    QVERIFY(sawFull);

    // A message sent from the PANEL lands in both surfaces too.
    mock.response = wrap(QStringLiteral("second reply"));
    auto* menuInput = qobject_cast<QPlainTextEdit*>(win.chatMenuInput);
    QVERIFY(menuInput);
    menuInput->setPlainText("sent from the menu");
    QTest::keyClick(menuInput, Qt::Key_Return);
    QTRY_COMPARE(win.chatHistory.size(), 4);
    QTRY_VERIFY(bodies(win.chatDock).contains(QStringLiteral("sent from the menu")));
    QVERIFY(bodies(win.chatMenuPanel).contains(QStringLiteral("sent from the menu")));
    QCOMPARE(bodies(win.chatMenuPanel), bodies(win.chatDock));

    // Clearing the conversation empties BOTH views.
    win.onChatClear();
    win.chatDock->clearConversation();
    QTRY_VERIFY(bodies(win.chatMenuPanel).isEmpty());
    QTRY_VERIFY(bodies(win.chatDock).isEmpty());

    win.llmClient.reset();
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatPanelMenu.gui.moc"
