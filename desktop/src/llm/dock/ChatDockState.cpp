// Dock state: busy, composer enablement, provider status, icon restyle, scrolling.
#include "../../support/skinPrefs.hpp"
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "iconSet.hpp"
#include "theme.hpp"
#include "../../support/sheets/chatSheets.hpp"

#include <QPlainTextEdit>
#include <QProgressBar>
#include <QLabel>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QLayout>
#include <QScrollBar>
#include <QTextCursor>

namespace stencil::gui {

  using namespace chatdock;
  // An item shows only while it can act (browser hides .chat-more-item the same way).
  void ChatDock::syncMoreMenuItems() {
    if (moreRows.attach)
      moreRows.attach->setVisible(!cmp.busyFlag && cmp.images.size() < MAX_ATTACHMENTS
                                   && cmp.videoPath.isEmpty());
    if (moreRows.clear) moreRows.clear->setVisible(!cmp.busyFlag && transcriptHasCards());
  }

  void ChatDock::setBusy(bool on) {
    // State, NOT the progress bar's visibility: a hidden widget is never isVisible().
    cmp.busyFlag = on;
    cmp.busy->setVisible(on);
    cmp.attach->setEnabled(!on);
    if (chrome.clearBtn) chrome.clearBtn->setEnabled(!on);
    syncMoreMenuItems();
    // While in flight, the send button IS the stop button.
    cmp.send->setIcon(themedIcon(on ? "stop" : "send", paletteCache.onAccent, ACCENT_ICON));
    cmp.send->setToolTip(on ? QStringLiteral("Stop the response")
                         : QString());
    updateSendEnabled();
  }

  bool ChatDock::isBusy() const { return cmp.busyFlag; }

  QSize ChatDock::floatingDefaultSize() const { return FLOATING_SIZE.expandedTo(minimumSize()); }
  QSize ChatDock::compactDefaultSize() const { return COMPACT_SIZE.expandedTo(minimumSize()); }

  void ChatDock::focusInput() { input->setFocus(); }

  bool ChatDock::hasComposerText() const { return !input->toPlainText().trimmed().isEmpty(); }

  void ChatDock::setComposerText(const QString& text) {
    input->setPlainText(text);
    input->moveCursor(QTextCursor::End);
  }

  void ChatDock::updateSendEnabled() {
    cmp.send->setEnabled(isBusy() || !input->toPlainText().trimmed().isEmpty());
  }

  void ChatDock::setProviderStatus(const QString& richTooltip, ProviderStatus status) {
    // The tooltip belongs on the … TRIGGER: the gear lives hidden inside the menu.
    styleProviderStatusDot(cmp.statusDot, cmp.more, richTooltip, status, palette());
    if (cmp.gear && !richTooltip.isEmpty()) cmp.gear->setToolTip(richTooltip);
  }

  void ChatDock::restyleIcons(const Palette& pal) {
    // Browser .chat-panel parity; values ported from browser/css/components/chat/panel.css.
    paletteCache = pal;
    accentCache = pal.accent;
    chipCache = pal.bgContainer;
    borderCache = pal.borderMain;
    textCache = pal.textMain;
    dangerCache = pal.danger;
    mutedCache = pal.textMuted;
    setStyleSheet(
        (support::isWebcore() ? QString() : support::chatDockSheet(pal))
        // The bubbles come from the SHARED sheet the context menu's panel applies too.
        + support::chatCardStyleSheet(pal, chatSwapSides));
    const QColor onAccent = pal.onAccent;
    cmp.send->setIcon(themedIcon(isBusy() ? "stop" : "send", onAccent, ACCENT_ICON));
    cmp.attach->setIcon(themedIcon("image", onAccent, ACCENT_ICON));
    cmp.gear->setIcon(themedIcon("gear", onAccent, ACCENT_ICON));
    chrome.clearBtn->setIcon(themedIcon("trash", onAccent, ACCENT_ICON));
    if (cmp.more) cmp.more->setIcon(themedIcon("dots", onAccent, ACCENT_ICON));
    restyleChatMoreMenu(moreRows, pal.textMain);
    chrome.closeBtn->setIcon(themedIcon("x", pal.textMain, 14));
    // A layout does not see a QSS border: qss/webcore/chat.qss's raised chatBody sides stay uncovered.
    if (QWidget* body = widget(); body && body->layout())
      body->layout()->setContentsMargins(support::isWebcore() ? QMargins(2, 0, 2, 2) : QMargins());
    updatePlacementState();
    chrome.headerIcon->setPixmap(themedIcon("sparkle", pal.textMain, 16).pixmap(16, 16));
    // browser .chat-drop-cue
    if (log.splitter) log.splitter->setPillColors(pal.borderMain, pal.accent);
    if (cmp.dropCue) {
      // SOLID, blended: a translucent slab let the placeholder text show through.
      cmp.dropCue->setStyleSheet(
          support::chatDropCueSheet(pal.accent, blendColors(pal.accent, pal.inputBg, 0.16)));
      if (cmp.dropCueIcon) cmp.dropCueIcon->setPixmap(themedIcon("image", pal.accent, 16).pixmap(16, 16));
      if (cmp.dropCueText)
        cmp.dropCueText->setStyleSheet(support::chatDropCueTextSheet(pal.accent));
    }
    chrome.headerTitle->setStyleSheet(support::chatHeaderTitleSheet(pal.textMain));
    if (log.jumpTop && log.jumpBottom) {
      const QString jumpQss = support::chatJumpButtonSheet(pal);
      log.jumpTop->setIcon(themedIcon("chevron-up", pal.textMuted, 14));
      log.jumpBottom->setIcon(themedIcon("chevron-down", pal.textMuted, 14));
      log.jumpTop->setStyleSheet(jumpQss);
      log.jumpBottom->setStyleSheet(jumpQss);
    }
    styleSuggestionChips(cmp.suggest, pal);
  }

  void ChatDock::scrollToBottom() {
    stickToBottom = true;
    // Deferred until the layout has run so the new card's height is included.
    QTimer::singleShot(0, scroll, [this] {
      scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    });
  }
}  // namespace stencil::gui
