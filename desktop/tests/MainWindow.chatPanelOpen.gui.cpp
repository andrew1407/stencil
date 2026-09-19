// MainWindow GUI e2e — The panel and the dock agreeing however the panel was opened.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The two chat surfaces render the SAME transcript across a §7 continuation, warnings, an
  // error card, a late note — and a §12 restore of the saved conversation. The panel replays
  // what the DOCK displayed, never chatHistory_: that is the model's view, carrying the
  // continuation note ("[The working image is now …]") and the interim reply the dock
  // deliberately never showed, so restoring it verbatim put internal text on screen (and
  // only on the surface that replayed it, once the two were fed from different places).
  // Run once with both surfaces up from the start, the way the report had them, and once
  // with the panel created LATE — the lazy replay is where the two used to diverge.
  void chatPanelAndDockAgreeHoweverThePanelOpens() {
    for (const bool panelOpensLate : {false, true}) {
      MainWindow win(nullptr, false);
      win.resize(1200, 850);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      win.settings_.llmProvider = "ollama";
      win.settings_.llmBaseUrl = "http://localhost:11434";
      MockChatTransport mock;
      const auto wrap = [](const QString& json) {
        return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", json}}}})
            .toJson(QJsonDocument::Compact);
      };
      win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
      win.actChat_->setChecked(true);
      QTRY_VERIFY(win.chatDock_->isVisible());
      // Round 1 of the §7 turn: the reply the dock HOLDS (the continuation's
      // settled answer replaces it), so no surface may ever show it.
      const QString interim = QStringLiteral(
          "Loading it into incognito, converting to black & white and cropping to portrait now.");
      const auto showPanel = [&win] {
        win.ensureChatMenuPanel();
        win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
        win.chatMenuPanel_->show();
      };
      if (!panelOpensLate) showPanel();

      // Every CARD, in order: its kind plus every text it shows (body + the notes
      // riding inside it). Comparing this catches a missing row, an extra row, a
      // note rendered as its own card, and a differing body — all at once.
      const auto cardsOf = [](QWidget* surface) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QStringList out;
        for (QFrame* f : surface->findChildren<QFrame*>()) {
          const QString kind = f->objectName();
          if (!kind.startsWith(QLatin1String("chatCard")) ||
              kind == QLatin1String("chatCardMore"))
            continue;
          QStringList texts;
          for (QLabel* l : f->findChildren<QLabel*>()) {
            const QString b = l->property("chatBody").toString();
            const QString n = l->property("chatNote").toString();
            if (!b.isEmpty()) texts << b;
            else if (!n.isEmpty()) texts << n;
          }
          if (texts.size() == 1 && texts.first() == QStringLiteral("…")) continue;  // pending
          out << kind + QStringLiteral(": ") + texts.join(QStringLiteral(" ¶ "));
        }
        return out;
      };
      const auto same = [&](const char* what) {
        settle([&] { return cardsOf(win.chatMenuPanel_) == cardsOf(win.chatDock_); }, 400);
        QVERIFY2(cardsOf(win.chatMenuPanel_) == cardsOf(win.chatDock_),
                 qPrintable(QStringLiteral("%1: the two surfaces disagree\n  dock : %2\n  panel: %3")
                                .arg(QString::fromLatin1(what),
                                     cardsOf(win.chatDock_).join(QStringLiteral(" | ")),
                                     cardsOf(win.chatMenuPanel_).join(QStringLiteral(" | ")))));
      };
      const auto noInternalText = [&](const char* what) {
        for (QWidget* s : {static_cast<QWidget*>(win.chatDock_), win.chatMenuPanel_})
          for (const QString& row : cardsOf(s)) {
            QVERIFY2(!row.contains(QStringLiteral("The working image is now")),
                     qPrintable(QStringLiteral("%1: the §7 continuation note is displayed: %2")
                                    .arg(QString::fromLatin1(what), row)));
            QVERIFY2(!row.contains(interim),
                     qPrintable(QStringLiteral("%1: the held interim reply is displayed: %2")
                                    .arg(QString::fromLatin1(what), row)));
          }
      };

      // ── 1. a §7 continuation turn: ONE settled bubble on both surfaces ──
      mock.queue.append(wrap(QStringLiteral(
          "{\"version\":1,\"reply\":\"%1\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\"}]}")
                                .arg(interim)));
      const QString settled = QStringLiteral(
          "Black & white applied, cropped to a 3:4 portrait, and I traced the hair silhouette.");
      mock.queue.append(wrap(
          QStringLiteral("{\"version\":1,\"reply\":\"%1\",\"actions\":[]}").arg(settled)));
      win.onChatSend(QStringLiteral("make it b&w and crop to 3:4"));
      QTRY_VERIFY(!win.chatDock_->isBusy());
      if (panelOpensLate) showPanel();
      same("continuation turn");
      noInternalText("continuation turn");
      QCOMPARE(cardsOf(win.chatDock_).size(), 2);   // the ask + the ONE settled reply

      // ── 2. the §12 round trip: saved conversation, reopened project ──
      const QJsonObject doc = win.buildActiveChatDoc();
      const QByteArray json = QJsonDocument(doc).toJson();
      QVERIFY2(!json.contains("The working image is now"),
               "the persisted doc must not carry the §7 continuation note (§12.1)");
      QVERIFY2(!json.contains(interim.toUtf8()),
               "the persisted doc must not carry the held interim reply (§12.1)");
      bool noteInHistory = false;   // the MODEL's view is untouched by the sanitising
      for (const auto& m : win.chatHistory_)
        if (m.text.contains(QStringLiteral("The working image is now"))) noteInHistory = true;
      QVERIFY2(noteInHistory, "the continuation note must still reach the model");
      win.restoreChatFromDoc(doc);
      QTRY_COMPARE(cardsOf(win.chatDock_).size(), 2);   // the wipe finished, 2 rows came back
      same("restored conversation");
      noInternalText("restored conversation");
      QVERIFY2(cardsOf(win.chatDock_).join(QChar('\n')).contains(settled),
               "the settled reply must survive the restore");

      // ── 3. warnings fold into the reply's own bubble on both ──
      mock.queue.append(wrap(QStringLiteral(
          "{\"version\":1,\"reply\":\"tinted\",\"actions\":[{\"op\":\"filter\",\"mode\":\"bw\"},"
          "{\"op\":\"wobble\"}]}")));
      win.onChatSend(QStringLiteral("make it grey"));
      QTRY_VERIFY(!win.chatDock_->isBusy());
      same("warnings turn");
      QVERIFY2(cardsOf(win.chatDock_).join(QChar('\n')).contains(QStringLiteral("wobble")),
               "the skipped-op warning is missing");

      // ── 4. an error card (a plan that will not validate) ──
      mock.queue.append(wrap(QStringLiteral(
          "{\"version\":1,\"reply\":\"here\",\"actions\":[{\"op\":\"blank\",\"color\":\"nope\"}]}")));
      win.onChatSend(QStringLiteral("break it"));
      QTRY_VERIFY(!win.chatDock_->isBusy());
      same("error turn");
      QVERIFY2(cardsOf(win.chatMenuPanel_).join(QChar('\n'))
                   .contains(QStringLiteral("chatCardError: Could not read")),
               "the panel is missing the error card");

      // ── 5. the late notes the §3 chain and the text-only retry report ──
      win.chatLateNote(QStringLiteral("The layout self-check kept the lines."));
      win.chatNote(QStringLiteral("This model is text-only — the image was not sent."));
      same("late notes");
      // …while the attachment cap is a TOAST (browser parity: notify(…, 'info')), not a
      // transcript card: neither surface grows a row, the window's stack shows the line in
      // the accent (never the danger red), and a batch's repeated hits fold into one toast.
      const int dockRows = cardsOf(win.chatDock_).size();
      win.chatDock_->warnAttachmentCap();
      win.chatDock_->warnAttachmentCap();
      same("cap toast");
      QCOMPARE(cardsOf(win.chatDock_).size(), dockRows);
      int capToasts = 0;
      for (QLabel* l : win.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly)) {
        if (!l->property("stencilToastText").toString().startsWith("Up to 3 images per message")) continue;
        ++capToasts;
        const auto pal = stencil::gui::themePalette(stencil::gui::resolveDark(win.settings_.themeMode), win.settings_.accentColor);
        QVERIFY2(!l->styleSheet().contains(pal.danger.name(), Qt::CaseInsensitive), "the cap toast is red");
      }
      QCOMPARE(capToasts, 1);

      // ── 6. clearing empties both ──
      win.onChatClear();
      win.chatDock_->clearConversation();
      QTRY_VERIFY(cardsOf(win.chatDock_).isEmpty());
      QTRY_VERIFY(cardsOf(win.chatMenuPanel_).isEmpty());
      win.llmClient_.reset();
    }
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatPanelOpen.gui.moc"
