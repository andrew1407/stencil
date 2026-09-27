// MainWindow GUI e2e — the anthropic session key in the assistant settings: typed, held in memory
// on Save, reported with its expiry, forgotten, and never written to the settings or anywhere else.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "AssistantSettingsDialog.hpp"
#include "SessionKey.hpp"

namespace {
  const QString KEY = QStringLiteral("sk-ant-gui-0123456789abcdef");
  // A refused loopback port: the form's probe and model list go here, never to the real API.
  const QString LOOPBACK = QStringLiteral("http://127.0.0.1:1");

  template <class T>
  T* part(QDialog* dlg, const char* name) { return dlg->findChild<T*>(QLatin1String(name)); }
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // Opens the dock's assistant dialog and hands it to `act` from inside its exec(); `act` ends it.
  static bool withAssistantDialog(MainWindow& win, const std::function<void(QDialog*)>& act) {
    bool ran = false;
    QTimer::singleShot(0, [&] {
      QDialog* dlg = nullptr;
      for (int i = 0; i < 200 && !dlg; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) QTest::qWait(10);
      }
      if (!dlg) return;
      ran = dlg->objectName() == QLatin1String("assistantSettingsDialog");
      act(dlg);
    });
    win.parts.dialogs.openAssistantSettings();
    return ran;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }
  void init() { stencil::llm::SessionKey::instance().forget(); }

  // Picking Anthropic shows the key rows instead of the stored-key row; Save holds the typed key in
  // memory and persists the provider, and neither the settings file nor QSettings ever sees the key.
  void anthropicKeyIsHeldNotSaved() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = QStringLiteral("none");   // whatever an earlier run saved
    win.settings.llmBaseUrl.clear();
    bool rowsRight = false, echoHidden = false, idleStatus = false;
    QVERIFY(withAssistantDialog(win, [&](QDialog* dlg) {
      auto* provider = part<QComboBox>(dlg, "llmProvider");
      provider->setCurrentIndex(provider->findData(QStringLiteral("anthropic")));
      auto* key = part<QLineEdit>(dlg, "llmAnthropicKey");
      rowsRight = key->isVisibleTo(dlg) && !part<QLineEdit>(dlg, "llmApiKey")->isVisibleTo(dlg) &&
                  part<QLabel>(dlg, "llmKeyNote")->isVisibleTo(dlg) &&
                  !part<QLabel>(dlg, "llmNote")->isVisibleTo(dlg) &&
                  part<QLineEdit>(dlg, "llmBaseUrl")->text() == QLatin1String("https://api.anthropic.com");
      echoHidden = key->echoMode() == QLineEdit::Password && key->text().isEmpty();
      idleStatus = part<QLabel>(dlg, "llmKeyStatus")->text() == QLatin1String("No key for this session.") &&
                   !part<QPushButton>(dlg, "llmForgetKey")->isEnabled();
      part<QLineEdit>(dlg, "llmBaseUrl")->setText(LOOPBACK);   // the form probes; never Anthropic itself
      QTest::keyClicks(key, KEY);
      dlg->accept();
    }));
    QVERIFY2(rowsRight, "anthropic shows its key rows, note and default URL, not the stored-key row");
    QVERIFY(echoHidden);
    QVERIFY(idleStatus);
    QCOMPARE(stencil::llm::SessionKey::instance().key(), KEY);
    QCOMPARE(win.settings.llmProvider, QString("anthropic"));
    QVERIFY(win.settings.llmApiKey.isEmpty());
    QCOMPARE(stencil::gui::fileStore::loadSettings().llmProvider, QString("anthropic"));
    const QStringList leaks = placesHolding(KEY);
    QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(", ")));
    beat();
  }

  // Reopened, the field starts empty and the status names the expiry; Forget drops the key at once.
  // A stored openai-compat key that IS the session key is never saved either.
  void heldKeyReportsAndForgets() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = QStringLiteral("anthropic");
    win.settings.llmBaseUrl = LOOPBACK;
    const QDateTime until = stencil::llm::SessionKey::instance().hold(KEY);
    QString heldText, forgottenText;
    bool emptyField = false, forgetLive = false, forgetGone = false, keyFocused = false;
    QVERIFY(withAssistantDialog(win, [&](QDialog* dlg) {
      emptyField = part<QLineEdit>(dlg, "llmAnthropicKey")->text().isEmpty();
      heldText = part<QLabel>(dlg, "llmKeyStatus")->text();
      auto* forget = part<QPushButton>(dlg, "llmForgetKey");
      forgetLive = forget->isEnabled();
      forget->click();
      forgottenText = part<QLabel>(dlg, "llmKeyStatus")->text();
      forgetGone = !forget->isEnabled();
      dlg->reject();
    }));
    QVERIFY(emptyField);
    QCOMPARE(heldText, stencil::gui::LlmSettingsForm::keyStatusText(until, QDateTime::currentDateTime()));
    QVERIFY(heldText.startsWith("Key kept until ") && heldText.endsWith(" or until Stencil quits."));
    QVERIFY(forgetLive && forgetGone);
    QCOMPARE(forgottenText, QString("No key for this session."));
    QVERIFY(stencil::llm::SessionKey::instance().key().isEmpty());

    // With no key held, the dialog opens on the key field.
    QVERIFY(withAssistantDialog(win, [&](QDialog* dlg) {
      keyFocused = QApplication::focusWidget() == part<QLineEdit>(dlg, "llmAnthropicKey") ||
                   dlg->focusWidget() == part<QLineEdit>(dlg, "llmAnthropicKey");
      dlg->reject();
    }));
    QVERIFY2(keyFocused, "no key held: the dialog should open on the key field");

    stencil::llm::SessionKey::instance().hold(KEY);
    Settings saved;
    QVERIFY(withAssistantDialog(win, [&](QDialog* dlg) {
      auto* provider = part<QComboBox>(dlg, "llmProvider");
      provider->setCurrentIndex(provider->findData(QStringLiteral("openai-compat")));
      part<QLineEdit>(dlg, "llmApiKey")->setText(KEY);
      saved = static_cast<stencil::gui::AssistantSettingsDialog*>(dlg)->result();
      dlg->reject();
    }));
    QVERIFY2(saved.llmApiKey.isEmpty(), "the session key rode into the stored openai-compat key");
    QVERIFY(placesHolding(KEY).isEmpty());
    stencil::llm::SessionKey::instance().forget();
    beat();
  }

  // A switch gives each provider its own default URL unless the user typed one — an Ollama default
  // left under stencil-server was never typed for it (browser twin: llmSettings.test.js).
  void providerSwitchGivesEachItsDefaultUrl() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = QStringLiteral("stencil-server");
    win.settings.llmBaseUrl = QStringLiteral("http://localhost:11434");
    QString toAnthropic, toServer, typedKept;
    QVERIFY(withAssistantDialog(win, [&](QDialog* dlg) {
      auto* provider = part<QComboBox>(dlg, "llmProvider");
      auto* url = part<QLineEdit>(dlg, "llmBaseUrl");
      provider->setCurrentIndex(provider->findData(QStringLiteral("anthropic")));
      toAnthropic = url->text();
      provider->setCurrentIndex(provider->findData(QStringLiteral("stencil-server")));
      toServer = url->text();
      url->setText(LOOPBACK);
      provider->setCurrentIndex(provider->findData(QStringLiteral("anthropic")));
      typedKept = url->text();
      dlg->reject();
    }));
    QCOMPARE(toAnthropic, QString("https://api.anthropic.com"));
    QCOMPARE(toServer, QString());
    QCOMPARE(typedKept, LOOPBACK);
    beat();
  }

  // The dialog's hover sweep reaches its buttons but not the Save-chats check (user report).
  void saveChatsCheckWearsNoShimmer() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    bool swept = false, checkSwept = true;
    QVERIFY(withAssistantDialog(win, [&](QDialog* dlg) {
      auto* closePill = part<QPushButton>(dlg, "modalClosePill");
      swept = QTest::qWaitFor([&] { return closePill->property("_shimmer").toBool(); }, 1000);
      checkSwept = part<QCheckBox>(dlg, "llmSaveChats")->property("_shimmer").toBool();
      dlg->reject();
    }));
    QVERIFY2(swept, "the dialog's controls should still wear the hover sweep");
    QVERIFY2(!checkSwept, "Save chats with projects should not shimmer on hover");
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.assistantKey.gui.moc"
