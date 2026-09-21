// MainWindow GUI e2e — The chat icon buttons' hover shimmer.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The chat's icon controls shimmer on hover like every button in the app: the sweep starts on
  // Enter, stops on Leave, never runs disabled — both composers, the title bar, the row "…".
  void chatIconButtonsShimmerOnHover() {
    const auto motion = withMotion();   // the sweep honours motionReduced(), which is on here
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = "ollama";
    win.settings.llmBaseUrl = "http://localhost:11434";
    win.actChat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    win.chatDock->appendUser(QStringLiteral("shimmer me"));
    win.ensureChatMenuPanel();
    win.chatMenuPanel->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel->show();
    win.chatMirror(QStringLiteral("You"), QStringLiteral("shimmer me too"), false);
    settleLayout(win.chatMenuPanel, 200);

    const auto overlayOf = [](QWidget* w) {
      return w ? w->findChild<QWidget*>("shimmerOverlay") : nullptr;
    };
    const auto hoverEnter = [](QWidget* w) {
      QEnterEvent e(QPointF(4, 4), QPointF(4, 4), w->mapToGlobal(QPoint(4, 4)));
      QApplication::sendEvent(w, &e);
    };
    // Every chat icon control carries the overlay, mouse-through and exactly the size of the
    // button, and a hover starts a sweep that a leave cancels.
    const auto wired = [&](QWidget* b, const char* what) {
      QWidget* fx = overlayOf(b);
      QVERIFY2(fx, qPrintable(QString("%1: no shimmer overlay").arg(what)));
      QCOMPARE(fx->geometry(), b->rect());
      QVERIFY2(fx->testAttribute(Qt::WA_TransparentForMouseEvents),
               qPrintable(QString("%1: the overlay would eat clicks").arg(what)));
    };
    // …and a hover sweeps it, a leave cancels at once.
    const auto sweeps = [&](QWidget* b, const char* what) {
      wired(b, what);
      QWidget* fx = overlayOf(b);
      QVERIFY(fx);
      hoverEnter(b);
      QTRY_VERIFY2(fx->property("sweepProgress").toReal() >= 0.0,
                   qPrintable(QString("%1: hover started no sweep").arg(what)));
      QEvent leave(QEvent::Leave);
      QApplication::sendEvent(b, &leave);
      QCOMPARE(fx->property("sweepProgress").toReal(), -1.0);   // cancelled at once
    };

    // An ENABLED dock control (the composer's send is disabled on an empty box —
    // it is the disabled case below).
    QToolButton* live = nullptr;
    for (QToolButton* b : win.chatDock->findChildren<QToolButton*>())
      if (b->isVisible() && b->isEnabled() && overlayOf(b)) { live = b; break; }
    QVERIFY2(live, "no shimmered enabled button in the chat dock");
    sweeps(live, "dock composer/header button");

    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock->findChildren<QFrame*>("chatCardUser")) card = f;
    QVERIFY(card);
    auto* more = qobject_cast<QToolButton*>(card->property("chatMoreBtn").value<QObject*>());
    QVERIFY2(more, "the card has no \"…\"");
    more->show();   // normally revealed by the card's own hover
    // Presence only: the "…" LIFTS itself 1px on hover, and offscreen QPA answers that move with
    // a synthetic Leave that cancels the sweep. A real pointer stays inside it.
    wired(more, "row-menu \"…\"");

    QToolButton* panelBtn = nullptr;
    for (QToolButton* b : win.chatMenuPanel->findChildren<QToolButton*>())
      if (b->isEnabled() && overlayOf(b)) { panelBtn = b; break; }
    QVERIFY2(panelBtn, "no shimmered button in the menu panel");
    sweeps(panelBtn, "menu panel composer button");

    // A DISABLED control stays quiet: send, with nothing typed.
    QToolButton* send = win.chatDock->findChild<QToolButton*>("chatSend");
    QVERIFY(send);
    QVERIFY2(!send->isEnabled(), "the empty composer's send should be disabled");
    QWidget* sendFx = overlayOf(send);
    QVERIFY2(sendFx, "the send button lost its shimmer overlay");
    hoverEnter(send);
    QCOMPARE(sendFx->property("sweepProgress").toReal(), -1.0);
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatCompactShimmer.gui.moc"
