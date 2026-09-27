#pragma once
// Shared ground for the chat-turns viewport case (MainWindow.chatTurnsViewport.gui.cpp): where a
// transcript row's "…" lands when the row is clipped, a sliver, or hard against a narrow edge.
#include "../../MainWindow.gui.hpp"

#include <functional>

namespace stencil::guitest {

  inline QRect globalRect(QWidget* w) {
    return QRect(w->mapToGlobal(QPoint(0, 0)), w->size());
  }
  inline QToolButton* moreOf(QFrame* card) {
    return qobject_cast<QToolButton*>(card->property("chatMoreBtn").value<QObject*>());
  }
  // Hover the card the way the user does, then read where its "…" landed.
  inline void hover(QFrame* card) {
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(card, &enter);
  }
  // `pillsBox` is the jump pills' current box: a row whose "…" would land under them hides it instead
  // (placeChatCardMore's shift-else-hide), so checks wanting a SHOWN "…" must skip that corner.
  inline bool crowdedByPills(const std::function<QRect()>& pillsBox, const QRect& cardGlobal) {
    const QRect p = pillsBox();
    return p.isValid() && cardGlobal.intersects(p.adjusted(-8, -8, 8, 8));
  }
  // Every surface is checked the same way: park the scroll somewhere in the
  // middle, then take a row clipped at each edge.
  inline void checkSurface(QWidget* host, QScrollArea* scroll, const char* what) {
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
  }

  // NARROW transcript: the bubbles reach the edge, which is where the "…"
  // (it hangs OUTSIDE the bubble) was landing half over the boundary.
  inline void checkNarrow(const std::function<QRect()>& pillsBox, QWidget* host, QScrollArea* scroll,
                          const char* what) {
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
          if (!seen.contains(g) || crowdedByPills(pillsBox, g)) continue;
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
  }

  // A row whose visible SLICE is too short to hold the pill shows none: the clamp would park it
  // across the neighbouring card. A fully visible row clear of the jump pills always shows it.
  inline void checkSliver(const std::function<QRect()>& pillsBox, QWidget* host, QScrollArea* scroll,
                          const char* what) {
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
        if (vp.contains(g) && !crowdedByPills(pillsBox, g)) whole = card;
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
  }

}  // namespace stencil::guitest
