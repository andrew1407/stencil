#pragma once

// Shared by the ChatDock*.cpp partials. Nested in `chatdock`: names like repolish and APPEAR_MS
// also live in other files' anonymous namespaces.

#include <QSize>
#include <QColor>
#include <QPixmap>
#include <QString>

class QImage;
class QToolButton;
class QWidget;

namespace stencil::gui::chatdock {

  inline constexpr int THUMB_EDGE = 160;
  // Browser .chat-result: a 44px cover thumbnail beside a label ellipsised at 120px.
  inline constexpr int RESULT_THUMB = 44;
  inline constexpr int RESULT_LABEL_MAX_PX = 120;
  inline constexpr int BUTTON_EDGE = 23;
  inline constexpr int HEADER_ICON = 13;
  // The context menu's assistant panel mirrors these exact numbers.
  inline constexpr int ACCENT_EDGE = 30;
  inline constexpr int ACCENT_ICON = 20;
  // browser css/animations.css chatCardLeave, surface/motion.js CHAT_LEAVE_MS
  inline constexpr int CHAT_LEAVE_MS = 260;
  inline constexpr int CHIP_NAME_MAX_PX = 150;
  // A removed chip HOLDS its slot so the scatter reads before the neighbours slide.
  inline constexpr int CHAT_CHIP_HOLD_MS = 140;
  // browser/extension controller.js MAX_ATTACHMENTS; past it the queue is refused (§7).
  inline constexpr int MAX_ATTACHMENTS = 3;
  // browser .chat-jump-btn / .chat-row-menu-btn rest opacity on all three surfaces
  inline constexpr double GHOST_REST_OPACITY = 0.7;
  // The transcript follows new content only inside this band.
  inline constexpr int STICKY_BOTTOM_PX = 40;

  inline constexpr int APPEAR_MS = 140;
  inline constexpr int APPEAR_SLIDE_PX = 6;
  // Compact default when torn off; 385 = 380 + 5 px so the per-row "…" clears the transcript edge.
  inline constexpr QSize FLOATING_SIZE{385, 480};
  // The popover shape beside the toolbar icon (browser chat/geometry.js COMPACT_CHAT_W/H).
  inline constexpr QSize COMPACT_SIZE{340, 460};

  // The snapshot was taken already, so the bubble melts into its own dust rather than vanishing.
  void fadeOutAndDelete(QWidget* w);

  QToolButton* makeGhostButton(QWidget* parent, const QString& tooltip);

  // Qt matches selectors at polish time, not on every paint.
  void repolish(QWidget* w);

  QImage readImageFile(const QString& path);

  // Browser .chat-result-thumb: the picture cover-fills an `edge` square, rounded by `radius`,
  // inside a 1px border; `dpr` keeps it crisp on a Retina screen.
  QPixmap coverThumb(const QImage& image, int edge, int radius, const QColor& border, qreal dpr);

}  // namespace stencil::gui::chatdock
