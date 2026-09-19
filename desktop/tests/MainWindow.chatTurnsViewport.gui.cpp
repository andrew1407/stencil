// MainWindow GUI e2e — A row the transcript is CLIPPING still has a reachable "…".
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A row the transcript is CLIPPING still has a reachable "…": parked at the intersection of card
  // and viewport, not the card's own bottom. Clipped both ways, docked, floating and in the panel.
  void chatRowMenuStaysInsideTheViewport() {
    MainWindow win(nullptr, false);
    win.resize(1100, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    // Enough long messages that the transcript really scrolls: bubbles stretch to the full cap width
    // once they wrap (applyChatBubbleWidths), so more turns are needed to leave a row clipped.
    for (int i = 0; i < 14; ++i) {
      win.chatDock_->appendUser(
          QStringLiteral("Loading the image into incognito, converting to black & white "
                         "and cropping to portrait 3:4. Once it's done I will report "
                         "back with the result (%1).").arg(i));
      win.chatDock_->appendAssistant(
          QStringLiteral("Working on it — this reply is deliberately long so the row is "
                         "taller than a line and gets clipped by the viewport edge "
                         "while scrolling (%1).").arg(i));
    }
    settleLayout(win.chatDock_, 300);

    const auto globalRect = [](QWidget* w) {
      return QRect(w->mapToGlobal(QPoint(0, 0)), w->size());
    };
    const auto moreOf = [](QFrame* card) {
      return qobject_cast<QToolButton*>(card->property("chatMoreBtn").value<QObject*>());
    };
    // Hover the card the way the user does, then read where its "…" landed.
    const auto hover = [](QFrame* card) {
      QEvent enter(QEvent::Enter);
      QApplication::sendEvent(card, &enter);
    };
    // The jump pills' current box: a row whose "…" would land under them hides it instead
    // (placeChatCardMore's shift-else-hide), so checks wanting a SHOWN "…" must skip that corner.
    const auto pillsBox = [&] {
      return static_cast<stencil::gui::ChatDock*>(win.chatDock_)->jumpPillsGlobalRect();
    };
    const auto crowdedByPills = [&](const QRect& cardGlobal) {
      const QRect p = pillsBox();
      return p.isValid() && cardGlobal.intersects(p.adjusted(-8, -8, 8, 8));
    };
    // Every surface is checked the same way: park the scroll somewhere in the
    // middle, then take a row clipped at each edge.
    const auto checkSurface = [&](QWidget* host, QScrollArea* scroll, const char* what) {
      QVERIFY2(scroll, what);
      QScrollBar* bar = scroll->verticalScrollBar();
      QVERIFY2(bar->maximum() > 0, qPrintable(QString("%1: the transcript does not scroll")
                                                  .arg(what)));
      // Read LIVE: the transcript's width settles over the first scroll steps (301 -> 334).
      const auto vpNow = [&] { return globalRect(scroll->viewport()); };
      QFrame* clippedTop = nullptr;
      QFrame* clippedBottom = nullptr;
      // A slice tall enough to CARRY the pill (a shorter one deliberately hides
      // it — that rule has its own checks below).
      const int room = 21 + 8;
      // Bubbles that wrap to the same line count land in lockstep, so a card pitch dividing the viewport
      // evenly can leave no row both clipped and clear of the pills; walk out from the middle.
      for (int v = bar->maximum() / 2; v <= bar->maximum(); v += 12) {
        bar->setValue(v);
        QTest::qWait(30);
        clippedTop = clippedBottom = nullptr;
        for (QFrame* card : host->findChildren<QFrame*>()) {
          if (!card->property("chatMoreBtn").isValid()) continue;
          const QRect g = globalRect(card);
          if (g.intersected(vpNow()).height() < room) continue;
          if (g.top() < vpNow().top()) clippedTop = card;
          if (g.bottom() > vpNow().bottom()) clippedBottom = card;
        }
        if (!clippedTop || !clippedBottom) continue;
        bool bothShow = true;
        for (QFrame* card : {clippedTop, clippedBottom}) {
          hover(card);
          QToolButton* more = moreOf(card);
          if (!more || !more->isVisible()) { bothShow = false; break; }
        }
        if (bothShow) break;
      }
      QVERIFY2(clippedTop, qPrintable(QString("%1: no row clipped at the top").arg(what)));
      QVERIFY2(clippedBottom, qPrintable(QString("%1: no row clipped at the bottom").arg(what)));
      for (QFrame* card : {clippedTop, clippedBottom}) {
        hover(card);
        QToolButton* more = moreOf(card);
        QVERIFY2(more, qPrintable(QString("%1: a clipped row has no \"…\"").arg(what)));
        QVERIFY2(more->isVisible(),
                 qPrintable(QString("%1: the clipped row's \"…\" never showed").arg(what)));
        QVERIFY2(vpNow().contains(globalRect(more)),
                 qPrintable(QString("%1: the \"…\" sits outside the viewport (%2 vs %3)")
                                .arg(what)
                                .arg(QDebug::toString(globalRect(more)))
                                .arg(QDebug::toString(vpNow()))));
      }
      // …and it tracks the view: scrolling must not leave it behind.
      hover(clippedBottom);
      bar->setValue(bar->value() + 40);
      QTest::qWait(80);
      QToolButton* more = moreOf(clippedBottom);
      if (more->isVisible())
        QVERIFY2(globalRect(scroll->viewport()).contains(globalRect(more)),
                 qPrintable(QString("%1: the \"…\" fell out of the viewport on scroll").arg(what)));
    };

    // NARROW transcript: the bubbles reach the edge, which is where the "…"
    // (it hangs OUTSIDE the bubble) was landing half over the boundary.
    const auto checkNarrow = [&](QWidget* host, QScrollArea* scroll, const char* what) {
      QScrollBar* bar = scroll->verticalScrollBar();
      // This is the HORIZONTAL edge (a narrow column's pill hanging off the bubble's side), so the row
      // must be fully on screen AND clear of the jump pills; each kind is hunted at its own scroll.
      for (const char* kind : {"chatCardUser",         // its "…" hangs off the LEFT
                               "chatCardAssistant"}) { // …and this one's off the RIGHT
        QFrame* card = nullptr;
        for (int v = 0; v <= bar->maximum() && !card; v += 12) {
          bar->setValue(v);
          QTest::qWait(20);
          const QRect seen = globalRect(scroll->viewport());
          for (QFrame* c : host->findChildren<QFrame*>()) {
            if (!c->property("chatMoreBtn").isValid()) continue;
            if (c->objectName() != QLatin1String(kind)) continue;
            const QRect g = globalRect(c);
            if (!seen.contains(g) || crowdedByPills(g)) continue;
            card = c;
            break;
          }
        }
        QVERIFY2(card, qPrintable(QString("%1: no fully visible %2 row to hang a \"…\" off")
                                      .arg(what, kind)));
        const QRect vp = globalRect(scroll->viewport());
        hover(card);
        QToolButton* more = moreOf(card);
        QVERIFY(more && more->isVisible());
        const QRect r = globalRect(more);
        QVERIFY2(vp.contains(r),
                 qPrintable(QString("%1 (%2): the \"…\" is clipped by the edge (%3 vs %4)")
                                .arg(what, card->objectName(),
                                     QDebug::toString(r), QDebug::toString(vp))));
        QVERIFY2(r.left() >= vp.left() + 4 && r.right() <= vp.right() - 4,
                 qPrintable(QString("%1 (%2): no padding at the edge (%3 in %4)")
                                .arg(what, card->objectName(),
                                     QDebug::toString(r), QDebug::toString(vp))));
        // …and inside the widget that actually CLIPS it (the scrolled content),
        // not merely inside the viewport.
        QVERIFY2(globalRect(scroll->widget()).contains(r),
                 qPrintable(QString("%1 (%2): the pill hangs outside its clipping parent")
                                .arg(what, card->objectName())));
        // It is CHROME, not a transcript row: the edge-reveal must not dissolve
        // it (it is pinned at the edge by design) nor replace its accent glow.
        QVERIFY2(qobject_cast<QGraphicsDropShadowEffect*>(more->graphicsEffect()),
                 qPrintable(QString("%1 (%2): the \"…\" lost its glow to the edge reveal")
                                .arg(what, card->objectName())));
      }
    };

    // A row whose visible SLICE is too short to hold the pill shows none: the clamp would park it
    // across the neighbouring card. A fully visible row clear of the jump pills always shows it.
    const auto checkSliver = [&](QWidget* host, QScrollArea* scroll, const char* what) {
      QScrollBar* bar = scroll->verticalScrollBar();
      const QRect vp = globalRect(scroll->viewport());
      // Walk the scroll until some row is only a sliver at the viewport's edge.
      QFrame* sliver = nullptr;
      QFrame* whole = nullptr;
      for (int v = 0; v <= bar->maximum() && !sliver; v += 7) {
        bar->setValue(v);
        QTest::qWait(20);
        whole = nullptr;   // only a row fully visible at THIS position counts
        for (QFrame* card : host->findChildren<QFrame*>()) {
          if (!card->property("chatMoreBtn").isValid()) continue;
          const QRect g = globalRect(card);
          const int slice = g.intersected(vp).height();
          if (slice > 2 && slice < 16 && g.height() > 40) sliver = card;
          if (vp.contains(g) && !crowdedByPills(g)) whole = card;
        }
      }
      QVERIFY2(sliver, qPrintable(QString("%1: no row ended up a sliver").arg(what)));
      hover(sliver);
      QToolButton* more = moreOf(sliver);
      QVERIFY(more);
      QVERIFY2(!more->isVisible(),
               qPrintable(QString("%1: a sliver of a row still shows its \"…\"").arg(what)));
      // …and it must not be straddling anything if it somehow shows later.
      if (whole) {
        hover(whole);
        QToolButton* m2 = moreOf(whole);
        QVERIFY2(m2 && m2->isVisible(),
                 qPrintable(QString("%1: a fully visible row lost its \"…\"").arg(what)));
        const QRect r = globalRect(m2);
        for (QFrame* card : host->findChildren<QFrame*>()) {
          if (card == whole || !card->property("chatMoreBtn").isValid()) continue;
          if (!card->isVisible()) continue;
          QVERIFY2(!globalRect(card).intersects(r),
                   qPrintable(QString("%1: the \"…\" overlaps a neighbouring card").arg(what)));
        }
      }
    };

    QScrollArea* dockScroll = nullptr;
    for (QScrollArea* a : win.chatDock_->findChildren<QScrollArea*>()) dockScroll = a;
    checkSurface(win.chatDock_, dockScroll, "docked");
    checkSliver(win.chatDock_, dockScroll, "docked");
    win.chatDock_->setMinimumWidth(0);
    win.resizeDocks({win.chatDock_}, {230}, Qt::Horizontal);   // squeeze it
    settleLayout(win.chatDock_, 300);
    checkNarrow(win.chatDock_, dockScroll, "docked narrow");

    // The floating/compact shape uses the same transcript widget.
    win.chatDock_->setFloating(true);
    win.chatDock_->resize(360, 460);
    settleLayout(win.chatDock_, 300);
    checkSurface(win.chatDock_, dockScroll, "floating");
    win.chatDock_->resize(240, 460);   // narrow float: bubbles at both edges
    settleLayout(win.chatDock_, 300);
    checkNarrow(win.chatDock_, dockScroll, "floating narrow");
    win.chatDock_->setFloating(false);
    settleLayout(win.chatDock_, 200);

    // …and so does the context menu's panel.
    win.ensureChatMenuPanel();
    QVERIFY(win.chatMenuPanel_);
    // It normally lives inside the menu's QWidgetAction; show it in place so it
    // lays out (a hidden scroll area has no range to scroll).
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();
    // The panel is built lazily and mirrors the SHARED history, which these
    // direct dock appends never touched — mirror the same volume into it.
    for (int i = 0; i < 14; ++i) {
      win.chatMirror(QStringLiteral("You"),
                     QStringLiteral("Loading the image into incognito, converting to "
                                    "black & white and cropping to portrait 3:4 (%1).").arg(i),
                     false);
      win.chatMirror(QStringLiteral("Assistant"),
                     QStringLiteral("Working on it — this reply is deliberately long so "
                                    "the row is taller than a line and gets clipped by "
                                    "the viewport edge while scrolling (%1).").arg(i),
                     false);
    }
    settleLayout(win.chatMenuPanel_, 200);
    checkSurface(win.chatMenuPanel_,
                 win.chatMenuPanel_->findChild<QScrollArea*>("chatMenuTranscript"), "menu panel");
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsViewport.gui.moc"
