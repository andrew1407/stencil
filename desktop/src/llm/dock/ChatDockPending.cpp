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
    pendingCard = lay->parentWidget();
    pendingCard->setObjectName(QStringLiteral("chatCardAssistant"));
    pendingRole = nullptr;  // the bubble's own tone says "assistant" (browser parity)
    // An in-flight turn shows bouncing dots; markPendingStopped swaps in the label.
    pendingBody = makePlainLabel(QString(), pendingCard);
    pendingBody->setWordWrap(true);
    pendingBody->setProperty("chatRole", QStringLiteral("Assistant"));
    pendingBody->setProperty("chatBody", QStringLiteral("…"));
    pendingBody->hide();
    pendingDots = new TypingDots(pendingCard);
    lay->addWidget(pendingDots);
    lay->addWidget(pendingBody);
    log.transcriptLayout->setAlignment(pendingCard, Qt::AlignLeft);
    applyBubbleWidths();
    // The "…" card is the send's tail end — bring it fully into view.
    scrollToBottom();
  }

  void ChatDock::clearPending() {
    if (pendingCard) pendingCard->deleteLater();
    pendingDots = nullptr;   // owned by the card
    pendingCard = nullptr;
    pendingRole = nullptr;
    pendingBody = nullptr;
  }

  void ChatDock::markPendingStopped(const QString& stoppedText) {
    if (!pendingCard) return;
    // The "…" card becomes the stop notice in place, error-card styled.
    if (pendingDots) { pendingDots->deleteLater(); pendingDots = nullptr; }
    pendingBody->show();
    pendingBody->setText(QStringLiteral("Stopped."));
    pendingBody->setProperty("chatBody", QStringLiteral("Stopped."));
    pendingCard->setObjectName(QStringLiteral("chatCardError"));
    // A stylesheet is matched when the widget is POLISHED, so renaming it afterwards changes nothing
    // until the style is re-run.
    repolish(pendingCard);
    for (QLabel* l : pendingCard->findChildren<QLabel*>()) repolish(l);
    // The browser renders a stopped turn with .chat-msg-error and the extension with .msg.error, both
    // in --danger; muted text made a stop look like an ordinary note on this surface alone.
    applyDangerText(pendingBody, dangerCache.isValid() ? dangerCache : QColor("#d6293e"));
    // Stopping is a change of mind, not a dead end: the card keeps the prompt.
    addRetryButton(qobject_cast<QVBoxLayout*>(pendingCard->layout()), stoppedText);
    // A SETTLED row gets the row menu, like every other one (browser chatRowMenuItems excludes only
    // pending rows). Built here rather than in showPending so an in-flight "..." never offers one.
    installCardMenu(qobject_cast<QFrame*>(pendingCard));
    pendingCard = nullptr;
    pendingRole = nullptr;
    pendingBody = nullptr;
  }

}  // namespace stencil::gui
