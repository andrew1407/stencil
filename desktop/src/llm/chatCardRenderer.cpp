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

  // Browser .chat-msg parity: the ROLE is the colour and the side, not a caption;
  // the role/body ride as widget PROPERTIES for the menu mirror and the GUI test.
  QString chatCardStyleSheet(const Palette& pal, bool swapped) {
    const auto rgba = [](const QColor& c, double a) {
      return QStringLiteral("rgba(%1,%2,%3,%4)")
          .arg(c.red()).arg(c.green()).arg(c.blue()).arg(a);
    };
    // Flattened at the corner the tail attaches to (browser border-bottom-*-radius:0,
    // mirrored by .chat-swapped) — the base 10px radius left a notch against the tail's
    // straight edges. Sheet-wide, not per-card: a per-card local QSS shifted that card's
    // wrapped-label height (a cascade-boundary quirk).
    const QString flatBL =
        QStringLiteral("border-top-left-radius:10px;border-top-right-radius:10px;"
                       "border-bottom-left-radius:0;border-bottom-right-radius:10px;");
    const QString flatBR =
        QStringLiteral("border-top-left-radius:10px;border-top-right-radius:10px;"
                       "border-bottom-left-radius:10px;border-bottom-right-radius:0;");
    const QString& userRadii = swapped ? flatBL : flatBR;
    const QString& otherRadii = swapped ? flatBR : flatBL;
    return QStringLiteral(
               "#chatCardUser{background:%3;border:1px solid %4;%10}"
               "#chatCardAssistant{background:%2;border:1px solid %1;%11}"
               // Muted informational cards (notes/notices): --text-muted through
               // the STYLESHEET — under QSS a palette colour loses, the same trap
               // the error label rule below documents. No tail, so no flattening.
               "#chatCardMuted{background:%8;border:1px solid %1;border-radius:10px;}"
               "#chatCardMuted QLabel{color:%7;background:transparent;}"
               // A failed / stopped turn reads as one at a glance: danger text over
               // a faint danger wash, not an ordinary bubble with a red edge
               // (browser .chat-msg-error). The label rule matters as much as the
               // frame — a stylesheet colour beats the palette one applyDangerText
               // sets.
               "#chatCardError{background:%6;border:1px solid %5;%11}"
               "#chatCardError QLabel{color:%9;background:transparent;}")
        .arg(pal.borderMain.name(), pal.bgContainer.name(),
             // The user bubble's accent tint + border (browser color-mix(accent
             // 14%/32%) rendered with explicit alphas).
             rgba(pal.accent, 0.14), rgba(pal.accent, 0.32),
             // The error bubble's hairline, wash and text (color-mix(danger
             // 45%/10%) + `color: var(--danger)`).
             rgba(pal.danger, 0.45), rgba(pal.danger, 0.10),
             rgba(pal.textMuted, pal.textMuted.alphaF()), pal.bgControls.name(),
             pal.danger.name())
        .arg(userRadii, otherRadii);
  }

  // Flattened opaque, not translucent: the card's own rgba() fill blends once
  // against a known backdrop, but ChatBubbleTail paints onto whatever the host
  // last left there — and its fill triangle would blend a second time over the
  // border triangle under it, muddying the two into one tone.
  bool chatBubbleColorsFor(const QString& objectName, const QColor& accent, const QColor& chip,
                           const QColor& border, const QColor& danger, const QColor& pageBg,
                           QColor& fillOut, QColor& borderOut) {
    if (objectName == QLatin1String("chatCardUser")) {
      fillOut = blendColors(accent, pageBg, 0.14);
      borderOut = blendColors(accent, pageBg, 0.32);
      return true;
    }
    if (objectName == QLatin1String("chatCardAssistant")) {
      fillOut = chip; borderOut = border;
      return true;
    }
    if (objectName == QLatin1String("chatCardError")) {
      fillOut = blendColors(danger, pageBg, 0.10);
      borderOut = blendColors(danger, pageBg, 0.45);
      return true;
    }
    return false;   // Muted (or unrecognised): no tail, browser .chat-msg.note parity
  }

  void applyChatBubbleSide(QFrame* card, QLayout* layout, bool right, const QColor& accent,
                           const QColor& chip, const QColor& border, const QColor& danger,
                           const QColor& pageBg) {
    if (!card) return;
    if (layout) layout->setAlignment(card, right ? Qt::AlignRight : Qt::AlignLeft);
    card->setProperty(kChatOnRightProperty, right);   // placeChatCardMore reads this back
    QColor fill, tailBorder;
    if (chatBubbleColorsFor(card->objectName(), accent, chip, border, danger, pageBg,
                            fill, tailBorder))
      placeChatBubbleTail(card, fill, tailBorder, right);
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

  // Re-skin every card already under `transcript` for the current swap preference —
  // alignment in `layout`, kChatOnRightProperty and tail side (both surfaces'
  // setChatSwapSides run this after re-issuing the shared stylesheet).
  void applyChatSwapToCards(QWidget* transcript, QLayout* layout, bool swapped,
                            const QColor& accent, const QColor& chip, const QColor& border,
                            const QColor& danger, const QColor& pageBg) {
    if (!transcript) return;
    for (QFrame* card : transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
      const bool user = card->objectName() == QLatin1String("chatCardUser");
      applyChatBubbleSide(card, layout, chatBubbleOnRight(user, swapped), accent, chip,
                          border, danger, pageBg);
    }
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
    // pageBg is the transcript's REAL backdrop (#chatBody's bgControls), not
    // chipCache_ (bgContainer) — the wrong base leaves the tail a shade off the
    // card's own composited fill/border.
    applyChatBubbleSide(card, transcriptLayout_, chatBubbleOnRight(user, chatSwapSides_),
                        accentCache_, chipCache_, borderCache_, dangerCache_,
                        paletteCache_.bgControls);
    installCardMenu(card);
    applyBubbleWidths();
    return lay;
  }

  void ChatDock::setChatSwapSides(bool on) {
    if (chatSwapSides_ == on) return;
    chatSwapSides_ = on;
    // The flattened tail corner rides the SHARED stylesheet (chatCardStyleSheet),
    // keyed off chatSwapSides_ — re-issue it so every card's corner flips too,
    // not just its alignment and tail.
    restyleIcons(paletteCache_);
    // Re-skin every EXISTING card in place — the conversation already on screen
    // flips too, not just future turns (browser/extension parity).
    applyChatSwapToCards(transcript_, transcriptLayout_, chatSwapSides_, accentCache_,
                         chipCache_, borderCache_, dangerCache_, paletteCache_.bgControls);
    applyBubbleWidths();   // the cap didn't change, but the tails' positions did
  }

}  // namespace stencil::gui
