#pragma once

// Constants and helpers shared by the ChatDock translation units — chatDock.cpp and the
// chatDock*.cpp partials split out of it. Declarations only: the bodies live in
// chatDockShared.cpp, so no includer compiles them. Nested in `chatdock` because names
// like repolish and kAppearMs also live in other files' anonymous namespaces.

#include <QSize>
#include <QString>

class QImage;
class QToolButton;
class QWidget;

namespace stencil::gui::chatdock {

  inline constexpr int kThumbEdge = 160;  // variant thumbnail long edge (px)
  inline constexpr int kButtonEdge = 23;  // compact ghost action buttons (browser .chat-hbtn: 23x23)
  inline constexpr int kHeaderIcon = 13;  // …with a 13px glyph, as in the browser header
  // The composer's action trio is a step larger than the title-bar ghosts —
  // it is the primary control cluster, and the context menu's assistant panel
  // mirrors these exact numbers so the two composers read identically.
  inline constexpr int kAccentEdge = 30;
  inline constexpr int kAccentIcon = 20;
  // Suggestion-chip corner: Qt silently draws a SQUARE box when border-radius exceeds
  // half the height; 14 is half the app-wide floor, so the chip stays a true pill.
  inline constexpr int kSuggestChipRadius = 14;
  // …and it FADES while the dust flies, instead of blinking out from under it
  // (browser css/animations.css chatCardLeave, motion.js CHAT_LEAVE_MS).
  inline constexpr int kChatLeaveMs = 260;
  // Widest a chip's filename may render — beyond it the name elides (tooltip has it).
  inline constexpr int kChipNameMaxPx = 150;
  // How long a removed chip HOLDS its slot before the neighbours slide over — the
  // scatter gets a beat to read before anything else moves (user-tuned).
  inline constexpr int kChatChipHoldMs = 140;
  // How many images ONE message may carry (browser/extension chatController.js
  // MAX_ATTACHMENTS). A turn's images are re-encoded, replayed and paid for per turn
  // (contract §7); past three the queue is refused rather than silently trimmed.
  inline constexpr int kMaxAttachments = 3;
  // Jump pills and the row "…" triggers rest translucent so a short bubble under
  // them stays readable; hover restores full opacity. One deliberately shared
  // figure (browser .chat-jump-btn / .chat-row-menu-btn on all three surfaces).
  inline constexpr double kGhostRestOpacity = 0.7;
  // How close to the bottom (px) still counts as "reading the end" — the
  // transcript follows new content only inside this band (chat stickiness).
  inline constexpr int kStickyBottomPx = 40;

  // Card appear motion (browser parity): subtle fade + short upward slide.
  inline constexpr int kAppearMs = 140;
  inline constexpr int kAppearSlidePx = 6;
  // Compact default when torn off (browser floating-panel parity). Without it
  // the floating dock inherits its docked span and stretches across the whole
  // main window, scattering the hint / input / buttons apart.
  // 385, not 380: the extra 5px is breathing room for the per-row "…", which
  // hangs OUTSIDE the bubble and was landing hard against the transcript edge.
  inline constexpr QSize kFloatingSize{385, 480};

  // Sink + dissolve `w` in place, then delete it. The snapshot the scatter is made
  // of was taken already, so the two play together exactly as they do in the
  // browser: the bubble melts into its own dust rather than vanishing first.
  void fadeOutAndDelete(QWidget* w);

  // The one factory behind every chat ghost button — title-bar, composer, card Resend.
  QToolButton* makeGhostButton(QWidget* parent, const QString& tooltip);

  // Re-run the stylesheet for a widget whose objectName just changed (Qt matches
  // selectors at polish time, not on every paint).
  void repolish(QWidget* w);

  // EXIF-aware file decode shared by the attach dialog and paste/drop routing.
  QImage readImageFile(const QString& path);

}  // namespace stencil::gui::chatdock
