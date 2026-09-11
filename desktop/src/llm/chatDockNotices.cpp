// Error, expired-session, unreachable, notice and note cards.
// Split out of chatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "chatDock.hpp"
#include "chatDockShared.hpp"
#include "theme.hpp"
#include "chatWidgets.hpp"

#include <QFrame>
#include <QPushButton>
#include <QPointer>
#include <QVBoxLayout>

namespace stencil::gui {

  using namespace chatdock;
  // A one-click Retry inside a card: re-sends exactly `retryText` through the owner's
  // normal send path (never auto-retried; the owner ignores it mid-turn). Icon-only —
  // a labelled button inside the bubble reads as part of the message.
  void ChatDock::appendError(const QString& text, const QString& retryText) {
    addRetryButton(appendCard("Error", text, CardKind::Error), retryText);
  }

  void ChatDock::addRetryButton(QVBoxLayout* lay, const QString& retryText) {
    if (!lay || retryText.isEmpty()) return;
    const QColor glyph = mutedCache_.isValid()
        ? mutedCache_
        : (textCache_.isValid() ? textCache_
                                : palette().color(QPalette::PlaceholderText));
    addChatRetryButton(lay, glyph, [this, retryText] { emit retryRequested(retryText); });
    // The card was measured before this button existed, so its wrapped text ends up
    // clipped under it. Re-run the width/height pass now that the row is complete.
    applyBubbleWidths();
  }

  void ChatDock::appendExpiredSession(const QString& text, const QString& host,
                                     const QString& retryText) {
    QVBoxLayout* lay = appendCard("Error", text, CardKind::Error);
    // The way back in, spelled out — an expired session is not something Resend
    // can fix, so the card leads with the reconnect (browser parity).
    auto* cta = new QPushButton(tr("Reconnect to %1").arg(host), lay->parentWidget());
    cta->setObjectName(QStringLiteral("chatReconnectCta"));   // the GUI test finds it
    cta->setProperty("accentCta", true);   // accent fill via theme.cpp's property rule
    cta->setCursor(Qt::PointingHandCursor);
    connect(cta, &QPushButton::clicked, this, [this, host] { emit reconnectRequested(host); });
    lay->addWidget(cta, 0, Qt::AlignLeft);
    addRetryButton(lay, retryText);   // …and the turn is still resendable after
    applyBubbleWidths();
  }

  void ChatDock::appendUnreachable(const QString& text, const QString& retryText) {
    QVBoxLayout* lay = appendCard("Error", text, CardKind::Error);
    // The reveal flies from THIS button — it lives in the transcript and stays on
    // screen through the click, unlike the gear behind "…".
    QPointer<ChatDock> self(this);
    addChatConfigureCta(lay, accentCache_, [self](QPushButton* cta) {
      if (self) emit self->configureProviderRequested(cta);
    });
    addRetryButton(lay, retryText);
    applyBubbleWidths();
  }

  void ChatDock::appendNotice(const QString& text) {
    appendCard("Assistant off", text, CardKind::Muted);
  }

  void ChatDock::appendLateNote(const QString& text) {
    // Anything the turn has to say about its own reply reports INTO that reply's
    // bubble (a separate card read as a second assistant message); falls back to
    // a plain note with no bubble.
    if (lastAssistantCard_) {
      if (auto* lay = qobject_cast<QVBoxLayout*>(lastAssistantCard_->layout())) {
        auto* note = makePlainLabel(text, lastAssistantCard_);
        note->setObjectName(QStringLiteral("chatNoteLabel"));
        note->setWordWrap(true);
        note->setProperty("chatNote", text);
        applyMutedText(note);
        lay->addWidget(note);
        installCardMenu(qobject_cast<QFrame*>(lastAssistantCard_.data()));
        applyBubbleWidths();
        emit lateNotePosted(text);
        return;
      }
    }
    appendNote(text);
  }

  void ChatDock::appendNote(const QString& text) {
    // Informational, about work that SUCCEEDED — the danger style is reserved
    // for actual turn errors (appendError / a stopped turn).
    appendCard("Note", text, CardKind::Muted);
    emit notePosted(text);
  }
}  // namespace stencil::gui
