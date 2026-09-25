// Variant thumbnails and clearing the conversation.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "../../../support/motion/DisintegrateOverlay.hpp"
#include "../../../support/motionPrefs.hpp"   // support::isDustAllowed()
#include "theme.hpp"
#include "chatWidgets.hpp"
#include "../../../support/control/FlowLayout.hpp"
#include "iconSet.hpp"

#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace stencil::gui {

  using namespace chatdock;
  // Browser .chat-results: one compact framed card per variant, wrapping as a row — the cover
  // thumbnail, the muted label ellipsised, then the download and open glyphs.
  void ChatDock::appendVariants(const QVector<VariantCard>& variants) {
    if (variants.isEmpty()) return;
    QVBoxLayout* lay = appendTranscriptCard(0);
    auto* host = qobject_cast<QFrame*>(lay->parentWidget());
    // The strip sits bare in the transcript, as the browser's does; the frame is each card's own.
    if (host) host->setFrameShape(QFrame::NoFrame);
    lay->setContentsMargins(0, 0, 0, 0);
    auto* strip = new QWidget(host);
    auto* flow = new FlowLayout(strip, 0, 8, 8);
    for (const VariantCard& v : variants) {
      auto* card = new QFrame(strip);
      card->setObjectName(QStringLiteral("chatResult"));
      auto* row = new QHBoxLayout(card);
      row->setContentsMargins(7, 5, 7, 5);
      row->setSpacing(6);
      auto* thumb = new QLabel(card);
      thumb->setFixedSize(RESULT_THUMB, RESULT_THUMB);
      thumb->setPixmap(coverThumb(v.image, RESULT_THUMB, 5, borderCache, devicePixelRatioF()));
      new HoverPreview(thumb, v.image, v.label);   // carries the label; no tooltip beside it
      row->addWidget(thumb);
      auto* name = makePlainLabel(v.label, card);
      name->setText(name->fontMetrics().elidedText(v.label, Qt::ElideRight, RESULT_LABEL_MAX_PX));
      name->setToolTip(v.label);
      row->addWidget(name);
      const QImage img = v.image;
      const QString label = v.label;
      QToolButton* save = makeGhostButton(card, QStringLiteral("Download %1").arg(v.label));
      save->setIcon(themedIcon(QStringLiteral("download"), textCache, HEADER_ICON));
      connect(save, &QToolButton::clicked, this, [this, img, label] {
        const QString path = QFileDialog::getSaveFileName(
            this, "Save variant", label + QStringLiteral(".png"), "PNG image (*.png)");
        if (!path.isEmpty()) img.save(path, "PNG");
      });
      row->addWidget(save);
      QToolButton* open = makeGhostButton(card, QStringLiteral("Open %1 in a new window").arg(v.label));
      open->setIcon(themedIcon(QStringLiteral("external"), textCache, HEADER_ICON));
      open->setEnabled(!v.projectId.isEmpty());
      const QString pid = v.projectId;
      connect(open, &QToolButton::clicked, this,
              [this, pid] { emit openVariantRequested(pid); });
      row->addWidget(open);
      flow->addWidget(card);
    }
    lay->addWidget(strip);
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
