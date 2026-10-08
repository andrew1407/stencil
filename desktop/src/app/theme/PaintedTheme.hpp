#pragma once
#include <QColor>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QWidget>

class QAction;

namespace stencil::gui {

  // The look the window last painted, so a repeat apply is a no-op, and the theme wipe in flight.
  struct PaintedTheme {
    bool done = false;
    bool dark = false;
    QString accent;
    // The wipe in flight; a second toggle is ignored. QPointer — it deleteLater()s itself.
    QPointer<QWidget> wipe;
    bool swapping() const { return !wipe.isNull(); }
    bool silent = false;   // a restyle nobody sees (the theme lens's photograph): no wipe
    // The ink the action icons were last drawn in, and each action's icon to redraw with it.
    QColor iconColor{Qt::black};
    QHash<QAction*, QString> iconNames;
  };

}  // namespace stencil::gui
