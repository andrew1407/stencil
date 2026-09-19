// Jump pills over the transcript's edges, and the row triggers that dodge them.
// Split out of ChatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "chatWidgets.hpp"

#include <QFrame>
#include <QToolButton>

namespace stencil::gui {

  using namespace chatdock;
  // Jump pills (browser chatPanel syncJumps): up while the view is off the beginning, down while off
  // the latest. Recomputed from live geometry, never a cached flag; null while neither shows.
  QRect ChatDock::jumpPillsGlobalRect() const {
    if (!log_.jumpBottom) return QRect();
    const auto globalOf = [](QWidget* w) {
      return QRect(w->parentWidget() ? w->parentWidget()->mapToGlobal(w->pos())
                                     : w->mapToGlobal(QPoint(0, 0)),
                   w->size());
    };
    QRect pills;
    if (log_.jumpBottom->isVisible()) pills = globalOf(log_.jumpBottom);
    if (log_.jumpTop && log_.jumpTop->isVisible())
      pills = pills.isNull() ? globalOf(log_.jumpTop) : pills.united(globalOf(log_.jumpTop));
    return pills;
  }

  // The pills just moved, so any row "..." already on screen must reconsider whether it still clears
  // them. placeChatCardMore is idempotent, so re-running it on an unchanged row is a no-op.
  void ChatDock::revalidateMoreButtons() {
    if (!log_.transcript || !scroll_) return;
    const QRect avoid = jumpPillsGlobalRect();
    // Direct children: placeChatCardMore parents every "…" to the transcript itself.
    for (QToolButton* more : log_.transcript->findChildren<QToolButton*>(
             QStringLiteral("chatCardMore"), Qt::FindDirectChildrenOnly)) {
      if (!more->isVisible()) continue;
      if (auto* card = qobject_cast<QFrame*>(more->property("chatMoreCard").value<QObject*>()))
        placeChatCardMore(card, more, scroll_, avoid);
    }
  }

  void ChatDock::updateJumpButtons() {
    if (!log_.jumpTop || !log_.jumpBottom || !scroll_) return;
    const auto* bar = scroll_->verticalScrollBar();
    const bool up = bar->value() > 12;
    const bool down = bar->maximum() - bar->value() > 12;
    if (up || down) positionJumpButtons();
    // The pills answer to the scroll position alone - a row's "..." gets out of THEIR way
    // (revalidateMoreButtons), never the reverse.
    log_.jumpTop->setVisible(up);
    log_.jumpBottom->setVisible(down);
    revalidateMoreButtons();
  }

  // Grace-hide for a card's "⋯": it sits across a small gap from the bubble, so
  // a Leave waits ~220ms for the cursor to land on it (or back on the card).
  void ChatDock::positionJumpButtons() {
    if (!log_.jumpTop || !log_.jumpBottom || !scroll_) return;
    // Bottom-right of the VIEWPORT (left of any scrollbar), riding the lower edge.
    const QRect vp = scroll_->viewport()->geometry();
    const int y = vp.bottom() - log_.jumpBottom->height() - 8;
    const int x = vp.right() - log_.jumpBottom->width() - 10;
    log_.jumpBottom->move(x, y);
    log_.jumpTop->move(x - log_.jumpTop->width() - 6, y);
    log_.jumpBottom->raise();
    log_.jumpTop->raise();
  }
}  // namespace stencil::gui
