// MainWindow GUI e2e — The card's own shape: reduced motion, the bubble tail, its hug, and the jump arrows.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Reduced motion: the card is simply THERE on the short fade — no cloud, and above all
  // no card left sitting at opacity 0 for a flight that never ran.
  void reducedMotionChatCardArrivesAtOnce() {
    qputenv("STENCIL_NO_ANIM", "1");   // the suite's own default; set explicitly for the reader
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    win.chatDock->appendUser(QStringLiteral("hi"), {});
    QTest::qWait(120);
    QVERIFY2(!win.findChild<QWidget*>(stencil::gui::DisintegrateOverlay::OBJECT_NAME),
             "no dust under reduced motion");
    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock->findChildren<QFrame*>("chatCardUser")) card = f;
    QVERIFY(card);
    if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect()))
      QTRY_COMPARE(fx->opacity(), 1.0);
    beat();
  }

  // Samples the rendered PIXEL at the card's corner: the tail's widget geometry can look flush
  // while a broken flattened-corner radius in chatCardStyleSheet() still shows a gap.
  void chatBubbleTailRendersFlushNoGap() {
    // This case PAINTS the bubble's own corner, and with motion on the card hides behind its
    // arrival dust for the whole flight, so motion is pinned off here.
    const auto still = withoutMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat->setChecked(true);
    openTranscript(win);   // the dock's real width: a card appended into a sliver lays out wrong
    win.chatDock->appendUser(QStringLiteral("Give me 3 variants"), {});
    win.chatHistory.append({QStringLiteral("user"), QStringLiteral("Give me 3 variants"), {}});
    win.chatError(QStringLiteral("not connected to http://localhost:8090 (no token)"), QString());
    QTRY_VERIFY(win.chatDock->findChild<QFrame*>("chatCardError"));
    QFrame* errCard = nullptr;
    for (QFrame* f : win.chatDock->findChildren<QFrame*>("chatCardError")) errCard = f;
    QFrame* userCard = nullptr;
    for (QFrame* f : win.chatDock->findChildren<QFrame*>("chatCardUser")) userCard = f;
    QVERIFY(errCard && userCard);
    // Laid out first: a card still at its 18px birth size has no corner to sample.
    QTRY_VERIFY(errCard->width() > 40 && userCard->width() > 40);
    QImage shot = win.chatDock->grab().toImage();
    // A pixel just inside the card's flattened corner, against a reference pixel further in that
    // is unambiguously fill. Equal ⇒ one continuous fill; a rounded corner samples the transcript.
    const auto sampleFlushCorner = [&](QFrame* card, bool right, const char* what) {
      const QPoint corner = card->mapTo(win.chatDock,
          right ? card->rect().bottomRight() : card->rect().bottomLeft());
      const int dx = right ? -1 : 1;
      const QColor atCorner = shot.pixelColor(corner.x() + dx, corner.y() - 1);
      const QColor reference = shot.pixelColor(corner.x() + dx * 6, corner.y() - 6);
      QVERIFY2(atCorner == reference,
               qPrintable(QStringLiteral("%1: corner pixel %2 != interior fill %3 — a gap")
                              .arg(what, atCorner.name(QColor::HexArgb), reference.name(QColor::HexArgb))));
    };
    sampleFlushCorner(errCard, false, "error card (left tail)");
    sampleFlushCorner(userCard, true, "user card (right tail)");
    beat();
  }

  void chatBubbleHugsItsText() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.chatDock->show();
    // Fill the transcript until it scrolls FIRST: appending into an already scrollable transcript
    // toggles no scrollbar, so no healing viewport-resize re-measure follows.
    QScrollArea* scroll = openTranscript(win);
    fillUntilScrollable(win, scroll);
    QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
    settleLayout(win.chatDock, 50);
    const QString text = QStringLiteral(
        "This reply is deliberately long enough to wrap across several transcript lines, "
        "so a height reserved at the wrong measurement width visibly disagrees with the "
        "height the rendered text actually needs — the regression this test guards.");
    win.chatDock->appendAssistant(text);
    QTRY_VERIFY(noneEntering(scroll->widget()));   // the entrance drops its own claim
    settleLayout(win.chatDock, 120);              // …and the deferred relayout follows
    QLabel* body = nullptr;
    for (QLabel* l : win.chatDock->findChildren<QLabel*>())
      if (l->property("chatBody").toString() == text) body = l;
    QVERIFY(body);
    QVERIFY(body->width() > 100);   // really laid out at bubble width
    // What the text NEEDS at the rendered width — measured with the reservation
    // lifted, because QLabel::heightForWidth reports no less than minimumHeight.
    const int reserved = body->minimumHeight();
    body->setMinimumHeight(0);
    const int needed = body->heightForWidth(body->width());
    body->setMinimumHeight(reserved);
    QVERIFY2(reserved <= needed + 2,
             qPrintable(QStringLiteral("reserved %1 px for text needing %2 px")
                            .arg(reserved)
                            .arg(needed)));
    const auto* card = body->parentWidget();
    QVERIFY2(card->height() <= needed + 24,   // text + the card's 6px paddings, no dead band
             qPrintable(QStringLiteral("bubble %1 px tall around %2 px of text")
                            .arg(card->height())
                            .arg(needed)));
    beat();
  }

  // The transcript's jump pills rest translucent (they float OVER bubbles and
  // fully covered a short message) and return to full opacity under the cursor.
  void chatJumpArrowsRestTranslucent() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.chatDock->show();
    QScrollArea* scroll = openTranscript(win);
    QScrollBar* bar = fillUntilScrollable(win, scroll, 24);
    QVERIFY(bar->maximum() > 24);
    bar->setValue(bar->maximum() / 2);   // mid-log: both pills visible
    const auto jumps =
        win.chatDock->findChildren<QToolButton*>(QStringLiteral("chatJumpBtn"));
    QCOMPARE(jumps.size(), 2);
    // The pills rest at 0.7, the shared figure across the three surfaces (browser .chat-jump-btn);
    // hover restores full opacity and lifts the glyph from --text-muted to --text-main.
    const bool dark = stencil::gui::resolveDark(win.settings.themeMode);
    const QColor muted = stencil::gui::themePalette(dark, win.settings.accentColor).textMuted;
    const QColor main = stencil::gui::themePalette(dark, win.settings.accentColor).textMain;
    const auto glyphIs = [](QToolButton* b, const QColor& want) {
      const QImage im = b->icon().pixmap(14, 14).toImage();
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) {
          const QColor c = im.pixelColor(x, y);
          if (c.alpha() < 120) continue;
          if (qAbs(c.red() - want.red()) < 40 && qAbs(c.green() - want.green()) < 40 &&
              qAbs(c.blue() - want.blue()) < 40)
            return true;
        }
      return false;
    };
    for (QToolButton* b : jumps) {
      QTRY_VERIFY(b->isVisible());
      auto* fx = qobject_cast<QGraphicsOpacityEffect*>(b->graphicsEffect());
      QVERIFY2(fx, "jump pill must carry the rest-opacity effect");
      QCOMPARE(fx->opacity(), 0.7);
      QVERIFY2(glyphIs(b, muted), "the pill's rest glyph is not --text-muted");
      QEvent enter(QEvent::Enter);
      QCoreApplication::sendEvent(b, &enter);
      QCOMPARE(fx->opacity(), 1.0);
      QVERIFY2(glyphIs(b, main), "hover must brighten the glyph to --text-main");
      QEvent leave(QEvent::Leave);
      QCoreApplication::sendEvent(b, &leave);
      QCOMPARE(fx->opacity(), 0.7);
      QVERIFY2(glyphIs(b, muted), "leaving must drop the glyph back to --text-muted");
    }
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatCardsShape.gui.moc"
