// Sliding and Transition's stand-in for a cloud (browser css/animations/reveal rowSlideLeave / rowShrinkLeave,
// groups.css revealModeIn): a picture slides along its axis or grows / shrinks, clipped to its list or slot.
#pragma once

#include <QPixmap>
#include <QRect>
#include <QWidget>

#include <functional>

namespace stencil::gui {

  // A row's slide or shrink, ms (browser enterLeave.js MODE_ROW_MS).
  inline constexpr int MODE_ROW_MS = 340;
  // Past it, the picture's own start (it is photographed first) and its last frame: a list rebuilt at exactly
  // MODE_ROW_MS drew its new rows under a leaving row still sliding out (user report).
  inline constexpr int MODE_ROW_SETTLE_MS = 80;

  // True in Sliding or Transition (on a real platform), where the picture is the motion.
  bool modeGhostMotion();

  // `shot` stood at `at` and moves inside `clip` (both host-local) for `ms`; `done` runs as it lands; false outside those modes.
  // `pill`: a keyword chip's travel (its width + 12px, fading over 40%); `slideAlways`: Transition slides too.
  bool playModeGhost(QWidget* host, const QPixmap& shot, const QRect& at, const QRect& clip, bool in,
                     Qt::Orientation axis, int ms, std::function<void()> done = {}, bool pill = false,
                     bool slideAlways = false);

}  // namespace stencil::gui
