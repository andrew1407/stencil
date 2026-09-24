// Variant thumbnails and clearing the conversation.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "../../../support/motion/DisintegrateOverlay.hpp"
#include "../../../support/motionPrefs.hpp"   // support::isDustAllowed()
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
          v.image.scaled(THUMB_EDGE, THUMB_EDGE, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation));
      thumb->setPixmap(pm);
      row->addWidget(thumb);
      auto* meta = new QVBoxLayout;
      meta->setSpacing(2);
      auto* name = makePlainLabel(v.label, card);
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

  // Provider settings and the working image are NOT touched; the owner clears the model history.
  void ChatDock::clearConversation() {
    clearPending();
    // Backwards so the indices stay valid; cmp.suggest and the bottom stretch ARE the empty state.
    bool wiped = false;
    for (int i = log.transcriptLayout->count() - 1; i >= 0; --i) {
      QLayoutItem* item = log.transcriptLayout->itemAt(i);
      QWidget* w = item ? item->widget() : nullptr;
      if (!w || w == cmp.suggest) continue;
      // Snapshot BEFORE the card leaves the layout, hosted on the WINDOW (the dock's content widget
      // paints over its children). Fall, not Rows: the same gesture as the cleared IMAGE.
      DisintegrateOverlay::over(w, window(), DisintegrateOverlay::Sweep::FALL,
                                CHAT_SCATTER_COLS, CHAT_SCATTER_ROWS,
                                DisintegrateOverlay::ITEM_MS,
                                w->palette().color(QPalette::WindowText));
      // Anything REMOVED means the empty state waits, even when over() declined the grab.
      wiped = true;
      delete log.transcriptLayout->takeAt(i);
      // Out of the layout but still painted while it fades; the fade owns the delete.
      fadeOutAndDelete(w);
    }
    clearAttachments();
    // Rows out first, THEN the placeholder (browser chat/view.js restoreEmptyState) — and
    // only while something is actually falling in front of it (browser wipeDurationMs).
    if (wiped && support::isDustAllowed()) {
      // A hair past the scatter's duration; the overlay deletes itself on finish.
      QTimer::singleShot(DisintegrateOverlay::ITEM_MS + 60, this, [this] {
        // A turn may have started while the wipe played.
        if (transcriptHasCards()) return;
        cmp.suggest->show();
        scrollToBottom();
      });
    } else {
      cmp.suggest->show();
    }
    scrollToBottom();
  }

  // The empty state and the bottom stretch don't count.
  bool ChatDock::transcriptHasCards() const {
    for (int i = 0; i < log.transcriptLayout->count(); ++i) {
      QLayoutItem* item = log.transcriptLayout->itemAt(i);
      QWidget* w = item ? item->widget() : nullptr;
      if (w && w != cmp.suggest && !w->isHidden()) return true;
    }
    return false;
  }
}  // namespace stencil::gui
