// MainWindow GUI e2e — A card's dust in a scrolled transcript, and the empty state's suggestion pills.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Both cases need a transcript long enough that every append really scrolls: the cloud is
  // photographed after the scroll lands, and its snapshot never covers a neighbouring bubble.
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
    // The cloud that belongs to THIS card: the fill above can still have arrivals in flight
    // (gatherChatCardIn waits the layout out in hops), so whichever overlay exists will not do.
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
    // An arrival is a surface cloud, so the layer is the whole window; what matters is where it
    // may PAINT. The clip is the transcript's viewport, so no mote reaches the composer.
    QCOMPARE(fx->paintClip(),
             QRect(scroll->viewport()->mapTo(&win, QPoint(0, 0)), scroll->viewport()->size()));
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) == nullptr,
                             stencil::gui::DisintegrateOverlay::DUST_MS + 2000);
    if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect()))
      QTRY_COMPARE(fx->opacity(), 1.0);
    // 2. the cloud follows its own card, never stranding between two: a turn's two cards land
    // back to back, and the second append is what scrolls the first one's snapshot off it.
    win.chatDock_->appendUser(QStringLiteral("Give me 3 variants: rotated, tinted, cropped"), {});
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) != nullptr, 3000);
    win.chatDock_->appendError(QStringLiteral("not connected to http://localhost:8090 (no token)"),
                               QStringLiteral("retry me"));

    // Every live cloud must sit on a card. The widget follows its card by retargeting what it
    // draws, so with its 16ms timer only a cloud that STAYS off its card strands.
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

    // When the tracker drops a stale snapshot, the card it stood in for takes over in that same
    // moment, or a cancelled flight leaves the message invisible. Resizing the dock drops it.
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

  // Qt draws a square box when border-radius exceeds half the widget's height, and these chips
  // settle at 28px against the browser's 16, so the pin is on the floor, after style time.
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

    // The box is the shared sheet's; the DOCK's rule is the first #chatSuggestChip in it.
    const QRegularExpression re(QStringLiteral("#chatSuggestChip \\{[^}]*radius: (\\d+)px"));
    const QRegularExpressionMatch m = re.match(qApp->styleSheet());
    QVERIFY2(m.hasMatch(), "the shared sheet gives the chip no border-radius at all");
    const int radius = m.captured(1).toInt();
    for (QPushButton* chip : btns) {
      QVERIFY2(radius * 2 <= chip->height(),
               qPrintable(QStringLiteral("radius %1 exceeds half of the chip's %2px height — "
                                         "Qt renders that as a rectangle")
                              .arg(radius).arg(chip->height())));
      QVERIFY2(radius >= 8, "…and it still has to read as a pill, not a soft rectangle");
    }

    // …and it really PAINTS rounded: rendered onto white, the corners show white through. grab()
    // alone leaves transparent pixels outside a corner, which read as the chip's own fill.
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

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatCardsScroll.gui.moc"
