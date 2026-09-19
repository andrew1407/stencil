// MainWindow GUI e2e — Retry resending attachments, a follow-up adopting one, and multi-image saves.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Retry resends the failed turn WITH its attachments: the tray drained on the
  // first send, so the retry handler re-queues them — thumbnails on the resent
  // bubble, images back in the wire payload.
  void chatRetryResendsAttachments() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.status = 0;
    mock.netError = "network down";
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage att(24, 24, QImage::Format_RGB32);
    att.fill(Qt::green);
    dock->addAttachmentImage(att, "cat.png");
    win.onChatSend("highlight the cat");
    QTRY_VERIFY(!dock->isBusy());
    QCOMPARE(dock->attachedImages().size(), 0);   // the send drained the tray
    const int firstImages = mock.body.value("messages").toArray().last().toObject()
                                .value("images").toArray().size();
    QVERIFY2(firstImages >= 1, "the failed turn carried the attachment");
    // The network heals; the error card's retry resends the whole turn.
    mock.status = 200;
    mock.netError.clear();
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"ok\",\"actions\":[]}"}}}})
                       .toJson(QJsonDocument::Compact);
    QToolButton* retry = nullptr;
    for (QToolButton* b : dock->findChildren<QToolButton*>("chatRetry")) retry = b;
    QVERIFY2(retry, "no retry button on the failed turn");
    retry->click();
    QTRY_VERIFY(!dock->isBusy());
    const auto msgs = mock.body.value("messages").toArray();
    const int retryImages =
        msgs.last().toObject().value("images").toArray().size();
    QCOMPARE(retryImages, firstImages);   // the resend carries the image again
    QCOMPARE(dock->attachedImages().size(), 0);   // and drained normally after
    beat();
  }

  // A follow-up turn with NO new attachments (an ask-card answer) keeps the prior
  // turn's attachments: the editing plan it triggers still adopts that image on an
  // empty canvas (browser parity — turnAttachments only resets when new ones queue).
  void chatFollowUpAdoptsPriorAttachment() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    // Turn 1: attachment queued, but the model only asks back — nothing to run,
    // so nothing is adopted and the canvas stays empty.
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"Which photo first?\",\"actions\":[]}"}}}})
                       .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage att(64, 48, QImage::Format_RGB32);
    att.fill(Qt::darkMagenta);
    dock->addAttachmentImage(att, "cat.png");
    win.onChatSend("edit these photos");
    QTRY_VERIFY(!dock->isBusy());
    QVERIFY(!win.canvas_->hasImage());
    QCOMPARE(dock->attachedImages().size(), 0);   // the send drained the tray
    // Turn 2: the attachment-less answer triggers an editing plan — it must still
    // adopt turn 1's image instead of failing on the empty canvas.
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Making it black and white.\",\"actions\":"
                      "[{\"op\":\"filter\",\"mode\":\"bw\"}]}"}}}})
                       .toJson(QJsonDocument::Compact);
    win.onChatSend("the first one");
    QTRY_VERIFY(win.canvas_->hasImage());
    beat();
  }

  // §2.1 multi-image plans: one turn edits SEVERAL attached images — each
  // `image` op switches the working image to that attachment, and each `save`
  // persists the result as its own LOCAL project (fresh id per save, named
  // after the image it was working on, suffixed when the name is taken).
  void chatMultiImagePlanSavesOneProjectPerImage() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const char* plan) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", plan}}}})
          .toJson(QJsonDocument::Compact);
    };
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Both done.\",\"actions\":["
        "{\"op\":\"image\",\"index\":1},{\"op\":\"filter\",\"mode\":\"bw\"},{\"op\":\"save\"},"
        "{\"op\":\"image\",\"index\":2},{\"op\":\"filter\",\"mode\":\"sepia\"},"
        "{\"op\":\"save\",\"name\":\"second\"}]}"));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage shore(64, 48, QImage::Format_RGB32);
    shore.fill(Qt::darkCyan);
    QImage dunes(40, 30, QImage::Format_RGB32);
    dunes.fill(Qt::darkMagenta);
    dock->addAttachmentImage(shore, "shore.jpg");
    dock->addAttachmentImage(dunes, "dunes.png");
    // An unnamed save takes its attachment's name, and a TAKEN name suffixes — so a
    // leftover "shore" in the shared state dir would rename everything asserted below.
    QStringList stale;
    for (const auto& p : win.projectList_) {
      const QString name = QString::fromStdString(p.meta.name);
      if (name.startsWith(QStringLiteral("shore")) || name.startsWith(QStringLiteral("second")))
        stale << QString::fromStdString(p.meta.id);
    }
    for (const QString& id : stale) win.eraseLocalProject(id);
    const int before = int(win.projectList_.size());
    win.onChatSend("make the first b&w and the second sepia, then save both");
    QTRY_VERIFY(!dock->isBusy());
    // One project per image, in plan order: the unnamed save took the name of
    // the attachment it was working on, the named one kept its own.
    QTRY_COMPARE(int(win.projectList_.size()), before + 2);
    QCOMPARE(QString::fromStdString(win.projectList_.at(before).meta.name),
             QStringLiteral("shore"));
    QCOMPARE(QString::fromStdString(win.projectList_.at(before + 1).meta.name),
             QStringLiteral("second"));
    QVERIFY2(win.projectList_.at(before).meta.id != win.projectList_.at(before + 1).meta.id,
             "each save must promote to a FRESH project, never overwrite the last");
    // …and each one holds ITS image, not the last one processed.
    QImage firstSaved, secondSaved;
    QVERIFY(firstSaved.load(win.projectList_.at(before).imagePath));
    QVERIFY(secondSaved.load(win.projectList_.at(before + 1).imagePath));
    QCOMPARE(firstSaved.size(), shore.size());
    QCOMPARE(secondSaved.size(), dunes.size());
    // The LAST processed image stays in the editor, with its own filter.
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(win.settings_.imageFilter, QStringLiteral("sepia"));
    QCOMPARE(win.activeProjectName(), QStringLiteral("second"));

    // A follow-up turn saving image 1 again cannot reuse the taken name: it
    // suffixes instead of losing the save.
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Saved again.\",\"actions\":["
        "{\"op\":\"image\",\"index\":1},{\"op\":\"save\"}]}"));
    win.onChatSend("save the first one again");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_COMPARE(int(win.projectList_.size()), before + 3);
    QCOMPARE(QString::fromStdString(win.projectList_.at(before + 2).meta.name),
             QStringLiteral("shore 2"));
    beat();
  }

  // §3.0: a multi-image plan that also draws is still ONE round — and it must not
  // apologise for a pass that no longer exists (this used to append a "layout
  // correction skipped" note).
  void chatMultiImageLayoutStillOneRound() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Outlined.\",\"actions\":["
                      "{\"op\":\"image\",\"index\":1},{\"op\":\"layout\",\"lines\":["
                      "{\"points\":[{\"x\":4,\"y\":4},{\"x\":20,\"y\":4},"
                      "{\"x\":20,\"y\":20},{\"x\":4,\"y\":4}]}]}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage att(64, 48, QImage::Format_RGB32);
    att.fill(Qt::darkYellow);
    dock->addAttachmentImage(att, "cat.jpg");
    win.onChatSend("outline both");
    QTRY_VERIFY(!dock->isBusy());
    QCOMPARE(mock.allBodies.size(), 1);   // nothing went out behind the turn
    QVERIFY2(!chatTranscriptHas(dock, "correction"),
             "no self-check note may reach the transcript");
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsAttach.gui.moc"
