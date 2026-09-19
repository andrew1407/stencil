// MainWindow GUI e2e — Out-of-range multi-image saves, and the project removal confirmations.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void chatMultiImageOutOfRangeAndEmptySaveWarn() {
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
        "{\"version\":1,\"reply\":\"The fourth one.\",\"actions\":["
        "{\"op\":\"image\",\"index\":4},{\"op\":\"filter\",\"mode\":\"bw\"}]}"));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage att(64, 48, QImage::Format_RGB32);
    att.fill(Qt::darkGreen);
    dock->addAttachmentImage(att, "cat.jpg");
    win.onChatSend("do the fourth one");
    QTRY_VERIFY(!dock->isBusy());
    QVERIFY2(chatTranscriptHas(dock, "attached image 4"),
             "an unsatisfiable index must warn, naming it");
    QCOMPARE(win.settings_.imageFilter, QStringLiteral("bw"));   // the rest still ran

    // Nothing on the canvas: the save is skipped with a warning, no project.
    win.resetToBlankEditor();   // the trash button's body, minus its confirmation
    QTRY_VERIFY(!win.canvas_->hasImage());
    const int before = int(win.projectList_.size());
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Saving.\",\"actions\":[{\"op\":\"save\",\"name\":\"x\"}]}"));
    win.onChatSend("save it");
    QTRY_VERIFY(!dock->isBusy());
    QVERIFY2(chatTranscriptHas(dock, "no working image"), "an empty save must warn");
    QCOMPARE(int(win.projectList_.size()), before);
    beat();
  }

  // §10 project management from chat: a removeProject plan runs the projects dialog's Delete flow,
  // confirm included, on exactly the named project; a DECLINED clearProjects notes "clear canceled".
  void chatRemoveProjectConfirmsAndClearDeclineNotes() {
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
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    // Seed two local projects to pick between.
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(Qt::darkBlue);
    const QString goneId = win.addImageProjectEntry(img, "chat del target");
    const QString keptId = win.addImageProjectEntry(img, "chat del keeper");
    QVERIFY(!goneId.isEmpty() && !keptId.isEmpty());
    const int before = int(win.projectList_.size());

    // Remove one by name; the blocking QMessageBox confirm is answered "Yes".
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Removed.\",\"actions\":["
        "{\"op\":\"removeProject\",\"name\":\"chat del target\"}]}"));
    dismissModal("OK");
    win.onChatSend("delete the chat del target project");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_COMPARE(int(win.projectList_.size()), before - 1);
    QVERIFY2(!win.findProject(goneId.toStdString()), "the named project must be gone");
    QVERIFY2(win.findProject(keptId.toStdString()), "the other project must remain");

    // clearProjects, confirm DECLINED: nothing removed, the note says so.
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Clearing.\",\"actions\":[{\"op\":\"clearProjects\"}]}"));
    dismissModal("Cancel");
    win.onChatSend("clear all my projects");
    QTRY_VERIFY(!dock->isBusy());
    QCOMPARE(int(win.projectList_.size()), before - 1);
    QVERIFY2(chatTranscriptHas(dock, "clear canceled"),
             "a declined clear must land as a note");
    beat();
  }

  // §10 removeProject{current:true} with nothing saved but an image open falls back to the `clear`
  // flow behind the SAME confirm: declined keeps the picture, accepted takes the image and lines.
  void chatRemoveCurrentFallsBackToClearWhenNothingIsSaved() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const char* plan) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", plan}}}})
          .toJson(QJsonDocument::Compact);
    };
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    const char* removeCurrent =
        "{\"version\":1,\"reply\":\"Removing.\",\"actions\":["
        "{\"op\":\"removeProject\",\"current\":true}]}";

    // The reported case: an incognito editor holding an edited image — nothing
    // saved (incognito blocks the project promotion a normal load would do).
    CanvasWidget* canvas = win.canvas_;
    QVERIFY(canvas);
    win.actIncognito_->setChecked(true);
    QImage shot(120, 90, QImage::Format_RGB32);
    shot.fill(Qt::darkCyan);
    canvas->loadFromImage(shot);
    QTRY_VERIFY(canvas->hasImage());
    QVERIFY(win.incognito_ && win.activeProjectId_.isEmpty());
    stencil::core::Line line;
    line.points = {{10, 10}, {80, 40}};
    canvas->setLines({line});
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);

    // Declined: the confirm ran, nothing went.
    mock.queue.append(wrap(removeCurrent));
    dismissModal("Cancel");
    win.onChatSend("remove this project");
    QTRY_VERIFY(!dock->isBusy());
    QVERIFY2(canvas->hasImage(), "a declined confirm must keep the image");
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);
    QVERIFY2(chatTranscriptHas(dock, "removal canceled"),
             "a declined confirm is a note, never a failed plan");
    QVERIFY2(!chatTranscriptHas(dock, "no saved project is open"),
             "the technicality refusal must be gone");

    // Accepted: the §10 clear flow — image and lines go, the editor is empty.
    mock.queue.append(wrap(removeCurrent));
    dismissModal("OK");
    win.onChatSend("remove this project");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_VERIFY2(!canvas->hasImage(), "the accepted fallback must clear the image");
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);

    // With a SAVED project open the old path is unchanged: the project itself goes.
    win.actIncognito_->setChecked(false);
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(Qt::darkBlue);
    const QString id = win.addImageProjectEntry(img, "chat current target");
    QVERIFY(!id.isEmpty());
    win.activeProjectId_ = id;
    const int before = int(win.projectList_.size());
    mock.queue.append(wrap(removeCurrent));
    dismissModal("OK");
    win.onChatSend("remove this project");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_COMPARE(int(win.projectList_.size()), before - 1);
    QVERIFY2(!win.findProject(id.toStdString()), "the saved project must be the one removed");
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsProjects.gui.moc"
