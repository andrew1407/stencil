#include "chatDock.hpp"
#include "chatWidgets.hpp"
#include "../support/theme.hpp"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

// Chat card rendering, split from chatDock.cpp: the shared bubble stylesheet,
// fillChatCard (both chat surfaces build their rows through it), and
// ChatDock::appendCard.

namespace stencil::gui {

  // Browser .chat-msg parity: the ROLE is carried by colour and side, not by a
  // caption — user bubbles accent-tinted on the right, assistant on the
  // container tone at the left, errors/notices in the danger/muted treatment.
  // The role/body still ride as widget PROPERTIES so the menu mirror and the
  // GUI test can read a row without depending on the visual structure.
  // Transcript bubbles (browser .chat-msg-user / -assistant / -error): the ROLE
  // is the colour and the side, not a caption. Applied by every surface that
  // hosts fillChatCard cards — the dock and the context menu's panel.
  QString chatCardStyleSheet(const Palette& pal) {
    const auto rgba = [](const QColor& c, double a) {
      return QStringLiteral("rgba(%1,%2,%3,%4)")
          .arg(c.red()).arg(c.green()).arg(c.blue()).arg(a);
    };
    return QStringLiteral(
               "#chatCardUser{background:%3;border:1px solid %4;border-radius:10px;}"
               "#chatCardAssistant{background:%2;border:1px solid %1;border-radius:10px;}"
               // Muted informational cards (notes/notices): --text-muted through
               // the STYLESHEET — under QSS a palette colour loses, the same trap
               // the error label rule below documents.
               "#chatCardMuted{background:%8;border:1px solid %1;border-radius:10px;}"
               "#chatCardMuted QLabel{color:%7;background:transparent;}"
               // A failed / stopped turn reads as one at a glance: danger text over
               // a faint danger wash, not an ordinary bubble with a red edge
               // (browser .chat-msg-error). The label rule matters as much as the
               // frame — a stylesheet colour beats the palette one applyDangerText
               // sets.
               "#chatCardError{background:%6;border:1px solid %5;border-radius:10px;}"
               "#chatCardError QLabel{color:%9;background:transparent;}")
        .arg(pal.borderMain.name(), pal.bgContainer.name(),
             // The user bubble's accent tint + border (browser color-mix(accent
             // 14%/32%) rendered with explicit alphas).
             rgba(pal.accent, 0.14), rgba(pal.accent, 0.32),
             // The error bubble's hairline, wash and text (color-mix(danger
             // 45%/10%) + `color: var(--danger)`).
             rgba(pal.danger, 0.45), rgba(pal.danger, 0.10),
             rgba(pal.textMuted, pal.textMuted.alphaF()), pal.bgControls.name(),
             pal.danger.name());
  }

  // The rendering itself is shared with the context menu's assistant panel, so
  // one message looks the same in both surfaces (fillChatCard below).
  QLabel* fillChatCard(QFrame* card, QVBoxLayout* lay, const QString& role,
                       const QString& text, ChatCardKind kind, const QColor& danger) {
    const bool user = role == QLatin1String("You");
    card->setObjectName(kind == ChatCardKind::Error ? QStringLiteral("chatCardError")
                        : kind == ChatCardKind::Muted ? QStringLiteral("chatCardMuted")
                        : user ? QStringLiteral("chatCardUser")
                               : QStringLiteral("chatCardAssistant"));
    auto* bodyLabel = makePlainLabel(text, card);   // model output is DATA
    bodyLabel->setWordWrap(true);                   // full text, never elided
    bodyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bodyLabel->setProperty("chatRole", role);
    bodyLabel->setProperty("chatBody", text);
    // Browser .chat-msg-error: the failure reads in --danger, not as muted prose.
    if (kind == ChatCardKind::Error)
      applyDangerText(bodyLabel, danger.isValid() ? danger : QColor("#d6293e"));
    if (kind == ChatCardKind::Muted) applyMutedText(bodyLabel);
    lay->addWidget(bodyLabel);
    return bodyLabel;
  }

  QVBoxLayout* ChatDock::appendCard(const QString& role, const QString& text, CardKind kind) {
    QVBoxLayout* lay = appendTranscriptCard(2);
    auto* card = qobject_cast<QFrame*>(lay->parentWidget());
    const bool user = role == QLatin1String("You");
    fillChatCard(card, lay, role, text,
                 kind == CardKind::Error   ? ChatCardKind::Error
                 : kind == CardKind::Muted ? ChatCardKind::Muted
                                           : ChatCardKind::Bubble,
                 dangerCache_);
    transcriptLayout_->setAlignment(card, user ? Qt::AlignRight : Qt::AlignLeft);
    installCardMenu(card);
    applyBubbleWidths();
    return lay;
  }

}  // namespace stencil::gui
