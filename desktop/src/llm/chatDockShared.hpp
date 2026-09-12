#pragma once

// Shared by the chatDock*.cpp partials. Nested in `chatdock`: names like repolish and kAppearMs
// also live in other files' anonymous namespaces.

#include <QSize>
#include <QString>

class QImage;
class QToolButton;
class QWidget;

namespace stencil::gui::chatdock {

  inline constexpr int kThumbEdge = 160;
  inline constexpr int kButtonEdge = 23;
  inline constexpr int kHeaderIcon = 13;
  // The context menu's assistant panel mirrors these exact numbers.
  inline constexpr int kAccentEdge = 30;
  inline constexpr int kAccentIcon = 20;
  // Qt draws a SQUARE box when border-radius exceeds half the height; 14 keeps the chip a pill.
  inline constexpr int kSuggestChipRadius = 14;
  // browser css/animations.css chatCardLeave, motion.js CHAT_LEAVE_MS
  inline constexpr int kChatLeaveMs = 260;
  inline constexpr int kChipNameMaxPx = 150;
  // A removed chip HOLDS its slot so the scatter reads before the neighbours slide.
  inline constexpr int kChatChipHoldMs = 140;
  // browser/extension chatController.js MAX_ATTACHMENTS; past it the queue is refused (§7).
  inline constexpr int kMaxAttachments = 3;
  // browser .chat-jump-btn / .chat-row-menu-btn rest opacity on all three surfaces
  inline constexpr double kGhostRestOpacity = 0.7;
  // The transcript follows new content only inside this band.
  inline constexpr int kStickyBottomPx = 40;

  inline constexpr int kAppearMs = 140;
  inline constexpr int kAppearSlidePx = 6;
  // Compact default when torn off; 385 = 380 + 5 px so the per-row "…" clears the transcript edge.
  inline constexpr QSize kFloatingSize{385, 480};

  // The snapshot was taken already, so the bubble melts into its own dust rather than vanishing.
  void fadeOutAndDelete(QWidget* w);

  QToolButton* makeGhostButton(QWidget* parent, const QString& tooltip);

  // Qt matches selectors at polish time, not on every paint.
  void repolish(QWidget* w);

  QImage readImageFile(const QString& path);

}  // namespace stencil::gui::chatdock
