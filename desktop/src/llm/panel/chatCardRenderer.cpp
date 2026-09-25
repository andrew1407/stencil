#include "ChatDock.hpp"
#include "chatWidgets.hpp"
#include "../../support/theme/theme.hpp"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

// Chat card rendering shared by both chat surfaces.

namespace stencil::gui {

  // Browser .chat-msg parity: the ROLE is colour and side; role/body ride as widget PROPERTIES.
  QString chatCardStyleSheet(const Palette& pal, bool swapped) {
    const auto rgba = [](const QColor& c, double a) {
      return QStringLiteral("rgba(%1,%2,%3,%4)")
          .arg(c.red()).arg(c.green()).arg(c.blue()).arg(a);
    };
    // Flattened at the tail corner (browser .chat-swapped). Sheet-wide: a per-card local QSS
    // shifted that card's wrapped-label height.
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
               // --text-muted through the STYLESHEET — under QSS a palette colour loses.
               "#chatCardMuted{background:%8;border:1px solid %1;border-radius:10px;}"
               "#chatCardMuted QLabel{color:%7;background:transparent;}"
               // browser .chat-msg-error; the label rule matters — a stylesheet colour beats applyDangerText.
               "#chatCardError{background:%6;border:1px solid %5;%11}"
               "#chatCardError QLabel{color:%9;background:transparent;}"
               // browser .chat-result: the variant card, its label in --text-muted
               "#chatResult{background:%2;border:1px solid %1;border-radius:8px;}"
               "#chatResult QLabel{color:%7;background:transparent;}")
        .arg(pal.borderMain.name(), pal.bgContainer.name(),
             // browser color-mix(accent 14%/32%)
             rgba(pal.accent, 0.14), rgba(pal.accent, 0.32),
             // color-mix(danger 45%/10%) + `color: var(--danger)`
             rgba(pal.danger, 0.45), rgba(pal.danger, 0.10),
             rgba(pal.textMuted, pal.textMuted.alphaF()), pal.bgControls.name(),
             pal.danger.name())
        .arg(userRadii, otherRadii);
  }

  // Flattened opaque: ChatBubbleTail's fill triangle would otherwise blend a second time over its border.
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
    return false;
  }

  void applyChatBubbleSide(QFrame* card, QLayout* layout, bool right, const QColor& accent,
                           const QColor& chip, const QColor& border, const QColor& danger,
                           const QColor& pageBg) {
    if (!card) return;
    if (layout) layout->setAlignment(card, right ? Qt::AlignRight : Qt::AlignLeft);
    card->setProperty(CHAT_ON_RIGHT_PROPERTY, right);
    QColor fill, tailBorder;
    if (chatBubbleColorsFor(card->objectName(), accent, chip, border, danger, pageBg,
                            fill, tailBorder))
      placeChatBubbleTail(card, fill, tailBorder, right);
  }

  QLabel* fillChatCard(QFrame* card, QVBoxLayout* lay, const QString& role,
                       const QString& text, ChatCardKind kind, const QColor& danger) {
    const bool user = role == QLatin1String("You");
    card->setObjectName(kind == ChatCardKind::ERROR ? QStringLiteral("chatCardError")
                        : kind == ChatCardKind::MUTED ? QStringLiteral("chatCardMuted")
                        : user ? QStringLiteral("chatCardUser")
                               : QStringLiteral("chatCardAssistant"));
    auto* bodyLabel = makePlainLabel(text, card);
    bodyLabel->setWordWrap(true);
    bodyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bodyLabel->setProperty("chatRole", role);
    bodyLabel->setProperty("chatBody", text);
    if (kind == ChatCardKind::ERROR)
      applyDangerText(bodyLabel, danger.isValid() ? danger : QColor("#d6293e"));
    if (kind == ChatCardKind::MUTED) applyMutedText(bodyLabel);
    lay->addWidget(bodyLabel);
    return bodyLabel;
  }

  // Both surfaces' setChatSwapSides run this after re-issuing the shared stylesheet.
  void applyChatSwapToCards(QWidget* transcript, QLayout* layout, bool swapped,
                            const QColor& accent, const QColor& chip, const QColor& border,
                            const QColor& danger, const QColor& pageBg) {
    if (!transcript) return;
    for (QFrame* card : transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
      const bool user = card->objectName() == QLatin1String("chatCardUser");
      applyChatBubbleSide(card, layout, isChatBubbleOnRight(user, swapped), accent, chip,
                          border, danger, pageBg);
    }
  }

  QVBoxLayout* ChatDock::appendCard(const QString& role, const QString& text, CardKind kind) {
    QVBoxLayout* lay = appendTranscriptCard(2);
    auto* card = qobject_cast<QFrame*>(lay->parentWidget());
    const bool user = role == QLatin1String("You");
    fillChatCard(card, lay, role, text,
                 kind == CardKind::ERROR   ? ChatCardKind::ERROR
                 : kind == CardKind::MUTED ? ChatCardKind::MUTED
                                           : ChatCardKind::BUBBLE,
                 dangerCache);
    // pageBg is the transcript's REAL backdrop (bgControls), not chipCache — else the tail is a shade off.
    applyChatBubbleSide(card, log.transcriptLayout, isChatBubbleOnRight(user, chatSwapSides),
                        accentCache, chipCache, borderCache, dangerCache,
                        paletteCache.bgControls);
    installCardMenu(card);
    applyBubbleWidths();
    return lay;
  }

  void ChatDock::setChatSwapSides(bool on) {
    if (chatSwapSides == on) return;
    chatSwapSides = on;
    // The flattened tail corner rides the SHARED stylesheet, keyed off chatSwapSides.
    restyleIcons(paletteCache);
    applyChatSwapToCards(log.transcript, log.transcriptLayout, chatSwapSides, accentCache,
                         chipCache, borderCache, dangerCache, paletteCache.bgControls);
    applyBubbleWidths();
  }

}  // namespace stencil::gui
