// MainWindow GUI e2e — The executor note riding with a reply, and the compare/zoom/rename plans.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // An executor note about SUCCESSFUL work ("Opened X in the editor first…") rides
  // INSIDE the assistant's reply bubble as muted text — one assistant card per turn,
  // never a second card in the red error treatment (browser parity: the note merges
  // into the reply's warnings). Standalone notes (appendNote/appendNotice) render on
  // the muted card style with the reply bubble's paddings; danger stays reserved for
  // actual turn errors.
  void chatExecutorNoteRidesWithReply() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = qobject_cast<stencil::gui::ChatDock*>(
        win.findChild<QDockWidget*>("llmChatDock"));
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(dock->width() > 200);
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Making it black and white.\",\"actions\":"
                      "[{\"op\":\"filter\",\"mode\":\"bw\"}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    // An editing plan arriving with an EMPTY canvas + an attachment adopts the
    // attachment as the working image and says so (the adoption note).
    QVERIFY(!win.canvas_->hasImage());
    QImage att(64, 48, QImage::Format_RGB32);
    att.fill(Qt::darkCyan);
    dock->addAttachmentImage(att, QStringLiteral("cat.png"));
    win.onChatSend("make it b&w");
    QTRY_VERIFY(win.canvas_->hasImage());

    auto* scrollArea = dock->findChild<QScrollArea*>();
    QVERIFY(scrollArea && scrollArea->widget());
    const auto cards = [scrollArea] {
      return scrollArea->widget()->findChildren<QFrame*>(QString(),
                                                         Qt::FindDirectChildrenOnly);
    };
    // ONE assistant bubble for the whole turn: user card + assistant card, and the
    // note is not a card of its own (it used to land as a chatCardError bubble).
    QTRY_COMPARE(cards().size(), 2);
    QFrame* reply = cards().last();
    QCOMPARE(reply->objectName(), QStringLiteral("chatCardAssistant"));
    QLabel* body = nullptr;
    QLabel* note = nullptr;
    for (QLabel* l : reply->findChildren<QLabel*>()) {
      if (!l->property("chatBody").toString().isEmpty()) body = l;
      if (!l->property("chatNote").toString().isEmpty()) note = l;
    }
    QVERIFY(body && note);
    QCOMPARE(body->property("chatBody").toString(),
             QString("Making it black and white."));
    QVERIFY(note->property("chatNote").toString().startsWith("Opened cat.png"));
    // The note is NEUTRAL: the muted stylesheet tone (QSS beats palettes here, so
    // the colour rides the chatNoteLabel rule), never the danger treatment.
    QCOMPARE(note->objectName(), QStringLiteral("chatNoteLabel"));
    QVERIFY(dock->styleSheet().contains("QLabel#chatNoteLabel{color:"));

    // Standalone notes (text-only retry, outline-refine) keep their own card, on
    // the MUTED style — and with exactly the reply bubble's vertical paddings, so
    // the same text renders at the same card height.
    const QString sample =
        QStringLiteral("A note long enough to wrap over a couple of lines in the dock.");
    dock->appendNote(sample);
    dock->appendAssistant(sample);
    QTRY_VERIFY(noneEntering(scrollArea->widget()));   // the appear animations landed
    const auto after = cards();
    QCOMPARE(after.size(), 4);
    QFrame* noteCard = after.at(2);
    QFrame* bubbleCard = after.at(3);
    QCOMPARE(noteCard->objectName(), QStringLiteral("chatCardMuted"));
    QVERIFY(dock->styleSheet().contains("#chatCardMuted QLabel{color:"));
    QCOMPARE(noteCard->layout()->contentsMargins(),
             bubbleCard->layout()->contentsMargins());
    QCOMPARE(noteCard->height(), bubbleCard->height());

    // The "assistant off" notice is the muted treatment too, never the red row.
    dock->appendNotice("The assistant is turned off.");
    QCOMPARE(cards().last()->objectName(), QStringLiteral("chatCardMuted"));

    if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
      QTest::qWait(50);
      dock->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) +
                        "/dock-executor-note.png");
    }
    beat();
  }

  // §10 new editor rows end-to-end: one mock-transport plan drives the compare
  // view (mode + split), the zoom, and a rename of the active saved project —
  // through the SAME setters the toolbar uses, so the combo/canvas/registry all
  // agree afterwards.
  void chatComparZoomRenamePlanDrivesEditor() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(64, 48, QImage::Format_RGB32);
    img.fill(Qt::darkYellow);
    win.loadImageWithLayout(img, QJsonObject());
    // A saved ACTIVE project, uniquely named per run (renameProjectById
    // validates against the persisted registry).
    const QString base =
        QStringLiteral("chat view src %1").arg(QDateTime::currentMSecsSinceEpoch());
    win.createLocalProject(base, /*announce=*/false);
    QVERIFY(!win.activeProjectId_.isEmpty());

    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const QString renamed = base + QStringLiteral(" renamed");
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      QStringLiteral(
                          "{\"version\":1,\"reply\":\"View set\",\"actions\":["
                          "{\"op\":\"compare\",\"mode\":\"vertical\",\"split\":0.3},"
                          "{\"op\":\"zoom\",\"percent\":150},"
                          "{\"op\":\"renameProject\",\"name\":\"%1\"}]}")
                          .arg(renamed)}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    const QSize sizeBefore = win.canvas_->image().size();
    win.onChatSend("compare it side by side, zoom in, and rename the project");

    // The compare view: canvas mode + divider, and the toolbar combo followed.
    QTRY_COMPARE(win.canvas_->compareMode(), QStringLiteral("vertical"));
    QCOMPARE(win.canvas_->compareSplit(), 0.3);
    QCOMPARE(win.compareCombo_->currentData().toString(), QStringLiteral("vertical"));
    // The zoom landed on the canvas scale (view-only — the image is untouched).
    QVERIFY(std::abs(win.canvas_->scale() - 1.5) < 1e-9);
    QCOMPARE(win.canvas_->image().size(), sizeBefore);
    // The rename went through the real registry path.
    QCOMPARE(win.activeProjectName(), renamed);
    bool inRegistry = false;
    for (const auto& p : win.projectList_)
      if (p.meta.name == renamed.toStdString()) inRegistry = true;
    QVERIFY2(inRegistry, "the renamed project is in the persisted registry");
    // The turn resolved as ONE assistant bubble with the plan's reply.
    auto* dock = qobject_cast<stencil::gui::ChatDock*>(
        win.findChild<QDockWidget*>("llmChatDock"));
    QVERIFY(dock);
    QTRY_VERIFY(assistantBubbleTexts(dock).contains(QStringLiteral("View set")));
    // Leave the shared registry tidy for the other cases.
    const QString id = win.activeProjectId_;
    win.setCompareModeUi(QStringLiteral("none"));
    win.eraseLocalProject(id);
    stencil::gui::fileStore::saveProjects(win.projectList_);
    beat();
  }

  // A follow-up {"op":"compare","mode":"none"} plan must CLEAR the split view:
  // canvas mode off (no divider, not read-only), toolbar combo back to None.
  void chatCompareNonePlanClearsSplit() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(64, 48, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto plan = [](const char* json) {
      return QJsonDocument(QJsonObject{
                 {"message", QJsonObject{{"content", QString::fromUtf8(json)}}}})
          .toJson(QJsonDocument::Compact);
    };
    // Turn 1 mirrors the reported flow: a red blank + a rectangle + the split
    // view in ONE plan (the layout makes the correction/refinement rounds run;
    // their empty responses are harmless keeps).
    mock.queue.append(plan(
        "{\"version\":1,\"reply\":\"split\",\"actions\":["
        "{\"op\":\"blank\",\"color\":\"red\",\"format\":\"a4\"},"
        "{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":100,\"y\":100},"
        "{\"x\":400,\"y\":100},{\"x\":400,\"y\":300},{\"x\":100,\"y\":300},"
        "{\"x\":100,\"y\":100}],\"color\":\"#000000\"}]},"
        "{\"op\":\"compare\",\"mode\":\"vertical\",\"split\":0.4}]}"));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("red album page with a rectangle, compared side by side");
    QTRY_COMPARE(win.canvas_->compareMode(), QStringLiteral("vertical"));
    mock.queue.clear();
    // The follow-up carries the mode alone: an echoed "split" beside "none" is a
    // parse failure since the registry's onlyWith rule (fixture 160).
    mock.response = plan("{\"version\":1,\"reply\":\"cleared\",\"actions\":["
                         "{\"op\":\"compare\",\"mode\":\"none\"}]}");
    win.onChatSend("turn the comparison off");
    QTRY_COMPARE(win.canvas_->compareMode(), QStringLiteral("none"));
    QVERIFY2(!win.canvas_->compareReadOnly(), "compare 'none' left the canvas read-only");
    QCOMPARE(win.compareCombo_->currentData().toString(), QStringLiteral("none"));
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsPlan.gui.moc"
