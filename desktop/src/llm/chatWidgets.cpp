#include "chatWidgets.hpp"

#include "../support/DisintegrateOverlay.hpp"
#include "../support/modalReveal.hpp"

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

  // browser motion.js clipDustToScroller
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

  void applyMutedText(QLabel* label) {
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
    label->setPalette(pal);
  }

  void applyDangerText(QLabel* label, const QColor& danger) {
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, danger);
    label->setPalette(pal);
  }

  QLabel* makeRoleLabel(const QString& role, QWidget* card) {
    auto* roleLabel = new QLabel(role, card);
    QFont f = roleLabel->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 0.85);
    roleLabel->setFont(f);
    return roleLabel;
  }

  // Qt::AutoText would RENDER a model's markup (and QTextDocument resolves local file
  // resources); the browser/extension use textContent only — this is the Qt spelling of that.
  QLabel* makePlainLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    return label;
  }

  // Bubble tail (browser .chat-msg-user::before/::after): border copy behind, 1 px smaller fill in front.
  static constexpr int TAIL_LEG = 9;
  static constexpr int TAIL_FILL_LEG = 8;
  static constexpr int TAIL_SHIFT = 1;
  static constexpr int TAIL_W = TAIL_SHIFT + TAIL_LEG;
  static constexpr int TAIL_H = TAIL_LEG;
  // Slack on every side: a path edge flush with the widget's clip is dropped or half-antialiased.
  static constexpr int TAIL_PAD = TAIL_SHIFT;
  static constexpr int TAIL_BOX_W = TAIL_W + 2 * TAIL_PAD;
  static constexpr int TAIL_BOX_H = TAIL_H + 2 * TAIL_PAD;

  ChatBubbleTail::ChatBubbleTail(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(TAIL_BOX_W, TAIL_BOX_H);
    hide();
  }

  void ChatBubbleTail::setColors(const QColor& fill, const QColor& border) {
    if (fill_ == fill && border_ == border) return;
    fill_ = fill;
    border_ = border;
    update();
  }

  void ChatBubbleTail::setSide(bool right) {
    if (right_ == right) return;
    right_ = right;
    update();
  }

  // Right-angle vertex `shift` px diagonally out from the bubble's corner; `leg` px legs.
  static QPainterPath tailTriangle(bool right, qreal leg, qreal shift, qreal boxW) {
    const qreal y = TAIL_FILL_LEG + shift;
    QPainterPath path;
    if (right) {
      path.moveTo(shift, y - leg);
      path.lineTo(shift, y);
      path.lineTo(shift + leg, y);
    } else {
      const qreal x = boxW - shift;
      path.moveTo(x, y - leg);
      path.lineTo(x, y);
      path.lineTo(x - leg, y);
    }
    path.closeSubpath();
    return path;
  }

  void ChatBubbleTail::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.translate(TAIL_PAD, TAIL_PAD);
    p.setBrush(border_);
    p.drawPath(tailTriangle(right_, TAIL_LEG, TAIL_SHIFT, TAIL_W));
    p.setBrush(fill_);
    p.drawPath(tailTriangle(right_, TAIL_FILL_LEG, 0, TAIL_W));
  }

  static void placeChatBubbleTailAt(QFrame* card, ChatBubbleTail* tail) {
    const QRect g = card->geometry();
    // QRect::right() is x()+width()-1; left() is the true edge, so it needs its own +1.
    const int left = (tail->isRight() ? g.right() : g.left() - TAIL_W + 1) - TAIL_PAD;
    tail->move(left, g.bottom() - TAIL_FILL_LEG - TAIL_PAD);
  }

  void placeChatBubbleTail(QFrame* card, const QColor& fill, const QColor& border, bool right) {
    if (!card) return;
    QWidget* host = card->parentWidget();
    if (!host) return;
    auto* tail = qobject_cast<ChatBubbleTail*>(card->property("chatTail").value<QObject*>());
    if (!tail) {
      tail = new ChatBubbleTail(host);
      card->setProperty("chatTail", QVariant::fromValue<QObject*>(tail));
      tail->setProperty("chatTailCard", QVariant::fromValue<QObject*>(card));
      // Sever the link first, so a stray reposition pass never reaches a dangling tail.
      QObject::connect(card, &QObject::destroyed, tail, [tail] {
        tail->setProperty("chatTailCard", QVariant());
        tail->deleteLater();
      });
    }
    tail->setColors(fill, border);
    tail->setSide(right);
    placeChatBubbleTailAt(card, tail);
    tail->show();
    tail->raise();
  }

  void repositionChatBubbleTails(QWidget* transcript) {
    if (!transcript) return;
    for (ChatBubbleTail* tail :
         transcript->findChildren<ChatBubbleTail*>(Qt::FindDirectChildrenOnly)) {
      if (auto* card = qobject_cast<QFrame*>(tail->property("chatTailCard").value<QObject*>()))
        placeChatBubbleTailAt(card, tail);
    }
  }

  // Anchors to the viewport-intersected rect, so ANY visible sliver of a row keeps its menu.
  static constexpr int MORE_PAD = 4;
  // browser CHAT_ROW_MENU_JUMP_GAP / extension MSG_MENU_JUMP_GAP — same number
  static constexpr int AVOID_GAP = 6;

  void placeChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll,
                         const QRect& avoidGlobal) {
    if (!card || !more) return;
    QWidget* host = card->parentWidget();
    if (!host) return;
    // setParent() HIDES a widget — re-show it, or a hover reveal is silently swallowed.
    if (more->parentWidget() != host) {
      const bool wasShown = more->isVisible();
      more->setParent(host);
      if (wasShown) more->show();
    }
    // The side the card actually renders on; falls back to the unswapped rule without it.
    const QVariant onRight = card->property(CHAT_ON_RIGHT_PROPERTY);
    const bool right = onRight.isValid() ? onRight.toBool()
                                         : card->objectName() == QLatin1String("chatCardUser");
    const QRect g = card->geometry();
    int x = right ? g.left() - more->width() - 6 : g.right() + 7;
    int y = g.bottom() - more->height() + 1;
    if (scroll && scroll->viewport()) {
      // The viewport in content coordinates, intersected with the content widget's OWN rect: that
      // widget is the button's parent and Qt clips a child to it.
      const QRect vp = QRect(host->mapFrom(scroll->viewport(), QPoint(0, 0)),
                             scroll->viewport()->size())
                           .intersected(host->rect());
      const QRect vis = g.intersected(vp);
      if (vis.isEmpty()) { more->hide(); return; }
      y = vis.bottom() - more->height() + 1;
      // The WHOLE button rect stays inside, with a hair of padding, on both sides.
      y = qBound(vp.top() + MORE_PAD, y, vp.bottom() - more->height() - MORE_PAD);
      x = qBound(vp.left() + MORE_PAD, x, vp.right() - more->width() - MORE_PAD);
      // Never straddles the NEIGHBOURING message; hysteresis (one pad to appear, a smaller one
      // to stay) keeps a row crossing the threshold mid-scroll from flickering.
      const int pad = more->isVisible() ? MORE_PAD : MORE_PAD + 2;
      QRect want(x, y, more->width(), more->height());
      if (vis.height() < more->height() + pad) { more->hide(); return; }
      // The furniture (jump pills) wins: lift clear of it, else hide (browser/extension parity).
      if (!avoidGlobal.isNull()) {
        QRect avoid(host->mapFromGlobal(avoidGlobal.topLeft()), avoidGlobal.size());
        avoid.adjust(-4, -4, 4, 4);
        if (avoid.intersects(want)) {
          const int liftedY = avoid.top() - more->height() - AVOID_GAP;
          const QRect lifted(x, liftedY, more->width(), more->height());
          if (liftedY < vis.top() + MORE_PAD || avoid.intersects(lifted)) {
            more->hide();
            return;
          }
          y = liftedY;
          want = lifted;
        }
      }
      // Neighbours are tested against their PLAIN rects: rows sit as little as 2 px apart.
      for (QFrame* sib : host->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (sib == card || !sib->isVisible()) continue;
        if (!sib->property("chatMoreBtn").isValid()) continue;
        if (sib->geometry().intersects(want)) {
          more->hide();
          return;
        }
      }
    }
    more->move(x, y);
    more->raise();
  }

}  // namespace stencil::gui
