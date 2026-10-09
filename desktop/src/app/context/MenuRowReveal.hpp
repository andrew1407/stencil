#pragma once
// Rows hosted in an open menu (QWidgetAction) arriving or leaving as sand: the particles carry
// the row while its height slides, and the menu re-fits on every frame, lifted to stay on its
// screen. Browser twin: the ctx formula rows' revealControls plus contextMenu/nav.js keepInView.
#include <QList>

class QWidgetAction;

namespace stencil::gui {

  // A closed menu, or no motion, just sets visibility.
  void revealMenuRows(const QList<QWidgetAction*>& rows, bool show);

}  // namespace stencil::gui
