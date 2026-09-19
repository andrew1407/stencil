// The pending (in-flight turn) card.
// Split out of ChatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "chatWidgets.hpp"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

namespace stencil::gui {

  using namespace chatdock;
  void ChatDock::showPending() {
    clearPending();  // defensive: never two pending cards
    QVBoxLayout* lay = appendTranscriptCard(2);
    pendingCard_ = lay->parentWidget();
    pendingCard_->setObjectName(QStringLiteral("chatCardAssistant"));
    pendingRole_ = nullptr;  // the bubble's own tone says "assistant" (browser parity)
    // An in-flight turn shows bouncing dots; markPendingStopped swaps in the label.
    pendingBody_ = makePlainLabel(QString(), pendingCard_);
    pendingBody_->setWordWrap(true);
    pendingBody_->setProperty("chatRole", QStringLiteral("Assistant"));
    pendingBody_->setProperty("chatBody", QStringLiteral("…"));
    pendingBody_->hide();
    pendingDots_ = new TypingDots(pendingCard_);
    lay->addWidget(pendingDots_);
    lay->addWidget(pendingBody_);
    log_.transcriptLayout->setAlignment(pendingCard_, Qt::AlignLeft);
    applyBubbleWidths();
    // The "…" card is the send's tail end — bring it fully into view.
    scrollToBottom();
  }

  void ChatDock::clearPending() {
    if (pendingCard_) pendingCard_->deleteLater();
    pendingDots_ = nullptr;   // owned by the card
    pendingCard_ = nullptr;
    pendingRole_ = nullptr;
    pendingBody_ = nullptr;
  }

  void ChatDock::markPendingStopped(const QString& stoppedText) {
    if (!pendingCard_) return;
    // The "…" card becomes the stop notice in place, error-card styled.
    if (pendingDots_) { pendingDots_->deleteLater(); pendingDots_ = nullptr; }
    pendingBody_->show();
    pendingBody_->setText(QStringLiteral("Stopped."));
    pendingBody_->setProperty("chatBody", QStringLiteral("Stopped."));
    pendingCard_->setObjectName(QStringLiteral("chatCardError"));
    // A stylesheet is matched when the widget is POLISHED, so renaming it afterwards changes nothing
    // until the style is re-run.
    repolish(pendingCard_);
    for (QLabel* l : pendingCard_->findChildren<QLabel*>()) repolish(l);
    // The browser renders a stopped turn with .chat-msg-error and the extension with .msg.error, both
    // in --danger; muted text made a stop look like an ordinary note on this surface alone.
    applyDangerText(pendingBody_, dangerCache_.isValid() ? dangerCache_ : QColor("#d6293e"));
    // Stopping is a change of mind, not a dead end: the card keeps the prompt.
    addRetryButton(qobject_cast<QVBoxLayout*>(pendingCard_->layout()), stoppedText);
    // A SETTLED row gets the row menu, like every other one (browser chatRowMenuItems excludes only
    // pending rows). Built here rather than in showPending so an in-flight "..." never offers one.
    installCardMenu(qobject_cast<QFrame*>(pendingCard_));
    pendingCard_ = nullptr;
    pendingRole_ = nullptr;
    pendingBody_ = nullptr;
  }

}  // namespace stencil::gui
