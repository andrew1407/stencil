// Bubble width capping, shared with the context menu's assistant panel.
// Split out of chatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "chatDock.hpp"
#include "chatDockShared.hpp"
#include "chatWidgets.hpp"

#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QSizePolicy>

namespace stencil::gui {

  using namespace chatdock;
  // The share of the viewport a bubble may take. Browser/extension parity
  // (@container chat-transcript (max-width: 300px)): below that the split has no
  // room to read as a SIDE any more, so bubbles widen toward the full column
  // instead of squeezing their text — the alignment itself is unchanged (unlike
  // the CSS surfaces, dropping it here would mean re-running the layout's
  // alignment on every resize, and the L/R split still reads fine at this width,
  // it is only the TEXT that was cramped).
  double chatBubbleCapFraction(int avail) { return avail > 0 && avail < 300 ? 0.96 : 0.88; }

  // Bubbles stop short of the full width so the side they sit on is legible;
  // re-applied whenever the viewport resizes. Shared with the context menu's
  // assistant panel: a wrapped label CLIPS ITSELF without this pass, so both
  // transcripts run it.
  void applyChatBubbleWidths(QWidget* transcript, QScrollArea* scroll) {
    // The widest a wrapped label inside `card` may be: the bubble cap minus the card's
    // own padding. Used both to cap the label and, when it has not been laid out yet,
    // to measure the height its text will need.
    const auto lwFor = [scroll](QFrame* card) {
      const int avail = scroll && scroll->viewport() ? scroll->viewport()->width() : 0;
      const int cap = qMax(120, static_cast<int>(avail * chatBubbleCapFraction(avail)));
      const QMargins m = card && card->layout() ? card->layout()->contentsMargins() : QMargins();
      return qMax(80, cap - m.left() - m.right());
    };
    const int avail = scroll && scroll->viewport() ? scroll->viewport()->width() : 0;
    // A viewport with no width yet (a surface that has never been shown) cannot
    // measure anything — leave the cards alone and let the owner re-run this
    // once it has a real width, rather than pinning them to a bogus cap.
    if (avail <= 0 || !transcript) return;
    const int cap = qMax(120, static_cast<int>(avail * chatBubbleCapFraction(avail)));
    for (QFrame* card : transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
      card->setMaximumWidth(cap);
      // Word-wrapped labels grow TALLER as the card narrows: without
      // heightForWidth the card keeps the height it computed at full width and
      // clips its own text (the layout must re-ask after every cap change).
      QSizePolicy sp = card->sizePolicy();
      sp.setHeightForWidth(true);
      sp.setVerticalPolicy(QSizePolicy::MinimumExpanding);
      card->setSizePolicy(sp);
      for (QLabel* l : card->findChildren<QLabel*>()) {
        if (!l->wordWrap()) continue;
        // Cap the LABEL too, not just the card: a wrapped label's sizeHint is its longest
        // unbreakable run (a URL), and that would push the card past the cap and widen
        // the whole transcript.
        const int lw = lwFor(card);
        l->setMaximumWidth(lw);
        // Browser shrink-to-fit parity: a wrapped bubble narrows below the cap only
        // when its text fits one line; otherwise it takes the FULL cap (Qt's wrapped
        // sizeHint favours a squarer, needlessly narrow shape instead).
        const int natural = l->fontMetrics().size(0, l->text()).width();
        // Pinned explicitly rather than left at minimumWidth 0: a wrapped QLabel's
        // sizeHint reads its CURRENT geometry, so a fresh label wrapped short lines.
        l->setMinimumWidth(qMin(natural, lw));
        QSizePolicy lp = l->sizePolicy();
        lp.setHeightForWidth(true);
        lp.setVerticalPolicy(QSizePolicy::MinimumExpanding);
        l->setSizePolicy(lp);
        l->setMinimumHeight(0);   // re-measured below, at the width it really gets
      }
      if (card->layout()) card->layout()->activate();
      card->updateGeometry();
      card->adjustSize();
      // …then RESERVE each wrapped label's height at the width it ACTUALLY got. The
      // cap is only an upper bound — a card sizes to its content and is usually
      // narrower, so text wrapped at the cap needs MORE room than reserved, and the
      // last line was cut off by the bubble's own edge. heightForWidth is a hint the
      // layout does not re-ask for once it has sized the card, so it is pinned here.
      bool regrew = false;
      for (QLabel* l : card->findChildren<QLabel*>()) {
        if (!l->wordWrap() || l->text().isEmpty()) continue;
        // Measure at the width the layout WILL give the label, computed from the
        // card's sizeHint (a pure query, valid before any layout pass): the card
        // takes min(hint, cap) and the label spans it minus the card padding.
        // l->width() lied here — a freshly appended card still carries its
        // default 100×30 child geometry, so the height was reserved for a much
        // narrower wrap and the bubble kept fat top/bottom padding around the
        // vertically centered text until the next viewport resize re-measured.
        const QMargins cm =
            card->layout() ? card->layout()->contentsMargins() : QMargins();
        const int w =
            qMax(80, qMin(card->sizeHint().width(), cap) - cm.left() - cm.right());
        const int wrapped = l->heightForWidth(w);
        if (wrapped > 0 && wrapped != l->minimumHeight()) {
          l->setMinimumHeight(wrapped);
          regrew = true;
        }
      }
      if (regrew) {
        if (card->layout()) card->layout()->activate();
        card->updateGeometry();
        card->adjustSize();
      }
    }
    if (transcript->layout()) transcript->layout()->activate();
    // Every card above may have just moved — its tail (if any) has to follow.
    repositionChatBubbleTails(transcript);
  }

  void ChatDock::applyBubbleWidths() { applyChatBubbleWidths(transcript_, scroll_); }
}  // namespace stencil::gui
