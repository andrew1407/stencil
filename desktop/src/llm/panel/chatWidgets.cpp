#include "chatWidgets.hpp"

#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/modal/modalReveal.hpp"

#include <QFrame>
#include <QPixmap>
#include <QGraphicsOpacityEffect>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>

namespace stencil::gui {

  // 1.5x brisker than a list row's 900 ms flight, off the ITEM clock: a message is read as it arrives.
  constexpr int CHAT_ARRIVE_MS = DisintegrateOverlay::ITEM_MS * 2 / 3;

  QRect scrollViewportInHost(QScrollArea* scroll, QWidget* host) {
    if (!scroll || !scroll->viewport() || !host) return host ? host->rect() : QRect();
    return QRect(scroll->viewport()->mapTo(host, QPoint(0, 0)), scroll->viewport()->size());
  }

  // browser surface/motion.js clipDustToScroller
  void clipChatDustToScroller(DisintegrateOverlay* dust, QScrollArea* scroll, QWidget* host) {
    if (dust) dust->setPaintClip(scrollViewportInHost(scroll, host));
  }

  // Gather point off the side the card sits against, read off the GEOMETRY, not the role. Host coords.
  QPoint chatArrivalPoint(const QRect& card, const QRect& view) {
    const double reach = 0.9;
    const bool right = (view.right() - card.right()) <= (card.left() - view.left());
    const int cx = card.center().x();
    return QPoint(cx + int((right ? 1 : -1) * card.width() * reach), card.center().y());
  }

  bool chatCardFullyInViewport(QWidget* card, QScrollArea* scroll) {
    if (!card || !scroll || !scroll->viewport()) return false;
    const QRect view = scroll->viewport()->rect();
    const QPoint topLeft = card->mapTo(scroll->viewport(), QPoint(0, 0));
    return QRect(topLeft, card->size()).isValid()
        && topLeft.y() >= view.top() - 1
        && topLeft.y() + card->height() <= view.bottom() + 1;
  }

  void trackChatCardDust(QWidget* card, DisintegrateOverlay* overlay, QScrollArea* scroll,
                         std::function<void()> settle) {
    QPointer<QWidget> cp(card);
    QPointer<DisintegrateOverlay> op(overlay);
    const QSize shot = card->size();
    QWidget* host = overlay->parentWidget();
    // A SURFACE cloud follows a scroll by retargeting, never by moving the layer.
    auto at = std::make_shared<QPoint>(card->mapTo(host, QPoint(0, 0)));
    // Parented to the CARD and stopped by the overlay's own death — a raw-pointer singleShot crashed here.
    auto* timer = new QTimer(card);
    timer->setInterval(CHAT_GATHER_SETTLE_MS);
    QObject::connect(timer, &QTimer::timeout, card, [cp, op, scroll, shot, timer, settle, at] {
      if (!op || !cp) { timer->stop(); timer->deleteLater(); return; }
      if (!chatCardFullyInViewport(cp, scroll) || cp->size() != shot) {
        op->deleteLater();
        timer->stop();
        timer->deleteLater();
        settle();
        return;
      }
      QWidget* host = op->parentWidget();
      const QPoint now = cp->mapTo(host, QPoint(0, 0));
      op->retarget(now - *at);
      *at = now;
      clipChatDustToScroller(op, scroll, host);
      op->raise();
    });
    timer->start();
  }

  void gatherChatCardIn(QWidget* card, QVBoxLayout* layout, QScrollArea* scroll,
                        QWidget* host, int cols, int rows, std::function<void()> settle,
                        std::function<void()> onFlight, int tries, QSize lastSize) {
    if (!card || !layout || !host) return;
    // A card already out of the layout is leaving; an entrance would fight the fade.
    if (layout->indexOf(card) < 0) return;
    // Every widget is guarded, not just the card: a nested event loop (QMenu::exec) can fire this
    // timer after the HOST window is gone while the card is still alive (a dangling parent).
    const auto retry = [card, layout, scroll, host, cols, rows, settle, onFlight,
                        tries](QSize sizeNow) {
      QPointer<QWidget> cp(card);
      QPointer<QVBoxLayout> lp(layout);
      QPointer<QScrollArea> sp(scroll);
      QPointer<QWidget> hp(host);
      QTimer::singleShot(CHAT_GATHER_SETTLE_MS, card,
                         [cp, lp, sp, hp, cols, rows, settle, onFlight, tries,
                          sizeNow] {
        if (cp && lp && hp)
          gatherChatCardIn(cp, lp, sp, hp, cols, rows, settle, onFlight,
                           tries - 1, sizeNow);
      });
    };
    // A card inserted this turn is still HIDDEN (a box layout skips hidden widgets): show, then lay out.
    if (!card->isVisible()) card->show();
    layout->activate();
    // Bounded wait: the message must never be held up behind a layout that may never come.
    if ((card->width() < 8 || card->height() < 8) && tries > 0) { retry(QSize()); return; }
    if (support::motionReduced()) { settle(); return; }
    // Require two matching size reads in a row; a stale grab was silently dropped by the tracker.
    if (card->size() != lastSize && tries > 0) { retry(card->size()); return; }
    // The cloud is drawn on the window, unclipped by the scroller.
    if (!chatCardFullyInViewport(card, scroll)) {
      if (tries > 0) { retry(lastSize); return; }
      settle();
      return;
    }
    // grab() renders THROUGH the graphics effect; off for the photograph, back on before returning.
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect());
    if (fx) fx->setEnabled(false);
    const QPixmap snap = card->grab();
    if (fx) fx->setEnabled(true);
    if (snap.isNull()) { settle(); return; }
    // The toast's flight (Notifications.cpp dustToastIn); cols/rows only cap overSurface's grid.
    const QRect box(card->mapTo(host, QPoint(0, 0)), card->size());
    auto* dust = DisintegrateOverlay::overSurface(
        snap, box, host, chatArrivalPoint(box, scrollViewportInHost(scroll, host)),
        /*gather=*/true, CHAT_ARRIVE_MS, card->palette().color(QPalette::WindowText),
        cols * rows);
    if (!dust) { settle(); return; }
    // Confined to the transcript: the layer is drawn on the WINDOW (clipDustToScroller).
    clipChatDustToScroller(dust, scroll, host);
    trackChatCardDust(card, dust, scroll, settle);
    if (onFlight) onFlight();
    // A CUT in the frame the overlay deletes itself: a fade-up would double the arrival.
    QPointer<QWidget> cp(card);
    // The card takes over at three quarters of the flight; the last motes settle on top of it.
    QTimer::singleShot(CHAT_ARRIVE_MS * 3 / 4, card, [cp, settle] { if (cp) settle(); });
  }

}  // namespace stencil::gui
