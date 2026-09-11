// Variant thumbnails and clearing the conversation.
// Split out of chatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "chatDock.hpp"
#include "chatDockShared.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "theme.hpp"
#include "chatWidgets.hpp"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace stencil::gui {

  using namespace chatdock;
  void ChatDock::appendVariants(const QVector<VariantCard>& variants) {
    if (variants.isEmpty()) return;
    QVBoxLayout* lay = appendTranscriptCard(6);
    QWidget* card = lay->parentWidget();
    for (const VariantCard& v : variants) {
      auto* row = new QHBoxLayout;
      row->setSpacing(8);
      auto* thumb = new QLabel(card);
      const QPixmap pm = QPixmap::fromImage(
          v.image.scaled(kThumbEdge, kThumbEdge, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation));
      thumb->setPixmap(pm);
      row->addWidget(thumb);
      auto* meta = new QVBoxLayout;
      meta->setSpacing(2);
      auto* name = makePlainLabel(v.label, card);   // model-written variant label
      name->setWordWrap(true);
      meta->addWidget(name);
      auto* btns = new QHBoxLayout;
      btns->setSpacing(4);
      auto* open = new QToolButton(card);
      open->setText("Open");
      open->setAutoRaise(true);
      open->setToolTip("Open this variant's project in a new window");
      open->setEnabled(!v.projectId.isEmpty());
      const QString pid = v.projectId;
      connect(open, &QToolButton::clicked, this,
              [this, pid] { emit openVariantRequested(pid); });
      btns->addWidget(open);
      auto* save = new QToolButton(card);
      save->setText("Save…");
      save->setAutoRaise(true);
      save->setToolTip("Save this variant image to disk");
      const QImage img = v.image;
      const QString label = v.label;
      connect(save, &QToolButton::clicked, this, [this, img, label] {
        const QString path = QFileDialog::getSaveFileName(
            this, "Save variant", label + QStringLiteral(".png"), "PNG image (*.png)");
        if (!path.isEmpty()) img.save(path, "PNG");
      });
      btns->addWidget(save);
      btns->addStretch(1);
      meta->addLayout(btns);
      meta->addStretch(1);
      row->addLayout(meta, 1);
      lay->addLayout(row);
    }
  }

  // Wipe the conversation surface: every transcript card (a pending "…"
  // included) plus the attachment state, then the empty state returns. The
  // provider settings and the working image are deliberately NOT touched; the
  // model-side history is the owner's to clear (clearRequested).
  void ChatDock::clearConversation() {
    clearPending();  // the in-flight card is a transcript card too
    // Walk backwards so the indices stay valid; suggest_ and the bottom stretch
    // ARE the empty state, so they survive.
    bool wiped = false;
    for (int i = transcriptLayout_->count() - 1; i >= 0; --i) {
      QLayoutItem* item = transcriptLayout_->itemAt(i);
      QWidget* w = item ? item->widget() : nullptr;
      if (!w || w == suggest_) continue;
      // Scatter a snapshot of the card over the dock BEFORE it leaves the layout —
      // the particles can't live inside a widget that is about to be destroyed.
      // Hosted on the WINDOW, not the dock: the dock's own content widget paints
      // over its children, so particles parented to the dock never show.
      // Fall, not Rows: a cleared message comes apart from its top edge and drops, the
      // way the cleared IMAGE does — the two removals now read as the same gesture.
      DisintegrateOverlay::over(w, window(), DisintegrateOverlay::Sweep::Fall,
                                kChatScatterCols, kChatScatterRows,
                                DisintegrateOverlay::kItemMs,   // a message is read, not glanced at
                                w->palette().color(QPalette::WindowText));
      // Anything REMOVED means the empty state waits, whether or not the scatter
      // could play (over() declines what it cannot grab — an off-screen dock, a
      // zero-sized card). Keying the wait off the animation instead made the wait
      // silently vanish in exactly the cases hardest to reason about, and the chips
      // came back over a transcript that was still emptying.
      wiped = true;
      delete transcriptLayout_->takeAt(i);
      // Out of the layout (so the transcript closes up) but still painted while it
      // fades under its own dust; the fade owns the delete.
      fadeOutAndDelete(w);
    }
    clearAttachments();
    // The empty state comes back only once the particles have landed. Showing it in
    // the same tick put the hint + chips on screen underneath a scatter that was
    // still playing, so the clear read as happening twice and the panel flickered.
    // Rows out first, THEN the placeholder — the browser (chatView.js
    // restoreEmptyState) and the cleared canvas sequence it exactly this way.
    if (wiped) {
      // A hair past the scatter's own duration, so the last particle is gone before
      // the empty state lands (the overlay deletes itself on its animation's finish).
      QTimer::singleShot(DisintegrateOverlay::kItemMs + 60, this, [this] {
        // A turn may have started while the wipe played — then the chips are wrong.
        if (transcriptHasCards()) return;
        suggest_->show();
        scrollToBottom();
      });
    } else {
      suggest_->show();
    }
    scrollToBottom();
  }

  // Any real transcript card present (the empty state and the bottom stretch don't
  // count) — what tells a deferred empty state whether it is still wanted.
  bool ChatDock::transcriptHasCards() const {
    for (int i = 0; i < transcriptLayout_->count(); ++i) {
      QLayoutItem* item = transcriptLayout_->itemAt(i);
      QWidget* w = item ? item->widget() : nullptr;
      if (w && w != suggest_ && !w->isHidden()) return true;
    }
    return false;
  }
}  // namespace stencil::gui
