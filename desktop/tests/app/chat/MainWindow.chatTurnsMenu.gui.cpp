// MainWindow GUI e2e — A card's row menu surviving transcript churn, and the hover button that opens it.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The card menu pops from a NESTED event loop, so the transcript can change under it: a turn can
  // land and repaint rows, the chat can be mid-close, and the card itself can be deleted.
  void chatCardMenuSurvivesTranscriptChurn() {
    MainWindow win(nullptr, false);
    win.resize(1150, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = "ollama";
    win.settings.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"working on it\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.actChat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    auto* dock = win.chatDock;

    const auto cardWithBody = [&](const QString& body) -> QFrame* {
      for (QLabel* l : dock->findChildren<QLabel*>())
        if (l->property("chatBody").toString() == body)
          return qobject_cast<QFrame*>(l->parentWidget());
      return nullptr;
    };
    // Right-click a card's LABEL (the path in the crash report) and, from inside
    // the menu's event loop, run `duringMenu` before closing it.
    const auto popMenu = [&](QWidget* on, std::function<void()> duringMenu) {
      QTimer::singleShot(0, [duringMenu] {
        for (int i = 0; i < 100; ++i) {
          if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            if (duringMenu) duringMenu();
            m->close();
            return;
          }
          QTest::qWait(5);
        }
      });
      const QPoint p = on->rect().center();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, p, on->mapToGlobal(p));
      QApplication::sendEvent(on, &ev);
      settle([] { return QApplication::activePopupWidget() == nullptr; }, 40);
    };

    // (a) a turn IN FLIGHT, with the transcript repainting under the menu.
    dock->addAttachmentImage(QImage(8, 8, QImage::Format_RGB32), "x.png");
    win.onChatSend(QStringLiteral("crop it, make it b&w, and highlight the edges"));
    QTRY_VERIFY(!dock->isBusy());
    QFrame* userCard = cardWithBody(QStringLiteral("crop it, make it b&w, and highlight the edges"));
    QVERIFY(userCard);
    QLabel* body = nullptr;
    for (QLabel* l : userCard->findChildren<QLabel*>())
      if (!l->property("chatBody").toString().isEmpty()) body = l;
    QVERIFY(body);
    popMenu(body, [&] {
      // …the turn's tail landing while the menu is up.
      dock->appendAssistant(QStringLiteral("late note while the menu is open"));
      dock->appendLateNote(QStringLiteral("layout corrected"));
    });

    // (b) mid-close: the dock is still visible for the length of its slide, but
    // the menu must not pop into a surface that is about to be hidden.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    win.actChat->setChecked(false);
    QVERIFY2(dock->isVisible(), "the close should still be animating");
    {
      bool popped = false;
      QTimer::singleShot(0, [&popped] {
        if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
          popped = true;
          m->close();
        }
      });
      const QPoint p = body->rect().center();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, p, body->mapToGlobal(p));
      QApplication::sendEvent(body, &ev);
      QTest::qWait(40);
      QVERIFY2(!popped, "the menu popped out of a chat that was closing");
    }
    if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim);
    QTRY_VERIFY(!dock->isVisible());
    win.actChat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    awaitAnim(win.chatAnim);   // the open slide, on its own end

    // (c) the card is DELETED while its own menu is up — nothing may touch it
    // after exec() returns.
    dock->appendUser(QStringLiteral("doomed row"));
    QTRY_VERIFY(cardWithBody(QStringLiteral("doomed row")));
    QFrame* doomed = cardWithBody(QStringLiteral("doomed row"));
    QVERIFY(doomed);
    QTRY_VERIFY(doomed->isVisible());
    QLabel* doomedBody = nullptr;
    for (QLabel* l : doomed->findChildren<QLabel*>())
      if (!l->property("chatBody").toString().isEmpty()) doomedBody = l;
    QVERIFY(doomedBody);
    QPointer<QFrame> gone(doomed);
    popMenu(doomedBody, [&] {
      delete gone.data();   // the transcript settling mid-menu, at its worst
    });
    QVERIFY2(!gone, "the card should be gone");
    QTRY_VERIFY2(!dock->isBusy(), "the dock survived the churn");

    // …and a right-click on the now-dangling label's siblings still does nothing bad.
    QFrame* survivor = cardWithBody(QStringLiteral("late note while the menu is open"));
    if (survivor) popMenu(survivor, nullptr);
    win.llmClient.reset();
    beat();
  }

  // Hovering a bubble reveals the ghost "⋯" in its bottom corner — LEFT on the right-aligned user
  // bubbles, RIGHT on assistant cards — and clicking it opens the right-click menu; Leave hides it.
  void chatCardHoverMenuButton() {
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
    dock->show();   // visibility checks below need visible ancestors
    win.onChatSend("highlight the cat");
    QTRY_VERIFY(!dock->isBusy());
    settleLayout(dock, 20);   // let the transcript layout activate (shows the cards)

    QFrame* userCard = nullptr;
    for (QFrame* f : dock->findChildren<QFrame*>("chatCardUser")) userCard = f;
    QVERIFY2(userCard, "no user bubble in the transcript");
    QFrame* assistantCard = nullptr;
    for (QLabel* l : dock->findChildren<QLabel*>())
      if (l->property("chatBody").toString() == QLatin1String("hi there"))
        assistantCard = qobject_cast<QFrame*>(l->parentWidget());
    QVERIFY2(assistantCard, "no assistant bubble in the transcript");

    // Offscreen has no real cursor, so hover is a synthetic Enter event.
    const auto hover = [](QWidget* w) {
      QEnterEvent ev(QPointF(2, 2), QPointF(2, 2), w->mapToGlobal(QPoint(2, 2)));
      QApplication::sendEvent(w, &ev);
    };
    // The button reparents beside its card on the first place — resolve it via
    // the property link, not parentage.
    auto* userMore = qobject_cast<QToolButton*>(
        userCard->property("chatMoreBtn").value<QObject*>());
    auto* asstMore = qobject_cast<QToolButton*>(
        assistantCard->property("chatMoreBtn").value<QObject*>());
    QVERIFY2(userMore && asstMore, "cards are missing the hover menu button");
    QVERIFY(userMore->toolTip().isEmpty());   // no "Message actions" tooltip
    QVERIFY(!userMore->isVisible());   // hidden at rest
    hover(userCard);
    QVERIFY(userMore->isVisible());
    // BESIDE the user bubble on its left — never overlapping it (parent coords
    // after placeCardMore reparents the button next to the card).
    QVERIFY(userMore->geometry().right() < userCard->geometry().left());
    QVERIFY(qAbs(userMore->geometry().bottom() - userCard->geometry().bottom()) <= 2);
    hover(assistantCard);
    QVERIFY(asstMore->isVisible());
    // …and beside the assistant bubble on its right.
    QVERIFY(asstMore->geometry().left() > assistantCard->geometry().right());
    QVERIFY(qAbs(asstMore->geometry().bottom() - assistantCard->geometry().bottom()) <= 2);
    // Leave hides after a short grace (the button sits across a gap) — park the
    // offscreen cursor away from both widgets first so the check can pass.
    QCursor::setPos(win.mapToGlobal(QPoint(5, 5)));
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(assistantCard, &leave);
    QTRY_VERIFY(!asstMore->isVisible());   // gone when the cursor moves off

    // Clicking it opens the SAME card menu (timer-driven pick, as above).
    bool sawMenu = false;
    QGuiApplication::clipboard()->setText(QString());
    QTimer::singleShot(0, [&sawMenu] {
      // Bounded wait for the blocking exec()'s popup, then activate the item.
      if (!QTest::qWaitFor(
              [] { return QApplication::activePopupWidget() != nullptr; }, 500))
        return;
      auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!m) return;
      for (QAction* a : m->actions())
        if (a->text() == QLatin1String("Copy message")) {
          sawMenu = true;
          m->setActiveAction(a);
          QTest::keyClick(m, Qt::Key_Return);
          return;
        }
      m->close();
    });
    userMore->click();   // blocking until the menu picks/closes
    QTRY_VERIFY2(sawMenu, "the hover button did not open the card menu");
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("highlight the cat"));
    // The menu is gone, so a Leave hides the button again (after the grace).
    QApplication::sendEvent(userCard, &leave);
    QTRY_VERIFY(!userMore->isVisible());
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsMenu.gui.moc"
