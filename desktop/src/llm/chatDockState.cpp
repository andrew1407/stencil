// Dock state: busy, composer enablement, provider status, icon restyle, scrolling.
// Split out of chatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "chatDock.hpp"
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
  // Show a … item only while it can actually act (user decision; the browser hides
  // its .chat-more-item the same way): attach until the §7 cap with no video queued,
  // clear only over a non-empty transcript, neither while a turn is in flight.
  void ChatDock::syncMoreMenuItems() {
    if (actAttach_)
      actAttach_->setVisible(!busyFlag_ && images_.size() < kMaxAttachments
                             && videoPath_.isEmpty());
    if (actClear_) actClear_->setVisible(!busyFlag_ && transcriptHasCards());
  }

  void ChatDock::setBusy(bool on) {
    // Tracked as state, NOT as the progress bar's visibility: a turn can be
    // driven from the context-menu chat with this dock closed, and a hidden
    // widget is never isVisible().
    busyFlag_ = on;
    busy_->setVisible(on);
    // Attaching is frozen while a request is in flight; typing stays open.
    attach_->setEnabled(!on);
    // …and so is clearing: the transcript can't be wiped out from under an
    // answer that is still landing in it.
    if (clearBtn_) clearBtn_->setEnabled(!on);
    // The … menu mirrors those two (its items are the visible affordance now):
    // mid-turn they leave the menu entirely instead of greying out.
    syncMoreMenuItems();
    // While in flight, the send button IS the stop button (the on-accent glyph on the
    // accent fill, like the rest of the action group).
    send_->setIcon(themedIcon(on ? "stop" : "send", paletteCache_.onAccent, kAccentIcon));
    send_->setToolTip(on ? QStringLiteral("Stop the response")
                         : QString());
    updateSendEnabled();
  }

  bool ChatDock::isBusy() const { return busyFlag_; }

  QSize ChatDock::floatingDefaultSize() const { return kFloatingSize.expandedTo(minimumSize()); }

  void ChatDock::focusInput() { input_->setFocus(); }

  bool ChatDock::hasComposerText() const { return !input_->toPlainText().trimmed().isEmpty(); }

  void ChatDock::updateSendEnabled() {
    // Busy = STOP mode (always clickable); idle = gated on non-empty input.
    send_->setEnabled(isBusy() || !input_->toPlainText().trimmed().isEmpty());
  }

  void ChatDock::setProviderStatus(const QString& richTooltip, ProviderStatus status) {
    // The rich provider tooltip belongs on the … TRIGGER: the gear now lives
    // inside the menu (hidden), so hanging it there would never be seen. The
    // gear keeps it too, for when the menu is open.
    styleProviderStatusDot(statusDot_, more_, richTooltip, status, palette());
    if (gear_ && !richTooltip.isEmpty()) gear_->setToolTip(richTooltip);
  }

  void ChatDock::restyleIcons(const Palette& pal) {
    // Browser .chat-panel parity: one card on the controls-panel tone with a
    // themed hairline border; transcript + input are recessed rounded surfaces
    // (page tone, 8px radius, accent focus ring).
    paletteCache_ = pal;   // so a later swap toggle can re-issue this stylesheet
    accentCache_ = pal.accent;
    chipCache_ = pal.bgContainer;
    borderCache_ = pal.borderMain;
    textCache_ = pal.textMain;
    dangerCache_ = pal.danger;
    mutedCache_ = pal.textMuted;
    // Values ported 1:1 from browser/css/components.css: header on --bg-info
    // with only a bottom divider, a BORDERLESS transcript on the panel tone,
    // and the composer on --input-bg (8px radius, accent focus ring).
    setStyleSheet(
        QStringLiteral(
            "#chatTitleBar{background:%1;border:1px solid %2;border-bottom:1px solid %2;}"
            // The app-wide QToolButton rule pads 5x7 and reserves a border; inside a fixed
            // 23px header chip that leaves ~7px for the glyph, i.e. half the browser's mark.
            // .chat-hbtn has neither (padding: 0, border: none), so the 13px glyph fills it.
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
            // The in-bubble executor note line: --text-muted through the
            // STYLESHEET — under QSS a palette colour loses.
            "QLabel#chatNoteLabel{color:%7;background:transparent;}"
            // Attachment chips (browser .chat-attach-chip): quiet pill on the
            // input tone with the themed hairline; the × turns accent on hover.
            "#chatAttachChip{background:%3;border:1px solid %2;border-radius:6px;}"
            "#chatAttachChip QLabel{color:%4;font-size:11px;background:transparent;}"
            "#chatAttachRemove{border:none;background:transparent;color:%4;"
            "font-size:13px;padding:0 2px;}"
            "#chatAttachRemove:hover{color:%5;}")
            .arg(pal.bgControls.name(), pal.borderMain.name(), pal.inputBg.name(),
                 pal.inputText.name(), pal.accent.name(), pal.bgContainer.name(),
                 // %7 — muted text (--text-muted), alpha-preserving.
                 QStringLiteral("rgba(%1,%2,%3,%4)")
                     .arg(pal.textMuted.red())
                     .arg(pal.textMuted.green())
                     .arg(pal.textMuted.blue())
                     .arg(pal.textMuted.alphaF()))
        // The transcript bubbles themselves come from the SHARED sheet the
        // context menu's panel applies too, so one message looks the same
        // wherever it is rendered.
        + chatCardStyleSheet(pal, chatSwapSides_));
    // The accent-filled composer buttons carry the accent's own ink (like checked
    // toolbar toggles); the title-bar float/close ghosts use the theme text.
    const QColor onAccent = pal.onAccent;
    send_->setIcon(themedIcon(isBusy() ? "stop" : "send", onAccent, kAccentIcon));
    attach_->setIcon(themedIcon("image", onAccent, kAccentIcon));
    gear_->setIcon(themedIcon("gear", onAccent, kAccentIcon));
    clearBtn_->setIcon(themedIcon("trash", onAccent, kAccentIcon));
    if (more_) more_->setIcon(themedIcon("dots", onAccent, kAccentIcon));
    if (actAttach_) actAttach_->setIcon(themedIcon("image", pal.textMain, 14));
    if (actClear_) actClear_->setIcon(themedIcon("trash", pal.textMain, 14));
    if (actSwapSides_) actSwapSides_->setIcon(themedIcon("swap", pal.textMain, 14));
    if (actSettings_) actSettings_->setIcon(themedIcon("gear", pal.textMain, 14));
    closeBtn_->setIcon(themedIcon("x", pal.textMain, 14));
    updatePlacementState();
    // Theme-tracking chrome: accent header sparkle/title, and the suggestion
    // pills as quiet solid chips — normal border/card background/text, with an
    // accent border + faint accent fill on hover.
    headerIcon_->setPixmap(themedIcon("sparkle", pal.textMain, 16).pixmap(16, 16));
    // The composer's drop cue: dashed accent border over a mostly-opaque accent tint
    // mixed into the card colour, so it stays legible in both themes (browser
    // .chat-drop-cue). The glyph is the same "image" mark the attach button uses.
    if (splitter_) splitter_->setPillColors(pal.borderMain, pal.accent);
    if (dropCue_) {
      // SOLID, blended — the browser's color-mix(accent 16%, card) is opaque, and a
      // translucent slab here let the placeholder text show straight through the label.
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
    // Jump pills: circle ghosts over the transcript (browser .chat-jump-btn). The
    // browser's hover comes from TWO rules that compose: its own (border → --accent,
    // glyph → --text-main, done below via the eventFilter) plus the app-wide generic
    // `button:hover { background: var(--accent-2) }`, which .chat-jump-btn:hover never
    // overrides — so the pill fills solid on hover there. QSS has no such generic rule
    // to fall back on, so it has to be stated here explicitly, or only the border
    // recolours and the pill stays unfilled.
    // pal.textKey doubles as --accent-2 (see theme.cpp themePalette).
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
    stickToBottom_ = true;   // an explicit jump to the end re-arms the follow pin
    // Defer until the layout has run so the new card's height is included; the
    // rangeChanged pin then keeps following any later growth.
    QTimer::singleShot(0, scroll_, [this] {
      scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->maximum());
    });
  }
}  // namespace stencil::gui
