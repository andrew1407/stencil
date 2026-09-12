#include "chatMenuPanel.hpp"
#include "chatMenuPanelParts.hpp"

#include "chatWidgets.hpp"   // placeChatBubbleTail / ChatBubbleTail
#include "../support/pillSplitter.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/iconSet.hpp"
#include "../support/modalReveal.hpp"   // support::motionReduced()
#include "../support/theme.hpp"

#include <QEasingCurve>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace stencil::gui {


  ChatMenuPanel::ChatMenuPanel(QWidget* parent, std::function<void(QString)> onSend,
                               std::function<void()> onStop, std::function<void()> onAttach,
                               std::function<void(QRect)> onSettings,
                               std::function<void(QString)> onRetry)
      : QWidget(parent),
        onSend_(std::move(onSend)),
        onStop_(std::move(onStop)),
        onAttach_(std::move(onAttach)),
        onSettings_(std::move(onSettings)),
        onRetry_(std::move(onRetry)) {
    setObjectName(QStringLiteral("chatMenuPanel"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(10, 2, 10, 6);
    col->setSpacing(4);

    // Transcript over the composer in a vertical splitter — the dock's layout.
    // The splitter's TOTAL height is pinned (a menu row needs a definite size);
    // the panel outlives menu rebuilds, so a dragged split persists.
    splitter_ = new PillSplitter(Qt::Vertical, this);
    splitter_->setObjectName(QStringLiteral("chatMenuSplitter"));
    splitter_->setChildrenCollapsible(false);
    splitter_->setHandleWidth(8);  // a slightly easier grab than the 6 px default

    scroll_ = new QScrollArea(splitter_);
    scroll_->setObjectName(QStringLiteral("chatMenuTranscript"));
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setMinimumHeight(180);  // a real transcript, not a peephole
    body_ = new QWidget(scroll_);
    rows_ = new QVBoxLayout(body_);
    rows_->setContentsMargins(0, 0, 0, 0);
    rows_->setSpacing(2);
    // Empty-state chips — the DOCK's, built by the shared factory. Clicking
    // prefills the composer; the block hides on the first message.
    suggest_ = makeSuggestionChips(body_, [this](QString prompt) {
      input_->setPlainText(prompt);  // prefill only — never send
      input_->moveCursor(QTextCursor::End);
      input_->setFocus();
      updateSendEnabled();
    });
    rows_->addWidget(suggest_);
    rows_->addStretch(1);
    scroll_->setWidget(body_);
    splitter_->addWidget(scroll_);

    auto* composer = new QWidget(splitter_);
    composer->setMinimumHeight(MENU_CHAT_COMPOSER_MIN);
    auto* ccol = new QVBoxLayout(composer);
    ccol->setContentsMargins(0, 0, 0, 0);
    ccol->setSpacing(4);
    auto* row = new QHBoxLayout;
    row->setSpacing(5);
    input_ = new QPlainTextEdit(composer);
    input_->setObjectName(QStringLiteral("chatMenuInput"));
    input_->setPlaceholderText(
        QStringLiteral("Ask the assistant… (Enter sends, Shift+Enter newline)"));
    input_->setMinimumHeight(38);  // grows with the splitter, never fixed
    input_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // ClickFocus, NOT the default WheelFocus: a popup hands focus to its first
    // tab-focusable child, which would route the menu's arrows/Enter into this
    // input before the user ever clicked it.
    input_->setFocusPolicy(Qt::ClickFocus);
    input_->installEventFilter(this);
    QObject::connect(input_, &QPlainTextEdit::textChanged, this,
                     [this] { updateSendEnabled(); });
    row->addWidget(input_, 1);

    // The dock's three composer buttons, same order, same factory
    // (makeChatAccentButton) — the two composers read identical side by side.
    const auto mkBtn = [this](const char* name, const QString& tip) {
      QToolButton* b = makeChatAccentButton(this, tip);
      b->setObjectName(QString::fromLatin1(name));
      return b;
    };
    send_ = mkBtn("chatMenuSend", QString());
    QObject::connect(send_, &QToolButton::clicked, this, [this] {
      if (busy_) {
        if (onStop_) onStop_();
        return;
      }
      submit();
    });
    attach_ = mkBtn("chatMenuAttach",
                    QStringLiteral("Attach an image or video (closes the menu)"));
    QObject::connect(attach_, &QToolButton::clicked, this, [this] {
      if (onAttach_) onAttach_();
    });
    gear_ = mkBtn("chatMenuGear", QStringLiteral("AI assistant settings"));
    QObject::connect(gear_, &QToolButton::clicked, this, [this] {
      // Captured NOW: the settings dialog opens after this popup closes (a
      // modal fights the popup's own grab), which would hide gear_ first.
      if (onSettings_)
        onSettings_(QRect(gear_->mapToGlobal(QPoint(0, 0)), gear_->size()));
    });
    auto* btnWrap = new QWidget(composer);
    auto* btnCol = new QVBoxLayout(btnWrap);
    btnCol->setContentsMargins(0, 0, 0, 0);
    btnCol->setSpacing(0);
    btnCol->addStretch(1);  // pin the row to the bottom of the input
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(2);
    btnRow->addWidget(send_);
    btnRow->addWidget(attach_);
    btnRow->addWidget(gear_);
    btnCol->addLayout(btnRow);
    row->addWidget(btnWrap, 0, Qt::AlignBottom);
    // Reachability dot riding on the gear's corner — the dock's badge, same
    // geometry, fed by the same refreshLlmStatus probe.
    statusDot_ = new QLabel(gear_);
    statusDot_->setObjectName(QStringLiteral("chatMenuStatusDot"));
    statusDot_->setFixedSize(7, 7);
    statusDot_->setAttribute(Qt::WA_TransparentForMouseEvents);
    statusDot_->move(MENU_CHAT_BUTTON_EDGE - statusDot_->width() - 1, 1);
    statusDot_->raise();
    ccol->addLayout(row, 1);
    splitter_->addWidget(composer);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 0);
    const int transcript = menuChatTranscriptHeight();
    // FIXED total, not just capped: a bare maximum lets the transcript collapse
    // to its content sizeHint. Pinned, it always shows several exchanges.
    // Line the drag pill up with the input column, not the whole row.
    splitter_->setPillReference(input_);
    updateSendEnabled();  // empty input ⇒ send starts disabled (dock parity)
    splitter_->setFixedHeight(transcript + MENU_CHAT_COMPOSER_HEIGHT);
    splitter_->setSizes({transcript, MENU_CHAT_COMPOSER_HEIGHT});
    col->addWidget(splitter_);

    setMinimumWidth(MENU_CHAT_WIDTH);
    setMaximumWidth(MENU_CHAT_WIDTH + 120);
  }

  QWidget* ChatMenuPanel::input() const { return input_; }

  // One card per message, built by the DOCK's fillChatCard — same frame, role
  // colour and side, and the same row menu / Resend affordances.
  void ChatMenuPanel::appendRow(const QString& role, const QString& text, bool muted,
                                const QString& retryText, bool pending,
                                const QStringList& notes, bool configure) {
    suggest_->hide();  // empty-state affordance only
    auto* card = new QFrame(body_);
    card->setFrameShape(QFrame::StyledPanel);
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(2);
    const ChatCardKind kind = role == QLatin1String("Error") ? ChatCardKind::ERROR
                              : muted                       ? ChatCardKind::MUTED
                                                            : ChatCardKind::BUBBLE;
    fillChatCard(card, lay, role, text, kind, danger_);
    // Warnings / executor notes ride INSIDE the bubble, exactly as the dock
    // renders them — one card per turn, never extra rows.
    for (const QString& n : notes) addChatCardNote(lay, n);
    if (configure) addConfigure(card);   // dock parity: CTA first, then Retry
    if (!retryText.isEmpty()) addRetry(card, retryText);
    rows_->insertWidget(rows_->count() - 1, card);
    const bool user = role == QLatin1String("You");
    // pageBg=chip_: this panel sits straight on the QMenu background, with no
    // separate #chatBody surface under it, so the fill colour IS the flatten base.
    applyChatBubbleSide(card, rows_, isChatBubbleOnRight(user, chatSwapSides_), accent_, chip_,
                        border_, danger_, chip_);
    // Every SETTLED row carries the menu (browser chatRowMenuItems excludes
    // only the pending one).
    if (!pending) installChatCardMenu(card, menuHooks());
    rowsAdded_.append(card);
    // Bounded like chatHistory_: past the cap the oldest mirrored row goes —
    // through the same scatter as any other row leaving, not a bare delete.
    while (rowsAdded_.size() > CHAT_HISTORY_BOUND) dissolveRow(rowsAdded_.takeFirst());
    applyChatBubbleWidths(body_, scroll_);   // the dock's wrap/measure pass
    scrollToBottom();
    // …and only THEN it arrives out of its own dust, the leave played backwards (the
    // dock's animateCardIn / browser motion.js chatIn). Measured widths AND the scroll
    // first: the gather is a photograph, and a row measured before either has landed is
    // either the wrong size or in the wrong place.
    gatherRow(card);
  }

  // A late note goes INTO the last ASSISTANT bubble, never into whatever row
  // happens to be last (dock parity: chatDock.cpp appendLateNote).
  void ChatMenuPanel::appendLateNote(const QString& text) {
    QFrame* last = nullptr;
    for (int i = rowsAdded_.size() - 1; i >= 0 && !last; --i)
      if (rowsAdded_.at(i) != pending_ &&
          rowsAdded_.at(i)->objectName() == QLatin1String("chatCardAssistant"))
        last = rowsAdded_.at(i);
    if (!last) return;
    if (auto* lay = qobject_cast<QVBoxLayout*>(last->layout())) {
      addChatCardNote(lay, text);
      applyChatBubbleWidths(body_, scroll_);
      scrollToBottom();
    }
  }

  // The body label inside a mirrored card (the one fillChatCard tagged).
  QLabel* ChatMenuPanel::bodyOf(QFrame* card) {
    if (!card) return nullptr;
    for (QLabel* l : card->findChildren<QLabel*>())
      if (l->property("chatBody").isValid()) return l;
    return nullptr;
  }

  // The error/stopped card's Resend, same control the dock adds.
  void ChatMenuPanel::addRetry(QFrame* card, const QString& retryText) {
    auto* lay = qobject_cast<QVBoxLayout*>(card->layout());
    if (!lay) return;
    std::function<void()> cb;
    if (onRetry_) cb = [this, retryText] { onRetry_(retryText); };
    addChatRetryButton(lay, muted_, cb);
  }

  // The shared unreachable-card CTA (chatDock.cpp addChatConfigureCta). Opening the
  // dialog closes this popup first (a modal fights the popup's own grab) — so the
  // CTA's global rect is captured HERE, while it is still on screen, and rides
  // through onSettings_ as the reveal's fallback anchor.
  void ChatMenuPanel::addConfigure(QFrame* card) {
    auto* lay = qobject_cast<QVBoxLayout*>(card->layout());
    if (!lay) return;
    QPointer<ChatMenuPanel> self(this);
    addChatConfigureCta(lay, accent_, [self](QPushButton* cta) {
      if (self && self->onSettings_)
        self->onSettings_(QRect(cta->mapToGlobal(QPoint(0, 0)), cta->size()));
    });
  }
}  // namespace stencil::gui

