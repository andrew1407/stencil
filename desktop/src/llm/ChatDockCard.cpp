// Transcript card creation and its entrance motion.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "../support/scrollReveal.hpp"
#include "../support/DisintegrateOverlay.hpp"
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
  // The deferred scrollToBottom measures the card after the caller populates it.
  QVBoxLayout* ChatDock::appendTranscriptCard(int spacing) {
    cmp_.suggest->hide();
    auto* card = new QFrame(log_.transcript);
    card->setFrameShape(QFrame::StyledPanel);
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(spacing);
    log_.transcriptLayout->insertWidget(log_.transcriptLayout->count() - 1, card);
    animateCardIn(card, lay);
    // Follow only while pinned to the end; sends re-pin.
    if (stickToBottom_) scrollToBottom();
    return lay;
  }

  // The slide is in the card's OWN contents margins, not pos(): the layout owns the geometry, and
  // the margin pair keeps the total height constant. Card-parented, so a mid-flight delete severs all.
  void ChatDock::animateCardIn(QWidget* card, QVBoxLayout* lay) {
    // Claimed and zeroed NOW: ScrollReveal drives the same effect, and two writers flicker.
    card->setProperty(ScrollReveal::ENTERING_PROPERTY, true);
    auto* fx = new QGraphicsOpacityEffect(card);
    fx->setOpacity(0.0);
    card->setGraphicsEffect(fx);
    // The dust must be a picture of the FINISHED bubble; the caller's scrollToBottom() is a
    // singleShot(0) queued AFTER this one, so one frame lets that scroll land first.
    if (support::motionReduced()) { startCardEntrance(card, lay); return; }
    QPointer<QWidget> cp(card);
    QTimer::singleShot(CHAT_GATHER_SETTLE_MS, card, [this, cp, lay] {
      if (cp) startCardEntrance(cp, lay);
    });
  }

  void ChatDock::startCardEntrance(QWidget* card, QVBoxLayout* lay) {
    const QMargins rest = lay->contentsMargins();
    // The resting state — every bail-out takes it, so a card is never stranded invisible.
    const auto settle = [this, card, lay, rest] {
      if (auto* e = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect())) e->setOpacity(1.0);
      lay->setContentsMargins(rest);
      card->setProperty(ScrollReveal::ENTERING_PROPERTY, false);
      // Laid out FIRST: setContentsMargins only QUEUES the move, and ScrollReveal::apply() measures.
      lay->activate();
      if (reveal_) reveal_->apply();
      repositionChatBubbleTails(log_.transcript);
    };
    // The slide starts only once the dust actually flies; the card waits fully hidden.
    const auto slide = [this, card, lay, rest] {
      auto* anim = new QVariantAnimation(card);
      anim->setDuration(APPEAR_MS);
      anim->setStartValue(0.0);
      anim->setEndValue(1.0);
      anim->setEasingCurve(QEasingCurve::OutCubic);
      connect(anim, &QVariantAnimation::valueChanged, card, [lay, rest](const QVariant& v) {
        const int off = qRound(APPEAR_SLIDE_PX * (1.0 - v.toDouble()));
        lay->setContentsMargins(rest.left(), rest.top() + off, rest.right(),
                                qMax(0, rest.bottom() - off));
      });
      anim->start(QAbstractAnimation::DeleteWhenStopped);
    };
    gatherChatCardIn(card, log_.transcriptLayout, scroll_, window(), CHAT_SCATTER_COLS,
                     CHAT_SCATTER_ROWS, settle, slide);
  }
}  // namespace stencil::gui
