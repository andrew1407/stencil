// MainWindow GUI e2e — The bubble's own context menu, and Escape closing a card menu.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Right-click on a bubble → Copy message / Insert into prompt on every card, plus Resend on user
  // bubbles with the original attachments; a mouse selection inside one copies with the shortcut.
  void chatBubbleContextMenu() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = "ollama";
    win.settings.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"hi there\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock;
    QVERIFY(dock);
    win.actChat->setChecked(true);   // the menu only pops on a VISIBLE surface
    QTRY_VERIFY(dock->isVisible());
    awaitAnim(win.chatAnim);   // the open slide, on its own end
    QImage att(24, 24, QImage::Format_RGB32);
    att.fill(Qt::green);
    dock->addAttachmentImage(att, "cat.png");
    win.onChatSend("highlight the cat");
    QTRY_VERIFY(!dock->isBusy());
    const int firstImages = mock.body.value("messages").toArray().last().toObject()
                                .value("images").toArray().size();
    QVERIFY2(firstImages >= 1, "the first turn did not carry the attachment");
    const int postsBefore = mock.allBodies.size();

    // Open the card's custom context menu and activate the item named `text`
    // (keyboard activation, so the blocking exec() returns that action).
    const auto pickMenuItem = [](QWidget* w, const QString& text) {
      bool found = false;
      QTimer::singleShot(0, [text, &found] {
        for (int i = 0; i < 100; ++i) {
          if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            for (QAction* a : m->actions())
              if (a->text() == text) {
                found = true;
                m->setActiveAction(a);
                QTest::keyClick(m, Qt::Key_Return);
                return;
              }
            m->close();
            return;
          }
          QTest::qWait(5);
        }
      });
      const QPoint pos = w->rect().center();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos));
      QApplication::sendEvent(w, &ev);
      settle([&found] { return found; }, 20);
      return found;
    };

    QFrame* userCard = nullptr;
    for (QFrame* f : dock->findChildren<QFrame*>("chatCardUser")) userCard = f;
    QVERIFY2(userCard, "no user bubble in the transcript");
    QFrame* assistantCard = nullptr;
    QLabel* userBody = nullptr;
    for (QLabel* l : dock->findChildren<QLabel*>()) {
      if (l->property("chatBody").toString() == QLatin1String("hi there"))
        assistantCard = qobject_cast<QFrame*>(l->parentWidget());
      if (l->property("chatBody").toString() == QLatin1String("highlight the cat"))
        userBody = l;
    }
    QVERIFY2(assistantCard, "no assistant bubble in the transcript");
    QVERIFY2(userBody, "no body label on the user bubble");

    // Copy message: the plain, role-stripped text lands on the clipboard.
    QGuiApplication::clipboard()->setText(QString());
    QVERIFY(pickMenuItem(userCard, QStringLiteral("Copy message")));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("highlight the cat"));
    QVERIFY(pickMenuItem(assistantCard, QStringLiteral("Copy message")));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hi there"));
    // …and a note card gets the same menu.
    dock->appendNote(QStringLiteral("just a note"));
    QFrame* noteCard = nullptr;
    for (QFrame* f : dock->findChildren<QFrame*>("chatCardMuted")) noteCard = f;
    QVERIFY2(noteCard, "no note card in the transcript");
    QVERIFY(pickMenuItem(noteCard, QStringLiteral("Copy message")));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("just a note"));
    // No Resend anywhere but on the user's own bubbles.
    QVERIFY(!pickMenuItem(assistantCard, QStringLiteral("Resend")));

    // "Select all" is NOT offered (dropped from both surfaces — a drag selects
    // what you actually want, and Copy message already takes the whole row).
    QVERIFY2(!pickMenuItem(userCard, QStringLiteral("Select all")),
             "Select all is still in the row menu");
    // What it stood in for still works: a selection inside the bubble copies
    // with the shortcut, because the label takes focus on click.
    userBody->setSelection(0, static_cast<int>(userBody->text().size()));
    userBody->setFocus(Qt::OtherFocusReason);
    QCOMPARE(userBody->selectedText(), QStringLiteral("highlight the cat"));
    // The offscreen platform leaves no window active after the popup closes, so
    // assert the WINDOW's recorded focus widget (what activation restores).
    QTRY_COMPARE(userBody->window()->focusWidget(), static_cast<QWidget*>(userBody));
    QGuiApplication::clipboard()->setText(QString());
    QTest::keyClick(userBody, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("highlight the cat"));

    // Insert into prompt: the text lands in the composer, which takes focus.
    auto* input = dock->findChild<QPlainTextEdit*>();
    QVERIFY(input);
    input->clear();
    QVERIFY(pickMenuItem(assistantCard, QStringLiteral("Insert into prompt")));
    QCOMPARE(input->toPlainText(), QStringLiteral("hi there"));
    input->clear();

    // Resend: a SECOND request with the same text AND the original turn's
    // attachment back in the payload (the tray drained on the first send).
    QVERIFY(pickMenuItem(userCard, QStringLiteral("Resend")));
    QTRY_VERIFY(!dock->isBusy());
    QCOMPARE(mock.allBodies.size(), postsBefore + 1);
    const QJsonObject last = mock.body.value("messages").toArray().last().toObject();
    QCOMPARE(last.value("content").toString(), QStringLiteral("highlight the cat"));
    QCOMPARE(last.value("images").toArray().size(), firstImages);
    QCOMPARE(dock->attachedImages().size(), 0);   // the resend drained the tray too
    beat();
  }

  // Escape closes the chat card menu natively: exec() returns no action, the popup grab is
  // released, nothing runs — guarded so the menu reveal animation can never break it.
  void chatCardMenuEscapeCloses() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.chatDock;
    QVERIFY(dock);
    win.actChat->setChecked(true);   // the menu only pops on a VISIBLE surface
    QTRY_VERIFY(dock->isVisible());
    awaitAnim(win.chatAnim);   // the open slide, on its own end
    dock->appendNote(QStringLiteral("escape me"));
    QFrame* noteCard = nullptr;
    for (QFrame* f : dock->findChildren<QFrame*>("chatCardMuted")) noteCard = f;
    QVERIFY2(noteCard, "no note card in the transcript");

    bool sawMenu = false;
    QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));
    QTimer::singleShot(0, [&sawMenu] {
      if (!QTest::qWaitFor(
              [] { return QApplication::activePopupWidget() != nullptr; }, 500))
        return;
      auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!m) return;
      sawMenu = true;
      QTest::keyClick(m, Qt::Key_Escape);
    });
    const QPoint pos = noteCard->rect().center();
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, noteCard->mapToGlobal(pos));
    QApplication::sendEvent(noteCard, &ev);   // blocks in exec() until Escape lands
    QTRY_VERIFY2(sawMenu, "the card menu never opened");
    QTRY_VERIFY(QApplication::activePopupWidget() == nullptr);   // grab released
    // exec() returned nullptr: no action ran, the sentinel clipboard survives.
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("sentinel"));
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsBubble.gui.moc"
