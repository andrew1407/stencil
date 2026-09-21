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
    if (!log.jumpBottom) return QRect();
    const auto globalOf = [](QWidget* w) {
      return QRect(w->parentWidget() ? w->parentWidget()->mapToGlobal(w->pos())
                                     : w->mapToGlobal(QPoint(0, 0)),
                   w->size());
    };
    QRect pills;
    if (log.jumpBottom->isVisible()) pills = globalOf(log.jumpBottom);
    if (log.jumpTop && log.jumpTop->isVisible())
      pills = pills.isNull() ? globalOf(log.jumpTop) : pills.united(globalOf(log.jumpTop));
    return pills;
  }

  // The pills just moved, so any row "..." already on screen must reconsider whether it still clears
  // them. placeChatCardMore is idempotent, so re-running it on an unchanged row is a no-op.
  void ChatDock::revalidateMoreButtons() {
    if (!log.transcript || !scroll) return;
    const QRect avoid = jumpPillsGlobalRect();
    // Direct children: placeChatCardMore parents every "…" to the transcript itself.
    for (QToolButton* more : log.transcript->findChildren<QToolButton*>(
             QStringLiteral("chatCardMore"), Qt::FindDirectChildrenOnly)) {
      if (!more->isVisible()) continue;
      if (auto* card = qobject_cast<QFrame*>(more->property("chatMoreCard").value<QObject*>()))
        placeChatCardMore(card, more, scroll, avoid);
    }
  }

  void ChatDock::updateJumpButtons() {
    if (!log.jumpTop || !log.jumpBottom || !scroll) return;
    const auto* bar = scroll->verticalScrollBar();
    const bool up = bar->value() > 12;
    const bool down = bar->maximum() - bar->value() > 12;
    if (up || down) positionJumpButtons();
    // The pills answer to the scroll position alone - a row's "..." gets out of THEIR way
    // (revalidateMoreButtons), never the reverse.
    log.jumpTop->setVisible(up);
    log.jumpBottom->setVisible(down);
    revalidateMoreButtons();
  }

  // Grace-hide for a card's "⋯": it sits across a small gap from the bubble, so
  // a Leave waits ~220ms for the cursor to land on it (or back on the card).
  void ChatDock::positionJumpButtons() {
    if (!log.jumpTop || !log.jumpBottom || !scroll) return;
    // Bottom-right of the VIEWPORT (left of any scrollbar), riding the lower edge.
    const QRect vp = scroll->viewport()->geometry();
    const int y = vp.bottom() - log.jumpBottom->height() - 8;
    const int x = vp.right() - log.jumpBottom->width() - 10;
    log.jumpBottom->move(x, y);
    log.jumpTop->move(x - log.jumpTop->width() - 6, y);
    log.jumpBottom->raise();
    log.jumpTop->raise();
  }
}  // namespace stencil::gui
