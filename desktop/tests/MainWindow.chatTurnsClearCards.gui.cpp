// MainWindow GUI e2e — Clearing empties everything, and a card survives its own animation.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Clearing empties the transcript, brings the chips back and drops the model-side state —
  // and a card appearing, or clearing mid-flight, must survive its own animation.
  void chatClearEmptiesAndCardsSurvive() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = qobject_cast<stencil::gui::ChatDock*>(
        win.findChild<QDockWidget*>("llmChatDock"));
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(dock->width() > 200);  // let the open slide settle before geometry


    auto* clearBtn = dock->findChild<QToolButton*>("chatClear");
    QVERIFY(clearBtn);

    auto* scrollArea = dock->findChild<QScrollArea*>();
    QVERIFY(scrollArea && scrollArea->widget());
    QWidget* transcript = scrollArea->widget();
    const auto cardCount = [transcript] {
      // Cards are the only direct QFrame children of the transcript column
      // (the suggestion block is a plain QWidget).
      return transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)
          .size();
    };
    dock->appendUser("make it sepia");
    dock->appendAssistant("done");
    QCOMPARE(cardCount(), 2);
    QImage att(8, 8, QImage::Format_RGB32);
    att.fill(Qt::blue);
    dock->addAttachmentImage(att);
    QCOMPARE(dock->attachedImages().size(), 1);

    auto* suggest = dock->findChild<QWidget*>("chatSuggest");
    QVERIFY(suggest && !suggest->isVisible());  // hidden by the first card

    // Frozen while a turn is in flight (like attach).
    auto* attachBtn = dock->findChild<QToolButton*>("chatAttach");
    QVERIFY(attachBtn);
    dock->setBusy(true);
    QVERIFY(!clearBtn->isEnabled());
    QVERIFY(!attachBtn->isEnabled());
    dock->setBusy(false);
    QVERIFY(clearBtn->isEnabled());

    // Click: transcript emptied, chips back, attachments + model state dropped;
    // the provider config survives.
    const QString providerBefore = win.currentLlmSettings().provider;
    QTest::mouseClick(clearBtn, Qt::LeftButton);
    QTRY_COMPARE(cardCount(), 0);   // cards go through deleteLater
    // The empty state is held back for the length of the scatter, or the chips sit under falling
    // particles. The wait is keyed off rows being REMOVED: offscreen there are no particles.
    QVERIFY2(!suggest->isVisible(), "the chips came back before the wipe finished");
    QTRY_VERIFY_WITH_TIMEOUT(suggest->isVisible(),
                             stencil::gui::DisintegrateOverlay::DUST_MS + 3000);
    QCOMPARE(dock->attachedImages().size(), 0);
    QVERIFY(dock->attachedVideoPath().isEmpty());
    QVERIFY(win.chatHistory.isEmpty());
    QVERIFY(win.chatVideoPath.isEmpty());
    QCOMPARE(win.chatVideoFrames, 0);
    QVERIFY(win.chatImageDigest.isEmpty());
    QVERIFY(win.chatImageEncoded.data.isEmpty());
    QCOMPARE(win.currentLlmSettings().provider, providerBefore);

    // Appear: a fresh card is claimed by its own opacity effect and ends fully visible, overlapping
    // appends each owning their animation. This suite runs REDUCED, where the card is simply there.
    dock->appendUser("again");
    dock->appendAssistant("sure");
    dock->appendError("nope");
    const auto cards =
        transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly);
    QCOMPARE(cards.size(), 3);
    for (QFrame* card : cards) {
      auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect());
      QTRY_COMPARE(fx->opacity(), 1.0);
      QVERIFY(card->layout());
      QTRY_COMPARE(card->layout()->contentsMargins().top(), 6);  // slide resolved
    }
    // Clearing mid-animation must not crash (the card owns its animation).
    dock->appendAssistant("mid-flight");
    QTest::mouseClick(clearBtn, Qt::LeftButton);
    QTRY_COMPARE(cardCount(), 0);
    QTest::qWait(200);  // let any surviving animation tick would-be-dangling

    // A card that SCROLLS while it fades must not crash: ScrollReveal's setGraphicsEffect deletes the
    // effect already there, so the fade claims the card (ENTERING_PROPERTY) as the entrance does.
    {
      for (int i = 0; i < 6; i++) {
        dock->appendUser(QStringLiteral("question %1").arg(i));
        dock->appendAssistant(QStringLiteral("a reply long enough to wrap and take real height %1").arg(i));
      }
      // Every entrance has landed and dropped its own claim on the effect — otherwise
      // the assertion below passes for the wrong reason.
      QTRY_VERIFY(noneEntering(transcript));
      dock->clearConversation();          // every card starts fading…
      // …and the INVARIANT holds from the first frame: every leaving card claims its graphics effect,
      // the flag ScrollReveal::apply() skips on. The crash cannot be reproduced offscreen.
      int claimed = 0, fading = 0;
      for (QFrame* card : transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!card->graphicsEffect()) continue;
        fading++;
        if (card->property(stencil::gui::ScrollReveal::ENTERING_PROPERTY).toBool()) claimed++;
      }
      QVERIFY2(fading > 0, "no card was actually fading — the guard would be vacuous");
      QCOMPARE(claimed, fading);
      for (int i = 0; i < 12; i++) {      // …while the transcript relayouts under them
        dock->resize(dock->width(), 300 + (i % 3) * 60);
        QTest::qWait(16);                 // one driver frame per relayout, no more
      }
      QTRY_VERIFY_WITH_TIMEOUT(cardCount() == 0,
                               stencil::gui::DisintegrateOverlay::DUST_MS + 3000);
    }

    // A LONG wrapped reply must not be cut off by its own bubble: the label's wrapped height is
    // RESERVED (applyBubbleWidths), since heightForWidth is a hint the layout never re-asks.
    {
      const QString essay =
          QStringLiteral("The layout is drawn on a 794x1123 px page, so the rectangle sits at "
                         "roughly 250,400-544,723 — tell me if the centre is off. The three "
                         "variants are rotated, tinted and cropped. I couldn't open an "
                         "incognito tab: that needs a URL you gave me in this conversation.");
      dock->appendAssistant(essay);
      // The appear animation offsets the card's margins; it drops its own claim when it
      // lands (ENTERING_PROPERTY), and the reserved wrap height follows one relayout later.
      QTRY_VERIFY(noneEntering(transcript));
      settleLayout(transcript, 250);
      QLabel* body = nullptr;
      for (QLabel* l : transcript->findChildren<QLabel*>())
        if (l->text() == essay) body = l;
      QVERIFY(body);
      const int wrapped = body->heightForWidth(body->width());
      QVERIFY2(body->height() >= wrapped,
               qPrintable(QStringLiteral("the reply is clipped: %1px tall for %2px of text")
                              .arg(body->height()).arg(wrapped)));
      QVERIFY2(body->parentWidget()->height() >= body->height(),
               "the bubble is shorter than the text inside it");
    }

    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsClearCards.gui.moc"
