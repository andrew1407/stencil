#pragma once
#include <QPoint>

class QMenu;

// Grow-from-the-cursor pop for the custom context menus (canvas + chat card):
// the popup fades in (windowOpacity 0→1) while its rect grows out of the click
// point. Sibling of modalReveal's dialog flight, tuned for menus.
namespace stencil::support {

  // Call BEFORE exec()/popup() with the GLOBAL click point; the flight starts on
  // the menu's own Show, so it survives the blocking exec(). No-op headless
  // (offscreen platform) or with STENCIL_NO_ANIM=1.
  void revealMenu(QMenu& menu, const QPoint& origin);

}  // namespace stencil::support
