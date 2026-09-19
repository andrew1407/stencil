// MainWindow GUI e2e — The transcript following sends and pinned replies, and one round per layout turn.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Transcript follow (chat stickiness): a send scrolls fully down to the pending "…"; a reply
  // follows only while the view already sits at the bottom, never yanking a reader upward.
  void chatTranscriptFollowsSendsAndPinnedReplies() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.chatDock_->show();
    settleLayout(win.chatDock_, 30);
    auto* scroll = win.chatDock_->findChild<QScrollArea*>();
    QVERIFY(scroll);
    auto* bar = scroll->verticalScrollBar();
    const QString filler = QStringLiteral(
        "Filler turn %1 — long enough to wrap across the bubble and give the "
        "transcript real scrollable height for the follow assertions below.");
    for (int i = 0; i < 8; ++i) {
      win.chatDock_->appendUser(filler.arg(i));
      win.chatDock_->appendAssistant(filler.arg(i + 100));
    }
    QTRY_VERIFY(bar->maximum() > 0);
    // A send lands the view at the very bottom, where the "…" card sits.
    bar->setValue(0);
    win.chatDock_->appendUser(QStringLiteral("newest question"));
    win.chatDock_->showPending();
    QTRY_VERIFY2(bar->maximum() > 0 && bar->value() == bar->maximum(),
                 "sending must scroll to the pending indicator at the bottom");
    QTest::qWait(50);   // drain the deferred scroll timers before scrolling away
    // Reading history releases the pin: a landing reply must not yank the view.
    bar->setValue(0);
    win.chatDock_->clearPending();
    win.chatDock_->appendAssistant(QStringLiteral("a reply landing mid-history"));
    QTest::qWait(80);
    QVERIFY2(bar->value() < bar->maximum() / 2,
             "a reply must not yank a reader back down from history");
    // Back at the bottom the pin re-arms: the next reply is followed.
    bar->setValue(bar->maximum());
    win.chatDock_->appendAssistant(QStringLiteral("and one the reader follows"));
    QTRY_VERIFY(bar->maximum() > 0 && bar->value() == bar->maximum());
    beat();
  }

  // §3.0: a turn ends when its plan has executed and its reply is shown — one model round, nothing
  // after it; no re-trace request per line and no whole-layout self-checks.
  void chatLayoutTurnIssuesExactlyOneRound() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.canvas_->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";

    // 17 small boxes — every one of them would have earned its own refinement
    // request, and the spread would have earned suspect rounds on top.
    QStringList lines;
    for (int i = 0; i < 17; ++i) {
      const int x = 5 + (i % 6) * 30, y = 5 + (i / 6) * 30;
      lines << QStringLiteral("{\"points\":[{\"x\":%1,\"y\":%2},{\"x\":%3,\"y\":%2},"
                              "{\"x\":%3,\"y\":%4},{\"x\":%1,\"y\":%2}]}")
                   .arg(x).arg(y).arg(x + 20).arg(y + 20);
    }
    MockChatTransport mock;
    mock.response =
        QJsonDocument(
            QJsonObject{{"message",
                         QJsonObject{{"content",
                                      QStringLiteral("{\"version\":1,\"reply\":\"Outlined.\","
                                                     "\"actions\":[{\"op\":\"layout\",\"lines\":[%1]}]}")
                                          .arg(lines.join(QLatin1Char(',')))}}}})
            .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    QElapsedTimer clock;
    clock.start();
    win.onChatSend("outline every box");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    const qint64 settledMs = clock.elapsed();

    QCOMPARE(mock.allBodies.size(), 1);   // ONE round for the whole turn
    QCOMPARE(int(win.canvas_->allLines().size()), 17);   // …and it drew all of them
    // Nothing may be queued behind the reply either: the turn is over.
    QTest::qWait(200);
    QCOMPARE(mock.allBodies.size(), 1);
    QVERIFY2(settledMs < 2000, "the turn must settle with its reply, not minutes later");

    // No self-check / sharpening vocabulary may reach the user, in the transcript
    // or in the toast.
    const QString shown = dockText(win.chatDock_) +
                          (win.chatToast_ ? dockText(win.chatToast_) : QString());
    for (const char* word : {"sharpen", "self-check", "re-checked", "correction", "refine"})
      QVERIFY2(!shown.contains(QLatin1String(word), Qt::CaseInsensitive),
               qPrintable(QString("the reply still mentions \"%1\": %2")
                              .arg(QLatin1String(word), shown.simplified())));
    beat();
  }

  // The same rule seen from the wire: with the transport HOLDING the answer, exactly
  // one request is ever made — answering it settles the turn and parks nothing new.
  void chatLayoutTurnParksNothingAfterTheAnswer() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.canvas_->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";

    DeferredChatTransport deferred;
    deferred.response =
        QJsonDocument(QJsonObject{
            {"message",
             QJsonObject{{"content",
                          "{\"version\":1,\"reply\":\"Outlined.\",\"actions\":[{\"op\":\"layout\","
                          "\"lines\":[{\"points\":[{\"x\":20,\"y\":20},{\"x\":50,\"y\":20},"
                          "{\"x\":50,\"y\":50},{\"x\":20,\"y\":20}]}]}]}"}}}})
            .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&deferred);
    win.onChatSend("outline the box");
    QCOMPARE(deferred.parked.size(), 1);   // the turn's own request, waiting
    QVERIFY(win.chatDock_->isBusy());

    deferred.answerNext();                 // …the answer lands
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QCOMPARE(int(win.canvas_->allLines().size()), 1);
    QTest::qWait(200);
    QVERIFY2(deferred.parked.isEmpty(), "a follow-up round was sent behind the reply");
    QCOMPARE(deferred.started, 1);
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsLayout.gui.moc"
