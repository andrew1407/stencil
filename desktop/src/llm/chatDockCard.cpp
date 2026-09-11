// Transcript card creation and its entrance motion.
// Split out of chatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "chatDock.hpp"
#include "chatDockShared.hpp"
#include "../app/scrollReveal.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/modalReveal.hpp"
#include "chatWidgets.hpp"

#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QAbstractAnimation>
#include <cmath>

namespace stencil::gui {

  using namespace chatdock;
  // Create a framed transcript card above the bottom stretch and hand back its
  // layout; the deferred scrollToBottom measures it after the caller populates
  // it (the scroll runs on the next event-loop turn).
  QVBoxLayout* ChatDock::appendTranscriptCard(int spacing) {
    suggest_->hide();  // suggestions are an empty-state affordance only
    auto* card = new QFrame(transcript_);
    card->setFrameShape(QFrame::StyledPanel);
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(spacing);
    transcriptLayout_->insertWidget(transcriptLayout_->count() - 1, card);
    animateCardIn(card, lay);
    // Follow only while pinned to the end — a reply landing while the user
    // reads history must not yank them back down. Sends re-pin (appendUser /
    // showPending call scrollToBottom directly).
    if (stickToBottom_) scrollToBottom();
    return lay;
  }

  // The slide is done in the card's OWN contents margins (top +off, bottom
  // -off) rather than pos(): the transcript's layout owns the card's geometry
  // and would fight a moved widget, while the margin pair keeps the card's
  // total height constant for the deferred scrollToBottom. The effect and the
  // animation are children of the card and the valueChanged connection uses
  // the card as its context, so a card deleted mid-flight severs everything;
  // DeleteWhenStopped reaps a normal finish.
  void ChatDock::animateCardIn(QWidget* card, QVBoxLayout* lay) {
    // Claim the card's opacity while the entrance plays: ScrollReveal drives the same
    // effect, and two writers on one effect flicker. Claimed and zeroed NOW even when
    // the entrance itself starts a turn later, so the card never flashes at full
    // strength in between.
    card->setProperty(ScrollReveal::kEnteringProperty, true);
    auto* fx = new QGraphicsOpacityEffect(card);
    fx->setOpacity(0.0);
    card->setGraphicsEffect(fx);  // the widget takes ownership of the effect
    // Reduced motion takes the immediate path: nothing to photograph, nothing to
    // wait for. Everything else defers — appendTranscriptCard hands the caller an EMPTY
    // card, and the dust has to be a picture of the FINISHED bubble, laid out at its real
    // width and already scrolled to. The caller's own scrollToBottom() is a singleShot(0)
    // queued AFTER this one, so a 0ms hop here would still measure the pre-scroll box;
    // one frame lets that scroll land first.
    if (support::motionReduced()) { startCardEntrance(card, lay); return; }
    QPointer<QWidget> cp(card);
    QTimer::singleShot(kChatGatherSettleMs, card, [this, cp, lay] {
      if (cp) startCardEntrance(cp, lay);
    });
  }

  void ChatDock::startCardEntrance(QWidget* card, QVBoxLayout* lay) {
    const QMargins rest = lay->contentsMargins();
    // The resting state — every bail-out in the shared machinery takes it, so a card
    // can never be stranded invisible behind a flight that did not happen.
    const auto settle = [this, card, lay, rest] {
      if (auto* e = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect())) e->setOpacity(1.0);
      lay->setContentsMargins(rest);
      card->setProperty(ScrollReveal::kEnteringProperty, false);
      // Laid out FIRST: setContentsMargins only QUEUES the move, and ScrollReveal::apply()
      // measures mapTo(viewport)/height() to decide a card's edge dissolve — run against
      // the old geometry it left the fresh bubble faint until the next scroll.
      lay->activate();
      if (reveal_) reveal_->apply();   // hand the card over to the scroll curve
      repositionChatBubbleTails(transcript_);
    };
    // The slide runs on its own short clock only once the dust actually flies — the
    // gap the card opens in the transcript is layout, not flourish, and the finished
    // bubble must never be drawn under the animation (the card waits fully hidden).
    const auto slide = [this, card, lay, rest] {
      auto* anim = new QVariantAnimation(card);
      anim->setDuration(kAppearMs);
      anim->setStartValue(0.0);
      anim->setEndValue(1.0);
      anim->setEasingCurve(QEasingCurve::OutCubic);
      connect(anim, &QVariantAnimation::valueChanged, card, [lay, rest](const QVariant& v) {
        const int off = qRound(kAppearSlidePx * (1.0 - v.toDouble()));
        lay->setContentsMargins(rest.left(), rest.top() + off, rest.right(),
                                qMax(0, rest.bottom() - off));
      });
      anim->start(QAbstractAnimation::DeleteWhenStopped);
    };
    gatherChatCardIn(card, transcriptLayout_, scroll_, window(), kChatScatterCols,
                     kChatScatterRows, settle, slide);
  }
}  // namespace stencil::gui
