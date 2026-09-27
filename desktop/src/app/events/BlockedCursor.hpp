#pragma once
#include <QApplication>
#include <QPointer>
#include <QWidget>

namespace stencil::gui {

  // The app-wide forbidden cursor over a disabled row, pushed at most once and popped with it.
  struct BlockedCursor {
    bool pushed = false;
    QPointer<QWidget> row;
    // Exactly one override on the stack, ever: the flag, not the caller, decides.
    void set(bool on) {
      if (on == pushed) return;
      if (on) QApplication::setOverrideCursor(Qt::ForbiddenCursor);
      else QApplication::restoreOverrideCursor();
      pushed = on;
    }
  };

}  // namespace stencil::gui
