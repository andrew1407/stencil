#include "chatWidgets.hpp"

#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/genie/GenieGhost.hpp"
#include "../../support/genie/ghostLabel.hpp"
#include "../../support/menu/popupSlide.hpp"
#include "../../support/motionPrefs.hpp"
#include "../../support/vsync/FramePulse.hpp"

#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPixmap>
#include <QScrollArea>

#include <memory>

namespace stencil::gui {

  namespace {
    // 1.5x brisker than a list row's 900 ms flight, off the ITEM clock: a message is read as it arrives.
    constexpr int CHAT_ARRIVE_MS = DisintegrateOverlay::ITEM_MS * 2 / 3;

    QRect scrollViewportInHost(QScrollArea* scroll, QWidget* host) {
      if (!scroll || !scroll->viewport() || !host) return host ? host->rect() : QRect();
      return QRect(scroll->viewport()->mapTo(host, QPoint(0, 0)), scroll->viewport()->size());
    }

    // browser control/chatFx.js clipDustToScroller
    void clipChatDustToScroller(DisintegrateOverlay* dust, QScrollArea* scroll, QWidget* host) {
      if (dust) dust->setPaintClip(scrollViewportInHost(scroll, host));
    }

    // Gather point off the side the card sits against, read off the GEOMETRY, not the role. Host coords; the reach
    // past the side is a share of the card's width (common/config/motion.json CHAT_ENTER_REACH).
    QPoint chatArrivalPoint(const QRect& card, const QRect& view) {
      const bool isRight = isOnRightSide(view, card);
      const double reach = support::motionTableShare("CHAT_ENTER_REACH", 0.9);
      return QPoint(card.center().x() + static_cast<int>((isRight ? 1 : -1) * card.width() * reach), card.center().y());
    }
  }  // namespace

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
    // On the display's own clock (support/vsync), owned by the CARD and over with the overlay's death.
    support::FramePulse::instance().join(card, [cp, op, scroll, shot, settle, at] {
      if (!op || !cp) return false;
      if (!chatCardFullyInViewport(cp, scroll) || cp->size() != shot) {
        op->deleteLater();
        settle();
        return false;
      }
      QWidget* host = op->parentWidget();
      const QPoint now = cp->mapTo(host, QPoint(0, 0));
      op->retarget(now - *at);
      *at = now;
      clipChatDustToScroller(op, scroll, host);
      op->raise();
      return true;
    });
  }

  // grab() fills the card's square window background behind its rounded bubble, and those corners showed
  // on the moving picture: rendered onto clear pixels without it, only the bubble travels.
  QPixmap chatCardShot(QWidget* card) {
    const qreal dpr = card->devicePixelRatioF();
    QPixmap clear(card->size() * dpr);
    clear.setDevicePixelRatio(dpr);
    clear.fill(Qt::transparent);
    auto* veil = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect());   // the entering card's veil
    if (veil) veil->setEnabled(false);
    card->render(&clear, QPoint(), QRegion(), QWidget::DrawChildren);
    if (veil) veil->setEnabled(true);
    return clear;
  }

  QRect chatTailHome(const QRect& seen, const QRect& at) {
    const bool isRight = isOnRightSide(seen, at);
    return QRect(isRight ? seen.right() - 2 : seen.left() + 1, std::max(seen.top(), at.bottom() - 17), 2, 16);
  }

  bool pourCardIn(QWidget* card, QScrollArea* scroll, std::function<void()> done) {
    QWidget* view = scroll ? scroll->viewport() : nullptr;
    if (!view) return false;
    const QRect at(card->mapToGlobal(QPoint(0, 0)), card->size());
    const QRect seen(view->mapToGlobal(QPoint(0, 0)), view->size());
    return support::genie::fly(view, chatCardShot(card), at, chatTailHome(seen, at), /*isOpening=*/true, /*asWindow=*/false,
                               std::move(done), support::POPUP_GENIE_MS);
  }

  // The card's picture slid in from its side (`shouldSlide`) or grown from its tail corner, a child of the card's
  // parent so the scroller clips it; `done` once it lands.
  void ghostCardIn(QWidget* card, bool isFromRight, bool shouldSlide, std::function<void()> done) {
    const QRect box = card->geometry();
    QLabel* ghost = support::genie::makeGhostLabel(card->parentWidget(), chatCardShot(card));
    auto place = [box, isFromRight, shouldSlide](QLabel& g, double t) {
      if (shouldSlide) {
        const int off = static_cast<int>((1.0 - t) * box.width() * 1.1) * (isFromRight ? 1 : -1);
        g.setGeometry(box.translated(off, 0));
        return;
      }
      g.setGeometry(support::genie::tailBox(box, 0.2 + 0.8 * t, isFromRight));
    };
    support::genie::flyGhostLabel(ghost, support::motionMs(shouldSlide ? chatSideMs() : chatGrowMs()), QEasingCurve::OutCubic,
                                  std::move(place), std::move(done));
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
    // A mode without particles keeps an entrance inside the transcript (browser chatFx.js): Sliding pours it up
    // out of the transcript's foot, Transition grows it out of its tail, any other mode slides it up, shown first.
    if (!dust) {
      const bool isFromRight = isOnRightSide(scrollViewportInHost(scroll, host), box);
      QPointer<QWidget> cp(card);
      if (support::isSlideMotionOk() && pourCardIn(card, scroll, [cp, settle] { if (cp) settle(); })) return;
      if (support::isSlideMotionOk() || support::isTransitionMotionOk()) {
        ghostCardIn(card, isFromRight, support::isSlideMotionOk(), [cp, settle] { if (cp) settle(); });
        return;
      }
      settle();
      if (onFlight && !support::isDustAllowed()) onFlight();
      return;
    }
    dust->bindToSurface(card);   // a card's arrival, not a surface flight: it dies with the dock
    // Confined to the transcript: the layer is drawn on the WINDOW (clipDustToScroller).
    clipChatDustToScroller(dust, scroll, host);
    trackChatCardDust(card, dust, scroll, settle);
    if (onFlight) onFlight();
    // A CUT in the frame the overlay deletes itself: a fade-up would double the arrival.
    QPointer<QWidget> cp(card);
    // The card takes over at three quarters of the flight; the last motes settle on top of it.
    QTimer::singleShot(support::motionMs(CHAT_ARRIVE_MS * 3 / 4), card, [cp, settle] { if (cp) settle(); });
  }

}  // namespace stencil::gui
