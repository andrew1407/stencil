#include "chatWidgets.hpp"

#include "../support/disintegrateOverlay.hpp"
#include "../support/modalReveal.hpp"   // support::motionReduced()

#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>

namespace stencil::gui {

  bool chatCardFullyInViewport(QWidget* card, QScrollArea* scroll) {
    if (!card || !scroll || !scroll->viewport()) return false;
    const QRect view = scroll->viewport()->rect();
    const QPoint topLeft = card->mapTo(scroll->viewport(), QPoint(0, 0));
    return QRect(topLeft, card->size()).isValid()
        && topLeft.y() >= view.top() - 1
        && topLeft.y() + card->height() <= view.bottom() + 1;
  }

  void trackChatCardDust(QWidget* card, QWidget* overlay, QScrollArea* scroll,
                         std::function<void()> settle) {
    QPointer<QWidget> cp(card);
    QPointer<QWidget> op(overlay);
    const QSize shot = card->size();
    // Parented to the CARD and stopped by the overlay's own death — a singleShot
    // holding the timer by raw pointer is what crashed here once.
    auto* timer = new QTimer(card);
    timer->setInterval(kChatGatherSettleMs);
    QObject::connect(timer, &QTimer::timeout, card, [cp, op, scroll, shot, timer, settle] {
      if (!op || !cp) { timer->stop(); timer->deleteLater(); return; }
      if (!chatCardFullyInViewport(cp, scroll) || cp->size() != shot) {
        op->deleteLater();          // the photograph no longer matches its subject
        timer->stop();
        timer->deleteLater();
        settle();                   // …and the card it was standing in for takes over NOW
        return;
      }
      op->move(cp->mapTo(op->parentWidget(), QPoint(0, 0)));
      op->raise();
    });
    timer->start();
  }

