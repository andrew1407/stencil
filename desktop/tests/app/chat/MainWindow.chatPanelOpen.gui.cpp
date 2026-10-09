// MainWindow GUI e2e — The panel and the dock agreeing however the panel was opened.
// Shared ground is in chatTurnsGui.hpp, over MainWindow.gui.hpp.
#include "chatTurnsGui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The two chat surfaces render the SAME transcript across a §7 continuation, warnings, an error
  // card, a late note and a §12 restore: the panel replays the DOCK's view, never chatHistory.
  void chatPanelAndDockAgreeHoweverThePanelOpens() {
    for (const bool panelOpensLate : {false, true}) {
      MainWindow win(nullptr, false);
      QVERIFY(showSized(win, 1200, 850));
      MockChatTransport mock;
      useOllama(win.settings, win.parts.chatAppliers.llmClient, mock);
      const auto wrap = &ollamaReply;
      win.acts.chat->setChecked(true);
      QTRY_VERIFY(win.chatDock->isVisible());
      // Round 1 of the §7 turn: the reply the dock HOLDS (the continuation's
      // settled answer replaces it), so no surface may ever show it.
      const QString interim = QStringLiteral(
          "Loading it into incognito, converting to black & white and cropping to portrait now.");
      const auto showPanel = [&win] {
        win.chatSession->ensureChatMenuPanel();
        win.chatSession->chatMenuPanel->setGeometry(20, 20, 340, 640);
        win.chatSession->chatMenuPanel->show();
      };
      if (!panelOpensLate) showPanel();

      // Every CARD in order: its kind plus every text it shows (body + the notes riding inside it), so
      const auto cardsOf = &displayedCards;   // the displayed transcript, per surface
      const auto same = [&](const char* what) {
        settle([&] { return cardsOf(win.chatSession->chatMenuPanel) == cardsOf(win.chatDock); }, 400);
        QVERIFY2(cardsOf(win.chatSession->chatMenuPanel) == cardsOf(win.chatDock),
                 qPrintable(QStringLiteral("%1: the two surfaces disagree\n  dock : %2\n  panel: %3")
                                .arg(QString::fromLatin1(what),
                                     cardsOf(win.chatDock).join(QStringLiteral(" | ")),
                                     cardsOf(win.chatSession->chatMenuPanel).join(QStringLiteral(" | ")))));
      };
      const auto noInternalText = [&](const char* what) {
        for (QWidget* s : {static_cast<QWidget*>(win.chatDock), win.chatSession->chatMenuPanel})
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
      win.chatSession->onChatSend(QStringLiteral("make it b&w and crop to 3:4"));
      QTRY_VERIFY(!win.chatDock->isBusy());
      if (panelOpensLate) showPanel();
      same("continuation turn");
      noInternalText("continuation turn");
      QCOMPARE(cardsOf(win.chatDock).size(), 2);   // the ask + the ONE settled reply

      // ── 2. the §12 round trip: saved conversation, reopened project ──
      const QJsonObject doc = win.chatSession->buildActiveChatDoc();
      const QByteArray json = QJsonDocument(doc).toJson();
      QVERIFY2(!json.contains("The working image is now"),
               "the persisted doc must not carry the §7 continuation note (§12.1)");
      QVERIFY2(!json.contains(interim.toUtf8()),
               "the persisted doc must not carry the held interim reply (§12.1)");
      bool noteInHistory = false;   // the MODEL's view is untouched by the sanitising
      for (const auto& m : win.chatSession->chatHistory)
        if (m.text.contains(QStringLiteral("The working image is now"))) noteInHistory = true;
      QVERIFY2(noteInHistory, "the continuation note must still reach the model");
      win.chatSession->restoreChatFromDoc(doc);
      QTRY_COMPARE(cardsOf(win.chatDock).size(), 2);   // the wipe finished, 2 rows came back
      same("restored conversation");
      noInternalText("restored conversation");
      QVERIFY2(cardsOf(win.chatDock).join(QChar('\n')).contains(settled),
               "the settled reply must survive the restore");

      // ── 3. warnings fold into the reply's own bubble on both ──
      mock.queue.append(wrap(QStringLiteral(
          "{\"version\":1,\"reply\":\"tinted\",\"actions\":[{\"op\":\"filter\",\"mode\":\"bw\"},"
          "{\"op\":\"wobble\"}]}")));
      win.chatSession->onChatSend(QStringLiteral("make it grey"));
      QTRY_VERIFY(!win.chatDock->isBusy());
      same("warnings turn");
      QVERIFY2(cardsOf(win.chatDock).join(QChar('\n')).contains(QStringLiteral("wobble")),
               "the skipped-op warning is missing");

      // ── 4. an error card (a plan that will not validate) ──
      mock.queue.append(wrap(QStringLiteral(
          "{\"version\":1,\"reply\":\"here\",\"actions\":[{\"op\":\"blank\",\"color\":\"nope\"}]}")));
      win.chatSession->onChatSend(QStringLiteral("break it"));
      QTRY_VERIFY(!win.chatDock->isBusy());
      same("error turn");
      QVERIFY2(cardsOf(win.chatSession->chatMenuPanel).join(QChar('\n'))
                   .contains(QStringLiteral("chatCardError: Could not read")),
               "the panel is missing the error card");

      // ── 5. the late notes the §3 chain and the text-only retry report ──
      win.chatSession->chatLateNote(QStringLiteral("The layout self-check kept the lines."));
      win.chatSession->chatNote(QStringLiteral("This model is text-only — the image was not sent."));
      same("late notes");
      // …while the attachment cap is a TOAST (browser parity: notify(…, 'info')), not a transcript
      // card: no row on either surface, the line in the accent, and a batch folds into one toast.
      const int dockRows = cardsOf(win.chatDock).size();
      win.chatDock->warnAttachmentCap();
      win.chatDock->warnAttachmentCap();
      same("cap toast");
      QCOMPARE(cardsOf(win.chatDock).size(), dockRows);
      int capToasts = 0;
      for (QLabel* l : win.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly)) {
        if (!l->property("stencilToastText").toString().startsWith("Up to 3 images per message")) continue;
        ++capToasts;
        const auto pal = stencil::gui::themePalette(stencil::gui::resolveDark(win.settings.themeMode), win.settings.accentColor);
        QVERIFY2(!l->styleSheet().contains(pal.danger.name(), Qt::CaseInsensitive), "the cap toast is red");
      }
      QCOMPARE(capToasts, 1);

      // ── 6. clearing empties both ──
      win.chatSession->onChatClear();
      win.chatDock->clearConversation();
      QTRY_VERIFY(cardsOf(win.chatDock).isEmpty());
      QTRY_VERIFY(cardsOf(win.chatSession->chatMenuPanel).isEmpty());
      win.parts.chatAppliers.llmClient.reset();
    }
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatPanelOpen.gui.moc"
