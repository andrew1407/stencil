#include "chatMenuPanel.hpp"

#include "../app/pillSplitter.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/iconSet.hpp"
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

  namespace {
    // Context-menu chat metrics (browser .ctx-assist parity): a flyout that is a
    // real chat WINDOW — height min(72vh, 600px), transcript 300..min(60vh, 520px).
    // 325, not 320: 5px of room for the per-row "…" that hangs outside the bubble.
    constexpr int kMenuChatWidth = 325;
    constexpr int kMenuChatTotalMax = 600;
    constexpr int kMenuChatTranscriptMin = 300;
    constexpr int kMenuChatTranscriptMax = 520;
    constexpr int kMenuChatComposerHeight = 96;  // initial composer slot
    constexpr int kMenuChatComposerMin = 62;     // splitter floor for it
    // A mirrored row fades while its dust flies, like the dock's cards
    // (chatDock.cpp kChatLeaveMs / browser motion.js CHAT_LEAVE_MS).
    constexpr int kChatRowLeaveMs = 260;
    constexpr int kMenuChatButtonEdge = 30;      // the dock's action-button box
    constexpr int kMenuChatIcon = 20;

    // The transcript takes what the composer leaves inside that window, floored
    // and capped as above — always bounded by the screen so the flyout can never
    // overflow a short display.
    int menuChatTranscriptHeight() {
      const QScreen* screen = QGuiApplication::primaryScreen();
      const int avail = screen ? screen->availableGeometry().height() : 0;
      const int total = avail > 0 ? qMin(int(avail * 0.72), kMenuChatTotalMax)
                                  : kMenuChatTotalMax;
      const int cap = avail > 0 ? qMin(int(avail * 0.60), kMenuChatTranscriptMax)
                                : kMenuChatTranscriptMax;
      const int floor = qMin(kMenuChatTranscriptMin, cap);
      return qMax(140, qBound(floor, total - kMenuChatComposerHeight, cap));
    }
  }  // namespace

  ChatMenuPanel::ChatMenuPanel(QWidget* parent, std::function<void(QString)> onSend,
                               std::function<void()> onStop, std::function<void()> onAttach,
                               std::function<void()> onSettings,
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
    composer->setMinimumHeight(kMenuChatComposerMin);
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
    send_ = mkBtn("chatMenuSend", QStringLiteral("Send (Enter)"));
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
      if (onSettings_) onSettings_();
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
    statusDot_->move(kMenuChatButtonEdge - statusDot_->width() - 1, 1);
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
    splitter_->setFixedHeight(transcript + kMenuChatComposerHeight);
    splitter_->setSizes({transcript, kMenuChatComposerHeight});
    col->addWidget(splitter_);

    setMinimumWidth(kMenuChatWidth);
    setMaximumWidth(kMenuChatWidth + 120);
  }

  QWidget* ChatMenuPanel::input() const { return input_; }

  // One card per message, built by the DOCK's fillChatCard — same frame, role
  // colour and side, and the same row menu / Resend affordances.
  void ChatMenuPanel::appendRow(const QString& role, const QString& text, bool muted,
                                const QString& retryText, bool pending,
                                const QStringList& notes) {
    suggest_->hide();  // empty-state affordance only
    auto* card = new QFrame(body_);
    card->setFrameShape(QFrame::StyledPanel);
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(2);
    const ChatCardKind kind = role == QLatin1String("Error") ? ChatCardKind::Error
                              : muted                       ? ChatCardKind::Muted
                                                            : ChatCardKind::Bubble;
    fillChatCard(card, lay, role, text, kind, danger_);
    // Warnings / executor notes ride INSIDE the bubble, exactly as the dock
    // renders them — one card per turn, never extra rows.
    for (const QString& n : notes) addChatCardNote(lay, n);
    if (!retryText.isEmpty()) addRetry(card, retryText);
    rows_->insertWidget(rows_->count() - 1, card);
    rows_->setAlignment(card, role == QLatin1String("You") ? Qt::AlignRight
                                                           : Qt::AlignLeft);
    // Every SETTLED row carries the menu (browser chatRowMenuItems excludes
    // only the pending one).
    if (!pending) installChatCardMenu(card, menuHooks());
    rowsAdded_.append(card);
    // Bounded like chatHistory_: past the cap the oldest mirrored row goes —
    // through the same scatter as any other row leaving, not a bare delete.
    while (rowsAdded_.size() > kChatHistoryBound) dissolveRow(rowsAdded_.takeFirst());
    applyChatBubbleWidths(body_, scroll_);   // the dock's wrap/measure pass
    scrollToBottom();
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

  // This surface's hooks for the shared row menu: its own composer, its own
  // send path, its own transcript viewport.
  ChatCardMenuHooks ChatMenuPanel::menuHooks() {
    ChatCardMenuHooks h;
    h.owner = this;
    h.scroll = scroll_;
    h.busy = [this] { return busy_; };
    h.insertIntoPrompt = [this](const QString& t) {
      const QString existing = input_->toPlainText();
      input_->setPlainText(existing.isEmpty() ? t
                                              : existing + QLatin1Char('\n') + t);
      input_->moveCursor(QTextCursor::End);
      input_->setFocus();
      updateSendEnabled();
    };
    h.resend = [this](QFrame*, const QString& t) { if (onSend_) onSend_(t); };
    h.text = text_;
    h.chip = chip_;
    h.border = border_;
    h.accent = accent_;
    h.muted = muted_;
    return h;
  }

  void ChatMenuPanel::showPending() {
    clearPending();
    appendRow(QStringLiteral("Assistant"), QStringLiteral("…"), true, QString(),
              /*pending=*/true);
    pending_ = rowsAdded_.isEmpty() ? nullptr : rowsAdded_.last();
    // The DOCK's bouncing dots, not a static "…": an in-flight turn has to
    // look in-flight on this surface too.
    if (!pending_) return;
    if (QLabel* body = bodyOf(pending_)) body->hide();
    if (auto* lay = qobject_cast<QVBoxLayout*>(pending_->layout()))
      lay->insertWidget(0, makeChatTypingDots(pending_));
  }

  void ChatMenuPanel::clearPending() {
    if (!pending_) return;
    rowsAdded_.removeAll(pending_);
    delete pending_;
    pending_ = nullptr;
  }

  // The dock's semantics: the in-flight row becomes a muted "Stopped." in
  // place — no error row, no history push.
  void ChatMenuPanel::markStopped(const QString& retryText) {
    if (!pending_) return;
    // The dots stop with the turn; the text takes their place.
    if (QWidget* dots = pending_->findChild<QWidget*>(QStringLiteral("chatTypingDots")))
      delete dots;
    if (QLabel* body = bodyOf(pending_)) {
      body->show();
      body->setText(QStringLiteral("Stopped."));
      body->setProperty("chatBody", QStringLiteral("Stopped."));
    }
    // A settled row: the dock's error tone, its Resend, and the row menu the
    // pending card deliberately went without.
    pending_->setObjectName(QStringLiteral("chatCardError"));
    pending_->style()->unpolish(pending_);
    pending_->style()->polish(pending_);
    if (!retryText.isEmpty()) addRetry(pending_, retryText);
    installChatCardMenu(pending_, menuHooks());
    applyChatBubbleWidths(body_, scroll_);
    pending_ = nullptr;
  }

  // A mirrored row leaves the way a dock card does: it scatters, then goes.
  // Returns whether anything is actually playing — over() declines when the
  // panel is off screen, and then there is nothing for the empty state to wait for.
  bool ChatMenuPanel::dissolveRow(QFrame* l) {
    if (!l) return false;
    // The same finer grid the dock's cards use (chatDock.cpp kChatScatter*).
    const bool playing =
        DisintegrateOverlay::over(l, window(), DisintegrateOverlay::Sweep::Fall, 32, 16)
        != nullptr;
    rows_->removeWidget(l);
    // Out of the layout, but painted while it fades under its own dust. Reuse
    // any effect already on the row and stop its animations first —
    // setGraphicsEffect() deletes the old effect, and anything still driving it
    // would be left dangling (chatDock.cpp fadeOutAndDelete).
    for (QVariantAnimation* a : l->findChildren<QVariantAnimation*>()) a->stop();
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(l->graphicsEffect());
    if (!fx) {
      fx = new QGraphicsOpacityEffect(l);
      l->setGraphicsEffect(fx);
    }
    auto* anim = new QVariantAnimation(l);
    anim->setDuration(kChatRowLeaveMs);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    // QPointer: the effect can be replaced out from under a running animation
    // (chatDock.cpp fadeOutAndDelete has the same guard, and the crash it fixes).
    QPointer<QGraphicsOpacityEffect> fxp(fx);
    QObject::connect(anim, &QVariantAnimation::valueChanged, l,
                     [fxp](const QVariant& v) { if (fxp) fxp->setOpacity(v.toDouble()); });
    QObject::connect(anim, &QVariantAnimation::finished, l, [l] {
      l->hide();
      l->deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
    return playing;
  }

  void ChatMenuPanel::clearRows() {
    clearPending();
    bool wiped = false;
    for (QFrame* l : rowsAdded_) wiped = dissolveRow(l) || wiped;
    rowsAdded_.clear();
    // The dock's sequencing (chatDock.cpp clearConversation): the rows leave
    // FIRST and the empty state returns only once the particles have landed.
    if (!wiped) { suggest_->show(); return; }
    QTimer::singleShot(DisintegrateOverlay::kMs, this, [this] {
      // A turn may have started while the wipe played — then the chips are wrong.
      if (!rowsAdded_.isEmpty() || pending_) return;
      suggest_->show();
    });
  }

  void ChatMenuPanel::setBusy(bool on) {
    busy_ = on;
    updateSendEnabled();
    send_->setIcon(themedIcon(on ? "stop" : "send", QColor(Qt::white), kMenuChatIcon));
    send_->setToolTip(on ? QStringLiteral("Stop the response")
                         : QStringLiteral("Send (Enter)"));
    attach_->setEnabled(!on);  // frozen mid-turn, exactly like the dock's
  }

  // Provider reachability badge + rich tooltip (the dock's shared gear
  // treatment, driven by the same MainWindow::refreshLlmStatus probe).
  void ChatMenuPanel::setProviderStatus(const QString& richTooltip,
                                        ChatDock::ProviderStatus status) {
    styleProviderStatusDot(statusDot_, gear_, richTooltip, status, palette());
  }

  // Menu chrome tones: the transcript rows and the composer track the live
  // theme palette (the menu itself is styled app-wide).
  void ChatMenuPanel::restyle(const Palette& pal) {
    danger_ = pal.danger;
    text_ = pal.textMain;
    chip_ = pal.bgContainer;
    border_ = pal.borderMain;
    accent_ = pal.accent;
    muted_ = pal.textMuted;
    setStyleSheet(
        QStringLiteral(
            "#chatMenuPanel QScrollArea{background:transparent;border:none;}"
            "#chatMenuPanel QScrollArea > QWidget > QWidget{background:transparent;}"
            "#chatMenuInput{background:%1;color:%2;border:1px solid %3;"
            "border-radius:6px;padding:3px 5px;}"
            "#chatMenuInput:focus{border:1px solid %4;}")
            .arg(pal.inputBg.name(), pal.inputText.name(), pal.borderMain.name(),
                 pal.accent.name())
        // …plus the DOCK's bubble sheet, so a mirrored message wears the same
        // colours and hairlines as the one in the dock.
        + chatCardStyleSheet(pal));
    // White line-art on the accent fill — identical to the dock's, including
    // the dark halo a LIGHT accent needs to keep the mark readable.
    const QColor onAccent = Qt::white;
    const bool halo = accentNeedsGlyphShadow(pal.accent);
    send_->setIcon(themedIcon(busy_ ? "stop" : "send", onAccent, kMenuChatIcon, halo));
    attach_->setIcon(themedIcon("image", onAccent, kMenuChatIcon, halo));
    gear_->setIcon(themedIcon("gear", onAccent, kMenuChatIcon, halo));
    splitter_->setPillColors(pal.borderMain, pal.accent);
    styleSuggestionChips(suggest_, pal);
  }

  void ChatMenuPanel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    applyChatBubbleWidths(body_, scroll_);  // re-wrap to the new width
  }

  void ChatMenuPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // The panel is built LAZILY and lives hidden inside a QWidgetAction, so
    // rows mirrored before its first appearance were measured against the
    // default 100px viewport. Re-measure on the way in, once the real width exists.
    QTimer::singleShot(0, this, [this] { applyChatBubbleWidths(body_, scroll_); });
  }

  bool ChatMenuPanel::eventFilter(QObject* obj, QEvent* event) {
    // Same composer convention as the dock: Enter sends, Shift+Enter is a
    // newline. Enter must never fall through to the menu (which would activate
    // the highlighted action and close it).
    if (obj == input_ && event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
          !(ke->modifiers() & Qt::ShiftModifier)) {
        submit();
        return true;
      }
    }
    return QWidget::eventFilter(obj, event);
  }

  // Busy = STOP mode (always clickable); idle = gated on non-empty input.
  // Exactly the dock's rule, so the trio reads the same in both composers.
  void ChatMenuPanel::updateSendEnabled() {
    send_->setEnabled(busy_ || !input_->toPlainText().trimmed().isEmpty());
  }

  void ChatMenuPanel::submit() {
    if (busy_) return;  // single turn at a time, exactly like the dock
    const QString text = input_->toPlainText().trimmed();
    if (text.isEmpty()) return;
    input_->clear();
    if (onSend_) onSend_(text);
  }

  void ChatMenuPanel::scrollToBottom() {
    QTimer::singleShot(0, scroll_, [this] {
      scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->maximum());
    });
  }

}  // namespace stencil::gui
