// Bubble width capping, shared with the context menu's assistant panel.
#include "chatDock.hpp"
#include "chatDockShared.hpp"
#include "chatWidgets.hpp"

#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QSizePolicy>

namespace stencil::gui {

  using namespace chatdock;
  // Browser/extension parity (@container chat-transcript (max-width: 300px)): below it bubbles
  // widen toward the full column instead of squeezing their text; the alignment is unchanged.
  double chatBubbleCapFraction(int avail) { return avail > 0 && avail < 300 ? 0.96 : 0.88; }

  // A wrapped label CLIPS ITSELF without this pass, so both transcripts run it.
  void applyChatBubbleWidths(QWidget* transcript, QScrollArea* scroll) {
    // The bubble cap minus the card's own padding.
    const auto lwFor = [scroll](QFrame* card) {
      const int avail = scroll && scroll->viewport() ? scroll->viewport()->width() : 0;
      const int cap = qMax(120, static_cast<int>(avail * chatBubbleCapFraction(avail)));
      const QMargins m = card && card->layout() ? card->layout()->contentsMargins() : QMargins();
      return qMax(80, cap - m.left() - m.right());
    };
    const int avail = scroll && scroll->viewport() ? scroll->viewport()->width() : 0;
    // A viewport with no width yet cannot measure; the owner re-runs this once it has one.
    if (avail <= 0 || !transcript) return;
    const int cap = qMax(120, static_cast<int>(avail * chatBubbleCapFraction(avail)));
    for (QFrame* card : transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
      card->setMaximumWidth(cap);
      // Without heightForWidth the card keeps its full-width height and clips its own text.
      QSizePolicy sp = card->sizePolicy();
      sp.setHeightForWidth(true);
      sp.setVerticalPolicy(QSizePolicy::MinimumExpanding);
      card->setSizePolicy(sp);
      for (QLabel* l : card->findChildren<QLabel*>()) {
        if (!l->wordWrap()) continue;
        // Cap the LABEL too: a wrapped label's sizeHint is its longest unbreakable run (a URL).
        const int lw = lwFor(card);
        l->setMaximumWidth(lw);
        // Browser shrink-to-fit: narrower than the cap only when the text fits one line.
        const int natural = l->fontMetrics().size(0, l->text()).width();
        // Pinned explicitly: a wrapped QLabel's sizeHint reads its CURRENT geometry.
        l->setMinimumWidth(qMin(natural, lw));
        QSizePolicy lp = l->sizePolicy();
        lp.setHeightForWidth(true);
        lp.setVerticalPolicy(QSizePolicy::MinimumExpanding);
        l->setSizePolicy(lp);
        l->setMinimumHeight(0);
      }
      if (card->layout()) card->layout()->activate();
      card->updateGeometry();
      card->adjustSize();
      // RESERVE each wrapped label's height at the width it ACTUALLY got: heightForWidth is a hint
      // the layout does not re-ask for once it has sized the card.
      bool regrew = false;
      for (QLabel* l : card->findChildren<QLabel*>()) {
        if (!l->wordWrap() || l->text().isEmpty()) continue;
        // Measured from the card's sizeHint (valid before any layout pass): l->width() still carries
        // the default 100×30 child geometry on a freshly appended card.
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
    repositionChatBubbleTails(transcript);
  }

  void ChatDock::applyBubbleWidths() { applyChatBubbleWidths(transcript_, scroll_); }
}  // namespace stencil::gui
