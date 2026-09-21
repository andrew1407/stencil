// MainWindow GUI e2e — A chat CARD arriving out of dust, from its own side of the transcript.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A chat card arrives the way a toast does: its dust gathers out of a point off the side it
  // sits against, the bubble held behind the motes (browser twin: surface/motion.js chatIn).
  void chatCardsArriveOutOfDustOnEverySurface() {
    const auto motion = withMotion();   // the suite runs with STENCIL_NO_ANIM on
    const char* DUST = stencil::gui::DisintegrateOverlay::OBJECT_NAME;
    for (const bool panel : {false, true}) {
      MainWindow win(nullptr, false);
      win.resize(panel ? 1200 : 1000, panel ? 850 : 760);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      if (panel) {
        win.ensureChatMenuPanel();
        QVERIFY(win.chatMenuPanel);
        win.chatMenuPanel->setGeometry(20, 20, 340, 640);
        win.chatMenuPanel->show();
        settleLayout(win.chatMenuPanel, 300);   // its width arrives before a grab can read it
      } else {
        win.actChat->setChecked(true);
        QTRY_VERIFY(win.chatDock->isVisible());
      }
      // A dock card's cloud is a SURFACE flight parented to the window; the panel gathers its rows
      // inside itself. Dock rows carry a "chatCard…" name, panel rows the "chatMoreBtn" property.
      const auto dust = [&win, panel, DUST] {
        return panel ? win.chatMenuPanel->findChild<QWidget*>(DUST)
                     : win.findChild<QWidget*>(DUST);
      };
      const auto rows = [&win, panel] {
        QList<QFrame*> out;
        QWidget* surface = panel ? static_cast<QWidget*>(win.chatMenuPanel) : win.chatDock;
        for (QFrame* f : surface->findChildren<QFrame*>())
          if (panel ? f->property("chatMoreBtn").isValid()
                    : f->objectName().startsWith(QLatin1String("chatCard")))
            out.append(f);
        return out;
      };
      QTRY_VERIFY_WITH_TIMEOUT(!dust(), stencil::gui::DisintegrateOverlay::DUST_MS + 2000);

      // Every card appearing gathers: the sent message, the "…" holding the turn, the reply and a
      // failure. The panel is fed the same turn through the mirror, its only route for a row.
      QVector<std::function<void()>> appends;
      if (panel) {
        appends = {[&win] {
          win.chatMirror(QStringLiteral("You"), QStringLiteral("crop it square"), false);
        }};
      } else {
        appends = {
            [&win] { win.chatDock->appendUser(QStringLiteral("crop it square"), {}); },
            [&win] { win.chatDock->showPending(); },
            [&win] { win.chatDock->appendAssistant(QStringLiteral("Cropped.")); },
            [&win] {
              win.chatDock->appendError(
                  QStringLiteral("Couldn't reach Ollama at localhost:11434 (fetch failed)"),
                  QStringLiteral("crop it square"));
            }};
      }
      for (const auto& append : appends) {
        append();
        // The grab is deferred — the dust must be a photograph of the FINISHED bubble at its real
        // width — so the cloud shows up a beat later, not in this tick.
        QTRY_VERIFY_WITH_TIMEOUT(dust() != nullptr, 3000);
        QFrame* card = rows().isEmpty() ? nullptr : rows().last();
        QVERIFY(card);
        // Held FULLY hidden while the motes fly: they ARE the bubble forming. Fading it up underneath
        // them would draw the finished card and play the arrival over the top of it.
        auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect());
        QVERIFY2(fx && fx->opacity() == 0.0, "the card is invisible until its motes land");
        QTRY_VERIFY_WITH_TIMEOUT(!dust(), stencil::gui::DisintegrateOverlay::DUST_MS + 2000);
        if (!panel) win.chatDock->clearPending();   // the "…" must not outlive its own case
      }

      // …and every card lands on its resting state: full opacity, resting margins, and the
      // effect handed back to the scroll-edge reveal (ENTERING_PROPERTY dropped).
      int settled = 0;
      for (QFrame* card : rows()) {
        ++settled;
        if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect()))
          QTRY_COMPARE(fx->opacity(), 1.0);
        QTRY_COMPARE(card->property(stencil::gui::ScrollReveal::ENTERING_PROPERTY).toBool(), false);
        if (!panel) QTRY_COMPARE(card->layout()->contentsMargins().top(), 6);
      }
      QVERIFY2(settled > 0, "no card was found — the checks above would be vacuous");
    }
    beat();
  }

  // On a transcript long enough to scroll, the entrance waits a frame for scrollToBottom() and
  // refuses to fly for a card not wholly in the viewport.
  void chatCardDustArrivesFromTheCardsOwnSide() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 620);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    const char* DUST = stencil::gui::DisintegrateOverlay::OBJECT_NAME;
    QTRY_VERIFY_WITH_TIMEOUT(!win.findChild<QWidget*>(DUST),
                             stencil::gui::DisintegrateOverlay::DUST_MS + 4000);

    // `side` is +1 when the cloud must come from the right of the card, -1 from the left.
    const auto arrivesFrom = [&](const char* cardName, int side, const char* what) {
      QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) != nullptr, 3000);
      // No Q_OBJECT on the overlay (it needs no MOC), so its unique object name IS the
      // type check — qobject_cast will not compile for it.
      auto* dust = static_cast<stencil::gui::DisintegrateOverlay*>(win.findChild<QWidget*>(DUST));
      QVERIFY2(dust, what);
      QVERIFY2(dust->gathering(), what);   // an arrival, not a leave
      QFrame* card = nullptr;
      for (QFrame* f : win.chatDock->findChildren<QFrame*>(QString::fromLatin1(cardName))) card = f;
      QVERIFY2(card, what);
      const QRect box(card->mapTo(&win, QPoint(0, 0)), card->size());
      const int dx = dust->surfaceTarget().x() - box.center().x();
      QVERIFY2(side * dx > 0, what);
      // …and clear of the card itself, so the motes visibly travel in over its edge.
      QVERIFY2(qAbs(dx) > box.width() / 2, what);
      QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) == nullptr,
                               stencil::gui::DisintegrateOverlay::DUST_MS + 2000);
    };
    win.chatDock->appendUser(QStringLiteral("mine, on the right"), {});
    arrivesFrom("chatCardUser", +1, "a user message must gather from the RIGHT");
    win.chatDock->appendAssistant(QStringLiteral("and the reply, on the left"));
    arrivesFrom("chatCardAssistant", -1, "an assistant message must gather from the LEFT");
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatCards.gui.moc"
