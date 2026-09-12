// MainWindow GUI e2e — A chat CARD in its own right: the dust it arrives out of, the bubble
// that hugs its text and the tail that hangs off it, the empty-state chips, the jump pills.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A chat card arrives the way a toast does: its dust gathers into place out of a point off
  // the side it sits against, while the bubble is held back behind the motes (which side is
  // chatCardDustArrivesFromTheCardsOwnSide's job — this is that it flies at all). Browser
  // twin: motion.js chatIn. The context menu's assistant panel is the third chat surface on
  // this front-end and mirrors the dock's transcript, so a row arriving there is an arrival
  // too (ChatMenuPanel::gatherRow, the dock's animateCardIn in miniature).
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
        QVERIFY(win.chatMenuPanel_);
        win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
        win.chatMenuPanel_->show();
        settleLayout(win.chatMenuPanel_, 300);   // its width arrives before a grab can read it
      } else {
        win.actChat_->setChecked(true);
        QTRY_VERIFY(win.chatDock_->isVisible());
      }
      // A dock card's cloud is a SURFACE flight parented to the window; the panel gathers
      // its rows inside itself. A row counts as a card on either surface: the dock's carry a
      // "chatCard…" object name, the panel's mirrored rows the "chatMoreBtn" property.
      const auto dust = [&win, panel, DUST] {
        return panel ? win.chatMenuPanel_->findChild<QWidget*>(DUST)
                     : win.findChild<QWidget*>(DUST);
      };
      const auto rows = [&win, panel] {
        QList<QFrame*> out;
        QWidget* surface = panel ? static_cast<QWidget*>(win.chatMenuPanel_) : win.chatDock_;
        for (QFrame* f : surface->findChildren<QFrame*>())
          if (panel ? f->property("chatMoreBtn").isValid()
                    : f->objectName().startsWith(QLatin1String("chatCard")))
            out.append(f);
        return out;
      };
      QTRY_VERIFY_WITH_TIMEOUT(!dust(), stencil::gui::DisintegrateOverlay::DUST_MS + 2000);

      // The message you send, the "…" holding the turn, the reply, and a failure — every one
      // of them is a card appearing, so every one of them gathers. The panel is fed the same
      // turn through the mirror instead, which is the only way a row reaches it.
      QVector<std::function<void()>> appends;
      if (panel) {
        appends = {[&win] {
          win.chatMirror(QStringLiteral("You"), QStringLiteral("crop it square"), false);
        }};
      } else {
        appends = {
            [&win] { win.chatDock_->appendUser(QStringLiteral("crop it square"), {}); },
            [&win] { win.chatDock_->showPending(); },
            [&win] { win.chatDock_->appendAssistant(QStringLiteral("Cropped.")); },
            [&win] {
              win.chatDock_->appendError(
                  QStringLiteral("Couldn't reach Ollama at localhost:11434 (fetch failed)"),
                  QStringLiteral("crop it square"));
            }};
      }
      for (const auto& append : appends) {
        append();
        // The grab is deferred (appendTranscriptCard hands the caller an EMPTY card — the
        // dust has to be a photograph of the FINISHED bubble, laid out at its real width),
        // so the cloud shows up a beat later, not in this tick.
        QTRY_VERIFY_WITH_TIMEOUT(dust() != nullptr, 3000);
        QFrame* card = rows().isEmpty() ? nullptr : rows().last();
        QVERIFY(card);
        // Held FULLY hidden while the motes fly — they ARE the bubble forming. Fading it up
        // underneath them drew the finished card first and played the animation over the top
        // of it, which is the one thing an arrival must not do (the reported bug).
        auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect());
        QVERIFY2(fx && fx->opacity() == 0.0, "the card is invisible until its motes land");
        QTRY_VERIFY_WITH_TIMEOUT(!dust(), stencil::gui::DisintegrateOverlay::DUST_MS + 2000);
        if (!panel) win.chatDock_->clearPending();   // the "…" must not outlive its own case
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

  // REGRESSION: on a transcript long enough to scroll, the cloud was photographed before
  // the scroll landed, so the motes flew at the card's pre-scroll box and rained over the
  // composer. The entrance now waits a frame for scrollToBottom() and refuses to fly for a
  // card not wholly inside the viewport.
  void chatCardDustArrivesFromTheCardsOwnSide() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 620);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
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
      for (QFrame* f : win.chatDock_->findChildren<QFrame*>(QString::fromLatin1(cardName))) card = f;
      QVERIFY2(card, what);
      const QRect box(card->mapTo(&win, QPoint(0, 0)), card->size());
      const int dx = dust->surfaceTarget().x() - box.center().x();
      QVERIFY2(side * dx > 0, what);
      // …and clear of the card itself, so the motes visibly travel in over its edge.
      QVERIFY2(qAbs(dx) > box.width() / 2, what);
      QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) == nullptr,
                               stencil::gui::DisintegrateOverlay::DUST_MS + 2000);
    };
    win.chatDock_->appendUser(QStringLiteral("mine, on the right"), {});
    arrivesFrom("chatCardUser", +1, "a user message must gather from the RIGHT");
    win.chatDock_->appendAssistant(QStringLiteral("and the reply, on the left"));
    arrivesFrom("chatCardAssistant", -1, "an assistant message must gather from the LEFT");
  }

  // REGRESSION (two of them, one fixture — both need a transcript long enough that every
  // further append really scrolls). A card's cloud was photographed before the scroll landed,
  // so the motes flew at the card's pre-scroll box and rained over the composer; the entrance
  // now waits a frame for scrollToBottom() and refuses to fly for a card not wholly inside
  // the viewport. And a card's dust is a SNAPSHOT placed once while the transcript keeps
  // moving under it — appending the next card scrolls the view, a wrapped label re-reserves
  // its height, the dock is resized. Left where it launched, that snapshot was drawn over a
  // NEIGHBOURING bubble, which reads as one message overlapping the one below it (reported).
  // The overlay now follows its own card, and is dropped the moment the card moves or resizes.
  void chatCardDustStaysWithItsCardInAScrolledTranscript() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 660);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QScrollArea* scroll = openTranscript(win);
    const char* DUST = stencil::gui::DisintegrateOverlay::OBJECT_NAME;

    // Fill it well past one viewport, so every further append really does scroll.
    for (int i = 0; i < 10; ++i) {
      win.chatDock_->appendUser(QStringLiteral("question %1 long enough to wrap onto a second line").arg(i), {});
      win.chatDock_->appendAssistant(QStringLiteral("reply %1, also long enough to take real height in the column").arg(i));
    }
    fillUntilScrollable(win, scroll);
    QTRY_VERIFY_WITH_TIMEOUT(!win.findChild<QWidget*>(DUST),
                             stencil::gui::DisintegrateOverlay::DUST_MS + 4000);
    QVERIFY2(scroll->verticalScrollBar()->maximum() > 0, "the transcript never became scrollable");

    // ── 1. no mote may reach the composer ──
    win.chatDock_->appendUser(QStringLiteral("one more, which has to scroll into view"), {});
    // The cloud that belongs to THIS card. The fill above can still have arrivals in
    // flight (gatherChatCardIn waits the layout out in hops), and grabbing whichever
    // overlay happened to exist measured one card's cloud against another's box.
    QFrame* card = nullptr;
    stencil::gui::DisintegrateOverlay* fx = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
      card = nullptr;
      for (QFrame* f : win.chatDock_->findChildren<QFrame*>(QStringLiteral("chatCardUser"))) card = f;
      if (!card) return false;
      const QRect box(card->mapTo(&win, QPoint(0, 0)), card->size());
      for (QWidget* w : win.findChildren<QWidget*>(QString::fromLatin1(DUST))) {
        auto* o = static_cast<stencil::gui::DisintegrateOverlay*>(w);
        if (o->surfacePicture() == box) { fx = o; return true; }
      }
      return false;
    }()), 4000);
    QVERIFY(fx);
    QVERIFY(card);
    // The card the cloud stands in for is inside the viewport — so the motes cannot be
    // flying anywhere near the composer.
    const QRect viewGlobal(scroll->viewport()->mapToGlobal(QPoint(0, 0)), scroll->viewport()->size());
    const QRect cardGlobal(card->mapToGlobal(QPoint(0, 0)), card->size());
    // VERTICALLY inside — the axis the scroll moves, and the one the composer is on. A
    // bubble's own furniture (the "…" trigger) deliberately hangs outside it sideways.
    QVERIFY2(cardGlobal.top() >= viewGlobal.top() && cardGlobal.bottom() <= viewGlobal.bottom(),
             "the dusted card is not wholly in the viewport");
    // An arrival is a surface cloud, so the layer is the whole window and carries the
    // card's box inside it; what matters is where it may PAINT. The clip is the
    // transcript's viewport, so no mote reaches the composer however far it flies.
    QCOMPARE(fx->paintClip(),
             QRect(scroll->viewport()->mapTo(&win, QPoint(0, 0)), scroll->viewport()->size()));
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) == nullptr,
                             stencil::gui::DisintegrateOverlay::DUST_MS + 2000);
    if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect()))
      QTRY_COMPARE(fx->opacity(), 1.0);
    // ── 2. the cloud follows its own card, never stranding between two ──
    // A turn's two cards land back to back — the second one's append is what scrolls the
    // first one's snapshot off its subject.
    win.chatDock_->appendUser(QStringLiteral("Give me 3 variants: rotated, tinted, cropped"), {});
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) != nullptr, 3000);
    win.chatDock_->appendError(QStringLiteral("not connected to http://localhost:8090 (no token)"),
                               QStringLiteral("retry me"));

    // Watch the whole flight: every live cloud must sit on a card, never between two. The
    // WIDGET covers the whole host and follows its card by retargeting what it draws
    // (chatWidgets.cpp trackChatCardDust), so the PICTURE is what has to line up. Its timer
    // ticks at 16ms, so a few samples out of step are lag — under load it can miss several
    // in a row — and only a cloud that STAYS off its card strands. A real one never returns:
    // stop the tracker and this counts hundreds, not a handful.
    QHash<QWidget*, int> misses;
    int strandedFrames = 0, sampled = 0;
    for (int f = 0; f < 90; ++f) {
      QTest::qWait(16);
      for (QWidget* d : win.findChildren<QWidget*>(DUST)) {
        // OBJECT_NAME is the overlay's own, set nowhere else — and the class carries no
        // Q_OBJECT, so this is the cast available.
        auto* fx = static_cast<stencil::gui::DisintegrateOverlay*>(d);
        if (!fx->isVisible()) continue;
        const QRect pic = fx->surfacePicture();   // host coords; invalid on an item cloud
        if (!pic.isValid()) continue;
        ++sampled;
        bool onACard = false;
        for (QFrame* c : win.chatDock_->findChildren<QFrame*>()) {
          if (!c->objectName().startsWith(QLatin1String("chatCard"))) continue;
          const QPoint cTL = c->mapTo(&win, QPoint(0, 0));
          // The picture IS the card's box, retargeted by the exact delta, so a tracked
          // cloud lands on it to the pixel — the slack is for rounding, nothing else.
          constexpr int SLACK = 2;
          if (qAbs(pic.left() - cTL.x()) <= SLACK && qAbs(pic.top() - cTL.y()) <= SLACK) {
            onACard = true;
            break;
          }
        }
        if (onACard) misses.remove(d);
        else if (++misses[d] > 5) ++strandedFrames;
      }
    }
    QVERIFY2(sampled > 0, "no overlay was ever sampled — the check would be vacuous");
    QCOMPARE(strandedFrames, 0);

    // …and when the tracker DOES drop a stale snapshot, the card it was standing in for
    // takes over in that same moment. The veil is otherwise lifted only at the end of the
    // full flight, so a cancel that just killed the motes left the message invisible with
    // nothing in its place (the browser twin had exactly this, found by resizing a live
    // entry mid-flight). Resizing the dock changes every card's width — the drop path.
    QTRY_VERIFY_WITH_TIMEOUT(!win.findChild<QWidget*>(DUST),
                             stencil::gui::DisintegrateOverlay::DUST_MS + 3000);
    win.chatDock_->appendUser(QStringLiteral("resized mid-flight"), {});
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) != nullptr, 3000);
    win.chatDock_->resize(win.chatDock_->width() - 90, win.chatDock_->height());
    settleLayout(win.chatDock_, 120);
    QFrame* resized = nullptr;
    for (QFrame* c : win.chatDock_->findChildren<QFrame*>("chatCardUser")) resized = c;
    QVERIFY(resized);
    if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(resized->graphicsEffect()))
      QTRY_VERIFY2_WITH_TIMEOUT(fx->opacity() == 1.0,
                                "a card whose snapshot was dropped must not stay invisible",
                                1500);

    // …and nothing is left behind: every card lands visible, at its resting margins.
    QTRY_VERIFY_WITH_TIMEOUT(!win.findChild<QWidget*>(DUST),
                             stencil::gui::DisintegrateOverlay::DUST_MS + 3000);
    for (QFrame* c : win.chatDock_->findChildren<QFrame*>()) {
      if (!c->objectName().startsWith(QLatin1String("chatCard"))) continue;
      if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(c->graphicsEffect()))
        QTRY_COMPARE(fx->opacity(), 1.0);
    }
    beat();
  }

  // REGRESSION: the empty-state chips rendered as sharp RECTANGLES (reported). Qt draws a
  // square box — silently — when border-radius exceeds half the widget's height, and these
  // chips settle at 28px while the sheet asked for the browser's 16. Reading the height at
  // style time does not save it either: the flow layout compresses the chip from 32 to 28
  // afterwards, so the radius has to be pinned to the floor the app-wide sheet guarantees.
  void suggestionChipsAreRoundedPills() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    awaitAnim(win.chatAnim_);   // the open slide, on its own end
    QWidget* chips = win.chatDock_->findChild<QWidget*>(QStringLiteral("chatSuggest"));
    QVERIFY(chips);
    const auto btns = chips->findChildren<QPushButton*>(QStringLiteral("chatSuggestChip"));
    QCOMPARE(btns.size(), 4);

    for (QPushButton* chip : btns) {
      // The radius the sheet asks for must be one this chip can actually carry.
      const QRegularExpression re(QStringLiteral("border-radius:(\\d+)px"));
      const QRegularExpressionMatch m = re.match(chip->styleSheet());
      QVERIFY2(m.hasMatch(), "the chip carries no border-radius at all");
      const int radius = m.captured(1).toInt();
      QVERIFY2(radius * 2 <= chip->height(),
               qPrintable(QStringLiteral("radius %1 exceeds half of the chip's %2px height — "
                                         "Qt renders that as a rectangle")
                              .arg(radius).arg(chip->height())));
      QVERIFY2(radius >= 8, "…and it still has to read as a pill, not a soft rectangle");
    }

    // …and it really PAINTS rounded: rendered onto white, the corners must show white
    // through. grab() alone cannot tell — outside a rounded corner it leaves transparent
    // pixels, which over this dark theme look exactly like the chip's own fill.
    QPushButton* chip = btns.first();
    QPixmap shot(chip->size());
    shot.fill(Qt::white);
    chip->render(&shot, QPoint(), QRegion(), QWidget::DrawChildren);
    const QImage img = shot.toImage();
    QVERIFY2(img.pixelColor(0, 0) == QColor(Qt::white),
             "the top-left corner is filled — the chip is a rectangle");
    QVERIFY2(img.pixelColor(img.width() - 1, img.height() - 1) == QColor(Qt::white),
             "the bottom-right corner is filled — the chip is a rectangle");
    // …while its middle is of course painted.
    QVERIFY2(img.pixelColor(img.width() / 2, img.height() / 2) != QColor(Qt::white),
             "the chip did not paint at all — the corner check would be vacuous");
    beat();
  }

  // Reduced motion: the card is simply THERE on the short fade — no cloud, and above all
  // no card left sitting at opacity 0 for a flight that never ran.
  void reducedMotionChatCardArrivesAtOnce() {
    qputenv("STENCIL_NO_ANIM", "1");   // the suite's own default; set explicitly for the reader
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.chatDock_->appendUser(QStringLiteral("hi"), {});
    QTest::qWait(120);
    QVERIFY2(!win.findChild<QWidget*>(stencil::gui::DisintegrateOverlay::OBJECT_NAME),
             "no dust under reduced motion");
    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardUser")) card = f;
    QVERIFY(card);
    if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect()))
      QTRY_COMPARE(fx->opacity(), 1.0);
    beat();
  }

  // A tail's WIDGET geometry can look perfectly flush (chatSwapSidesReskinsRetroactively
  // checks that) while the pixels underneath still show a gap, so this samples the
  // rendered PIXEL at the card's corner: a broken flattened-corner radius in
  // chatCardStyleSheet() fails here rather than only on a user's eyeball.
  void chatBubbleTailRendersFlushNoGap() {
    // This case PAINTS: it samples the bubble's own corner. With motion on, the card is
    // hidden behind its arrival dust for the whole flight, so the grab caught motes and
    // the corner came back a different blend every run (and a longer chat clock made it
    // reproducible). Pin motion off for it — the flight has its own cases.
    const auto still = withoutMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    openTranscript(win);   // the dock's real width: a card appended into a sliver lays out wrong
    win.chatDock_->appendUser(QStringLiteral("Give me 3 variants"), {});
    win.chatHistory_.append({QStringLiteral("user"), QStringLiteral("Give me 3 variants"), {}});
    win.chatError(QStringLiteral("not connected to http://localhost:8090 (no token)"), QString());
    QTRY_VERIFY(win.chatDock_->findChild<QFrame*>("chatCardError"));
    QFrame* errCard = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardError")) errCard = f;
    QFrame* userCard = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardUser")) userCard = f;
    QVERIFY(errCard && userCard);
    QImage shot = win.chatDock_->grab().toImage();
    // A pixel just inside the card's own flattened corner (well within the
    // round notch a 10px radius would otherwise leave unfilled there) —
    // sampled against a reference pixel a few px further in, which is
    // unambiguously plain bubble fill either way. Equal ⇒ the corner reads as
    // one continuous fill, same as the reference; a regressed (still rounded)
    // corner would instead sample the transcript's own, different background.
    const auto sampleFlushCorner = [&](QFrame* card, bool right, const char* what) {
      const QPoint corner = card->mapTo(win.chatDock_,
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
    win.chatDock_->show();
    // Fill the transcript until it scrolls FIRST: appending into an already
    // scrollable transcript triggers no scrollbar toggle, so no healing
    // viewport-resize re-measure follows — exactly the intermittent case users
    // hit, where the first (wrong-width) reservation was the one that stuck.
    QScrollArea* scroll = openTranscript(win);
    fillUntilScrollable(win, scroll);
    QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
    settleLayout(win.chatDock_, 50);
    const QString text = QStringLiteral(
        "This reply is deliberately long enough to wrap across several transcript lines, "
        "so a height reserved at the wrong measurement width visibly disagrees with the "
        "height the rendered text actually needs — the regression this test guards.");
    win.chatDock_->appendAssistant(text);
    QTRY_VERIFY(noneEntering(scroll->widget()));   // the entrance drops its own claim
    settleLayout(win.chatDock_, 120);              // …and the deferred relayout follows
    QLabel* body = nullptr;
    for (QLabel* l : win.chatDock_->findChildren<QLabel*>())
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
    win.chatDock_->show();
    QScrollArea* scroll = openTranscript(win);
    QScrollBar* bar = fillUntilScrollable(win, scroll, 24);
    QVERIFY(bar->maximum() > 24);
    bar->setValue(bar->maximum() / 2);   // mid-log: both pills visible
    const auto jumps =
        win.chatDock_->findChildren<QToolButton*>(QStringLiteral("chatJumpBtn"));
    QCOMPARE(jumps.size(), 2);
    // The pills rest at 0.7 — the shared figure across the three surfaces (the browser's
    // .chat-jump-btn and every row "…" trigger); hover restores full opacity and
    // brightens the glyph from --text-muted to --text-main.
    // …in whichever theme this window actually resolved to.
    const bool dark = stencil::gui::resolveDark(win.settings_.themeMode);
    const QColor muted = stencil::gui::themePalette(dark, win.settings_.accentColor).textMuted;
    const QColor main = stencil::gui::themePalette(dark, win.settings_.accentColor).textMain;
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
#include "MainWindow.chatCards.gui.moc"
