// MainWindow GUI e2e — The jump pills yielding to the row menu, and error cards carrying it.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The jump pills float in the bottom-right corner, where a clipped row's "…" also lands. The pills
  // win: the row trigger shifts clear, or hides when the bubble leaves nowhere to shift to.
  void chatJumpPillsYieldToTheRowMenu() {
    MainWindow win(nullptr, false);
    win.resize(1100, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    for (int i = 0; i < 10; ++i)
      win.chatDock->appendAssistant(
          QStringLiteral("Row %1 — long enough that the transcript scrolls and a row "
                         "gets clipped at the bottom edge of the viewport.").arg(i));
    // A NARROW dock: the bubbles then reach the width cap, so an assistant row's
    // "…" (it hangs off the right) lands in the pills' corner.
    win.chatDock->setMinimumWidth(0);
    win.resizeDocks({win.chatDock}, {250}, Qt::Horizontal);
    QScrollArea* scroll = nullptr;
    for (QScrollArea* a : win.chatDock->findChildren<QScrollArea*>()) scroll = a;
    QVERIFY(scroll);
    QScrollBar* bar = scroll->verticalScrollBar();
    QTRY_VERIFY(bar->maximum() > 24);
    bar->setValue(bar->maximum() / 2);   // mid-log: both pills want to show
    settleLayout(win.chatDock, 200);
    const auto jumps = win.chatDock->findChildren<QToolButton*>(QStringLiteral("chatJumpBtn"));
    QCOMPARE(jumps.size(), 2);
    // Precondition: no row menu on screen, so the pills' own rule lets them show. The clear + nudge is
    // re-run each poll, since only a scroll re-evaluates it.
    const auto pillsUp = [&] {
      for (QToolButton* m : win.chatDock->findChildren<QToolButton*>("chatCardMore"))
        m->hide();
      // Re-centre each poll: the bubble-width pass keeps changing the range while
      // the transcript settles, and an end position legitimately hides one pill.
      bar->setValue(bar->maximum() / 2);
      bar->setValue(bar->value() + 1);
      bar->setValue(bar->value() - 1);
      return jumps[0]->isVisible() && jumps[1]->isVisible();
    };
    QTRY_VERIFY(pillsUp());
    const auto pillsRect = [&] {
      return QRect(jumps[0]->mapToGlobal(QPoint(0, 0)), jumps[0]->size())
          .united(QRect(jumps[1]->mapToGlobal(QPoint(0, 0)), jumps[1]->size()));
    };

    // Hover the row whose "…" lands in the pills' corner — same real Enter path a
    // cursor takes, so placeChatCardMore runs its actual shift/hide logic.
    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock->findChildren<QFrame*>())
      if (f->property("chatMoreBtn").isValid() &&
          QRect(f->mapToGlobal(QPoint(0, 0)), f->size())
              .intersects(QRect(scroll->viewport()->mapToGlobal(QPoint(0, 0)),
                                scroll->viewport()->size())))
        card = f;
    QVERIFY(card);
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(card, &enter);
    auto* more = qobject_cast<QToolButton*>(card->property("chatMoreBtn").value<QObject*>());
    QVERIFY(more);
    // The pills never stand down for this any more — up before AND after the hover.
    QVERIFY2(jumps[0]->isVisible() && jumps[1]->isVisible(),
             "the pills should still be up before the button reaches them");
    QVERIFY2(jumps[0]->isVisible() && jumps[1]->isVisible(),
             "the jump pills must stay up — the row's trigger yields, not them");
    // The trigger either shifted clear of the pills or, with nowhere left in this row's visible slice,
    // hid instead. It must never sit ON them: two round controls stacked are unclickable.
    if (more->isVisible()) {
      QVERIFY2(!QRect(more->mapToGlobal(QPoint(0, 0)), more->size()).intersects(pillsRect()),
               "the trigger sat under the pills instead of shifting clear of them");
    }
    // …and forcing it directly onto the pills (as a stale position from before a resize might) is
    // corrected on the next real placement pass, never by the pills hiding.
    if (more->isVisible()) {
      more->move(more->parentWidget()->mapFromGlobal(pillsRect().topLeft()));
      win.chatDock->revalidateMoreButtons();
      QVERIFY2(jumps[0]->isVisible() && jumps[1]->isVisible(), "the pills stayed up");
      if (more->isVisible())
        QVERIFY2(!QRect(more->mapToGlobal(QPoint(0, 0)), more->size()).intersects(pillsRect()),
                 "revalidateMoreButtons must pull the trigger back off the pills");
    }
    beat();
  }

  // Error and stopped cards are settled rows, so they carry the SAME affordances the browser gives
  // them: the hover "…", the right-click menu (Copy / Insert into prompt) and the neutral Resend.
  void chatErrorCardsCarryTheRowMenu() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = "ollama";
    win.settings.llmBaseUrl = "http://localhost:11434";
    win.actChat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    win.chatHistory.append({QStringLiteral("user"), QStringLiteral("remove this project"), {}});
    // The panel exists BEFORE the failure, as it does in use — errors are
    // mirrored live (they never enter the replayed history).
    win.ensureChatMenuPanel();
    win.chatError(QStringLiteral("Could not read the assistant's plan: \"clear\" is an "
                                 "editor-settings op"),
                  QString());
    // …and a stopped turn, which is built through the PENDING card on both
    // surfaces, not through the ordinary append path.
    win.chatDock->showPending();
    win.chatMirrorPending(true);
    win.chatDock->markPendingStopped(QStringLiteral("remove this project"));
    win.chatMirrorStopped(QStringLiteral("remove this project"));
    settleLayout(win.chatDock, 200);

    const auto menuItems = [](QWidget* w) {
      QStringList names;
      QTimer::singleShot(0, [&names] {
        for (int i = 0; i < 100; ++i) {
          if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            for (QAction* a : m->actions()) names << a->text();
            m->close();
            return;
          }
          QTest::qWait(5);
        }
      });
      const QPoint pos = w->rect().center();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos));
      QApplication::sendEvent(w, &ev);
      settle([&names] { return !names.isEmpty(); }, 20);
      return names;
    };

    QList<QFrame*> errorCards = win.chatDock->findChildren<QFrame*>("chatCardError");
    QVERIFY2(errorCards.size() >= 2, "expected the error card AND the stopped card");
    for (QFrame* card : errorCards) {
      const QString what = card->findChildren<QLabel*>().isEmpty()
                               ? QString()
                               : card->findChildren<QLabel*>().first()->text().left(20);
      QVERIFY2(card->property("chatMoreBtn").value<QObject*>(),
               qPrintable(QString("%1: no \"…\" on an error card").arg(what)));
      QCOMPARE(card->contextMenuPolicy(), Qt::CustomContextMenu);
      const QStringList items = menuItems(card);
      QVERIFY2(items.contains(QStringLiteral("Copy message")), qPrintable(what + ": no Copy"));
      QVERIFY2(items.contains(QStringLiteral("Insert into prompt")),
               qPrintable(what + ": no Insert into prompt"));
      // Resend is a USER-bubble item (browser parity); the error row's own
      // retry control is the neutral refresh button instead.
      QVERIFY2(!items.contains(QStringLiteral("Resend")),
               qPrintable(what + ": Resend leaked onto a non-user row"));
      QVERIFY2(!items.contains(QStringLiteral("Select all")),
               qPrintable(what + ": Select all is still offered"));
      QVERIFY2(card->findChild<QToolButton*>("chatRetry"),
               qPrintable(what + ": the error card has no Resend control"));
    }

    // The mirrored panel gets the same treatment. Its cards are only on screen while its menu is open
    // — show it, or the row menu rightly refuses to pop into an invisible surface.
    win.chatMenuPanel->setGeometry(20, 20, 340, 620);
    win.chatMenuPanel->show();
    settleLayout(win.chatMenuPanel, 150);
    QList<QFrame*> panelErrors = win.chatMenuPanel->findChildren<QFrame*>("chatCardError");
    QVERIFY2(!panelErrors.isEmpty(), "the panel mirrored no error card");
    bool sawRetry = false;
    for (QFrame* card : panelErrors) {
      QVERIFY2(card->property("chatMoreBtn").value<QObject*>(),
               "no \"…\" on the panel's error card");
      const QStringList items = menuItems(card);
      QVERIFY(items.contains(QStringLiteral("Copy message")));
      QVERIFY(items.contains(QStringLiteral("Insert into prompt")));
      if (card->findChild<QToolButton*>("chatRetry")) sawRetry = true;
    }
    QVERIFY2(sawRetry, "the panel's error card offers no Resend");
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsRowMenu.gui.moc"
