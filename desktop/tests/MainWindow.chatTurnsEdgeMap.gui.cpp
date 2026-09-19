// MainWindow GUI e2e — Clear-chat deferring its confirm, and the edge map riding along a turn.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // §10 clearChat from chat: DEFERRED (the plan's other action runs first even
  // when clearChat is listed first) and always confirmed. Declined: a "clear
  // canceled" note, everything kept. Accepted: dock transcript, chatHistory_,
  // the §12 persisted copy AND the §7 text-only latch all clear.
  void chatClearChatDefersConfirmsAndClears() {
    using stencil::gui::Project;
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    win.settings_.saveChatsWithProject = true;
    MockChatTransport mock;
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    // A local project to file the persisted copy under (§12).
    win.adoptCanvasAsLocalProject();
    QVERIFY(!win.activeProjectId_.isEmpty());
    const QString projectId = win.activeProjectId_;
    const auto wrap = [](const char* plan) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", plan}}}})
          .toJson(QJsonDocument::Compact);
    };

    // Declined round: clearChat FIRST, units second — the units op still runs
    // (deferral), and the decline lands as a note with everything kept.
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Inches it is — clearing next.\",\"actions\":["
        "{\"op\":\"clearChat\"},{\"op\":\"units\",\"value\":\"in\"}]}"));
    // The deferred confirm is QUEUED at turn end, so arm the dismissal AFTER
    // the send: its poll then runs inside the modal's own event loop.
    win.onChatSend("switch to inches, then clear the chat");
    dismissModal("Cancel");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_VERIFY2(chatTranscriptHas(dock, "clear canceled"),
                 "a declined confirm must land as a note");
    QCOMPARE(win.settings_.units, QString("in"));  // ran despite being listed second
    win.applyUnits("cm");                          // tidy the persisted setting
    QCOMPARE(win.chatHistory_.size(), 2);          // user + assistant kept
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY2(pr && !pr->chat.isEmpty(), "the persisted copy must survive a decline");
    }

    // Accepted round: transcript + history + persisted copy go, latch re-arms.
    win.chatTextOnlyKey_ = QStringLiteral("some|other|model");
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Clearing.\",\"actions\":[{\"op\":\"clearChat\"}]}"));
    win.onChatSend("clear the chat");
    dismissModal("OK");   // after the send — the confirm is queued (see above)
    QTRY_VERIFY(!dock->isBusy());
    QTRY_VERIFY2(win.chatHistory_.isEmpty(), "the replay history must clear");
    QTRY_VERIFY2(assistantBubbleTexts(dock).isEmpty(), "the transcript must clear");
    QVERIFY2(win.chatTextOnlyKey_.isEmpty(), "the §7 text-only latch must re-arm");
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY2(pr && pr->chat.isEmpty(), "the §12 persisted copy must clear (§12.2)");
    }

    // Tidy the dev state dir: drop the project this test created.
    dismissModal("OK");
    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
    beat();
  }

  // §7 edge map: a turn that carries the working snapshot carries a SECOND
  // image — the contour render — directly after it, with the exact suffix
  // sentence riding along, and the edge map is never replayed on later turns.
  // §3.0: a layout answer ends the turn, so ONE request goes out — the edge map
  // belongs to the main turn and to nothing else.
  void chatEdgeMapRidesAlong() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.canvas_->hasImage());
    // The fixture is a flat white image; a perfectly uniform snapshot's contour render
    // can coincide with the plain snapshot bit-for-bit (applyContourRGBA maps ANY
    // uniform image to solid white). One line breaks the uniformity so the edge map
    // and the plain snapshot are guaranteed to differ, regardless of that overlap.
    stencil::core::Line line;
    line.color = "#000000";
    line.thickness = 4;
    line.points.push_back({20.0, 20.0});
    line.points.push_back({100.0, 100.0});
    win.canvas_->setLines({line});
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Outlined.\",\"actions\":[{\"op\":\"layout\","
                      "\"lines\":[{\"points\":[{\"x\":40,\"y\":40},{\"x\":80,\"y\":40},"
                      "{\"x\":80,\"y\":80},{\"x\":40,\"y\":40}]}]}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("outline the box");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QCOMPARE(mock.allBodies.size(), 1);   // the turn, and nothing behind it

    const QString sentence =
        "The second attached image is an edge-map render of the working image at the "
        "same pixel coordinates: use it to place outline points on real edges.";
    const QJsonArray msgs = mock.allBodies.first().value("messages").toArray();
    const QJsonArray images = msgs.last().toObject().value("images").toArray();
    QCOMPARE(images.size(), 2);   // snapshot first, edge map second
    QVERIFY2(images.at(0).toString() != images.at(1).toString(),
             "the edge map must be a distinct (contoured) render");
    const QString sys = msgs.at(0).toObject().value("content").toString();
    QVERIFY2(sys.endsWith(sentence), "suffix must end with the exact edge-map sentence");
    QCOMPARE(sys.count(sentence), qsizetype(1));

    // The next turn replays the PRIOR turn's snapshot — never its edge map.
    mock.allBodies.clear();
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"ok\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.onChatSend("thanks");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    const QJsonArray msgs2 = mock.allBodies.first().value("messages").toArray();
    // system, user1, assistant1, user2: the replayed user1 keeps exactly its
    // snapshot; the fresh turn carries snapshot + edge map again.
    QCOMPARE(msgs2.at(1).toObject().value("role").toString(), QString("user"));
    const QJsonArray prior = msgs2.at(1).toObject().value("images").toArray();
    QCOMPARE(prior.size(), 1);
    QCOMPARE(prior.at(0).toString(), images.at(0).toString());   // the snapshot
    QCOMPARE(msgs2.last().toObject().value("images").toArray().size(), 2);
    beat();
  }

  // §7 auto-continuation: the re-sent round carries the NEW working snapshot
  // plus its edge map (browser parity), with the exact suffix sentence — while
  // the imageless first round carried neither image nor sentence.
  void chatEdgeMapOnContinuation() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(!win.canvas_->hasImage());   // empty editor: turn 1 has no snapshot
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    // A load-only plan (blank) triggers the single §7 continuation round. A
    // COLOURED blank: the edge map contours to flat white, so a white blank's
    // snapshot would coincide with it byte-for-byte and void the ≠ check below.
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Blank page.\",\"actions\":"
                      "[{\"op\":\"blank\",\"color\":\"#3366cc\",\"format\":\"a6\"}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("give me a blank a6 page");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(mock.allBodies.size(), 2);   // the turn + exactly one continuation

    const QString sentence =
        "The second attached image is an edge-map render of the working image at the "
        "same pixel coordinates: use it to place outline points on real edges.";
    const QJsonArray msgs1 = mock.allBodies.first().value("messages").toArray();
    QVERIFY(!msgs1.last().toObject().contains("images"));   // nothing to snapshot yet
    QVERIFY(!msgs1.at(0).toObject().value("content").toString().contains(sentence));

    const QJsonArray msgs2 = mock.allBodies.at(1).value("messages").toArray();
    const QJsonArray images = msgs2.last().toObject().value("images").toArray();
    QCOMPARE(images.size(), 2);   // fresh snapshot + its edge map
    QVERIFY(images.at(0).toString() != images.at(1).toString());
    QVERIFY2(msgs2.at(0).toObject().value("content").toString().endsWith(sentence),
             "continuation suffix must end with the exact edge-map sentence");
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsEdgeMap.gui.moc"