  void gatherChatCardIn(QWidget* card, QVBoxLayout* layout, QScrollArea* scroll,
                        QWidget* host, int cols, int rows, std::function<void()> settle,
                        std::function<void()> onFlight, int tries, QSize lastSize) {
    if (!card || !layout || !host) return;
    // A card already out of the layout is leaving (a clear takes it out before fading
    // it): an entrance here would fight the fade for the same effect.
    if (layout->indexOf(card) < 0) return;
    const auto retry = [card, layout, scroll, host, cols, rows, settle, onFlight,
                        tries](QSize sizeNow) {
      QPointer<QWidget> cp(card);
      QTimer::singleShot(kChatGatherSettleMs, card,
                         [cp, layout, scroll, host, cols, rows, settle, onFlight, tries,
                          sizeNow] {
        if (cp)
          gatherChatCardIn(cp, layout, scroll, host, cols, rows, settle, onFlight,
                           tries - 1, sizeNow);
      });
    };
    // Geometry first — the grab is only as good as the layout behind it. A card
    // inserted this very turn is still HIDDEN (a box layout skips hidden widgets),
    // so the grab would photograph a 0-width box. Show it, then lay out.
    if (!card->isVisible()) card->show();
    layout->activate();
    // …and the surface may not have given it a real box yet. Wait it out, bounded:
    // the message must never be held up behind a layout that may never come.
    if ((card->width() < 8 || card->height() < 8) && tries > 0) { retry(QSize()); return; }
    if (support::motionReduced()) { settle(); return; }
    // sizeHint can still be one layout pass from final even past the degenerate-size
    // check; a grab on that stale reading fed the tracker a mismatched picture, which
    // it silently dropped (the "no dust" bug). Require two matching reads in a row.
    if (card->size() != lastSize && tries > 0) { retry(card->size()); return; }
    // Only a card wholly inside the viewport flies — the cloud is drawn on the window
    // and the scroller does not clip it (dust over the composer was the reported bug).
    if (!chatCardFullyInViewport(card, scroll)) {
      if (tries > 0) { retry(lastSize); return; }
      settle();
      return;
    }
    // grab() renders THROUGH the graphics effect — with the entrance's effect already
    // at 0 the motes would be a cloud of nothing. Off for the photograph, back on
    // before this returns, so no frame is ever painted with the card at full strength.
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect());
    if (fx) fx->setEnabled(false);
    QWidget* dust = DisintegrateOverlay::over(card, host, DisintegrateOverlay::Sweep::Gather,
                                              cols, rows);
    if (fx) fx->setEnabled(true);
    if (!dust) { settle(); return; }   // nothing to hide behind
    // The card stays FULLY HIDDEN for the whole flight and takes the motes' place when
    // they land; the snapshot follows the card per frame, or is dropped as stale.
    trackChatCardDust(card, dust, scroll, settle);
    if (onFlight) onFlight();
    // The reveal is a CUT in the frame the overlay deletes itself: the motes have
    // already drawn the bubble into place, so fading it up would double the arrival.
    QPointer<QWidget> cp(card);
    QTimer::singleShot(DisintegrateOverlay::kMs, card, [cp, settle] { if (cp) settle(); });
  }

  // Theme-provided muted text (palette PlaceholderText, not a hardcoded hex).
  void applyMutedText(QLabel* label) {
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
    label->setPalette(pal);
  }

  // Error text in the theme's --danger (browser .chat-msg-error).
  void applyDangerText(QLabel* label, const QColor& danger) {
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, danger);
    label->setPalette(pal);
  }

  // The small bold role caption every transcript card starts with.
  QLabel* makeRoleLabel(const QString& role, QWidget* card) {
    auto* roleLabel = new QLabel(role, card);
    QFont f = roleLabel->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 0.85);
    roleLabel->setFont(f);
    return roleLabel;
  }

  // A QLabel for UNTRUSTED text — model output, or a dropped filename. QLabel
  // defaults to Qt::AutoText, so mightBeRichText() would decide per string whether
  // to RENDER a model's markup (and QTextDocument resolves local file resources).
  // The browser/extension use textContent only; this is the Qt spelling of that.
  QLabel* makePlainLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    return label;
  }

  // ── Message bubble tail (browser .chat-msg-user::before/::after parity) ─────
  // Two right triangles from a QPainterPath — Qt has no CSS border-triangle
  // trick, so the shape is built directly: the border copy behind, the 1px
  // smaller fill in front, its anchor pushed out on BOTH axes so the outline
  // grows past the fill evenly on all three edges. See components.css for the CSS.
  static constexpr int kTailLeg = 9;        // border (::before) leg length
  static constexpr int kTailFillLeg = 8;    // fill (::after) leg length — 1px smaller
  static constexpr int kTailShift = 1;      // border anchor's extra outward push, both axes
  static constexpr int kTailW = kTailShift + kTailLeg;    // the SHAPE's own box (10 x 9) —
  static constexpr int kTailH = kTailLeg;                 // tailTriangle()'s coordinate space
  // Slack on every side: a path edge flush with its widget's own clip boundary is
  // dropped or half-antialiased. paintEvent and placeChatBubbleTailAt both offset
  // by it, so every drawn pixel keeps its unpadded absolute position.
  static constexpr int kTailPad = kTailShift;
  static constexpr int kTailBoxW = kTailW + 2 * kTailPad;
  static constexpr int kTailBoxH = kTailH + 2 * kTailPad;

  ChatBubbleTail::ChatBubbleTail(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);   // decoration only, never eats a click/hover
    setFixedSize(kTailBoxW, kTailBoxH);
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

  // Right triangle whose right-angle vertex sits `shift`px diagonally out from the
  // bubble's own corner (shift=0 puts it exactly on it), with `leg`px legs up the
  // bubble's edge and out along its bottom.
  static QPainterPath tailTriangle(bool right, qreal leg, qreal shift, qreal boxW) {
    const qreal y = kTailFillLeg + shift;
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
    p.translate(kTailPad, kTailPad);   // placeChatBubbleTailAt moves the widget back by it
    p.setBrush(border_);
    p.drawPath(tailTriangle(right_, kTailLeg, kTailShift, kTailW));
    p.setBrush(fill_);
    p.drawPath(tailTriangle(right_, kTailFillLeg, 0, kTailW));
  }

  // The tail's box starts flush with the bubble's own bottom corner and runs
  // kTailW/kTailH out from there, less kTailPad on every side.
  static void placeChatBubbleTailAt(QFrame* card, ChatBubbleTail* tail) {
    const QRect g = card->geometry();
    // QRect::right() is x()+width()-1, already flush; left() is the true edge, so
    // the left-side branch needs its own +1 to match.
    const int left = (tail->isRight() ? g.right() : g.left() - kTailW + 1) - kTailPad;
    tail->move(left, g.bottom() - kTailFillLeg - kTailPad);
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
      // The card owns the tail's lifetime — sever the link first, so a stray
      // reposition pass on the (about to be freed) card never reaches a dangling tail.
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
    // Direct children: placeChatBubbleTail parents every tail to the card's own parent.
    for (ChatBubbleTail* tail :
         transcript->findChildren<ChatBubbleTail*>(Qt::FindDirectChildrenOnly)) {
      if (auto* card = qobject_cast<QFrame*>(tail->property("chatTailCard").value<QObject*>()))
        placeChatBubbleTailAt(card, tail);
    }
  }

  // Park a card's "⋯" beside the bottom corner of its VISIBLE SLICE (user
  // bubbles get it to the left, the rest to the right): the button anchors to
  // the viewport-intersected rect, so ANY visible sliver of a row keeps its menu.
  static constexpr int kMorePad = 4;
  // Clearance the button keeps once lifted clear of `avoidGlobal` (browser
  // CHAT_ROW_MENU_JUMP_GAP / extension MSG_MENU_JUMP_GAP — same number, all three).
  static constexpr int kAvoidGap = 6;

  void placeChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll,
                         const QRect& avoidGlobal) {
    if (!card || !more) return;
    QWidget* host = card->parentWidget();
    if (!host) return;
    // NOTE: setParent() HIDES a widget — re-show it, or the first placement of a
    // button revealed by hover would silently swallow that reveal.
    if (more->parentWidget() != host) {
      const bool wasShown = more->isVisible();
      more->setParent(host);
      if (wasShown) more->show();
    }
    // The side the card actually renders on (applyChatBubbleSide stashes it) —
    // objectName() alone stopped answering that once a swap could flip either role
    // to either side. Falls back to the unswapped rule for a card without it.
    const QVariant onRight = card->property(kChatOnRightProperty);
    const bool right = onRight.isValid() ? onRight.toBool()
                                         : card->objectName() == QLatin1String("chatCardUser");
    const QRect g = card->geometry();   // in the content widget's coordinates
    int x = right ? g.left() - more->width() - 6 : g.right() + 7;
    int y = g.bottom() - more->height() + 1;
    if (scroll && scroll->viewport()) {
      // The viewport, in those same content coordinates (it scrolls under them)
      // — intersected with the content widget's OWN rect, because that widget is
      // the button's parent and Qt clips a child to it. Clamping to the viewport
      // alone let the pill sit in a content-widget margin, half clipped.
      const QRect vp = QRect(host->mapFrom(scroll->viewport(), QPoint(0, 0)),
                             scroll->viewport()->size())
                           .intersected(host->rect());
      const QRect vis = g.intersected(vp);
      if (vis.isEmpty()) { more->hide(); return; }   // scrolled clean out of view
      y = vis.bottom() - more->height() + 1;
      // The WHOLE button rect stays inside, with a hair of padding — not just the
      // anchor point, and on both sides (a user row hangs left, the rest right).
      y = qBound(vp.top() + kMorePad, y, vp.bottom() - more->height() - kMorePad);
      x = qBound(vp.left() + kMorePad, x, vp.right() - more->width() - kMorePad);
      // …and it never straddles the NEIGHBOURING message. With only a sliver of
      // this row on screen the clamp above would push the pill up (or down) over
      // the card next to it, which reads as a bug — so for such a row it simply
      // does not show. A fully visible row always keeps its button: the pill sits
      // inside that row's own y-band, where no neighbour can reach it.
      // Hysteresis (one pad to appear, a smaller one to stay) keeps a row
      // crossing the threshold mid-scroll from flickering.
      const int pad = more->isVisible() ? kMorePad : kMorePad + 2;
      QRect want(x, y, more->width(), more->height());
      if (vis.height() < more->height() + pad) { more->hide(); return; }
      // The furniture (the dock's jump pills) wins: it never stands down for this
      // button any more — the button lifts clear of it, or (nowhere left in this
      // row's own visible slice to lift TO) hides instead, same as it already does
      // for a neighbouring row below (browser/extension parity: "shift, else hide").
      if (!avoidGlobal.isNull()) {
        QRect avoid(host->mapFromGlobal(avoidGlobal.topLeft()), avoidGlobal.size());
        avoid.adjust(-4, -4, 4, 4);   // a near miss still crowds it
        if (avoid.intersects(want)) {
          const int liftedY = avoid.top() - more->height() - kAvoidGap;
          const QRect lifted(x, liftedY, more->width(), more->height());
          if (liftedY < vis.top() + kMorePad || avoid.intersects(lifted)) {
            more->hide();
            return;
          }
          y = liftedY;
          want = lifted;
        }
      }
      // The neighbours are tested against their PLAIN rects: rows sit as little
      // as 2px apart (the menu panel's transcript), so padding them out would
      // suppress every pill — and it is unnecessary, because a pill that fits its
      // own row's slice is inside that row's band, where no neighbour reaches.
      // This fires exactly when the clamp above pushed it out of the slice.
      for (QFrame* sib : host->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (sib == card || !sib->isVisible()) continue;
        if (!sib->property("chatMoreBtn").isValid()) continue;   // rows only
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
