#pragma once
// The dust a popup or menu forms out of, shared by the reveal filters and the public popup API.
// Private to the menuReveal TUs; the public surface stays menuReveal.hpp.
#include "menuReveal.hpp"
#include "DisintegrateOverlay.hpp"
#include "modalReveal.hpp"  // motionReduced()

#include <QMenu>
#include <QPoint>
#include <QWidget>

namespace stencil::support {

  inline constexpr int MENU_MS = 140;

  // Browser twin: js/ui/motion.js surfaceIn. escapeHost is safe because placeForSurface's
  // escape layer is Qt::ToolTip, not a grab-stealing Qt::Window.
  inline bool dustPopupIn(QWidget* popup, QWidget* host, const QPoint& originGlobal, int ms) {
    if (!gui::flyTipDust(popup, host, originGlobal, /*gather=*/true, ms,
                         /*escapeHost=*/true))
      return false;
    gui::fadeUpBehindDust(popup, ms);
    return true;
  }

  inline bool dustPopupOut(QWidget* popup, QWidget* host, const QPoint& originGlobal, int ms) {
    return gui::flyTipDust(popup, host, originGlobal, /*gather=*/false, ms,
                           /*escapeHost=*/true)
           != nullptr;
  }

  // A submenu's parent is another QMenu (a popup), so ->window() stops there: walk past them.
  inline QWidget* menuHostWindow(QWidget* w) {
    while (w && qobject_cast<QMenu*>(w)) w = w->parentWidget();
    return w ? w->window() : nullptr;
  }

  inline bool dustMenuIn(QMenu* m, const QPoint& originGlobal, int ms) {
    return dustPopupIn(m, menuHostWindow(m->parentWidget()), originGlobal, ms);
  }

  // No isVisible() gate: QMenu emits aboutToHide from its hideEvent, when the popup is
  // ALREADY hidden; grab() still renders it.
  inline bool dustMenuOut(QMenu* m, const QPoint& originGlobal, int ms) {
    return dustPopupOut(m, menuHostWindow(m->parentWidget()), originGlobal, ms);
  }
}  // namespace stencil::support
