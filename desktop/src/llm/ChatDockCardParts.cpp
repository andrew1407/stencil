// Card parts — typing dots, notes, retry/configure buttons — and the dock's menu hooks.
// Split out of ChatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "iconSet.hpp"
#include "theme.hpp"
#include "chatWidgets.hpp"

#include <QPlainTextEdit>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

namespace stencil::gui {

  using namespace chatdock;
  QWidget* makeChatTypingDots(QWidget* parent) {
    auto* dots = new TypingDots(parent);
    dots->setObjectName(QStringLiteral("chatTypingDots"));
    return dots;
  }

  QLabel* addChatCardNote(QVBoxLayout* lay, const QString& text) {
    if (!lay) return nullptr;
    auto* note = makePlainLabel(text, lay->parentWidget());
    note->setObjectName(QStringLiteral("chatNoteLabel"));   // muted via the QSS rule
    note->setWordWrap(true);
    note->setProperty("chatNote", text);
    applyMutedText(note);
    lay->addWidget(note);
    return note;
  }

  QToolButton* addChatRetryButton(QVBoxLayout* lay, const QColor& glyph,
                                  std::function<void()> onClick) {
    if (!lay) return nullptr;
    auto* retry = makeGhostButton(lay->parentWidget(),
                                  QStringLiteral("Send this message again"));
    retry->setObjectName("chatRetry");
    // Sized up from makeGhostButton's header-ghost default (HEADER_ICON/BUTTON_EDGE): a lone icon-only
    // action at the foot of an error card — often the ONLY thing on it, since a plain failure has no
    // Configure CTA beside it — reads as an afterthought at that size.
    static constexpr int RETRY_ICON = 18;
    static constexpr int RETRY_EDGE = 30;
    retry->setIconSize(QSize(RETRY_ICON, RETRY_ICON));
    retry->setFixedSize(RETRY_EDGE, RETRY_EDGE);
    // Neutral glyph on EVERY card, error ones included: the browser's retry is a .chat-hbtn, which sets
    // its own `color: var(--text-muted)` and never inherits the bubble's --danger. Painted red it sat
    // red-on-red in the danger wash; the red belongs to the card's ground, not the way out.
    retry->setIcon(labelIcon("refresh", glyph, RETRY_ICON));
    QObject::connect(retry, &QToolButton::clicked, retry,
                     [onClick] { if (onClick) onClick(); });
    lay->addWidget(retry, 0, Qt::AlignLeft);
    return retry;
  }

  // The unreachable-card "Configure provider" CTA (browser chatConfigureButton), shared by the dock
  // and the menu panel: accent-filled, white gear glyph, haloed on a light accent. `onClick` gets the
  // button, still on screen, as the settings reveal's anchor; the objectName stays free for the tests.
  QPushButton* addChatConfigureCta(QVBoxLayout* lay, const QColor& accent,
                                   std::function<void(QPushButton*)> onClick) {
    if (!lay) return nullptr;
    auto* cta = new QPushButton(QObject::tr("Configure provider"), lay->parentWidget());
    cta->setObjectName(QStringLiteral("chatConfigureCta"));
    cta->setProperty("accentCta", true);
    cta->setCursor(Qt::PointingHandCursor);
    cta->setIcon(labelIcon("gear", onAccentInk(accent), 14));
    cta->setIconSize(QSize(14, 14));
    QObject::connect(cta, &QPushButton::clicked, cta,
                     [cta, onClick] { if (onClick) onClick(cta); });
    lay->addWidget(cta, 0, Qt::AlignLeft);
    return cta;
  }

  // The dock's own hooks for the shared menu above: its composer, its resend
  // (attachments requeued), its transcript viewport.
  ChatCardMenuHooks ChatDock::cardMenuHooks() {
    ChatCardMenuHooks h;
    h.owner = this;
    h.scroll = scroll_;
    h.busy = [this] { return isBusy(); };
    h.insertIntoPrompt = [this](const QString& text) {
      const QString existing = input_->toPlainText();
      input_->setPlainText(existing.isEmpty() ? text : existing + QLatin1Char('\n') + text);
      input_->moveCursor(QTextCursor::End);
      QTimer::singleShot(0, input_, [this] { focusInput(); });  // after the menu's focus restore
    };
    h.resend = [this](QFrame* card, const QString& text) {
      // Requeue the turn's own images as fresh tray attachments (the browser's
      // requeueLastTurnAttachments), then send the same text through the normal
      // path — one bubble, one wire payload, tray drained by the send.
      for (const QVariant& v : card->property("chatImages").toList())
        addAttachmentImage(v.value<QImage>());
      emit sendRequested(text);
    };
    // No h.moreMoved: the relationship inverted (the pills win and the trigger gets out of THEIR way),
    // so a moved trigger has nothing to tell them. Wiring it back to updateJumpButtons would also be
    // reentrant — that now moves triggers itself, which would fire this same hook.
    h.avoidRect = [this] { return jumpPillsGlobalRect(); };
    h.leaving = [this] { return closing_; };
    h.text = textCache_.isValid() ? textCache_ : palette().color(QPalette::Text);
    h.chip = chipCache_.isValid() ? chipCache_ : palette().color(QPalette::AlternateBase);
    h.border = borderCache_.isValid() ? borderCache_ : palette().color(QPalette::Mid);
    h.accent = accentCache_.isValid() ? accentCache_ : palette().highlight().color();
    h.muted = mutedCache_.isValid() ? mutedCache_ : palette().color(QPalette::PlaceholderText);
    return h;
  }

  void ChatDock::installCardMenu(QFrame* card) { installChatCardMenu(card, cardMenuHooks()); }

}  // namespace stencil::gui
