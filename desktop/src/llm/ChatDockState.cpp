// Dock state: busy, composer enablement, provider status, icon restyle, scrolling.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "iconSet.hpp"
#include "theme.hpp"
#include "chatWidgets.hpp"

#include <QPlainTextEdit>
#include <QProgressBar>
#include <QLabel>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>

namespace stencil::gui {

  using namespace chatdock;
  // An item shows only while it can act (browser hides .chat-more-item the same way).
  void ChatDock::syncMoreMenuItems() {
    if (moreRows_.attach)
      moreRows_.attach->setVisible(!busyFlag_ && images_.size() < MAX_ATTACHMENTS
                                   && videoPath_.isEmpty());
    if (moreRows_.clear) moreRows_.clear->setVisible(!busyFlag_ && transcriptHasCards());
  }

  void ChatDock::setBusy(bool on) {
    // State, NOT the progress bar's visibility: a hidden widget is never isVisible().
    busyFlag_ = on;
    busy_->setVisible(on);
    attach_->setEnabled(!on);
    if (clearBtn_) clearBtn_->setEnabled(!on);
    syncMoreMenuItems();
    // While in flight, the send button IS the stop button.
    send_->setIcon(themedIcon(on ? "stop" : "send", paletteCache_.onAccent, ACCENT_ICON));
    send_->setToolTip(on ? QStringLiteral("Stop the response")
                         : QString());
    updateSendEnabled();
  }

  bool ChatDock::isBusy() const { return busyFlag_; }

  QSize ChatDock::floatingDefaultSize() const { return FLOATING_SIZE.expandedTo(minimumSize()); }

  void ChatDock::focusInput() { input_->setFocus(); }

  bool ChatDock::hasComposerText() const { return !input_->toPlainText().trimmed().isEmpty(); }

  void ChatDock::updateSendEnabled() {
    send_->setEnabled(isBusy() || !input_->toPlainText().trimmed().isEmpty());
  }

  void ChatDock::setProviderStatus(const QString& richTooltip, ProviderStatus status) {
    // The tooltip belongs on the … TRIGGER: the gear lives hidden inside the menu.
    styleProviderStatusDot(statusDot_, more_, richTooltip, status, palette());
    if (gear_ && !richTooltip.isEmpty()) gear_->setToolTip(richTooltip);
  }

  void ChatDock::restyleIcons(const Palette& pal) {
    // Browser .chat-panel parity; values ported from browser/css/components.css.
    paletteCache_ = pal;
    accentCache_ = pal.accent;
    chipCache_ = pal.bgContainer;
    borderCache_ = pal.borderMain;
    textCache_ = pal.textMain;
    dangerCache_ = pal.danger;
    mutedCache_ = pal.textMuted;
    setStyleSheet(
        QStringLiteral(
            "#chatTitleBar{background:%1;border:1px solid %2;border-bottom:1px solid %2;}"
            // .chat-hbtn has no padding/border, so the 13 px glyph fills the 23 px chip.
            "#chatTitleBar QToolButton{padding:0;border:none;background:transparent;"
            "border-radius:5px;}"
            "#chatTitleBar QToolButton:hover{background:%6;}"
            "#chatBody{background:%1;border:1px solid %2;border-top:none;}"
            "#chatBody QScrollArea{background:%1;border:none;}"
            "#chatInputArea{background:%1;border-top:1px solid %2;}"
            "#chatBody QScrollArea > QWidget > QWidget{background:transparent;}"
            "#chatInput{background:%3;color:%4;border:1px solid %2;border-radius:8px;"
            "padding:6px 8px;font-size:14px;}"
            "#chatInput:focus{border:1px solid %5;}"
            // --text-muted through the STYLESHEET — under QSS a palette colour loses.
            "QLabel#chatNoteLabel{color:%7;background:transparent;}"
            // browser .chat-attach-chip
            "#chatAttachChip{background:%3;border:1px solid %2;border-radius:6px;}"
            "#chatAttachChip QLabel{color:%4;font-size:11px;background:transparent;}"
            "#chatAttachRemove{border:none;background:transparent;color:%4;"
            "font-size:13px;padding:0 2px;}"
            "#chatAttachRemove:hover{color:%5;}")
            .arg(pal.bgControls.name(), pal.borderMain.name(), pal.inputBg.name(),
                 pal.inputText.name(), pal.accent.name(), pal.bgContainer.name(),
                 QStringLiteral("rgba(%1,%2,%3,%4)")
                     .arg(pal.textMuted.red())
                     .arg(pal.textMuted.green())
                     .arg(pal.textMuted.blue())
                     .arg(pal.textMuted.alphaF()))
        // The bubbles come from the SHARED sheet the context menu's panel applies too.
        + chatCardStyleSheet(pal, chatSwapSides_));
    const QColor onAccent = pal.onAccent;
    send_->setIcon(themedIcon(isBusy() ? "stop" : "send", onAccent, ACCENT_ICON));
    attach_->setIcon(themedIcon("image", onAccent, ACCENT_ICON));
    gear_->setIcon(themedIcon("gear", onAccent, ACCENT_ICON));
    clearBtn_->setIcon(themedIcon("trash", onAccent, ACCENT_ICON));
    if (more_) more_->setIcon(themedIcon("dots", onAccent, ACCENT_ICON));
    restyleChatMoreMenu(moreRows_, pal.textMain);
    closeBtn_->setIcon(themedIcon("x", pal.textMain, 14));
    updatePlacementState();
    headerIcon_->setPixmap(themedIcon("sparkle", pal.textMain, 16).pixmap(16, 16));
    // browser .chat-drop-cue
    if (splitter_) splitter_->setPillColors(pal.borderMain, pal.accent);
    if (dropCue_) {
      // SOLID, blended: a translucent slab let the placeholder text show through.
      dropCue_->setStyleSheet(
          QStringLiteral("#chatDropCue{border:2px dashed %1;border-radius:10px;background:%2;}")
              .arg(pal.accent.name(), blendColors(pal.accent, pal.inputBg, 0.16).name()));
      if (dropCueIcon_) dropCueIcon_->setPixmap(themedIcon("image", pal.accent, 16).pixmap(16, 16));
      if (dropCueText_)
        dropCueText_->setStyleSheet(
            QStringLiteral("color:%1;background:transparent;font-weight:600;").arg(pal.accent.name()));
    }
    headerTitle_->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;").arg(pal.textMain.name()));
    // browser .chat-jump-btn: its hover fill comes from the app-wide generic `button:hover`
    // rule, which QSS has no equivalent of, so it is stated here. pal.textKey doubles as --accent-2.
    if (jumpTop_ && jumpBottom_) {
      const QString jumpQss =
          QStringLiteral(
              "QToolButton{border:1px solid %1;border-radius:14px;background:%2;}"
              "QToolButton:hover{border-color:%3;background:%4;}")
              .arg(pal.borderMain.name(), pal.bgControls.name(), pal.accent.name(),
                   pal.textKey.name());
      jumpTop_->setIcon(themedIcon("chevron-up", pal.textMuted, 14));
      jumpBottom_->setIcon(themedIcon("chevron-down", pal.textMuted, 14));
      jumpTop_->setStyleSheet(jumpQss);
      jumpBottom_->setStyleSheet(jumpQss);
    }
    styleSuggestionChips(suggest_, pal);
  }

  void ChatDock::scrollToBottom() {
    stickToBottom_ = true;
    // Deferred until the layout has run so the new card's height is included.
    QTimer::singleShot(0, scroll_, [this] {
      scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->maximum());
    });
  }
}  // namespace stencil::gui
