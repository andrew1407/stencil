// Jump pills over the transcript's edges, and the row triggers that dodge them.
// Split out of ChatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "chatWidgets.hpp"

#include <QFrame>
#include <QToolButton>

namespace stencil::gui {

  using namespace chatdock;
  // Jump pills (browser chatPanel syncJumps parity): ⌃ while the view is off the
  // beginning, ⌄ while it is off the latest message, both mid-log, neither fits.
  // The pills' own global box, whichever are visible right now — a null QRect while
  // neither is. Recomputed from the live geometry (never a cached flag), so a row's
  // "…" always reads where the pills ACTUALLY are, this instant.
  QRect ChatDock::jumpPillsGlobalRect() const {
    if (!jumpBottom_) return QRect();
    const auto globalOf = [](QWidget* w) {
      return QRect(w->parentWidget() ? w->parentWidget()->mapToGlobal(w->pos())
                                     : w->mapToGlobal(QPoint(0, 0)),
                   w->size());
    };
    QRect pills;
    if (jumpBottom_->isVisible()) pills = globalOf(jumpBottom_);
    if (jumpTop_ && jumpTop_->isVisible())
      pills = pills.isNull() ? globalOf(jumpTop_) : pills.united(globalOf(jumpTop_));
    return pills;
  }

  // The pills just moved, appeared or vanished — any row "…" already on screen must
  // reconsider whether it still clears them (shift further, settle back, or hide).
  // placeChatCardMore is idempotent, so re-running it on a row that needed no change
  // is a no-op.
  void ChatDock::revalidateMoreButtons() {
    if (!transcript_ || !scroll_) return;
    const QRect avoid = jumpPillsGlobalRect();
    // Direct children: placeChatCardMore parents every "…" to the transcript itself.
    for (QToolButton* more : transcript_->findChildren<QToolButton*>(
             QStringLiteral("chatCardMore"), Qt::FindDirectChildrenOnly)) {
      if (!more->isVisible()) continue;
      if (auto* card = qobject_cast<QFrame*>(more->property("chatMoreCard").value<QObject*>()))
        placeChatCardMore(card, more, scroll_, avoid);
    }
  }

  void ChatDock::updateJumpButtons() {
    if (!jumpTop_ || !jumpBottom_ || !scroll_) return;
    const auto* bar = scroll_->verticalScrollBar();
    const bool up = bar->value() > 12;
    const bool down = bar->maximum() - bar->value() > 12;
    if (up || down) positionJumpButtons();
    // The pills answer to the scroll position alone — a row's "…" gets out of THEIR way
    // (revalidateMoreButtons), never the reverse: the arrows are the higher-priority
    // control and stay put.
    jumpTop_->setVisible(up);
    jumpBottom_->setVisible(down);
    revalidateMoreButtons();
  }

  // Grace-hide for a card's "⋯": it sits across a small gap from the bubble, so
  // a Leave waits ~220ms for the cursor to land on it (or back on the card).
  void ChatDock::positionJumpButtons() {
    if (!jumpTop_ || !jumpBottom_ || !scroll_) return;
    // Bottom-right of the VIEWPORT (left of any scrollbar), riding the lower edge.
    const QRect vp = scroll_->viewport()->geometry();
    const int y = vp.bottom() - jumpBottom_->height() - 8;
    const int x = vp.right() - jumpBottom_->width() - 10;
    jumpBottom_->move(x, y);
    jumpTop_->move(x - jumpTop_->width() - 6, y);
    jumpBottom_->raise();
    jumpTop_->raise();
  }
}  // namespace stencil::gui
