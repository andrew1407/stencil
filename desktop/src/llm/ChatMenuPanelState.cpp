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
#include <QAction>
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

  void ChatMenuPanel::setBusy(bool on) {
    busy_ = on;
    updateSendEnabled();
    send_->setIcon(themedIcon(on ? "stop" : "send", paletteCache_.onAccent, MENU_CHAT_ICON));
    send_->setToolTip(on ? QStringLiteral("Stop the response")
                         : QString());
    if (moreRows_.attach) moreRows_.attach->setVisible(!on);  // hidden mid-turn, browser parity
  }

  // Provider reachability badge + rich tooltip (the dock's shared gear
  // treatment, driven by the same MainWindow::refreshLlmStatus probe).
  void ChatMenuPanel::setProviderStatus(const QString& richTooltip,
                                        ChatDock::ProviderStatus status) {
    styleProviderStatusDot(statusDot_, more_, richTooltip, status, palette());
  }

  // Menu chrome tones: the transcript rows and the composer track the live
  // theme palette (the menu itself is styled app-wide).
  void ChatMenuPanel::restyle(const Palette& pal) {
    paletteCache_ = pal;   // so a later swap toggle can re-issue this stylesheet
    danger_ = pal.danger;
    text_ = pal.textMain;
    chip_ = pal.bgContainer;
    border_ = pal.borderMain;
    accent_ = pal.accent;
    muted_ = pal.textMuted;
    setStyleSheet(
        QStringLiteral(
            "#chatMenuPanel QScrollArea{background:transparent;border:none;}"
            "#chatMenuPanel QScrollArea > QWidget > QWidget{background:transparent;}")
        // …plus the DOCK's bubble sheet, so a mirrored message wears the same
        // colours and hairlines as the one in the dock.
        + chatCardStyleSheet(pal, chatSwapSides_));
    // The accent's own line-art ink on the accent fill — identical to the dock's.
    const QColor onAccent = pal.onAccent;
    send_->setIcon(themedIcon(busy_ ? "stop" : "send", onAccent, MENU_CHAT_ICON));
    more_->setIcon(themedIcon("dots", onAccent, MENU_CHAT_ICON));
    restyleChatMoreMenu(moreRows_, pal.textMain);   // the dock's own glyphs on the same rows
    splitter_->setPillColors(pal.borderMain, pal.accent);
    styleSuggestionChips(suggest_, pal);
  }

  void ChatMenuPanel::setChatSwapSides(bool on) {
    if (chatSwapSides_ == on) return;
    chatSwapSides_ = on;
    // The flattened tail corner rides the SHARED stylesheet (chatCardStyleSheet), keyed off
    // chatSwapSides_ - re-issue it so every card's corner flips too, not just its alignment and tail.
    restyle(paletteCache_);
    applyChatSwapToCards(body_, rows_, chatSwapSides_, accent_, chip_, border_, danger_, chip_);
    applyChatBubbleWidths(body_, scroll_);
  }

  void ChatMenuPanel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    applyChatBubbleWidths(body_, scroll_);  // re-wrap to the new width
  }

  void ChatMenuPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // The panel is built LAZILY and lives hidden inside a QWidgetAction, so rows mirrored before its
    // first appearance were measured against the default 100px viewport. Re-measure on the way in.
    QTimer::singleShot(0, this, [this] { applyChatBubbleWidths(body_, scroll_); });
  }

  bool ChatMenuPanel::eventFilter(QObject* obj, QEvent* event) {
    // Same composer convention as the dock: Enter sends, Shift+Enter is a newline. Enter must never
    // fall through to the menu (which would activate the highlighted action and close it).
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

