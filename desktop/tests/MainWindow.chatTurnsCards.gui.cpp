// MainWindow GUI e2e — The expired-session card offering reconnect, and model text shown literally.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A 401 from the SERVER provider is an expired session: the card says which server and carries a
  // "Reconnect to <host>" that opens Connections. A local provider's 401 stays an error card.
  void chatExpiredSessionCardOffersReconnect() {
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.chatHistory_.append({QStringLiteral("user"), QStringLiteral("crop it"), {}});

    stencil::llm::LlmReply expired;
    expired.ok = false;
    expired.failure = stencil::llm::LlmFailure::EXPIRED;
    expired.expiredHost = QStringLiteral("localhost:8090");
    expired.error = QStringLiteral(
        "Your session on localhost:8090 has expired — reconnect to that server, then "
        "send this again.");
    win.onChatReply(expired);
    QTRY_VERIFY2(win.chatDock_->findChild<QFrame*>("chatCardError"),
                 "no error card for the expired session");
    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardError")) card = f;
    QVERIFY2(card, "no error card for the expired session");
    bool saidIt = false;
    for (QLabel* l : card->findChildren<QLabel*>())
      if (l->property("chatBody").toString().contains(QStringLiteral("has expired")) &&
          l->property("chatBody").toString().contains(QStringLiteral("localhost:8090")))
        saidIt = true;
    QVERIFY2(saidIt, "the card does not name the server or say the session expired");
    auto* cta = card->findChild<QPushButton*>(QStringLiteral("chatReconnectCta"));
    QVERIFY2(cta, "no Reconnect CTA on the expired card");
    QCOMPARE(cta->text(), QStringLiteral("Reconnect to localhost:8090"));
    QVERIFY2(card->findChild<QToolButton*>("chatRetry"),
             "the turn should still be resendable after signing in");

    // The CTA opens Connections (dismissed straight away here).
    bool opened = false;
    QTimer::singleShot(0, [&opened] {
      for (int i = 0; i < 80; ++i) {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
          opened = true;
          d->reject();
          return;
        }
        QTest::qWait(5);
      }
    });
    cta->click();
    QTRY_VERIFY2(opened, "the CTA did not open Connections");

    // …and an ordinary failure keeps the plain card (no CTA).
    stencil::llm::LlmReply plain;
    plain.ok = false;
    plain.failure = stencil::llm::LlmFailure::HTTP;
    plain.error = QStringLiteral("localhost:11434 answered: HTTP 401");
    win.onChatReply(plain);
    QTRY_COMPARE(win.chatDock_->findChildren<QFrame*>("chatCardError").size(), 2);
    QFrame* last = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardError")) last = f;
    QVERIFY(last);
    QVERIFY2(!last->findChild<QPushButton*>(QStringLiteral("chatReconnectCta")),
             "a local provider's 401 must not offer a server reconnect");
    beat();
  }

  void chatShowsModelTextLiterally() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";

    // The img is what would reach QTextDocument's resource loader if interpreted.
    const QString reply = QStringLiteral("<b>done</b> <img src=\"/etc/passwd\">");
    const QString question = QStringLiteral("<i>Which</i> one?");
    const QString option = QStringLiteral("<u>the first</u>");

    MockChatTransport mock;
    const QJsonObject plan{
        {"version", 1},
        {"reply", reply},
        // Nothing to run: asking INSTEAD of acting is the §11 case, and the chat-only path must still
        // render the card. Options carry no "actions", so it needs no loaded image for previews.
        {"actions", QJsonArray{}},
        {"ask", QJsonObject{{"question", question},
                            {"mode", "single"},
                            {"options", QJsonArray{QJsonObject{{"label", option}},
                                                   QJsonObject{{"label", "the second"}}}}}},
    };
    mock.response =
        QJsonDocument(QJsonObject{
                          {"message",
                           QJsonObject{{"content", QString::fromUtf8(
                                                       QJsonDocument(plan).toJson(QJsonDocument::Compact))}}}})
            .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    auto* chat = win.findChild<QAction*>("actChat");
    chat->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    auto* dockInput = win.chatDock_->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(dockInput);
    dockInput->setPlainText("go");
    QTest::keyClick(dockInput, Qt::Key_Return);
    QCOMPARE(win.chatHistory_.size(), 2);
    win.ensureChatMenuPanel();
    QVERIFY(win.chatMenuPanel_);

    // Every transcript row, on BOTH surfaces, is identified by its property —
    // not by where it sits — so a restyle can't quietly drop this from cover.
    int bodies = 0;
    for (QWidget* surface : {static_cast<QWidget*>(win.chatDock_),
                             static_cast<QWidget*>(win.chatMenuPanel_)}) {
      for (QLabel* l : surface->findChildren<QLabel*>()) {
        if (l->property("chatBody").toString().isEmpty()) continue;
        ++bodies;
        QVERIFY2(l->textFormat() == Qt::PlainText,
                 qPrintable(QStringLiteral("a transcript row renders model text as %1, not PlainText: %2")
                                .arg(int(l->textFormat()))
                                .arg(l->property("chatBody").toString())));
      }
    }
    QVERIFY2(bodies >= 4, "expected the user + assistant row on each of the two surfaces");

    // The assistant row shows the tags themselves; interpreted markup would leave text() holding the
    // source while the SCREEN showed bold, so both the format and the round-trip are asserted.
    bool sawReply = false, sawQuestion = false, sawOption = false;
    for (QLabel* l : win.chatDock_->findChildren<QLabel*>()) {
      if (l->text() == reply) { sawReply = true; QCOMPARE(l->textFormat(), Qt::PlainText); }
      if (l->text() == question) { sawQuestion = true; QCOMPARE(l->textFormat(), Qt::PlainText); }
      if (l->text() == option) { sawOption = true; QCOMPARE(l->textFormat(), Qt::PlainText); }
    }
    QVERIFY2(sawReply, "the assistant reply is not on screen as the literal text the model sent");
    QVERIFY2(sawQuestion, "the ask card's question is not on screen as literal text");
    QVERIFY2(sawOption, "the ask card's option label is not on screen as literal text");

    // The menu mirror carries the FULL text in the row itself (the dock's card
    // rendering — no tooltip, no elision), so the round-trip holds there too.
    bool checkedMirror = false;
    for (QLabel* l : win.chatMenuPanel_->findChildren<QLabel*>()) {
      if (l->property("chatBody").toString() != reply) continue;
      checkedMirror = true;
      QCOMPARE(l->text(), reply);
      QCOMPARE(l->textFormat(), Qt::PlainText);
    }
    QVERIFY2(checkedMirror, "no mirrored row carried the assistant reply");

    win.llmClient_.reset();
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsCards.gui.moc"
