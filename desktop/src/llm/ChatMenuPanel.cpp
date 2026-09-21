#include "ChatMenuPanel.hpp"
#include "chatMenuPanelParts.hpp"

#include "chatWidgets.hpp"   // placeChatBubbleTail / ChatBubbleTail
#include "../support/PillSplitter.hpp"
#include "../support/DisintegrateOverlay.hpp"
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
        onSend(std::move(onSend)),
        onStop(std::move(onStop)),
        onAttach(std::move(onAttach)),
        onSettings(std::move(onSettings)),
        onRetry(std::move(onRetry)) {
    setObjectName(QStringLiteral("chatMenuPanel"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(MENU_CHAT_PADDING);
    col->setSpacing(4);

    // Transcript over the composer in a vertical splitter - the dock's layout. The splitter's TOTAL
    // height is pinned (a menu row needs a definite size); the panel outlives menu rebuilds.
    splitter = new PillSplitter(Qt::Vertical, this);
    splitter->setObjectName(QStringLiteral("chatMenuSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(8);  // a slightly easier grab than the 6 px default

    scroll = new QScrollArea(splitter);
    scroll->setObjectName(QStringLiteral("chatMenuTranscript"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumHeight(180);  // a real transcript, not a peephole
    body = new QWidget(scroll);
    rows = new QVBoxLayout(body);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(2);
    // Empty-state chips — the DOCK's, built by the shared factory. Clicking
    // prefills the composer; the block hides on the first message.
    suggest = makeSuggestionChips(body, MENU_CHAT_CHIP_GAP, [this](QString prompt) {
      input->setPlainText(prompt);  // prefill only — never send
      input->moveCursor(QTextCursor::End);
      input->setFocus();
      updateSendEnabled();
    });
    rows->addWidget(suggest);
    rows->addStretch(1);
    scroll->setWidget(body);
    splitter->addWidget(scroll);

    auto* composer = new QWidget(splitter);
    composer->setMinimumHeight(MENU_CHAT_COMPOSER_MIN);
    auto* ccol = new QVBoxLayout(composer);
    ccol->setContentsMargins(0, 0, 0, 0);
    ccol->setSpacing(4);
    auto* row = new QHBoxLayout;
    row->setSpacing(MENU_CHAT_ROW_GAP);
    input = new QPlainTextEdit(composer);
    input->setObjectName(QStringLiteral("chatMenuInput"));
    input->setPlaceholderText(
        QStringLiteral("Ask the assistant… (Enter sends, Shift+Enter newline)"));
    input->setMinimumHeight(MENU_CHAT_INPUT_MIN_H);  // grows with the splitter, never fixed
    input->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // ClickFocus, NOT the default WheelFocus: a popup hands focus to its first tab-focusable child,
    // which would route the menu's arrows/Enter into this input before the user ever clicked it.
    input->setFocusPolicy(Qt::ClickFocus);
    input->installEventFilter(this);
    QObject::connect(input, &QPlainTextEdit::textChanged, this,
                     [this] { updateSendEnabled(); });
    row->addWidget(input, 1);

    auto* btnWrap = new QWidget(composer);
    auto* btnCol = new QVBoxLayout(btnWrap);
    btnCol->setContentsMargins(0, 0, 0, 0);
    btnCol->setSpacing(0);
    btnCol->addStretch(1);  // pin the row to the bottom of the input
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(MENU_CHAT_ACTION_GAP);
    buildComposerActions(this, btnRow);
    btnCol->addLayout(btnRow);
    row->addWidget(btnWrap, 0, Qt::AlignBottom);
    ccol->addLayout(row, 1);
    splitter->addWidget(composer);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    const int transcript = menuChatTranscriptHeight();
    // FIXED total, not just capped: a bare maximum lets the transcript collapse to its content
    // sizeHint. Pinned, it always shows several exchanges.
    splitter->setPillReference(input);
    updateSendEnabled();  // empty input ⇒ send starts disabled (dock parity)
    splitter->setFixedHeight(transcript + MENU_CHAT_COMPOSER_HEIGHT);
    splitter->setSizes({transcript, MENU_CHAT_COMPOSER_HEIGHT});
    col->addWidget(splitter);

    setMinimumWidth(MENU_CHAT_WIDTH);
    setMaximumWidth(MENU_CHAT_WIDTH + 120);
  }

  QWidget* ChatMenuPanel::getInput() const { return input; }

  // One card per message, built by the DOCK's fillChatCard — same frame, role
  // colour and side, and the same row menu / Resend affordances.
  void ChatMenuPanel::appendRow(const QString& role, const QString& text, bool muted,
                                const QString& retryText, bool pending,
                                const QStringList& notes, bool configure) {
    suggest->hide();  // empty-state affordance only
    auto* card = new QFrame(body);
    card->setFrameShape(QFrame::StyledPanel);
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(2);
    const ChatCardKind kind = role == QLatin1String("Error") ? ChatCardKind::ERROR
                              : muted                       ? ChatCardKind::MUTED
                                                            : ChatCardKind::BUBBLE;
    fillChatCard(card, lay, role, text, kind, danger);
    // Warnings / executor notes ride INSIDE the bubble, exactly as the dock
    // renders them — one card per turn, never extra rows.
    for (const QString& n : notes) addChatCardNote(lay, n);
    if (configure) addConfigure(card);   // dock parity: CTA first, then Retry
    if (!retryText.isEmpty()) addRetry(card, retryText);
    rows->insertWidget(rows->count() - 1, card);
    const bool user = role == QLatin1String("You");
    // pageBg=chip: this panel sits straight on the QMenu background, with no
    // separate #chatBody surface under it, so the fill colour IS the flatten base.
    applyChatBubbleSide(card, rows, isChatBubbleOnRight(user, chatSwapSides), accent, chip,
                        border, danger, chip);
    // Every SETTLED row carries the menu (browser chatRowMenuItems excludes
    // only the pending one).
    if (!pending) installChatCardMenu(card, menuHooks());
    rowsAdded.append(card);
    // Bounded like chatHistory: past the cap the oldest mirrored row goes —
    // through the same scatter as any other row leaving, not a bare delete.
    while (rowsAdded.size() > CHAT_HISTORY_BOUND) dissolveRow(rowsAdded.takeFirst());
    applyChatBubbleWidths(body, scroll);   // the dock's wrap/measure pass
    scrollToBottom();
    // It arrives out of its own dust, the leave played backwards (the dock's animateCardIn / browser
    // motion.js chatIn). Measured widths AND the scroll first: the gather is a photograph.
    gatherRow(card);
  }

  // A late note goes INTO the last ASSISTANT bubble, never into whatever row
  // happens to be last (dock parity: ChatDock.cpp appendLateNote).
  void ChatMenuPanel::appendLateNote(const QString& text) {
    QFrame* last = nullptr;
    for (int i = rowsAdded.size() - 1; i >= 0 && !last; --i)
      if (rowsAdded.at(i) != pending &&
          rowsAdded.at(i)->objectName() == QLatin1String("chatCardAssistant"))
        last = rowsAdded.at(i);
    if (!last) return;
    if (auto* lay = qobject_cast<QVBoxLayout*>(last->layout())) {
      addChatCardNote(lay, text);
      applyChatBubbleWidths(body, scroll);
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
    if (onRetry) cb = [this, retryText] { onRetry(retryText); };
    addChatRetryButton(lay, muted, cb);
  }

  // The shared unreachable-card CTA (ChatDock.cpp addChatConfigureCta). Opening the dialog closes
  // this popup first, so the CTA's global rect is captured HERE while it is still on screen.
  void ChatMenuPanel::addConfigure(QFrame* card) {
    auto* lay = qobject_cast<QVBoxLayout*>(card->layout());
    if (!lay) return;
    QPointer<ChatMenuPanel> self(this);
    addChatConfigureCta(lay, accent, [self](QPushButton* cta) {
      if (self && self->onSettings)
        self->onSettings(QRect(cta->mapToGlobal(QPoint(0, 0)), cta->size()));
    });
  }
}  // namespace stencil::gui

