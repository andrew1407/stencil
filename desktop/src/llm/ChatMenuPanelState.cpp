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
    busy = on;
    updateSendEnabled();
    send->setIcon(themedIcon(on ? "stop" : "send", paletteCache.onAccent, MENU_CHAT_ICON));
    send->setToolTip(on ? QStringLiteral("Stop the response")
                         : QString());
    if (moreRows.attach) moreRows.attach->setVisible(!on);  // hidden mid-turn, browser parity
  }

  // Provider reachability badge + rich tooltip (the dock's shared gear
  // treatment, driven by the same MainWindow::refreshLlmStatus probe).
  void ChatMenuPanel::setProviderStatus(const QString& richTooltip,
                                        ChatDock::ProviderStatus status) {
    styleProviderStatusDot(statusDot, more, richTooltip, status, palette());
  }

  // Menu chrome tones: the transcript rows and the composer track the live
  // theme palette (the menu itself is styled app-wide).
  void ChatMenuPanel::restyle(const Palette& pal) {
    paletteCache = pal;   // so a later swap toggle can re-issue this stylesheet
    danger = pal.danger;
    text = pal.textMain;
    chip = pal.bgContainer;
    border = pal.borderMain;
    accent = pal.accent;
    muted = pal.textMuted;
    setStyleSheet(
        QStringLiteral(
            "#chatMenuPanel QScrollArea{background:transparent;border:none;}"
            "#chatMenuPanel QScrollArea > QWidget > QWidget{background:transparent;}")
        // …plus the DOCK's bubble sheet, so a mirrored message wears the same
        // colours and hairlines as the one in the dock.
        + chatCardStyleSheet(pal, chatSwapSides));
    // The accent's own line-art ink on the accent fill — identical to the dock's.
    const QColor onAccent = pal.onAccent;
    send->setIcon(themedIcon(busy ? "stop" : "send", onAccent, MENU_CHAT_ICON));
    more->setIcon(themedIcon("dots", onAccent, MENU_CHAT_ICON));
    restyleChatMoreMenu(moreRows, pal.textMain);   // the dock's own glyphs on the same rows
    splitter->setPillColors(pal.borderMain, pal.accent);
    styleSuggestionChips(suggest, pal);
  }

  void ChatMenuPanel::setChatSwapSides(bool on) {
    if (chatSwapSides == on) return;
    chatSwapSides = on;
    // The flattened tail corner rides the SHARED stylesheet (chatCardStyleSheet), keyed off
    // chatSwapSides - re-issue it so every card's corner flips too, not just its alignment and tail.
    restyle(paletteCache);
    applyChatSwapToCards(body, rows, chatSwapSides, accent, chip, border, danger, chip);
    applyChatBubbleWidths(body, scroll);
  }

  void ChatMenuPanel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    applyChatBubbleWidths(body, scroll);  // re-wrap to the new width
  }

  void ChatMenuPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // The panel is built LAZILY and lives hidden inside a QWidgetAction, so rows mirrored before its
    // first appearance were measured against the default 100px viewport. Re-measure on the way in.
    QTimer::singleShot(0, this, [this] { applyChatBubbleWidths(body, scroll); });
  }

  bool ChatMenuPanel::eventFilter(QObject* obj, QEvent* event) {
    // Same composer convention as the dock: Enter sends, Shift+Enter is a newline. Enter must never
    // fall through to the menu (which would activate the highlighted action and close it).
    if (obj == input && event->type() == QEvent::KeyPress) {
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
    send->setEnabled(busy || !input->toPlainText().trimmed().isEmpty());
  }

  void ChatMenuPanel::submit() {
    if (busy) return;  // single turn at a time, exactly like the dock
    const QString text = input->toPlainText().trimmed();
    if (text.isEmpty()) return;
    input->clear();
    if (onSend) onSend(text);
  }

  void ChatMenuPanel::scrollToBottom() {
    QTimer::singleShot(0, scroll, [this] {
      scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    });
  }
}  // namespace stencil::gui

