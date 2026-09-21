// MainWindow GUI e2e — What stands in for a chat surface when none is up: the toast, and the gear's own dialog.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Completions landing while the chat dock is HIDDEN surface as a clickable bottom-left toast
  // (browser closedToast parity): text truncated to ~90 chars; an open dock shows no toast.
  void chatToastWhenDockHidden() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());

    // Success (chat-only plan) while hidden → success toast with the reply.
    stencil::llm::LlmReply ok;
    ok.ok = true;
    ok.text = "{\"version\":1,\"reply\":\"All done, boss\",\"actions\":[]}";
    win.onChatReply(ok);
    auto* toast = win.findChild<QWidget*>("chatToast");
    QVERIFY(toast);
    QTRY_VERIFY(toast->isVisible());
    auto* label = toast->findChild<QLabel*>();
    QVERIFY(label);
    QVERIFY(label->text().contains("Assistant finished"));
    QVERIFY(label->text().contains("All done, boss"));
    // Anchored bottom-left (18 px inset).
    QCOMPARE(toast->x(), 18);
    QVERIFY(toast->geometry().bottom() > win.height() - 40);

    // Click → the dock opens through the normal path and the toast dismisses.
    QTest::mouseClick(toast, Qt::LeftButton);
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(chat->isChecked());
    QTRY_VERIFY(!toast->isVisible());

    // Failure while hidden → error toast, truncated to the ~90-char bound.
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    stencil::llm::LlmReply bad;
    bad.ok = false;
    bad.failure = stencil::llm::LlmFailure::HTTP;
    bad.error = QString(200, QChar('x'));
    win.onChatReply(bad);
    QTRY_VERIFY(toast->isVisible());
    QVERIFY(label->text().startsWith("Assistant failed"));
    QVERIFY(label->text().size() <= 90);
    QVERIFY(label->text().endsWith(QChar(0x2026)));
    QTest::mouseClick(toast, Qt::LeftButton);  // dismiss + reopen for the next check
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(!toast->isVisible());

    // Open dock: completions must NOT raise a toast.
    win.onChatReply(ok);
    QVERIFY(!toast->isVisible());
    beat();
  }

  // The chat dock's gear opens the DEDICATED assistant dialog (browser llmSettingsModal parity)
  // — provider / base URL / model / key / server only — and saving round-trips the provider.
  void chatGearOpensAssistantOnlySettings() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    auto* gear = dock->findChild<QToolButton*>("chatGear");
    QVERIFY(gear);

    const stencil::gui::Settings before = stencil::gui::fileStore::loadSettings();
    // Pick a provider that differs from whatever is configured now.
    const QString target =
        win.settings.llmProvider == QLatin1String("openai-compat") ? "ollama"
                                                                   : "openai-compat";
    // The dialog is modal (exec blocks the click), so inspect + drive it from a
    // timer, the way dismissModal does for the confirmations.
    bool inspected = false, wrongControls = false, sawProvider = false;
    QString dialogName;
    QTimer::singleShot(0, [&] {
      QDialog* dlg = nullptr;
      for (int i = 0; i < 200 && !dlg; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) QTest::qWait(10);
      }
      if (!dlg) return;
      dialogName = dlg->objectName();
      auto* provider = dlg->findChild<QComboBox*>("llmProvider");
      sawProvider = provider != nullptr;
      // Assistant-only: none of the full Settings dialog's controls belong here. The one checkbox
      // that does is the §12 save-chats opt-in (llmSaveChats).
      wrongControls = !dlg->findChildren<QDoubleSpinBox*>().isEmpty() ||
                      !dlg->findChildren<QSpinBox*>().isEmpty();
      for (QCheckBox* cb : dlg->findChildren<QCheckBox*>())
        if (cb->objectName() != QLatin1String("llmSaveChats")) wrongControls = true;
      for (QComboBox* c : dlg->findChildren<QComboBox*>())
        if (c->findData(QString("A4")) >= 0 || c->findText("A4") >= 0)
          wrongControls = true;
      // The LLM rows the browser modal has, all present.
      for (const char* name : {"llmBaseUrl", "llmModel", "llmApiKey", "llmServer"})
        if (!dlg->findChild<QWidget*>(name)) wrongControls = true;
      if (provider) {
        const int idx = provider->findData(target);
        if (idx >= 0) provider->setCurrentIndex(idx);
      }
      inspected = true;
      dlg->accept();  // Save
    });
    gear->click();  // blocks in exec() until the timer accepts

    QVERIFY2(inspected, "the gear did not open a modal dialog");
    QCOMPARE(dialogName, QString("assistantSettingsDialog"));
    QVERIFY(sawProvider);
    QVERIFY2(!wrongControls, "the assistant dialog carries unrelated settings");
    // Saved through the same path as the full dialog: live settings + the file.
    QCOMPARE(win.settings.llmProvider, target);
    QCOMPARE(stencil::gui::fileStore::loadSettings().llmProvider, target);

    stencil::gui::fileStore::saveSettings(before);  // leave the user's config alone
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatCompactToast.gui.moc"
