// MainWindow GUI e2e — What the chat doc saves, and the neutral glyph a retry wears.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // §12.1 WRITE side: the persisted document is the DISPLAYED transcript, never
  // chatHistory_ — that is the model's view, carrying the §7 continuation note
  // and the held round-1 reply the dock never showed. The doc rides the .stencil
  // file and the server "chat" kind to the browser, the bot and the consoles, so
  // internal text written here can no longer be filtered out anywhere.
  void chatDocSavesOnlyTheDisplayedTranscript() {
    using stencil::gui::fileStore::parseChatDoc;
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
    win.ensureChatMenuPanel();
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();

    // Every card's body + in-card notes, per surface — the displayed transcript.
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

    // A §7 continuation turn: round 1 loads without tracing, so its reply is
    // HELD and round 2's settled answer is the only bubble the user gets.
    const QString interim = QStringLiteral("Loading the blank page and cropping now.");
    const QString settled = QStringLiteral("Blank page ready, converted to black & white.");
    const QString ask = QStringLiteral("give me a blank page in b&w");
    mock.queue.append(wrap(QStringLiteral(
        "{\"version\":1,\"reply\":\"%1\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\"}]}")
                              .arg(interim)));
    mock.queue.append(wrap(
        QStringLiteral("{\"version\":1,\"reply\":\"%1\",\"actions\":[]}").arg(settled)));
    win.onChatSend(ask);
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QTest::qWait(200);

    // ── 1. the document is exactly the two displayed rows ──
    const QJsonObject doc = win.buildActiveChatDoc();
    const QByteArray json = QJsonDocument(doc).toJson();
    QVERIFY2(!json.contains("The working image is now"),
             qPrintable("the §7 continuation note was persisted: " + QString::fromUtf8(json)));
    QVERIFY2(!json.contains(interim.toUtf8()),
             qPrintable("the held interim reply was persisted: " + QString::fromUtf8(json)));
    const QJsonArray saved = parseChatDoc(doc);
    QCOMPARE(saved.size(), 2);
    QCOMPARE(saved.at(0).toObject().value("role").toString(), QString("user"));
    QCOMPARE(saved.at(0).toObject().value("text").toString(), ask);
    QCOMPARE(saved.at(1).toObject().value("role").toString(), QString("assistant"));
    QCOMPARE(saved.at(1).toObject().value("text").toString(), settled);
    QCOMPARE(doc.value("version").toInt(), 1);
    QVERIFY(doc.value("savedAt").toDouble() > 0);
    // The MODEL's view is untouched: the live conversation still replays both.
    bool noteInHistory = false, interimInHistory = false;
    for (const auto& m : win.chatHistory_) {
      if (m.text.contains(QStringLiteral("The working image is now"))) noteInHistory = true;
      if (m.text == interim) interimInHistory = true;
    }
    QVERIFY2(noteInHistory && interimInHistory,
             "chatHistory_ must keep the full model-side history");

    // ── 2. it round-trips to the same transcript on BOTH surfaces ──
    const QStringList before = cardsOf(win.chatDock_);
    win.restoreChatFromDoc(doc);
    QTRY_COMPARE(cardsOf(win.chatDock_).size(), 2);
    QCOMPARE(cardsOf(win.chatDock_), before);
    QCOMPARE(cardsOf(win.chatMenuPanel_), cardsOf(win.chatDock_));
    // …and re-saving the restored conversation is a fixed point.
    QCOMPARE(parseChatDoc(win.buildActiveChatDoc()), saved);

    // ── 3. defence in depth: an OLD-style doc (written before the machinery
    // filter) is laundered on read — the §7 note never resurfaces on screen or
    // in the replay history; the interim reply is indistinguishable from
    // conversation and survives (browser sanitizeChatMessages parity) ──
    QJsonArray old;
    const auto row = [](const char* role, const QString& text) {
      return QJsonObject{{"role", QString::fromLatin1(role)}, {"text", text}};
    };
    old.append(row("user", ask));
    old.append(row("assistant", interim));
    old.append(row("user", QStringLiteral(
        "[The working image is now the picture those actions loaded — continue with it.]")));
    old.append(row("assistant", settled));
    // The write side filters too: the note never even reaches a new document.
    const QJsonObject oldDoc{{"version", 1},
                             {"savedAt", 42},
                             {"messages", old}};
    QCOMPARE(stencil::gui::fileStore::buildChatDoc(old, 42).value("messages").toArray().size(), 3);
    win.restoreChatFromDoc(oldDoc);
    QTRY_COMPARE(cardsOf(win.chatDock_).size(), 3);
    QCOMPARE(cardsOf(win.chatMenuPanel_), cardsOf(win.chatDock_));
    for (const QString& r : cardsOf(win.chatDock_))
      QVERIFY2(!r.contains(QStringLiteral("The working image is now")),
               qPrintable("an old doc put the continuation note on screen: " + r));
    QCOMPARE(win.chatHistory_.size(), 3);   // the model replays the same laundered view
    bool oldNoteInHistory = false;
    for (const auto& m : win.chatHistory_)
      if (m.text.contains(QStringLiteral("The working image is now"))) oldNoteInHistory = true;
    QVERIFY2(!oldNoteInHistory, "the §7 note must not be replayed from storage");

    // ── 4. the §12.1 bound: writers trim to the most recent 32 ──
    win.onChatClear();
    win.chatDock_->clearConversation();
    for (int i = 0; i < 40; ++i)
      win.chatMirror(i % 2 ? QStringLiteral("Assistant") : QStringLiteral("You"),
                     QStringLiteral("row %1").arg(i), false);
    const QJsonArray trimmed = parseChatDoc(win.buildActiveChatDoc());
    QCOMPARE(trimmed.size(), 32);
    QCOMPARE(trimmed.at(0).toObject().value("text").toString(), QString("row 8"));

    // ── 5. muted plumbing is never conversation ──
    win.onChatClear();
    win.chatDock_->clearConversation();
    win.chatMirror(QStringLiteral("You"), ask, false);
    win.chatError(QStringLiteral("Could not read the assistant's plan: bad op"), QString());
    win.chatMirror(QStringLiteral("Attached"), QStringLiteral("1 image(s)"), true);
    const QJsonArray onlyUser = parseChatDoc(win.buildActiveChatDoc());
    QCOMPARE(onlyUser.size(), 1);
    QCOMPARE(onlyUser.at(0).toObject().value("text").toString(), ask);

    win.onChatClear();
    win.chatDock_->clearConversation();
    QVERIFY(win.buildActiveChatDoc().isEmpty());   // nothing displayed ⇒ nothing filed
    win.llmClient_.reset();
    beat();
  }

  // The error card's Resend glyph is NEUTRAL in both themes, never the card's own
  // red: the browser's retry is a .chat-hbtn, which sets `color: var(--text-muted)`
  // itself and does not inherit the bubble's --danger. Painted red it sat red-on-red
  // in the danger wash and barely read.
  void chatErrorRetryGlyphIsNeutral() {
    for (const QString mode : {QStringLiteral("light"), QStringLiteral("dark")}) {
      MainWindow win(nullptr, false);
      win.resize(1100, 760);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      win.settings_.themeMode = mode;
      win.applyTheme();
      win.actChat_->setChecked(true);
      QTRY_VERIFY(win.chatDock_->isVisible());
      win.chatDock_->appendError(
          QStringLiteral("Could not read the assistant's plan: \"clear\" is an "
                         "editor-settings op"),
          QStringLiteral("remove this project"));
      QToolButton* retry = win.chatDock_->findChild<QToolButton*>("chatRetry");
      QVERIFY2(retry, "the error card has no Resend button");
      QVERIFY2(retry->parentWidget()->objectName() == QLatin1String("chatCardError"),
               "the Resend button is not on the error card");
      const QImage glyph = retry->icon().pixmap(14, 14).toImage();
      QVERIFY(!glyph.isNull());
      const QColor danger = stencil::gui::themePalette(mode == "dark").danger;
      int ink = 0, red = 0;
      for (int y = 0; y < glyph.height(); ++y)
        for (int x = 0; x < glyph.width(); ++x) {
          const QColor c = glyph.pixelColor(x, y);
          if (c.alpha() < 60) continue;
          ++ink;
          if (qAbs(c.red() - danger.red()) < 45 && qAbs(c.green() - danger.green()) < 45
              && qAbs(c.blue() - danger.blue()) < 45)
            ++red;
        }
      QVERIFY2(ink > 0, qPrintable(mode + ": the Resend glyph rendered nothing"));
      QVERIFY2(red == 0, qPrintable(mode + ": the Resend glyph is still danger red"));
      beat();
    }
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsDoc.gui.moc"
